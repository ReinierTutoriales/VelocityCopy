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
    const std::filesystem::path& destination,
    const velocitycopy::FileOperation operation = velocitycopy::FileOperation::Copy) {
    velocitycopy::CopyPlan plan{};
    plan.source_roots.push_back(source);
    plan.destination_root = destination;
    plan.operation = operation;
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

    ExecutionControl first_control;
    const auto first = executor.execute(plan, first_control, JobExecutionOptions{1}, {});
    if (first.success || first.cancelled || first.stopped || !first.destination_conflict ||
        first.conflict_file_id != 1 || first.conflict_source != source / L"a.txt" ||
        first.conflict_destination != destination / L"a.txt" ||
        read_text(destination / L"a.txt") != "OLD-A" ||
        read_text(destination / L"b.txt") != "OLD-B" ||
        plan.completed_files() != 0 || plan.remaining_files() != 2) {
        fs::remove_all(root, ec);
        return 1;
    }

    // One-shot authorization replaces file 1 and yields immediately. This is a
    // transaction boundary: the session loop can restore adaptive parallelism,
    // and authorization cannot bleed into file 2.
    ExecutionControl second_control;
    JobExecutionOptions replace_first{1};
    replace_first.replace_file_id = first.conflict_file_id;
    const auto replaced_first = executor.execute(plan, second_control, replace_first, {});
    if (!replaced_first.success || replaced_first.cancelled || replaced_first.stopped ||
        replaced_first.destination_conflict || read_text(destination / L"a.txt") != "AAAA" ||
        read_text(destination / L"b.txt") != "OLD-B" || plan.completed_files() != 1 ||
        plan.completed_bytes() != 4 || plan.remaining_files() != 1) {
        fs::remove_all(root, ec);
        return 2;
    }

    // Running again with the safe default must independently surface file 2.
    ExecutionControl third_control;
    const auto second_conflict = executor.execute(plan, third_control, JobExecutionOptions{1}, {});
    if (second_conflict.success || second_conflict.cancelled || second_conflict.stopped ||
        !second_conflict.destination_conflict || second_conflict.conflict_file_id != 2 ||
        read_text(destination / L"b.txt") != "OLD-B" || plan.completed_files() != 1 ||
        plan.remaining_files() != 1) {
        fs::remove_all(root, ec);
        return 3;
    }

    ExecutionControl fourth_control;
    JobExecutionOptions replace_second{1};
    replace_second.replace_file_id = second_conflict.conflict_file_id;
    const auto completed = executor.execute(plan, fourth_control, replace_second, {});
    if (!completed.success || completed.cancelled || completed.stopped || completed.destination_conflict ||
        read_text(destination / L"a.txt") != "AAAA" || read_text(destination / L"b.txt") != "BBBB" ||
        plan.completed_files() != 2 || plan.completed_bytes() != 8 || plan.remaining_files() != 0) {
        fs::remove_all(root, ec);
        return 4;
    }

    // Move conflicts are transactional: an unresolved conflict must preserve
    // every source, and a one-shot replacement may delete only the source whose
    // destination was successfully replaced.
    const auto move_source = root / L"move-source";
    const auto move_destination = root / L"move-destination";
    write_text(move_source / L"a.txt", "MAAA");
    write_text(move_source / L"b.txt", "MBBB");
    write_text(move_destination / L"a.txt", "OLD-MA");
    write_text(move_destination / L"b.txt", "OLD-MB");

    LiveCopyPlan move_plan(make_plan(move_source, move_destination, FileOperation::Move));
    ExecutionControl move_first_control;
    const auto move_first = executor.execute(move_plan, move_first_control, JobExecutionOptions{1}, {});
    if (move_first.success || !move_first.destination_conflict ||
        !fs::exists(move_source / L"a.txt") || !fs::exists(move_source / L"b.txt") ||
        read_text(move_destination / L"a.txt") != "OLD-MA" ||
        read_text(move_destination / L"b.txt") != "OLD-MB") {
        fs::remove_all(root, ec);
        return 5;
    }

    ExecutionControl move_replace_control;
    JobExecutionOptions move_replace{1};
    move_replace.replace_file_id = move_first.conflict_file_id;
    const auto move_replaced = executor.execute(move_plan, move_replace_control, move_replace, {});
    if (!move_replaced.success || fs::exists(move_source / L"a.txt") ||
        !fs::exists(move_source / L"b.txt") ||
        read_text(move_destination / L"a.txt") != "MAAA" ||
        read_text(move_destination / L"b.txt") != "OLD-MB" ||
        move_plan.completed_files() != 1 || move_plan.remaining_files() != 1) {
        fs::remove_all(root, ec);
        return 6;
    }

    fs::remove_all(root, ec);
    return 0;
}
