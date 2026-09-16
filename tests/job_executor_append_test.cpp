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

} // namespace

int wmain() {
    namespace fs = std::filesystem;
    using namespace velocitycopy;

    const auto root = fs::temp_directory_path() / L"VelocityCopyExecutorAppendTest";
    const auto source = root / L"source";
    const auto destination = root / L"destination";
    std::error_code ec;
    fs::remove_all(root, ec);

    write_text(source / L"a.txt", "a");
    write_text(source / L"b.txt", "b");
    write_text(source / L"c.txt", "c");
    write_text(source / L"d.txt", "d");

    LiveCopyPlan live(initial_plan(source, destination));
    JobExecutor executor;
    ExecutionControl control;
    bool appended = false;
    bool saw_expanded_totals = false;

    const auto result = executor.execute(
        live,
        control,
        JobExecutionOptions{1},
        [&](const JobProgress& progress) {
            if (!appended && progress.completed_files >= 1) {
                auto extra = appended_plan(source, destination);
                if (live.append(std::move(extra)) != LivePlanAppendResult::Appended) {
                    return JobDecision::Cancel;
                }
                appended = true;
            }

            if (appended && progress.total_files == 4 && progress.total_bytes == 4) {
                saw_expanded_totals = true;
            }
            return JobDecision::Continue;
        });

    if (!result.success || result.cancelled || result.stopped ||
        !appended || !saw_expanded_totals) {
        fs::remove_all(root, ec);
        return 1;
    }

    for (const auto* name : {L"a.txt", L"b.txt", L"c.txt", L"d.txt"}) {
        if (!fs::exists(destination / name, ec) || ec) {
            fs::remove_all(root, ec);
            return 2;
        }
    }

    const auto snapshot = live.snapshot();
    if (!snapshot.pending_files.empty() || !snapshot.active_files.empty() ||
        snapshot.total_files != 4 || snapshot.total_bytes != 4) {
        fs::remove_all(root, ec);
        return 3;
    }

    fs::remove_all(root, ec);
    return 0;
}
