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

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

velocitycopy::CopyPlan plan_for(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    velocitycopy::CopyPlan plan{};
    plan.source_roots.push_back(source);
    plan.destination_root = destination;
    plan.directories.push_back({destination});
    plan.files.push_back({1, source / "a.txt", destination / "a.txt", 3});
    plan.files.push_back({2, source / "b.txt", destination / "b.txt", 3});
    plan.total_bytes = 6;
    plan.largest_file_bytes = 3;
    return plan;
}
}

int wmain() {
    namespace fs = std::filesystem;
    using namespace velocitycopy;

    const auto root = fs::temp_directory_path() / L"VelocityCopyConflictPolicyTest";
    const auto source = root / L"source";
    std::error_code ec;
    fs::remove_all(root, ec);
    write_text(source / L"a.txt", "new");
    write_text(source / L"b.txt", "new");

    JobExecutor executor;

    {
        const auto destination = root / L"replace-all";
        write_text(destination / L"a.txt", "old");
        write_text(destination / L"b.txt", "old");
        LiveCopyPlan live(plan_for(source, destination));
        ExecutionControl control;
        JobExecutionOptions options{1};
        options.conflict_policy = ConflictPolicy::ReplaceAll;
        const auto result = executor.execute(live, control, options, {});
        if (!result.success || result.cancelled || result.stopped || result.destination_conflict ||
            result.outcomes.succeeded != 2 || result.outcomes.skipped != 0 ||
            read_text(destination / L"a.txt") != "new" || read_text(destination / L"b.txt") != "new") {
            fs::remove_all(root, ec);
            return 1;
        }
    }

    {
        const auto destination = root / L"skip-all";
        write_text(destination / L"a.txt", "old");
        write_text(destination / L"b.txt", "old");
        LiveCopyPlan live(plan_for(source, destination));
        ExecutionControl control;
        JobExecutionOptions options{1};
        options.conflict_policy = ConflictPolicy::SkipAll;
        const auto result = executor.execute(live, control, options, {});
        if (!result.success || result.cancelled || result.stopped || result.destination_conflict ||
            result.outcomes.skipped != 2 || result.outcomes.succeeded != 0 ||
            live.remaining_files() != 0 || read_text(destination / L"a.txt") != "old" ||
            read_text(destination / L"b.txt") != "old") {
            fs::remove_all(root, ec);
            return 2;
        }
    }


    {
        const auto destination = root / L"skip-all-mixed";
        write_text(destination / L"a.txt", "old");
        LiveCopyPlan live(plan_for(source, destination));
        ExecutionControl control;
        JobExecutionOptions options{1};
        options.conflict_policy = ConflictPolicy::SkipAll;
        const auto result = executor.execute(live, control, options, {});
        if (!result.success || result.cancelled || result.stopped || result.destination_conflict ||
            result.outcomes.skipped != 1 || result.outcomes.succeeded != 1 ||
            live.remaining_files() != 0 || read_text(destination / L"a.txt") != "old" ||
            read_text(destination / L"b.txt") != "new") {
            fs::remove_all(root, ec);
            return 3;
        }
    }

    fs::remove_all(root, ec);
    return 0;
}
