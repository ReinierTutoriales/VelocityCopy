#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}
int fail(const int code, const char* message) {
    std::cerr << "skip architecture contract " << code << ": " << message << '\n';
    return code;
}
} // namespace

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto control_h = read_all(root / "src/core/include/velocitycopy/execution_control.hpp");
    const auto control_cpp = read_all(root / "src/core/execution_control.cpp");
    const auto executor_h = read_all(root / "src/core/include/velocitycopy/job_executor.hpp");
    const auto executor_cpp = read_all(root / "src/core/job_executor.cpp");
    const auto live_h = read_all(root / "src/core/include/velocitycopy/live_copy_plan.hpp");
    const auto live_cpp = read_all(root / "src/core/live_copy_plan.cpp");
    const auto snapshot_h = read_all(root / "src/core/include/velocitycopy/ui_snapshot.hpp");
    const auto snapshot_cpp = read_all(root / "src/core/ui_snapshot.cpp");
    const auto xaml = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml");
    const auto window_h = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.h");
    const auto execution = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Execution.cpp");

    if (control_h.empty() || control_cpp.empty() || executor_h.empty() || executor_cpp.empty() ||
        live_h.empty() || live_cpp.empty() || snapshot_h.empty() || snapshot_cpp.empty() ||
        xaml.empty() || window_h.empty() || execution.empty()) {
        return fail(1, "required production source missing");
    }

    if (!contains(control_h, "request_skip(std::uint64_t file_id)") ||
        !contains(control_h, "consume_skip(std::uint64_t file_id)") ||
        !contains(control_cpp, "skip_file_ids_.insert(file_id)") ||
        !contains(control_cpp, "skip_file_ids_.erase(file_id)")) {
        return fail(2, "Skip must be targeted by stable file id");
    }

    if (!contains(executor_h, "current_file_id") ||
        !contains(executor_h, "current_file_skippable") ||
        !contains(executor_cpp, "destination_is_safe_to_discard") ||
        !contains(executor_cpp, "control.consume_skip(file_id)") ||
        !contains(executor_cpp, "CopyDecision::Skip") ||
        !contains(executor_cpp, "remove_partial_destination") ||
        !contains(executor_cpp, "plan.skip_active(file_id)")) {
        return fail(3, "executor targeted Skip path incomplete");
    }

    if (!contains(live_h, "skip_active(std::uint64_t file_id)") ||
        !contains(live_h, "recompute_largest_file_bytes_locked") ||
        !contains(live_cpp, "reserved_destination_keys_.erase(destination_key)") ||
        !contains(live_cpp, "recompute_largest_file_bytes_locked()")) {
        return fail(4, "live plan Skip accounting incomplete");
    }

    if (!contains(snapshot_h, "current_file_id") ||
        !contains(snapshot_h, "current_file_skippable") ||
        !contains(snapshot_cpp, "snapshot.current_file_id = progress.current_file_id") ||
        !contains(snapshot_cpp, "snapshot.current_file_skippable = progress.current_file_skippable")) {
        return fail(5, "Skip identity/safety must reach UI snapshots");
    }

    if (!contains(xaml, "x:Name=\"SkipButton\"") ||
        !contains(xaml, "Click=\"OnSkipClick\"") ||
        !contains(window_h, "current_file_skippable_") ||
        !contains(execution, "snapshot.current_file_skippable") ||
        !contains(execution, "request_skip(current_file_id_)") ||
        !contains(execution, "current_file_id_ != 0 && current_file_skippable_") ||
        !contains(execution, "Localization failure must never mutate the execution state")) {
        return fail(6, "WinUI safe Skip contract incomplete");
    }

    return 0;
}
