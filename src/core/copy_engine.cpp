#include "velocitycopy/copy_engine.hpp"
#include "velocitycopy/execution_control.hpp"
#include "velocitycopy/path_key.hpp"
#include "destination_path_guard.hpp"
#include "file_fingerprint.hpp"
#include "win32_handle.hpp"

#include <memory>

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>
#include <system_error>
#include <vector>

namespace velocitycopy {

struct SourceLease::State {
    detail::DestinationPathGuard ancestors;
    std::filesystem::path path;
    HANDLE handle{INVALID_HANDLE_VALUE};
    FileFingerprint fingerprint{};
    ~State() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }

    bool prepare(const std::filesystem::path& source, const bool for_rename, std::error_code& error) {
        path = std::filesystem::absolute(source).lexically_normal();
        if (path.native().find(L'\0') != std::wstring::npos) {
            error = std::error_code(ERROR_INVALID_NAME, std::system_category());
            return false;
        }
        if (!ancestors.lock_existing_chain(path.parent_path(), error) || !ancestors.missing.empty()) {
            if (!error) error = std::error_code(ERROR_PATH_NOT_FOUND, std::system_category());
            return false;
        }
        handle = CreateFileW(path.c_str(), GENERIC_READ | (for_rename ? DELETE : 0),
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
            return false;
        }
        if (detail::name_surrogate_reparse(handle)) {
            error = std::error_code(ERROR_CANT_ACCESS_FILE, std::system_category());
            return false;
        }
        const auto probe = detail::fingerprint_from_handle(handle);
        if (probe.status != FingerprintProbe::Present) {
            error = std::error_code(HRESULT_CODE(probe.hresult), std::system_category());
            return false;
        }
        fingerprint = probe.fingerprint;
        return true;
    }
};

SourceLease::SourceLease() = default;
SourceLease::~SourceLease() = default;
void SourceLease::reset() noexcept { state_.reset(); }

namespace {

constexpr ULONG kDefaultIoSize = 1u * 1024u * 1024u;
constexpr ULONG kLargeFileIoSize = 4u * 1024u * 1024u;
constexpr std::uint64_t kLargeFileThreshold = 1ull * 1024ull * 1024ull * 1024ull;

bool sparse_copy_available() noexcept {
    static const bool available = [] {
        // RtlGetVersion avoids compatibility-manifest version virtualization.
        // Resolve the optional query without importing another runtime DLL.
        using GetVersion = LONG (WINAPI*)(OSVERSIONINFOW*);
        const auto module = GetModuleHandleW(L"ntdll.dll");
        const auto get_version = module == nullptr ? nullptr :
            reinterpret_cast<GetVersion>(GetProcAddress(module, "RtlGetVersion"));
        OSVERSIONINFOW version{sizeof(version)};
        return get_version != nullptr && get_version(&version) == 0 &&
            (version.dwMajorVersion > 10 ||
             (version.dwMajorVersion == 10 && version.dwBuildNumber >= 22621));
    }();
    return available;
}

struct CallbackContext {
    const ProgressCallback* callback{};
    bool callback_failed{};
    bool has_progress{};
    CopyProgress last_progress{};
    COPYFILE2_MESSAGE_ACTION latched_action{COPYFILE2_PROGRESS_CONTINUE};
    bool preserve_basic_metadata{};
    FILE_BASIC_INFO source_basic{};
    HANDLE destination_handle{};
    HRESULT metadata_error{S_OK};
    FileFingerprint expected_source{};
    FileFingerprint copied_source{};
    std::optional<bool> expected_destination_case;
    const std::filesystem::path* destination_parent{};

    ~CallbackContext() { reset_metadata(); }
    void reset_metadata() noexcept {
        if (destination_handle != nullptr) CloseHandle(destination_handle);
        destination_handle = nullptr;
        metadata_error = S_OK;
        copied_source = {};
    }

