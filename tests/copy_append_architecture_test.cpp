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
    const auto project = read_all(root / "src/ui/VelocityCopy.UI/VelocityCopy.UI.vcxproj");

    if (shell.empty() || xaml.empty() || header.empty() || window.empty() ||
        append.empty() || project.empty()) {
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

    // Queue UI materialization remains bounded even when pending_count is huge.
    if (!contains(window, "kVisibleQueueItems = 256") ||
        !contains(window, "queue_view(kVisibleQueueItems)") ||
        !contains(window, "view.pending_count")) {
        return 7;
    }

    // Visual queue identity is the immutable file_id, never the displayed path.
    // Drag and multi-select commands must use one bulk core operation rather
    // than repeated O(n) per-item mutations.
    if (!contains(window, "row.Tag(box_value(file.id))") ||
        !contains(window, "unbox_value<std::uint64_t>(row.Tag())") ||
        !contains(window, "reorder_pending_files(ordered_ids)") ||
        !contains(window, "move_pending_files_up(SelectedPendingIds())") ||
        !contains(window, "move_pending_files_down(SelectedPendingIds())") ||
        !contains(window, "remove_pending_files(SelectedPendingIds())") ||
        contains(window, "source == hstring(queue_snapshot_")) {
        return 8;
    }

    // The old queue-drag translation unit is intentionally gone. Keeping a
    // placeholder or compiling it again would recreate a parallel production path.
    if (std::filesystem::exists(root / "src/ui/VelocityCopy.UI/MainWindow.QueueDrag.cpp") ||
        contains(project, "MainWindow.QueueDrag.cpp")) {
        return 9;
    }

    return 0;
}
