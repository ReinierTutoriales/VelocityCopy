#include "velocitycopy/job_planning_worker.hpp"

#include <windows.h>

#include <system_error>
#include <utility>
#include <vector>

namespace velocitycopy {

namespace {

std::int32_t cancellation_code() noexcept {
    return static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED));
}

} // namespace

JobPlanningWorker::JobPlanningWorker()
    : worker_([this](std::stop_token token) { run(token); }) {
}

JobPlanningWorker::~JobPlanningWorker() {
    {
        std::lock_guard lock(mutex_);
        if (active_request_id_ != 0) {
            active_stop_source_.request_stop();
        }
        pending_.clear();
    }
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

JobPlanningResult JobPlanningWorker::cancelled_result(Request request) noexcept {
    JobPlanningResult result{};
    result.request_id = request.id;
    result.job = std::move(request.job);
    result.error_code = cancellation_code();
    return result;
}

void JobPlanningWorker::cancel_pending() noexcept {
    std::vector<Request> cancelled;
    {
        std::lock_guard lock(mutex_);
        if (active_request_id_ != 0) {
            active_stop_source_.request_stop();
        }
        cancelled.reserve(pending_.size());
        while (!pending_.empty()) {
            cancelled.push_back(std::move(pending_.front()));
            pending_.pop_front();
        }
    }

    // Deliver explicit-cancellation callbacks outside the worker mutex. The UI
    // uses them to release append reservations, and callback code is allowed to
    // enqueue new work without deadlocking this worker.
    for (auto& request : cancelled) {
        if (!request.callback) continue;
        try {
            request.callback(cancelled_result(std::move(request)));
        } catch (...) {
            // Consumer callbacks must not destabilize cancellation.
        }
    }
}

void JobPlanningWorker::run(const std::stop_token stop_token) noexcept {
    while (!stop_token.stop_requested()) {
        Request request{};
        std::stop_token request_stop_token;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, stop_token, [this] { return !pending_.empty(); });
            if (stop_token.stop_requested()) {
                return;
            }
            request = std::move(pending_.front());
            pending_.pop_front();
            active_stop_source_ = std::stop_source{};
            active_request_id_ = request.id;
            request_stop_token = active_stop_source_.get_token();
        }

        JobPlanningResult result{};
        result.request_id = request.id;
        try {
            result.plan = planner_.build(request.job, request_stop_token);
            if (request_stop_token.stop_requested()) {
                result.plan.reset();
                result.error_code = cancellation_code();
            }
        } catch (const std::system_error& error) {
            if (error.code() == std::make_error_code(std::errc::operation_canceled) ||
                request_stop_token.stop_requested()) {
                result.error_code = cancellation_code();
            } else {
                const auto code = error.code().value();
                result.error_code = static_cast<std::int32_t>(
                    HRESULT_FROM_WIN32(code == 0 ? ERROR_INVALID_DATA : code));
            }
        } catch (...) {
            result.error_code = static_cast<std::int32_t>(E_FAIL);
        }
        result.job = std::move(request.job);

        {
            std::lock_guard lock(mutex_);
            if (active_request_id_ == request.id) {
                active_request_id_ = 0;
                active_stop_source_ = std::stop_source{};
            }
        }

        if (stop_token.stop_requested()) {
            return;
        }

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
