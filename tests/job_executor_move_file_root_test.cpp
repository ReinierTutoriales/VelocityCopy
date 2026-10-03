#include "velocitycopy/execution_control.hpp"
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/live_copy_plan.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {
namespace fs = std::filesystem;
using namespace velocitycopy;
void write_text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}
}

int wmain() {
    const auto root = fs::temp_directory_path() / L"VelocityCopyMoveFileRootTest";
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto source = root / L"source" / L"image.iso";
    const auto destination_root = root / L"destination";
    write_text(source, "ISO!");
    CopyPlan raw{};
    raw.operation = FileOperation::Move;
    raw.source_roots.push_back(source);
    raw.destination_root = destination_root;
    raw.directories.push_back({destination_root});
    raw.files.push_back({1, source, destination_root / L"image.iso", 4});
    raw.total_bytes = 4;
    raw.largest_file_bytes = 4;
    LiveCopyPlan plan(std::move(raw));
    ExecutionControl control;
    JobExecutor executor;
    const auto result = executor.execute(plan, control, JobExecutionOptions{1}, {});
    const bool ok = result.success && !result.cancelled && !result.stopped &&
        result.native_code == 0 && result.outcomes.succeeded == 1 &&
        !fs::exists(source) && fs::exists(destination_root / L"image.iso") &&
        fs::exists(source.parent_path());
    fs::remove_all(root, ec);
    return ok ? 0 : 1;
}
