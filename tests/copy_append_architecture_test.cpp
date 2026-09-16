#include <filesystem>
#include <fstream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {

std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}

} // namespace

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};

    const auto shell = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Shell.cpp");
    const auto xaml = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml");
    const auto header = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.h");
    const auto window = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto append = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.CopyAppend.cpp");

    if (shell.empty() || xaml.empty() || header.empty() || window.empty() || append.empty()) {
        return 1;
    }

    // Explorer and drag/drop must share the same queue-aware production route.
    // The obsolete direct-start handler must not survive as a parallel path.
    if (!contains(shell, "QueueOrStartCopy(*dispatch.job)") ||
        !contains(xaml, "Click=\"OnQueueOrStartCopyClick\"") ||
        contains(xaml, "OnStartCopyClick") ||
        contains(header, "OnStartCopyClick") ||
        contains(window, "OnStartCopyClick")) {
        return 2;
    }

    // Planning stays off the UI thread and an accepted same-destination batch
    // reserves the active session before enumeration begins.
    if (!contains(append, "append_planner_.enqueue") ||
        !contains(append, "planning_count") ||
        !contains(append, "target_plan->append(std::move(*result.plan), true)") ||
        !contains(append, "LivePlanAppendResult::Appended")) {
        return 3;
    }

    // If the worker pool drains while a reserved append is still planning, the
    // same copy session waits without polling and resumes the same LiveCopyPlan.
    if (!contains(window, "gate->condition.wait") ||
        !contains(window, "gate->planning_count") ||
        !contains(window, "snapshot.pending_files.empty()") ||
        !contains(window, "EnqueueAppend(")) {
        return 4;
    }

    return 0;
}
