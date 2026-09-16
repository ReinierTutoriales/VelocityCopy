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

    const auto root = fs::temp_directory_path() / L"VelocityCopyExistingDestinationTest";
    const auto source = root / L"source";
    const auto destination = root / L"destination";
    std::error_code ec;
    fs::remove_all(root, ec);

    write_text(source / L"a.txt", "AAAA");
    write_text(source / L"b.txt", "BBBB");
    write_text(destination / L"a.txt", "OLD-A");
    write_text(destination / L"b.txt", "OLD-B");

    LiveCopyPlan plan(make_plan(source, destination));
    JobExecutor executor;

    // Default policy must never overwrite an existing destination.
    ExecutionControl first_control;
    const auto first = executor.execute(
        plan, first_control, JobExecutionOptions{1}, {});
    if (first.success || first.cancelled || first.stopped || !first.destination_conflict ||
        first.conflict_file_id != 1 || first.conflict_source != source / L"a.txt" ||
        first.conflict_destination != destination / L"a.txt" ||
        read_text(destination / L"a.txt") != "OLD-A" ||
        read_text(destination / L"b.txt") != "OLD-B" ||
        plan.completed_files() != 0 || plan.remaining_files() != 2) {
        fs::remove_all(root, ec);
        return 1;
    }

    // Authorizing the first conflict must replace only file 1. File 2 must
    // still stop on its own conflict rather than inheriting a global overwrite.
    ExecutionControl second_control;
    JobExecutionOptions replace_first{1};
    replace_first.replace_file_id = first.conflict_file_id;
    const auto second = executor.execute(plan, second_control, replace_first, {});
    if (second.success || second.cancelled || second.stopped || !second.destination_conflict ||
        second.conflict_file_id != 2 || read_text(destination / L"a.txt") != "AAAA" ||
        read_text(destination / L"b.txt") != "OLD-B" ||
        plan.completed_files() != 1 || plan.completed_bytes() != 4 ||
        plan.remaining_files() != 1) {
        fs::remove_all(root, ec);
        return 2;
    }

    ExecutionControl third_control;
    JobExecutionOptions replace_second{1};
    replace_second.replace_file_id = second.conflict_file_id;
    const auto third = executor.execute(plan, third_control, replace_second, {});
    if (!third.success || third.cancelled || third.stopped || third.destination_conflict ||
        read_text(destination / L"a.txt") != "AAAA" ||
        read_text(destination / L"b.txt") != "BBBB" ||
        plan.completed_files() != 2 || plan.completed_bytes() != 8 ||
        plan.remaining_files() != 0) {
        fs::remove_all(root, ec);
        return 3;
    }

    fs::remove_all(root, ec);
    return 0;
}
