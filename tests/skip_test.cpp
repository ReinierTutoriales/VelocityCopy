#include "velocitycopy/execution_control.hpp"
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/live_copy_plan.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

velocitycopy::CopyPlan make_plan(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    velocitycopy::CopyPlan plan{};
    plan.source_roots.push_back(source);
    plan.destination_root = destination;
    plan.directories.push_back({destination});
    plan.files.push_back({1, source / L"a.txt", destination / L"a.txt", 4});
    plan.files.push_back({2, source / L"b.txt", destination / L"b.txt", 4});
    plan.total_bytes = 8;
    plan.largest_file_bytes = 4;
    return plan;
}

} // namespace

int wmain() {
    namespace fs = std::filesystem;
    using namespace velocitycopy;

    const auto root = fs::temp_directory_path() / L"VelocityCopySkipTest";
    const auto source = root / L"source";
    std::error_code ec;
    fs::remove_all(root, ec);
    write_text(source / L"a.txt", "AAAA");
    write_text(source / L"b.txt", "BBBB");

    JobExecutor executor;

    // New destination: a targeted pre-start Skip removes only that file from
    // the logical plan and continues the same session.
    {
        const auto destination = root / L"safe";
        LiveCopyPlan plan(make_plan(source, destination));
        ExecutionControl control;
        control.request_skip(1);

        bool saw_second = false;
        const auto result = executor.execute(
            plan, control, JobExecutionOptions{1},
            [&](const JobProgress& progress) {
                if (progress.current_file_id == 2) {
                    saw_second = true;
                    if (!progress.current_file_skippable) return JobDecision::Cancel;
                }
                return JobDecision::Continue;
            });

        if (!result.success || result.cancelled || result.stopped || !saw_second ||
            fs::exists(destination / L"a.txt") ||
            !fs::exists(destination / L"b.txt") ||
            plan.total_files() != 1 || plan.total_bytes() != 4 ||
            plan.completed_files() != 1 || plan.completed_bytes() != 4 ||
            plan.remaining_files() != 0) {
            fs::remove_all(root, ec);
            return 1;
        }
    }

    // Pre-existing destination: the active item must be advertised as unsafe
    // to skip. Even if a skip request is injected directly, the executor must
    // not consume it as a destructive Skip or delete the destination.
    {
        const auto destination = root / L"existing";
        write_text(destination / L"a.txt", "ORIGINAL");
        LiveCopyPlan plan(make_plan(source, destination));
        ExecutionControl control;
        control.request_skip(1);

        bool saw_protected = false;
        const auto result = executor.execute(
            plan, control, JobExecutionOptions{1},
            [&](const JobProgress& progress) {
                if (progress.current_file_id == 1) {
                    saw_protected = true;
                    if (progress.current_file_skippable) return JobDecision::Cancel;
                }
                return JobDecision::Continue;
            });

        if (!result.success || result.cancelled || result.stopped || !saw_protected ||
            !fs::exists(destination / L"a.txt") ||
            read_text(destination / L"a.txt") != "AAAA" ||
            !fs::exists(destination / L"b.txt") ||
            plan.total_files() != 2 || plan.completed_files() != 2) {
            fs::remove_all(root, ec);
            return 2;
        }
    }

    fs::remove_all(root, ec);
    return 0;
}
