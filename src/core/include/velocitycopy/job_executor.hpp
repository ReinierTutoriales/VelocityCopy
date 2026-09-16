#pragma once

#include "velocitycopy/copy_engine.hpp"
#include "velocitycopy/copy_job.hpp"
#include "velocitycopy/job_planner.hpp"
#include "velocitycopy/live_copy_plan.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>

namespace velocitycopy {

struct JobProgress {
    std::uint64_t total_bytes{};
    std::uint64_t transferred_bytes{};
    std::uint64_t total_files{};
    std::uint64_t completed_files{};
    std::filesystem::path current_source;
    std::filesystem::path current_destination;
};

enum class JobDecision {
    Continue,
    Cancel,
};

using JobProgressCallback = std::function<JobDecision(const JobProgress&)>;

struct JobResult {
    bool success{};
    bool cancelled{};
    std::int32_t native_code{};
};

class JobExecutor final {
public:
    [[nodiscard]] JobResult execute(
        const CopyJob& job,
        const JobProgressCallback& progress = {}) const noexcept;

    [[nodiscard]] JobResult execute(
        const CopyPlan& plan,
        const JobProgressCallback& progress = {}) const noexcept;

    [[nodiscard]] JobResult execute(
        LiveCopyPlan& plan,
        const JobProgressCallback& progress = {}) const noexcept;

private:
    CopyEngine engine_;
    JobPlanner planner_;
};

} // namespace velocitycopy
