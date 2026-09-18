#include "velocitycopy/copy_engine.hpp"

#include <windows.h>

#include <system_error>

namespace velocitycopy {
namespace {

struct CallbackContext {
    const ProgressCallback* callback{};
    bool callback_failed{};
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

COPYFILE2_MESSAGE_ACTION CALLBACK copy_progress_routine(
    const COPYFILE2_MESSAGE* message,
    void* context) noexcept {
    if (message == nullptr || context == nullptr) {
        return COPYFILE2_PROGRESS_CONTINUE;
    }

    auto* callback_context = static_cast<CallbackContext*>(context);
    if (callback_context->latched_action != COPYFILE2_PROGRESS_CONTINUE) {
        return callback_context->latched_action;
    }
    if (callback_context->callback == nullptr || !(*callback_context->callback)) {
        return COPYFILE2_PROGRESS_CONTINUE;
    }

    CopyProgress progress{};
    bool has_progress = false;

    switch (message->Type) {
    case COPYFILE2_CALLBACK_CHUNK_FINISHED:
        progress.total_bytes = message->Info.ChunkFinished.uliTotalFileSize.QuadPart;
        progress.transferred_bytes = message->Info.ChunkFinished.uliTotalBytesTransferred.QuadPart;
        has_progress = true;
        break;

    case COPYFILE2_CALLBACK_STREAM_FINISHED:
        progress.total_bytes = message->Info.StreamFinished.uliTotalFileSize.QuadPart;
        progress.transferred_bytes = message->Info.StreamFinished.uliTotalBytesTransferred.QuadPart;
        has_progress = true;
        break;

    default:
        break;
    }

    if (!has_progress) {
        return COPYFILE2_PROGRESS_CONTINUE;
    }

    try {
        const auto action = to_native_action((*callback_context->callback)(progress));
        if (action != COPYFILE2_PROGRESS_CONTINUE) {
            callback_context->latched_action = action;
        }
        return action;
    } catch (...) {
        callback_context->callback_failed = true;
        callback_context->latched_action = COPYFILE2_PROGRESS_CANCEL;
        return COPYFILE2_PROGRESS_CANCEL;
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


bool existing_path_is_reparse_point(const std::filesystem::path& path) noexcept {
    const HANDLE handle = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    FILE_ATTRIBUTE_TAG_INFO info{};
    const bool result = GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info)) != 0 && (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    CloseHandle(handle);
    return result;
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
    if (source_is_unsafe_reparse_point(source)) {
        return {
            false,
            static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_CANT_ACCESS_FILE)),
        };
    }

    CallbackContext callback_context{&progress, false, COPYFILE2_PROGRESS_CONTINUE};

    COPYFILE2_EXTENDED_PARAMETERS parameters{};
    parameters.dwSize = sizeof(parameters);
    parameters.dwCopyFlags = options.copy_flags | COPY_FILE_COPY_SYMLINK;
    if (options.resume_from_pause) {
        parameters.dwCopyFlags |= COPY_FILE_RESUME_FROM_PAUSE;
    }
    if (options.existing_destination == ExistingDestinationPolicy::Fail) {
        parameters.dwCopyFlags |= COPY_FILE_FAIL_IF_EXISTS;
    }
    if (progress) {
        parameters.pProgressRoutine = copy_progress_routine;
        parameters.pvCallbackContext = &callback_context;
    }

    const HRESULT result = CopyFile2(
        source.c_str(),
        destination.c_str(),
        &parameters);

    if (callback_context.callback_failed) {
        return {false, static_cast<std::int32_t>(E_FAIL)};
    }

    return {
        SUCCEEDED(result),
        static_cast<std::int32_t>(result),
    };
}

} // namespace velocitycopy
