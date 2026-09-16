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

std::size_t count_occurrences(const std::string& text, const std::string& value) {
    if (value.empty()) return 0;
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(value, offset)) != std::string::npos) {
        ++count;
        offset += value.size();
    }
    return count;
}

int fail(const int code, const char* message) {
    std::cerr << "architecture contract " << code << ": " << message << '\n';
    return code;
}

} // namespace

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto shell = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Shell.cpp");
    const auto xaml = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml");
    const auto header = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.h");
    const auto window = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto append = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.CopyAppend.cpp");
    const auto execution = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Execution.cpp");
    const auto queue = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Queue.cpp");
    const auto project = read_all(root / "src/ui/VelocityCopy.UI/VelocityCopy.UI.vcxproj");

    if (shell.empty() || xaml.empty() || header.empty() || window.empty() ||
        append.empty() || execution.empty() || queue.empty() || project.empty()) {
        return fail(1, "required production source missing");
    }

    if (!contains(shell, "QueueOrStartCopy(*dispatch.job)") ||
        !contains(xaml, "OnQueueOrStartCopyClick") ||
        contains(xaml, "OnStartCopyClick") || contains(header, "OnStartCopyClick") ||
        contains(window, "OnStartCopyClick")) {
        return fail(2, "Explorer/drop must share queue-aware start route");
    }

    if (!contains(append, "append_planner_.enqueue") ||
        !contains(append, "planning_count") ||
        !contains(append, "target_plan->append(std::move(*result.plan), true)") ||
        !contains(append, "deferred_after_stop_jobs_") ||
        !contains(append, "stopped_session_")) {
        return fail(3, "same-destination append pipeline missing");
    }

    // One function definition plus Start and Resume callers. The old executor
    // loop must not survive in MainWindow.xaml.cpp.
    if (count_occurrences(execution, "RunLivePlanSession(") < 3 ||
        !contains(execution, "ResumeStoppedCopy()") ||
        !contains(execution, "stop_token, true") ||
        !contains(execution, "stop_token, false") ||
        contains(window, "executor_.execute(")) {
        return fail(4, "Start and Resume must share one executor loop");
    }

    const auto stop_pos = execution.find("void MainWindow::OnStopClick");
    const auto cancel_pos = execution.find("void MainWindow::OnCancelClick", stop_pos);
    if (stop_pos == std::string::npos || cancel_pos == std::string::npos) {
        return fail(5, "Stop/Cancel handlers missing");
    }
    const auto stop_body = execution.substr(stop_pos, cancel_pos - stop_pos);
    if (!contains(stop_body, "stop_requested_ = true") ||
        !contains(stop_body, "request_stop()") ||
        contains(stop_body, "append_planner_.cancel_pending()") ||
        contains(stop_body, "queued_sessions_.clear()")) {
        return fail(6, "Stop must preserve accepted/future work semantics");
    }

    if (!contains(execution, "ExecutionDirective::Cancel") ||
        !contains(execution, "result.stopped && cancel_requested_.load") ||
        !contains(execution, "execution_control_->request_cancel()")) {
        return fail(7, "Cancel must have precedence over Stop");
    }

    if (!contains(execution, "ResumeStoppedCopy();") ||
        !contains(execution, "resume_requested_ = true") ||
        !contains(execution, "PauseButton().IsEnabled(false)") ||
        !contains(execution, "last_queue_completed_files_ = live_plan_->completed_files()") ||
        !contains(execution, "SetExecutionButtonsStopped()") ||
        !contains(execution, "ActionResume") ||
        !contains(header, "bool resume_requested_{}")) {
        return fail(8, "stopped-session Resume state missing");
    }

    if (!contains(append, "resume_requested_") ||
        !contains(append, "ResumeStoppedCopy()") ||
        !contains(append, "release_reservation()")) {
        return fail(9, "append planner must consume remembered Resume intent");
    }

    if (!contains(execution, "SetExecutionButtonsPlanning()") ||
        !contains(execution, "PauseButton().IsEnabled(false)") ||
        !contains(execution, "StopButton().IsEnabled(false)")) {
        return fail(10, "initial planning controls unsafe");
    }

    if (!contains(header, "queued_sessions_") ||
        !contains(append, "queued_sessions_.push_back") ||
        !contains(execution, "stopped_session_ || stop_requested_") ||
        !contains(execution, "StartNextQueuedSession()")) {
        return fail(11, "future destinations must remain serialized");
    }

    if (!contains(queue, "kVisibleQueueItems = 256") ||
        !contains(queue, "row.Tag(box_value(file.id))") ||
        !contains(queue, "unbox_value<std::uint64_t>(row.Tag())") ||
        !contains(queue, "reorder_pending_files(ordered_ids)") ||
        !contains(queue, "move_pending_files_up(SelectedPendingIds())") ||
        !contains(queue, "move_pending_files_down(SelectedPendingIds())") ||
        !contains(queue, "remove_pending_files(SelectedPendingIds())") ||
        !contains(queue, "FinalizeStoppedSessionIfEmpty()")) {
        return fail(12, "bounded stable-id bulk queue contract missing");
    }

    if (!contains(project, "MainWindow.Execution.cpp") ||
        !contains(project, "MainWindow.Queue.cpp") ||
        std::filesystem::exists(root / "src/ui/VelocityCopy.UI/MainWindow.QueueDrag.cpp") ||
        contains(project, "MainWindow.QueueDrag.cpp")) {
        return fail(13, "WinUI translation-unit cutover incomplete");
    }

    return 0;
}
