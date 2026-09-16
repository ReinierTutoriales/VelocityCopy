#include "velocitycopy/job_planning_worker.hpp"

#include <windows.h>

#include <system_error>
#include <utility>

namespace velocitycopy {

JobPlanningWorker::JobPlanningWorker()
    : worker_([this](std::stop_token token) { run(token); }) {
}

JobPlanningWorker::~JobPlanningWorker() {
    worker_.request_stop();
    condition_.notify_all();
}

std::uint64_t JobPlanningWorker::enqueue(CopyJob job, JobPlanningCallback callback) {
    std::lock_guard lock(mutex_);
    const auto id = next_request_id_++;
    pending_.push_back(Request{id, std::move(job), std::move(callback)});
    condition_.notify_one();
    return id;
}

void JobPlanningWorker::cancel_pending() noexcept {
    std::lock_guard lock(mutex_);
    pending_.clear();
}

void JobPlanningWorker::run(const std::stop_token stop_token) noexcept {
    while (!stop_token.stop_requested()) {
        Request request{};
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, stop_token, [this] { return !pending_.empty(); });
            if (stop_token.stop_requested()) {
                return;
            }
            request = std::move(pending_.front());
            pending_.pop_front();
        }

        JobPlanningResult result{};
        result.request_id = request.id;
        try {
            result.plan = planner_.build(request.job);
        } catch (const std::filesystem::filesystem_error& error) {
            const auto code = error.code().value();
            result.error_code = static_cast<std::int32_t>(
                HRESULT_FROM_WIN32(code == 0 ? ERROR_INVALID_DATA : code));
        } catch (...) {
            result.error_code = static_cast<std::int32_t>(E_FAIL);
        }
        result.job = std::move(request.job);

        if (request.callback) {
            try {
                request.callback(std::move(result));
            } catch (...) {
                // Consumer callbacks must not terminate the planner worker.
            }
        }
    }
}

} // namespace velocitycopy
