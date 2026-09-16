#include "velocitycopy/copy_engine.hpp"

#include <windows.h>

#include <system_error>

namespace velocitycopy {
namespace {

struct CallbackContext {
    const ProgressCallback* callback{};
};

COPYFILE2_MESSAGE_ACTION CALLBACK copy_progress_routine(
    const COPYFILE2_MESSAGE* message,
    void* context) {
    if (message == nullptr || context == nullptr) {
        return COPYFILE2_PROGRESS_CONTINUE;
    }

    const auto* callback_context = static_cast<CallbackContext*>(context);
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

    return (*callback_context->callback)(progress) == CopyDecision::Cancel
        ? COPYFILE2_PROGRESS_CANCEL
        : COPYFILE2_PROGRESS_CONTINUE;
}

} // namespace

CopyResult CopyEngine::copy_file(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const ProgressCallback& progress) const noexcept {
    std::error_code directory_error;
    const auto parent = destination.parent_path();

    if (!parent.empty()) {
        std::filesystem::create_directories(parent, directory_error);
        if (directory_error) {
            return {false, static_cast<std::int32_t>(HRESULT_FROM_WIN32(directory_error.value()))};
        }
    }

    CallbackContext callback_context{&progress};

    COPYFILE2_EXTENDED_PARAMETERS parameters{};
    parameters.dwSize = sizeof(parameters);
    parameters.dwCopyFlags = 0;
    parameters.pProgressRoutine = copy_progress_routine;
    parameters.pvCallbackContext = &callback_context;

    const HRESULT result = CopyFile2(
        source.c_str(),
        destination.c_str(),
        &parameters);

    return {
        SUCCEEDED(result),
        static_cast<std::int32_t>(result),
    };
}

} // namespace velocitycopy
