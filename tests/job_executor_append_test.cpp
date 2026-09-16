#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/live_copy_plan.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write_text(const std::filesystem::path& path, const std::string& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << value;
}

velocitycopy::CopyPlan initial_plan(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    velocitycopy::CopyPlan plan{};
    plan.source_roots.push_back(source);
    plan.destination_root = destination;
    plan.directories.push_back({destination});
    plan.files.push_back({1, source / "a.txt", destination / "a.txt", 1});
    plan.files.push_back({2, source / "b.txt", destination / "b.txt", 1});
    plan.total_bytes = 2;
    plan.largest_file_bytes = 1;
    return plan;
}

velocitycopy::CopyPlan appended_plan(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    velocitycopy::CopyPlan plan{};
    plan.source_roots.push_back(source);
    plan.destination_root = destination;
    plan.directories.push_back({destination});
    plan.files.push_back({1, source / "c.txt", destination / "c.txt", 1});
    plan.files.push_back({2, source / "d.txt", destination / "d.txt", 1});
    plan.total_bytes = 2;
    plan.largest_file_bytes = 1;
    return plan;
}

velocitycopy::CopyPlan four_file_plan(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    auto plan = initial_plan(source, destination);
    plan.files.push_back({3, source / "c.txt", destination / "c.txt", 1});
    plan.files.push_back({4, source / "d.txt", destination / "d.txt", 1});
    plan.total_bytes = 4;
    plan.largest_file_bytes = 1;
    return plan;
}

bool all_outputs_exist(const std::filesystem::path& destination) {
    std::error_code ec;
    for (const auto* name : {L"a.txt", L"b.txt", L"c.txt", L"d.txt"}) {
        if (!std::filesystem::exists(destination / name, ec) || ec) return false;
    }
    return true;
}

} // namespace

int wmain() {
    namespace fs = std::filesystem;
    using namespace velocitycopy;

    const auto root = fs::temp_directory_path() / L"VelocityCopyExecutorAppendTest";
    const auto source = root / L"source";
    std::error_code ec;
    fs::remove_all(root, ec);

    write_text(source / L"a.txt", "a");
    write_text(source / L"b.txt", "b");
    write_text(source / L"c.txt", "c");
    write_text(source / L"d.txt", "d");

    JobExecutor executor;

    {
        const auto destination = root / L"hot";
        LiveCopyPlan live(initial_plan(source, destination));
        ExecutionControl control;
        bool appended = false;
        bool saw_expanded_totals = false;
        const auto result = executor.execute(
            live, control, JobExecutionOptions{1},
            [&](const JobProgress& progress) {
                if (!appended && progress.completed_files >= 1) {
                    auto extra = appended_plan(source, destination);
                    if (live.append(std::move(extra)) != LivePlanAppendResult::Appended) return JobDecision::Cancel;
                    appended = true;
                }
                if (appended && progress.total_files == 4 && progress.total_bytes == 4) saw_expanded_totals = true;
                return JobDecision::Continue;
            });
        if (!result.success || result.cancelled || result.stopped || !appended ||
            !saw_expanded_totals || !all_outputs_exist(destination)) {
            fs::remove_all(root, ec); return 1;
        }
        const auto snapshot = live.snapshot();
        if (!snapshot.pending_files.empty() || !snapshot.active_files.empty() ||
            snapshot.total_files != 4 || snapshot.total_bytes != 4 ||
            snapshot.completed_files != 4 || snapshot.completed_bytes != 4) {
            fs::remove_all(root, ec); return 2;
        }
    }

    {
        const auto destination = root / L"reactivated";
        LiveCopyPlan live(initial_plan(source, destination));
        ExecutionControl control;
        const auto first_result = executor.execute(live, control, JobExecutionOptions{1}, {});
        if (!first_result.success || live.completed_files() != 2 || live.completed_bytes() != 2 || live.remaining_files() != 0) {
            fs::remove_all(root, ec); return 3;
        }
        auto extra = appended_plan(source, destination);
        if (live.append(std::move(extra), true) != LivePlanAppendResult::Appended) {
            fs::remove_all(root, ec); return 4;
        }
        std::uint64_t last_transferred = 2;
        std::uint64_t last_completed = 2;
        bool saw_progress = false;
        const auto second_result = executor.execute(
            live, control, JobExecutionOptions{1},
            [&](const JobProgress& progress) {
                saw_progress = true;
                if (progress.total_files != 4 || progress.total_bytes != 4 ||
                    progress.transferred_bytes < 2 || progress.completed_files < 2 ||
                    progress.transferred_bytes < last_transferred || progress.completed_files < last_completed) {
                    return JobDecision::Cancel;
                }
                last_transferred = progress.transferred_bytes;
                last_completed = progress.completed_files;
                return JobDecision::Continue;
            });
        if (!second_result.success || second_result.cancelled || second_result.stopped ||
            !saw_progress || !all_outputs_exist(destination) || live.completed_files() != 4 ||
            live.completed_bytes() != 4 || live.remaining_files() != 0) {
            fs::remove_all(root, ec); return 5;
        }
    }

    {
        const auto destination = root / L"stopped";
        LiveCopyPlan live(four_file_plan(source, destination));
        ExecutionControl first_control;
        bool stop_requested = false;
        const auto stopped = executor.execute(
            live, first_control, JobExecutionOptions{1},
            [&](const JobProgress& progress) {
                if (!stop_requested && progress.completed_files >= 1) {
                    stop_requested = true;
                    first_control.request_stop();
                }
                return JobDecision::Continue;
            });

        if (!stopped.stopped || stopped.success || stopped.cancelled || !stop_requested ||
            live.completed_files() == 0 || live.completed_files() >= 4 || live.remaining_files() == 0) {
            fs::remove_all(root, ec); return 6;
        }

        const auto completed_before_resume = live.completed_files();
        const auto bytes_before_resume = live.completed_bytes();
        ExecutionControl resumed_control;
        bool resumed_progress = false;
        const auto resumed = executor.execute(
            live, resumed_control, JobExecutionOptions{1},
            [&](const JobProgress& progress) {
                resumed_progress = true;
                if (progress.completed_files < completed_before_resume ||
                    progress.transferred_bytes < bytes_before_resume ||
                    progress.total_files != 4 || progress.total_bytes != 4) return JobDecision::Cancel;
                return JobDecision::Continue;
            });

        if (!resumed.success || resumed.cancelled || resumed.stopped || !resumed_progress ||
            live.completed_files() != 4 || live.completed_bytes() != 4 || live.remaining_files() != 0 ||
            !all_outputs_exist(destination)) {
            fs::remove_all(root, ec); return 7;
        }
    }

    fs::remove_all(root, ec);
    return 0;
}
