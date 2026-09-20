#include "velocitycopy/copy_engine.hpp"

#include <windows.h>

#include <algorithm>
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
    const DWORD attrs = GetFileAttributesW(source.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        // Let CopyFile2 report missing/inaccessible sources with its native error.
        return false;
    }
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

std::uint64_t source_size_no_throw(const std::filesystem::path& source) noexcept {
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

struct DestinationPathGuard {
    std::vector<HANDLE> parents;

    ~DestinationPathGuard() noexcept {
        for (const HANDLE handle : parents) CloseHandle(handle);
    }

    bool lock_non_reparse_parents(const std::filesystem::path& destination) noexcept {
        std::error_code ec;
        auto probe = std::filesystem::absolute(destination.parent_path(), ec);
        if (ec) return false;
        const auto root = probe.root_path();
        while (!probe.empty() && probe != root) {
            const HANDLE handle = CreateFileW(
                probe.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE) return false;

            FILE_ATTRIBUTE_TAG_INFO info{};
            if (GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info)) == 0 ||
                (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                CloseHandle(handle);
                return false;
            }
            parents.push_back(handle);
            probe = probe.parent_path();
        }
        return true;
    }
};

bool destination_chain_contains_reparse_point(
    const std::filesystem::path& destination,
    DestinationPathGuard& guard) noexcept {
    return !guard.lock_non_reparse_parents(destination);
}

} // namespace

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
    std::error_code directory_error;
    const auto parent = destination.parent_path();

    if (!parent.empty()) {
        std::filesystem::create_directories(parent, directory_error);
        if (directory_error) {
            return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(directory_error.value()))};
        }
    }

    // Revalidate immediately before CopyFile2 so a source that was safe at
    // planning time cannot be silently followed after being swapped for a
    // symlink/junction. This narrows the remaining TOCTOU window.
    DestinationPathGuard destination_guard;
    if (source_is_unsafe_reparse_point(source) ||
        destination_chain_contains_reparse_point(destination, destination_guard)) {
        return {
            false,
            static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_CANT_ACCESS_FILE)),
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

    const HRESULT result = CopyFile2(
        source.c_str(),
        destination.c_str(),
        reinterpret_cast<COPYFILE2_EXTENDED_PARAMETERS*>(&parameters));

    if (callback_context.callback_failed) {
        return {false, static_cast<std::int32_t>(E_FAIL)};
    }

    return {
        SUCCEEDED(result),
        static_cast<std::int32_t>(result),
    };
}

} // namespace velocitycopy
