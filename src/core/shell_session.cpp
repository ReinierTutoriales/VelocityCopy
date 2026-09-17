#include "velocitycopy/shell_session.hpp"

#include <utility>

namespace velocitycopy {

ShellDispatchResult ShellSession::dispatch(const ShellRequest& request) noexcept {
    if (!shell_request_valid(request)) {
        return {ShellDispatchStatus::InvalidRequest, false, std::nullopt};
    }

    switch (request.action) {
    case ShellAction::CopySelection:
        stage_sources(request.sources, FileOperation::Copy);
        return {ShellDispatchStatus::Accepted, false, std::nullopt};

    case ShellAction::PasteToFolder:
        if (staged_sources_.empty()) {
            return {ShellDispatchStatus::NoStagedSources, true, std::nullopt};
        }
        break;

    case ShellAction::CopySelectionTo:
        break;

    case ShellAction::CopySelectionPromptDestination:
        return {ShellDispatchStatus::Accepted, true, std::nullopt};

    case ShellAction::OpenVelocityCopy:
        return {ShellDispatchStatus::Accepted, true, std::nullopt};
    }

    CopyJob job{};
    job.id = next_job_id_++;
    job.destination = request.destination;
    job.layout = request.layout;
    job.operation = request.action == ShellAction::PasteToFolder
        ? staged_operation_
        : FileOperation::Copy;
    job.display_name = L"Explorer transfer";
    job.sources = request.action == ShellAction::PasteToFolder ? staged_sources_ : request.sources;

    return {ShellDispatchStatus::Accepted, true, std::move(job)};
}

const std::vector<std::filesystem::path>& ShellSession::staged_sources() const noexcept {
    return staged_sources_;
}

FileOperation ShellSession::staged_operation() const noexcept {
    return staged_operation_;
}

void ShellSession::stage_sources(
    std::vector<std::filesystem::path> sources,
    const FileOperation operation) {
    staged_sources_ = std::move(sources);
    staged_operation_ = operation;
}

void ShellSession::clear_staged_sources() noexcept {
    staged_sources_.clear();
    staged_operation_ = FileOperation::Copy;
}

} // namespace velocitycopy