    bool capture_metadata(HANDLE source, HANDLE destination) noexcept {
        if (destination_handle != nullptr) return true;
        try {
            if (expected_destination_case &&
                directory_case_sensitive(*destination_parent) != *expected_destination_case) {
                metadata_error = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
                latched_action = COPYFILE2_PROGRESS_STOP;
                return false;
            }
        } catch (const std::system_error& error) {
            metadata_error = HRESULT_FROM_WIN32(error.code().value());
            latched_action = COPYFILE2_PROGRESS_STOP;
            return false;
        } catch (...) {
            metadata_error = E_FAIL;
            latched_action = COPYFILE2_PROGRESS_STOP;
            return false;
        }
        const auto probe = detail::fingerprint_from_handle(source);
        if (detail::name_surrogate_reparse(source) || probe.status != FingerprintProbe::Present ||
            (expected_source.has_identity && !detail::same_file_fingerprint(expected_source, probe.fingerprint))) {
            metadata_error = HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
            latched_action = COPYFILE2_PROGRESS_STOP;
            return false;
        }
        copied_source = probe.fingerprint;
        if (!GetFileInformationByHandleEx(source, FileBasicInfo, &source_basic, sizeof(source_basic)) ||
            !DuplicateHandle(GetCurrentProcess(), destination, GetCurrentProcess(),
                &destination_handle, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            metadata_error = HRESULT_FROM_WIN32(GetLastError());
            latched_action = COPYFILE2_PROGRESS_STOP;
            return false;
        }
        return true;
    }

    HRESULT apply_metadata() noexcept {
        if (!preserve_basic_metadata) return S_OK;
        if (destination_handle == nullptr) return HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
        FILE_BASIC_INFO basic{};
        basic.CreationTime = source_basic.CreationTime;
        basic.LastWriteTime = source_basic.LastWriteTime;
        constexpr DWORD carried = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
            FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_TEMPORARY |
            FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
        basic.FileAttributes = source_basic.FileAttributes & carried;
        if (basic.FileAttributes == 0) basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        return SetFileInformationByHandle(destination_handle, FileBasicInfo, &basic, sizeof(basic))
            ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    }
};

COPYFILE2_MESSAGE_ACTION to_native_action(const CopyDecision decision) noexcept {
    switch (decision) {
    case CopyDecision::Pause:
        return COPYFILE2_PROGRESS_PAUSE;
    case CopyDecision::Stop:
        return COPYFILE2_PROGRESS_STOP;
    case CopyDecision::Skip:
    case CopyDecision::Cancel:
        return COPYFILE2_PROGRESS_CANCEL;
    case CopyDecision::Continue:
    default:
        return COPYFILE2_PROGRESS_CONTINUE;
    }
}

COPYFILE2_MESSAGE_ACTION dispatch_progress(
    CallbackContext& context,
    const CopyProgress& progress) noexcept {
    context.last_progress = progress;
    context.has_progress = true;

    if (context.callback == nullptr || !(*context.callback)) {
        return COPYFILE2_PROGRESS_CONTINUE;
    }

    try {
        const auto action = to_native_action((*context.callback)(progress));
        if (action != COPYFILE2_PROGRESS_CONTINUE) {
            context.latched_action = action;
        }
        return action;
    } catch (...) {
        context.callback_failed = true;
        context.latched_action = COPYFILE2_PROGRESS_CANCEL;
        return COPYFILE2_PROGRESS_CANCEL;
    }
}

COPYFILE2_MESSAGE_ACTION CALLBACK copy_progress_routine(
    const COPYFILE2_MESSAGE* message,
    void* context) noexcept {
    if (message == nullptr || context == nullptr) {
        return COPYFILE2_PROGRESS_CONTINUE;
    }

    auto& callback_context = *static_cast<CallbackContext*>(context);
    if (callback_context.latched_action != COPYFILE2_PROGRESS_CONTINUE) {
        return callback_context.latched_action;
    }

    switch (message->Type) {
    case COPYFILE2_CALLBACK_CHUNK_STARTED: {
        CopyProgress progress = callback_context.last_progress;
        progress.total_bytes = message->Info.ChunkStarted.uliTotalFileSize.QuadPart;
        return dispatch_progress(callback_context, progress);
    }

    case COPYFILE2_CALLBACK_CHUNK_FINISHED:
        return dispatch_progress(callback_context, {
            message->Info.ChunkFinished.uliTotalFileSize.QuadPart,
            message->Info.ChunkFinished.uliTotalBytesTransferred.QuadPart,
        });

    case COPYFILE2_CALLBACK_STREAM_STARTED:
        if (!callback_context.capture_metadata(message->Info.StreamStarted.hSourceFile,
                message->Info.StreamStarted.hDestinationFile)) return COPYFILE2_PROGRESS_STOP;
        return dispatch_progress(callback_context, {
            message->Info.StreamStarted.uliTotalFileSize.QuadPart,
            callback_context.has_progress ? callback_context.last_progress.transferred_bytes : 0,
        });

    case COPYFILE2_CALLBACK_STREAM_FINISHED:
        return dispatch_progress(callback_context, {
            message->Info.StreamFinished.uliTotalFileSize.QuadPart,
            message->Info.StreamFinished.uliTotalBytesTransferred.QuadPart,
        });

    case COPYFILE2_CALLBACK_POLL_CONTINUE:
        // POLL_CONTINUE is a control heartbeat, not merely a byte-progress event.
        // Dispatch it even before the first CHUNK_FINISHED notification so a
        // stalled/slow first I/O cycle can still observe Pause/Stop/Cancel.
        return dispatch_progress(callback_context, callback_context.last_progress);

    case COPYFILE2_CALLBACK_ERROR:
        return dispatch_progress(callback_context, {
            message->Info.Error.uliTotalFileSize.QuadPart,
            message->Info.Error.uliTotalBytesTransferred.QuadPart,
        });

    default:
        return COPYFILE2_PROGRESS_CONTINUE;
    }
}

ULONG desired_io_size(
    const std::uint64_t source_size,
    const std::uint32_t requested_io_size) noexcept {
    if (requested_io_size != 0) {
        return static_cast<ULONG>(requested_io_size);
    }
    return source_size >= kLargeFileThreshold ? kLargeFileIoSize : kDefaultIoSize;
}

// CopyFile2 and MoveFileEx refuse to replace a destination marked read-only,
// hidden or system (ERROR_ACCESS_DENIED). Once replacing it was decided, clear
// those marks so the replacement behaves like Explorer's. Reparse points are
// left alone. Returns true when an attribute was cleared.
bool clear_replace_blocking_attributes(const std::filesystem::path& destination) noexcept {
    const DWORD attributes = GetFileAttributesW(destination.c_str());
    constexpr DWORD kBlocking = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        (attributes & kBlocking) == 0) {
        return false;
    }
    return SetFileAttributesW(destination.c_str(), attributes & ~kBlocking) != FALSE;
}

// Antivirus and indexers briefly open files that were just written; such a
// sharing or lock violation usually clears within a fraction of a second.
bool is_transient_lock(const HRESULT result) noexcept {
    return result == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) ||
        result == HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION);
}

