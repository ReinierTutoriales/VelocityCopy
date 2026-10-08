#include "velocitycopy/copy_engine.hpp"
#include "destination_path_guard.hpp"

#include <memory>

#include <windows.h>

#include <algorithm>
#include <new>
#include <system_error>
#include <vector>

namespace velocitycopy {
namespace {

constexpr ULONG kDefaultIoSize = 1u * 1024u * 1024u;
constexpr ULONG kLargeFileIoSize = 4u * 1024u * 1024u;
constexpr std::uint64_t kLargeFileThreshold = 1ull * 1024ull * 1024ull * 1024ull;

struct CallbackContext {
    const ProgressCallback* callback{};
    bool callback_failed{};
    bool has_progress{};
    CopyProgress last_progress{};
    COPYFILE2_MESSAGE_ACTION latched_action{COPYFILE2_PROGRESS_CONTINUE};
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

bool source_is_unsafe_reparse_point(const std::filesystem::path& source) noexcept {
    const HANDLE handle = CreateFileW(
        source.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        // Let CopyFile2 report missing/inaccessible sources with its native error.
        return false;
    }
    const bool unsafe = detail::name_surrogate_reparse(handle);
    CloseHandle(handle);
    return unsafe;
}

std::uint64_t source_size_no_throw(const std::filesystem::path& source) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(source, ec);
    return ec ? 0 : size;
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
        const auto parent = destination.parent_path();
        const auto prepare_parent = [&]() {
            if (parent.empty()) return true;
            return options.lease != nullptr
                ? options.lease->state_->prepare(parent, directory_error)
                : destination_guard.prepare_directory(parent, directory_error);
        };
        if (source_is_unsafe_reparse_point(source) || !prepare_parent()) {
            const auto native = directory_error
                ? static_cast<DWORD>(directory_error.value())
                : ERROR_CANT_ACCESS_FILE;
            return {
                false,
                static_cast<std::int32_t>(HRESULT_FROM_WIN32(native)),
            };
        }

        const auto source_size = source_size_no_throw(source);
        CallbackContext callback_context{&progress};
        callback_context.last_progress = {source_size, 0};
        callback_context.has_progress = source_size != 0;

        // VelocityCopy targets Windows 11, so use CopyFile2 V2 deliberately. Keep
        // I/O cycles bounded so callbacks remain frequent enough for live controls.
        // StrategySelector can override the fallback size through CopyOptions; this
        // makes the storage-profile recommendation part of the production path.
        COPYFILE2_EXTENDED_PARAMETERS_V2 parameters{};
        parameters.dwSize = sizeof(parameters);
        parameters.dwCopyFlags = options.copy_flags;
        if (options.resume_from_pause) {
            parameters.dwCopyFlags |= COPY_FILE_RESUME_FROM_PAUSE;
        }
        if (options.existing_destination == ExistingDestinationPolicy::Fail) {
            parameters.dwCopyFlags |= COPY_FILE_FAIL_IF_EXISTS;
        }
        parameters.ioDesiredSize = desired_io_size(source_size, options.io_size_bytes);
        if (progress) {
            parameters.pProgressRoutine = copy_progress_routine;
            parameters.pvCallbackContext = &callback_context;
        }

        const auto copy = [&]() {
            return CopyFile2(
                source.c_str(),
                destination.c_str(),
                reinterpret_cast<COPYFILE2_EXTENDED_PARAMETERS*>(&parameters));
        };
        HRESULT result = copy();
        if (result == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) && !callback_context.callback_failed &&
            options.existing_destination == ExistingDestinationPolicy::Replace &&
            clear_replace_blocking_attributes(destination)) {
            result = copy();
        }
        for (const DWORD delay : kTransientRetryDelaysMs) {
            if (!is_transient_lock(result) || callback_context.callback_failed) break;
            Sleep(delay);
            result = copy();
        }

        if (callback_context.callback_failed) {
            return {false, static_cast<std::int32_t>(E_FAIL)};
        }

        return {
            SUCCEEDED(result),
            static_cast<std::int32_t>(result),
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
    const ExistingDestinationPolicy existing_destination) const noexcept {
    try {
        detail::DestinationPathGuard destination_guard;
        std::error_code directory_error;
        const auto parent = destination.parent_path();
        if (source_is_unsafe_reparse_point(source) ||
            (!parent.empty() && !destination_guard.prepare_directory(parent, directory_error))) {
            const auto native = directory_error
                ? static_cast<DWORD>(directory_error.value())
                : ERROR_CANT_ACCESS_FILE;
            return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(native))};
        }

        const DWORD flags = existing_destination == ExistingDestinationPolicy::Replace
            ? MOVEFILE_REPLACE_EXISTING
            : 0;
        if (MoveFileExW(source.c_str(), destination.c_str(), flags) != FALSE) {
            return {true, static_cast<std::int32_t>(S_OK)};
        }
        DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED && existing_destination == ExistingDestinationPolicy::Replace &&
            clear_replace_blocking_attributes(destination)) {
            if (MoveFileExW(source.c_str(), destination.c_str(), flags) != FALSE) {
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
