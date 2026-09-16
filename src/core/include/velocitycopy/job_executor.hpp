#pragma once

#include "velocitycopy/copy_engine.hpp"
#include "velocitycopy/copy_job.hpp"
#include "velocitycopy/execution_control.hpp"
#include "velocitycopy/job_planner.hpp"
#include "velocitycopy/live_copy_plan.hpp"
#include "velocitycopy/storage_profiler.hpp"
#include "velocitycopy/strategy_selector.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>

namespace velocitycopy {

struct JobProgress {
    std::uint64_t total_bytes{};
    std::uint64_t transferred_bytes{};
    std::uint64_t total_files{};
    std::uint64_t completed_files{};
    std::uint64_t current_file_id{};
    bool current_file_skippable{};
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
    bool stopped{};
    bool destination_conflict{};
    std::uint64_t conflict_file_id{};
    std::filesystem::path conflict_source;
    std::filesystem::path conflict_destination;
};

struct JobExecutionOptions {
    std::uint32_t worker_count{1};
    ExistingDestinationPolicy existing_destination{ExistingDestinationPolicy::Fail};
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

    [[nodiscard]] JobResult execute(
        LiveCopyPlan& plan,
        ExecutionControl& control,
        const JobProgressCallback& progress = {}) const noexcept;

    [[nodiscard]] JobResult execute(
        LiveCopyPlan& plan,
        ExecutionControl& control,
        const JobExecutionOptions& options,
        const JobProgressCallback& progress = {}) const noexcept;

    [[nodiscard]] JobExecutionOptions recommend_options(
        const CopyJob& job,
        const CopyPlan& plan) const noexcept;

    [[nodiscard]] JobExecutionOptions recommend_options(
        const LiveCopyPlan& plan) const noexcept;

private:
    CopyEngine engine_;
    JobPlanner planner_;
    StorageProfiler storage_profiler_;
    StrategySelector strategy_selector_;
};

} // namespace velocitycopy
