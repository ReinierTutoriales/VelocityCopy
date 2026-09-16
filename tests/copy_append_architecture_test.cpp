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

    // Explorer and drag/drop share the same queue-aware production route. The
    // obsolete direct-start handler must not survive as a parallel path.
    if (!contains(shell, "QueueOrStartCopy(*dispatch.job)") ||
        !contains(xaml, "Click=\"OnQueueOrStartCopyClick\"") ||
        contains(xaml, "OnStartCopyClick") ||
        contains(header, "OnStartCopyClick") ||
        contains(window, "OnStartCopyClick")) {
        return 2;
    }

    // Same-destination work is planned off the UI thread and reserves the
    // active session before enumeration, including the drain-boundary append.
    if (!contains(append, "append_planner_.enqueue") ||
        !contains(append, "planning_count") ||
        !contains(append, "target_plan->append(std::move(*result.plan), true)") ||
        !contains(append, "LivePlanAppendResult::Appended")) {
        return 3;
    }

    // The copy thread waits without polling while reserved planning is active
    // and checks remaining work in O(1) instead of copying a full plan snapshot.
    if (!contains(window, "gate->condition.wait") ||
        !contains(window, "gate->planning_count") ||
        !contains(window, "plan->remaining_files()") ||
        contains(window, "snapshot.pending_files.empty()")) {
        return 4;
    }

    // A second destination never replaces the active jthread. It is serialized
    // as a future session and advanced only after the current session succeeds.
    if (!contains(header, "queued_sessions_") ||
        !contains(append, "queued_sessions_.push_back") ||
        !contains(window, "StartNextQueuedSession()") ||
        !contains(window, "queued_sessions_.pop_front()")) {
        return 5;
    }

    // Cancel clears future sessions, while Stop does not. This preserves the
    // distinction between abort-all and stop-current-session semantics.
    const auto cancel_pos = window.find("void MainWindow::OnCancelClick");
    const auto finish_pos = window.find("void MainWindow::ApplySnapshot", cancel_pos);
    if (cancel_pos == std::string::npos || finish_pos == std::string::npos ||
        window.substr(cancel_pos, finish_pos - cancel_pos).find("queued_sessions_.clear()") == std::string::npos) {
        return 6;
    }

    // Queue UI materialization must remain bounded even when pending_count is huge.
    if (!contains(window, "kVisibleQueueItems = 256") ||
        !contains(window, "queue_view(kVisibleQueueItems)") ||
        !contains(window, "view.pending_count")) {
        return 7;
    }

    return 0;
}
