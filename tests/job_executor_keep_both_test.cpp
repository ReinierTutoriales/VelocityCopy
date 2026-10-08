#include "velocitycopy/execution_control.hpp"
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/live_copy_plan.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
namespace fs = std::filesystem;
using namespace velocitycopy;

void write_text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

std::string read_text(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

CopyPlan make_plan(const fs::path& source, const fs::path& destination, const FileOperation operation) {
    CopyPlan raw{};
    raw.operation = operation;
    raw.source_roots.push_back(source);
    raw.destination_root = destination.parent_path();
    raw.directories.push_back({destination.parent_path()});
    raw.files.push_back({1, source, destination, fs::file_size(source)});
    raw.total_bytes = raw.files.front().size;
    raw.largest_file_bytes = raw.total_bytes;
    return raw;
}
} // namespace

int wmain() {
    const auto root = fs::temp_directory_path() / L"VelocityCopyKeepBothTest";
    std::error_code ec;
    fs::remove_all(root, ec);

    // Copy: the existing file stays untouched and the incoming one is written
    // beside it as "report (2).txt"; "report (2).txt" already taken -> (3).
    {
        const auto source = root / L"copy-source" / L"report.txt";
        const auto destination = root / L"copy-destination" / L"report.txt";
        write_text(source, "incoming");
        write_text(destination, "existing");
        write_text(root / L"copy-destination" / L"report (2).txt", "older copy");

        LiveCopyPlan plan(make_plan(source, destination, FileOperation::Copy));
        ExecutionControl control;
        JobExecutionOptions options{1};
        options.conflict_policy = ConflictPolicy::KeepBothAll;
        const auto result = JobExecutor{}.execute(plan, control, options, {});
        if (!result.success || result.destination_conflict || result.outcomes.succeeded != 1 ||
            read_text(destination) != "existing" ||
            read_text(root / L"copy-destination" / L"report (2).txt") != "older copy" ||
            read_text(root / L"copy-destination" / L"report (3).txt") != "incoming") {
            fs::remove_all(root, ec);
            return 1;
        }
    }

    // Move with a targeted KeepBoth decision: source removed, both kept.
    {
        const auto source = root / L"move-source" / L"photo.jpg";
        const auto destination = root / L"move-destination" / L"photo.jpg";
        write_text(source, "new photo");
        write_text(destination, "old photo");

        LiveCopyPlan plan(make_plan(source, destination, FileOperation::Move));
        ExecutionControl control;
        JobExecutionOptions options{1};
        options.replace_file_id = 1;
        options.conflict_policy = ConflictPolicy::KeepBoth;
        const auto result = JobExecutor{}.execute(plan, control, options, {});
        if (!result.success || result.outcomes.succeeded != 1 || fs::exists(source) ||
            read_text(destination) != "old photo" ||
            read_text(root / L"move-destination" / L"photo (2).jpg") != "new photo") {
            fs::remove_all(root, ec);
            return 2;
        }
    }

    // Without a keep-both decision the conflict is still reported, untouched.
    {
        const auto source = root / L"prompt-source" / L"a.txt";
        const auto destination = root / L"prompt-destination" / L"a.txt";
        write_text(source, "incoming");
        write_text(destination, "existing");

        LiveCopyPlan plan(make_plan(source, destination, FileOperation::Copy));
        ExecutionControl control;
        const auto result = JobExecutor{}.execute(plan, control, JobExecutionOptions{1}, {});
        if (result.success || !result.destination_conflict || read_text(destination) != "existing" ||
            fs::exists(root / L"prompt-destination" / L"a (2).txt")) {
            fs::remove_all(root, ec);
            return 3;
        }
    }

    fs::remove_all(root, ec);
    return 0;
}
