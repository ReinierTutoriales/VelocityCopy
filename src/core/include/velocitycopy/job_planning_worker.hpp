#pragma once

#include "velocitycopy/copy_job.hpp"
#include "velocitycopy/job_planner.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace velocitycopy {

struct JobPlanningResult {
    std::uint64_t request_id{};
    CopyJob job;
    std::optional<CopyPlan> plan;
    std::int32_t error_code{};
};

using JobPlanningCallback = std::function<void(JobPlanningResult)>;

class JobPlanningWorker final {
public:
    JobPlanningWorker();
    ~JobPlanningWorker();

    JobPlanningWorker(const JobPlanningWorker&) = delete;
    JobPlanningWorker& operator=(const JobPlanningWorker&) = delete;

    [[nodiscard]] std::uint64_t enqueue(CopyJob job, JobPlanningCallback callback);
    void cancel_pending() noexcept;

private:
    struct Request {
        std::uint64_t id{};
        CopyJob job;
        JobPlanningCallback callback;
    };

    void run(std::stop_token stop_token) noexcept;

    JobPlanner planner_;
    std::mutex mutex_;
    std::condition_variable_any condition_;
    std::deque<Request> pending_;
    std::uint64_t next_request_id_{1};
    std::jthread worker_;
};

} // namespace velocitycopy
