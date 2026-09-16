#include <filesystem>
#include <fstream>
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
        append.empty() || execution.empty() || queue.empty() || project.empty()) return 1;

    if (!contains(shell, "QueueOrStartCopy(*dispatch.job)") ||
        !contains(xaml, "Click=\"OnQueueOrStartCopyClick\"") ||
        contains(xaml, "OnStartCopyClick") || contains(header, "OnStartCopyClick") ||
        contains(window, "OnStartCopyClick")) return 2;

    if (!contains(append, "append_planner_.enqueue") ||
        !contains(append, "planning_count") ||
        !contains(append, "target_plan->append(std::move(*result.plan), true)") ||
        !contains(append, "deferred_after_stop_jobs_") ||
        !contains(append, "stopped_session_")) return 3;

    // Start and Resume share one production executor loop. The old loop must not
    // survive inside MainWindow.xaml.cpp.
    if (!contains(execution, "RunLivePlanSession(") ||
        !contains(execution, "ResumeStoppedCopy()") ||
        !contains(execution, "RunLivePlanSession(\n                plan, control, gate, stop_token, true)") ||
        !contains(execution, "RunLivePlanSession(\n                plan, control, gate, stop_token, false)") ||
        contains(window, "executor_.execute(")) return 4;

    // Stop is not Cancel: already-reserved append planning is allowed to commit,
    // while jobs arriving after Stop are routed to the stopped-session FIFO.
    const auto stop_pos = execution.find("void MainWindow::OnStopClick");
    const auto cancel_pos = execution.find("void MainWindow::OnCancelClick", stop_pos);
    if (stop_pos == std::string::npos || cancel_pos == std::string::npos) return 5;
    const auto stop_body = execution.substr(stop_pos, cancel_pos - stop_pos);
    if (!contains(stop_body, "stop_requested_ = true") ||
        !contains(stop_body, "request_stop()") ||
        contains(stop_body, "append_planner_.cancel_pending()") ||
        contains(stop_body, "queued_sessions_.clear()")) return 6;

    // Cancel has precedence over a stopped result, including the race while Stop
    // waits for already-accepted append planning to finish.
    if (!contains(execution, "control->directive() == velocitycopy::ExecutionDirective::Cancel") ||
        !contains(execution, "result.stopped && cancel_requested_.load") ||
        !contains(execution, "execution_control_->request_cancel()")) return 7;

    // A stopped session has no ExecutionControl. Resume uses the same LiveCopyPlan;
    // if append planning is still active the user's intent is remembered and the
    // button is disabled until the final reservation commits.
    if (!contains(execution, "if (stopped_session_) {\n        ResumeStoppedCopy();") ||
        !contains(execution, "resume_requested_ = true") ||
        !contains(execution, "PauseButton().IsEnabled(false)") ||
        !contains(execution, "last_queue_completed_files_ = live_plan_->completed_files()") ||
        !contains(execution, "SetExecutionButtonsStopped()") ||
        !contains(execution, "loader.GetString(L\"ActionResume\")") ||
        !contains(header, "bool resume_requested_{}")) return 8;

    // The final append callback consumes that remembered Resume request. Failure
    // also releases the reservation and resumes the pre-existing pending work.
    if (!contains(append, "stopped_session_ && self->resume_requested_") ||
        !contains(append, "self->ResumeStoppedCopy()") ||
        !contains(append, "release_reservation()")) return 9;

    // Initial planning cannot be paused/stopped because no LiveCopyPlan exists.
    if (!contains(execution, "SetExecutionButtonsPlanning()") ||
        !contains(execution, "PauseButton().IsEnabled(false)") ||
        !contains(execution, "StopButton().IsEnabled(false)")) return 10;

    // Different destinations remain serialized; a stopped session blocks the
    // future-session queue until resumed, emptied, or cancelled.
    if (!contains(header, "queued_sessions_") ||
        !contains(append, "queued_sessions_.push_back") ||
        !contains(execution, "stopped_session_ || stop_requested_") ||
        !contains(execution, "StartNextQueuedSession()")) return 11;

    // Queue implementation is isolated, bounded, stable-id based, and bulk.
    if (!contains(queue, "kVisibleQueueItems = 256") ||
        !contains(queue, "row.Tag(box_value(file.id))") ||
        !contains(queue, "unbox_value<std::uint64_t>(row.Tag())") ||
        !contains(queue, "reorder_pending_files(ordered_ids)") ||
        !contains(queue, "move_pending_files_up(SelectedPendingIds())") ||
        !contains(queue, "move_pending_files_down(SelectedPendingIds())") ||
        !contains(queue, "remove_pending_files(SelectedPendingIds())") ||
        !contains(queue, "FinalizeStoppedSessionIfEmpty()")) return 12;

    if (!contains(project, "MainWindow.Execution.cpp") ||
        !contains(project, "MainWindow.Queue.cpp") ||
        std::filesystem::exists(root / "src/ui/VelocityCopy.UI/MainWindow.QueueDrag.cpp") ||
        contains(project, "MainWindow.QueueDrag.cpp")) return 13;

    return 0;
}
