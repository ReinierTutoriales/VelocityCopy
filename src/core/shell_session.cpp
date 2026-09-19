#include "velocitycopy/shell_session.hpp"
#include <utility>
namespace velocitycopy {
ShellDispatchResult ShellSession::dispatch(const ShellRequest& request) {
    if (!shell_request_valid(request)) return {ShellDispatchStatus::InvalidRequest, false, std::nullopt};
    if (request.action == ShellAction::OpenVelocityCopy)
        return {ShellDispatchStatus::Accepted, true, std::nullopt};
    CopyJob job{};
    job.id = next_job_id_++;
    job.destination = request.destination;
    job.layout = request.layout;
    job.operation = request.operation;
    job.display_name = L"Explorer transfer";
    job.sources = request.sources;
    return {ShellDispatchStatus::Accepted, true, std::move(job)};
}
}
