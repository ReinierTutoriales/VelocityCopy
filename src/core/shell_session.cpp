#include "velocitycopy/shell_session.hpp"

#include <utility>

namespace velocitycopy {

ShellDispatchResult ShellSession::dispatch(const ShellRequest& request) noexcept {
    if (!shell_request_valid(request)) {
        return {ShellDispatchStatus::InvalidRequest, false, std::nullopt};
    }

    switch (request.action) {
    case ShellAction::CopySelection:
        staged_sources_ = request.sources;
        return {ShellDispatchStatus::Accepted, false, std::nullopt};

    case ShellAction::PasteToFolder:
        if (staged_sources_.empty()) {
            return {ShellDispatchStatus::NoStagedSources, true, std::nullopt};
        }
        break;

    case ShellAction::CopySelectionTo:
        break;

    case ShellAction::OpenVelocityCopy:
        return {ShellDispatchStatus::Accepted, true, std::nullopt};
    }

    CopyJob job{};
    job.id = next_job_id_++;
    job.destination = request.destination;
    job.layout = request.layout;
    job.display_name = L"Explorer transfer";
    job.sources = request.action == ShellAction::PasteToFolder ? staged_sources_ : request.sources;

    return {ShellDispatchStatus::Accepted, true, std::move(job)};
}

const std::vector<std::filesystem::path>& ShellSession::staged_sources() const noexcept {
    return staged_sources_;
}

void ShellSession::clear_staged_sources() noexcept {
    staged_sources_.clear();
}

} // namespace velocitycopy