constexpr DWORD kTransientRetryDelaysMs[] = {150, 400, 900};

bool wait_before_retry(const DWORD delay, ExecutionControl* control) noexcept {
    if (control == nullptr) {
        Sleep(delay);
        return true;
    }
    return control->wait_for_retry(std::chrono::milliseconds(delay)) == ExecutionDirective::Run;
}

} // namespace

struct DestinationLease::State {
    std::filesystem::path folder;
    std::unique_ptr<detail::DestinationPathGuard> guard;

    bool prepare(const std::filesystem::path& parent, std::error_code& error) {
        if (guard && folder == parent) return true;
        guard.reset();
        folder.clear();
        auto fresh = std::make_unique<detail::DestinationPathGuard>();
        if (!fresh->prepare_directory(parent, error)) return false;
        guard = std::move(fresh);
        folder = parent;
        return true;
    }
};

DestinationLease::DestinationLease() : state_(std::make_unique<State>()) {}
DestinationLease::~DestinationLease() = default;

CopyResult CopyEngine::copy_file(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const ProgressCallback& progress) const noexcept {
    return copy_file(source, destination, CopyOptions{}, progress);
}

CopyResult CopyEngine::copy_file(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const CopyOptions& options,
    const ProgressCallback& progress) const noexcept {
    try {
        // Lock the existing parent chain before creating anything. Directory creation
        // must not follow a junction, so a name-surrogate parent fails before any
        // directory is created through it.
        detail::DestinationPathGuard destination_guard;
        std::error_code directory_error;
        const auto target = std::filesystem::absolute(destination).lexically_normal();
        if (target.native().find(L'\0') != std::wstring::npos)
            return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_NAME))};
        SourceLease local_source;
        auto& source_lease = options.source_lease ? *options.source_lease : local_source;
        if (!source_lease.state_) {
            const auto prepare_source = [&] {
                auto fresh = std::make_unique<SourceLease::State>();
                directory_error.clear();
                if (!fresh->prepare(source, false, directory_error)) return false;
                source_lease.state_ = std::move(fresh);
                return true;
            };
            bool prepared = prepare_source();
            for (const DWORD delay : kTransientRetryDelaysMs) {
                if (prepared || !is_transient_lock(HRESULT_FROM_WIN32(directory_error.value()))) break;
                if (!wait_before_retry(delay, options.control))
                    return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED))};
                prepared = prepare_source();
            }
            if (!prepared) {
                return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(directory_error.value()))};
            }
        } else if (source_lease.state_->path != std::filesystem::absolute(source).lexically_normal()) {
            return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE))};
        }
        const auto parent = target.parent_path();
        const auto prepare_parent = [&]() {
            if (parent.empty()) return true;
            return options.lease != nullptr
                ? options.lease->state_->prepare(parent, directory_error)
                : destination_guard.prepare_directory(parent, directory_error);
        };
        if (!prepare_parent()) {
            const auto native = directory_error
                ? static_cast<DWORD>(directory_error.value())
                : ERROR_CANT_ACCESS_FILE;
            return {
                false,
                static_cast<std::int32_t>(HRESULT_FROM_WIN32(native)),
            };
        }
        if (options.destination_case_sensitive && directory_case_sensitive(parent) != *options.destination_case_sensitive)
            return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE))};

        const auto source_size = source_lease.state_->fingerprint.size;
        CallbackContext callback_context{&progress};
        callback_context.expected_source = source_lease.state_->fingerprint;
        callback_context.expected_destination_case = options.destination_case_sensitive;
        callback_context.destination_parent = &parent;
        callback_context.preserve_basic_metadata = options.preserve_basic_metadata;
        callback_context.last_progress = {source_size, 0};
        callback_context.has_progress = source_size != 0;

        // VelocityCopy targets Windows 11, so use CopyFile2 V2 deliberately. Keep
        // I/O cycles bounded so callbacks remain frequent enough for live controls.
        // StrategySelector can override the fallback size through CopyOptions; this
        // makes the storage-profile recommendation part of the production path.
        COPYFILE2_EXTENDED_PARAMETERS_V2 parameters{};
        parameters.dwSize = sizeof(parameters);
        parameters.dwCopyFlags = options.copy_flags;
        if (!sparse_copy_available()) parameters.dwCopyFlags &= ~COPY_FILE_ENABLE_SPARSE_COPY;
        if (options.resume_from_pause) {
            parameters.dwCopyFlags |= COPY_FILE_RESUME_FROM_PAUSE;
        }
        if (options.existing_destination == ExistingDestinationPolicy::Fail) {
            parameters.dwCopyFlags |= COPY_FILE_FAIL_IF_EXISTS;
        }
        parameters.ioDesiredSize = desired_io_size(source_size, options.io_size_bytes);
        parameters.pProgressRoutine = copy_progress_routine;
        parameters.pvCallbackContext = &callback_context;

        const auto copy = [&]() {
            callback_context.reset_metadata();
            return CopyFile2(
                source_lease.state_->path.c_str(),
                target.c_str(),
                reinterpret_cast<COPYFILE2_EXTENDED_PARAMETERS*>(&parameters));
        };
        HRESULT result = copy();
        if (result == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) && !callback_context.callback_failed &&
            options.existing_destination == ExistingDestinationPolicy::Replace &&
            clear_replace_blocking_attributes(target)) {
            result = copy();
        }
        for (const DWORD delay : kTransientRetryDelaysMs) {
            if (!is_transient_lock(result) || callback_context.callback_failed) break;
            if (!wait_before_retry(delay, options.control))
                return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED))};
            result = copy();
        }

        if (callback_context.callback_failed) {
            return {false, static_cast<std::int32_t>(E_FAIL)};
        }
        if (FAILED(callback_context.metadata_error)) {
            return {false, static_cast<std::int32_t>(callback_context.metadata_error)};
        }
        if (SUCCEEDED(result)) {
            // Keep the actual copied object open through metadata completion;
            // reopening the destination by name could alter a replacement file.
            result = callback_context.apply_metadata();
        }

        FileFingerprint destination_fingerprint{};
        if (SUCCEEDED(result)) {
            const auto probe = detail::fingerprint_from_handle(callback_context.destination_handle);
            if (probe.status != FingerprintProbe::Present) return {false, probe.hresult};
            destination_fingerprint = probe.fingerprint;
        }

        return {
            SUCCEEDED(result),
            static_cast<std::int32_t>(result),
            callback_context.copied_source,
            destination_fingerprint,
        };
    } catch (const std::bad_alloc&) {
        return {false, static_cast<std::int32_t>(E_OUTOFMEMORY)};
    } catch (const std::system_error& error) {
        return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(error.code().value()))};
    } catch (...) {
        return {false, static_cast<std::int32_t>(E_FAIL)};
    }
}

CopyResult CopyEngine::rename_file(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const ExistingDestinationPolicy existing_destination,
    DestinationLease* lease,
    const std::optional<bool> destination_case_sensitive) const noexcept {
    try {
        detail::DestinationPathGuard destination_guard;
        std::error_code directory_error;
        SourceLease::State source_guard;
        if (!source_guard.prepare(source, true, directory_error)) {
            return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(directory_error.value()))};
        }
        const auto target = std::filesystem::absolute(destination).lexically_normal();
        if (target.native().find(L'\0') != std::wstring::npos)
            return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_NAME))};
        const auto parent = target.parent_path();
        const auto prepare_parent = [&]() {
            if (parent.empty()) return true;
            return lease != nullptr
                ? lease->state_->prepare(parent, directory_error)
                : destination_guard.prepare_directory(parent, directory_error);
        };
        if (!prepare_parent()) {
            const auto native = directory_error
                ? static_cast<DWORD>(directory_error.value())
                : ERROR_CANT_ACCESS_FILE;
            return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(native))};
        }
        if (destination_case_sensitive && velocitycopy::directory_case_sensitive(parent) != *destination_case_sensitive)
            return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_INVALID_STATE))};

        const auto& name = target.native();
        const auto bytes = offsetof(FILE_RENAME_INFO, FileName) + (name.size() + 1) * sizeof(wchar_t);
        auto storage = std::make_unique<std::byte[]>(bytes);
        auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.get());
        rename->ReplaceIfExists = existing_destination == ExistingDestinationPolicy::Replace;
        rename->RootDirectory = nullptr;
        rename->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
        std::memcpy(rename->FileName, name.c_str(), (name.size() + 1) * sizeof(wchar_t));
        const auto perform_rename = [&] {
            return SetFileInformationByHandle(source_guard.handle, FileRenameInfo, rename, static_cast<DWORD>(bytes));
        };
        if (perform_rename() != FALSE) {
            return {true, static_cast<std::int32_t>(S_OK)};
        }
        DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED && existing_destination == ExistingDestinationPolicy::Replace &&
            clear_replace_blocking_attributes(target)) {
            if (perform_rename() != FALSE) {
                return {true, static_cast<std::int32_t>(S_OK)};
            }
            error = GetLastError();
        }
        return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(error))};
    } catch (const std::bad_alloc&) {
        return {false, static_cast<std::int32_t>(E_OUTOFMEMORY)};
    } catch (const std::system_error& error) {
        return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(error.code().value()))};
    } catch (...) {
        return {false, static_cast<std::int32_t>(E_FAIL)};
    }
}

} // namespace velocitycopy
