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
bool contains(const std::string& text, const std::string& value) { return text.find(value) != std::string::npos; }
std::size_t count_occurrences(const std::string& text, const std::string& value) {
    if (value.empty()) return 0;
    std::size_t count = 0, offset = 0;
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
    const auto app = read_all(root / "src/ui/VelocityCopy.UI/App.xaml.cpp");
    const auto shell = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Shell.cpp");
    const auto xaml = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml");
    const auto header = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.h");
    const auto window = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto append = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.CopyAppend.cpp");
    const auto conflict = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Conflict.cpp");
    const auto execution = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Execution.cpp");
    const auto queue = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Queue.cpp");
    const auto project = read_all(root / "src/ui/VelocityCopy.UI/VelocityCopy.UI.vcxproj");
    const auto manifest = read_all(root / "src/ui/VelocityCopy.UI/Package.appxmanifest");
    const auto explorer = read_all(root / "src/shell/explorer_commands.cpp");
    const auto cli = read_all(root / "src/app/main.cpp");
    const auto cmake = read_all(root / "CMakeLists.txt");
    const auto engine_h = read_all(root / "src/core/include/velocitycopy/copy_engine.hpp");
    const auto engine_cpp = read_all(root / "src/core/copy_engine.cpp");
    const auto live_h = read_all(root / "src/core/include/velocitycopy/live_copy_plan.hpp");
    const auto executor_h = read_all(root / "src/core/include/velocitycopy/job_executor.hpp");
    const auto executor_cpp = read_all(root / "src/core/job_executor.cpp");

    if (app.empty() || shell.empty() || xaml.empty() || header.empty() || window.empty() ||
        append.empty() || conflict.empty() || execution.empty() || queue.empty() || project.empty() ||
        manifest.empty() || explorer.empty() || cli.empty() || cmake.empty() ||
        engine_h.empty() || engine_cpp.empty() || live_h.empty() || executor_h.empty() || executor_cpp.empty()) {
        return fail(1, "required production source missing");
    }

    if (!contains(shell, "QueueOrStartCopy(std::move(*dispatch.job))") ||
        !contains(xaml, "OnQueueOrStartCopyClick") || contains(xaml, "OnStartCopyClick") ||
        contains(header, "OnStartCopyClick") || contains(window, "OnStartCopyClick")) {
        return fail(2, "Explorer/drop must share queue-aware start route");
    }

    if (!contains(append, "append_planner_.enqueue") || !contains(append, "planning_count") ||
        !contains(append, "target_plan->append(std::move(*result.plan), true)") ||
        !contains(append, "deferred_interrupted_jobs_") || !contains(append, "stopped_session_") ||
        !contains(append, "conflict_session_")) {
        return fail(3, "same-destination append pipeline missing");
    }

    if (count_occurrences(execution, "RunLivePlanSession(") < 3 ||
        !contains(execution, "ResumeStoppedCopy()") ||
        !contains(execution, "stop_token, true, 0") ||
        !contains(execution, "stop_token, false, 0") ||
        contains(window, "executor_.execute(")) {
        return fail(4, "Start and Resume must share one executor loop");
    }

    const auto stop_pos = execution.find("void MainWindow::OnStopClick");
    const auto cancel_pos = execution.find("void MainWindow::OnCancelClick", stop_pos);
    if (stop_pos == std::string::npos || cancel_pos == std::string::npos) return fail(5, "Stop/Cancel handlers missing");
    const auto stop_body = execution.substr(stop_pos, cancel_pos - stop_pos);
    if (!contains(stop_body, "stop_requested_ = true") || !contains(stop_body, "request_stop()") ||
        contains(stop_body, "append_planner_.cancel_pending()") || contains(stop_body, "queued_sessions_.clear()")) {
        return fail(6, "Stop must preserve accepted/future work semantics");
    }

    if (!contains(execution, "ExecutionDirective::Cancel") ||
        !contains(execution, "result.stopped && cancel_requested_.load") ||
        !contains(execution, "execution_control_->request_cancel()")) {
        return fail(7, "Cancel must have precedence over Stop");
    }

    if (!contains(execution, "ResumeStoppedCopy();") || !contains(execution, "resume_requested_ = true") ||
        !contains(execution, "PauseButton().IsEnabled(false)") ||
        !contains(execution, "last_queue_completed_files_ = live_plan_->completed_files()") ||
        !contains(execution, "SetExecutionButtonsStopped()") || !contains(execution, "ActionResume") ||
        !contains(header, "bool resume_requested_{}")) {
        return fail(8, "stopped-session Resume state missing");
    }

    if (!contains(append, "resume_requested_") || !contains(append, "ResumeStoppedCopy()") ||
        !contains(append, "release_reservation()")) {
        return fail(9, "append planner must consume remembered Resume intent");
    }

    if (!contains(execution, "SetExecutionButtonsPlanning()") ||
        !contains(execution, "PauseButton().IsEnabled(false)") ||
        !contains(execution, "StopButton().IsEnabled(false)")) {
        return fail(10, "initial planning controls unsafe");
    }

    if (!contains(header, "queued_sessions_") || !contains(append, "queued_sessions_.push_back") ||
        !contains(execution, "conflict_session_") || !contains(execution, "StartNextQueuedSession()")) {
        return fail(11, "future destinations must remain serialized");
    }

    if (!contains(queue, "kVisibleQueueItems = 256") || !contains(queue, "row.Tag(box_value(file.id))") ||
        !contains(queue, "unbox_value<std::uint64_t>(row.Tag())") ||
        !contains(queue, "reorder_pending_files(ordered_ids)") ||
        !contains(queue, "move_pending_files_up(SelectedPendingIds())") ||
        !contains(queue, "move_pending_files_down(SelectedPendingIds())") ||
        !contains(queue, "remove_pending_files(SelectedPendingIds())") ||
        !contains(queue, "FinalizeStoppedSessionIfEmpty()")) {
        return fail(12, "bounded stable-id bulk queue contract missing");
    }

    if (!contains(project, "MainWindow.Execution.cpp") || !contains(project, "MainWindow.Queue.cpp") ||
        !contains(project, "MainWindow.Shell.cpp") || !contains(project, "MainWindow.Conflict.cpp") ||
        std::filesystem::exists(root / "src/ui/VelocityCopy.UI/MainWindow.QueueDrag.cpp") ||
        contains(project, "MainWindow.QueueDrag.cpp")) {
        return fail(13, "WinUI translation-unit cutover incomplete");
    }

    if (!contains(app, "SingleInstance") || !contains(app, "ShellIpcServer") ||
        !contains(app, "is_stage_only_activation") || !contains(app, "send_shell_request(*initial_request, 1000)") ||
        !contains(app, "if (!startup_activation && !is_stage_only_activation(initial_request))") ||
        contains(cli, "--shell-runtime")) {
        return fail(14, "WinUI must be the sole Explorer activation host");
    }

    if (!contains(shell, "ShellAction::PasteToFolder") || !contains(shell, "BeginShellLayoutAsync") ||
        !contains(shell, "resume_background()") || !contains(shell, "GetFileAttributesW") ||
        !contains(shell, "classification_failed") || !contains(shell, "items.size() != sources.size()") ||
        !contains(shell, "flow_.begin") || !contains(shell, "SelectDestination(destination)") ||
        !contains(shell, "DropFlowFlyout().ShowAt")) {
        return fail(15, "Explorer Paste must enter the shared layout flow safely");
    }

    if (!contains(header, "shell_layout_generation_") || !contains(shell, "++shell_layout_generation_") ||
        !contains(shell, "shell_layout_generation_ != generation") || !contains(window, "++shell_layout_generation_")) {
        return fail(16, "stale Explorer layout completions must be suppressed");
    }

    if (!contains(explorer, "VelocityCopy.WinUI.exe") ||
        contains(explorer, "parent_path() / L\"VelocityCopy.exe\"") ||
        !contains(explorer, "send_shell_request(request, 25)") ||
        !contains(explorer, "launch_velocitycopy_with_request")) {
        return fail(17, "Explorer DLL must dispatch to the WinUI executable");
    }

    if (!contains(manifest, "Version=\"0.20.0.0\"") || !contains(cmake, "project(VelocityCopy VERSION 0.20.0") ||
        !contains(manifest, "VelocityCopy.Shell.dll") || !contains(manifest, "windows.fileExplorerContextMenus") ||
        !contains(manifest, "Executable=\"$targetnametoken$.exe\"")) {
        return fail(18, "package/Explorer registration version contract drifted");
    }

    if (!contains(engine_h, "ExistingDestinationPolicy") ||
        !contains(engine_h, "ExistingDestinationPolicy::Fail") ||
        !contains(engine_cpp, "COPY_FILE_FAIL_IF_EXISTS") ||
        !contains(executor_h, "destination_conflict") ||
        !contains(executor_h, "replace_file_id") ||
        !contains(executor_cpp, "options.replace_file_id == file_id") ||
        !contains(executor_cpp, "ExistingDestinationPolicy::Replace") ||
        !contains(executor_cpp, "if (options.replace_file_id == file_id)")) {
        return fail(19, "existing destinations must fail safely and replacement must be one-shot");
    }

    if (!contains(execution, "result.destination_conflict") ||
        !contains(execution, "conflict_session_ = true") ||
        !contains(execution, "SetExecutionButtonsConflict()") ||
        !contains(execution, "ShowConflictDialogAsync(result)") ||
        !contains(execution, "deferred_interrupted_jobs_") ||
        !contains(conflict, "ContentDialog") ||
        !contains(conflict, "ActionReplace") ||
        !contains(conflict, "ActionSkip") ||
        !contains(conflict, "ResumeConflictCopy") ||
        !contains(conflict, "remove_pending_file(conflict.conflict_file_id)") ||
        !contains(conflict, "CancelCurrentSession()")) {
        return fail(20, "native per-file conflict resolution route incomplete");
    }

    if (!contains(live_h, "LiveDirectoryBatch") ||
        !contains(live_h, "pending_directories() const") ||
        !contains(live_h, "mark_directories_materialized") ||
        !contains(live_h, "has_pending_directories() const noexcept") ||
        !contains(executor_cpp, "const auto directory_batch = plan.pending_directories()") ||
        !contains(executor_cpp, "plan.mark_directories_materialized(directory_batch.through_index)") ||
        contains(append, "create_directories") || contains(execution, "create_directories") ||
        contains(queue, "create_directories") || contains(conflict, "create_directories")) {
        return fail(21, "JobExecutor must be the sole live-directory materializer");
    }

    if (!contains(execution, "plan->remaining_files() != 0 || plan->has_pending_directories()") ||
        !contains(execution, "void MainWindow::ResumeStoppedCopy()") ||
        !contains(execution, "live_plan_->remaining_files() == 0 && !live_plan_->has_pending_directories()") ||
        !contains(conflict, "void MainWindow::ResumeConflictCopy") ||
        !contains(conflict, "live_plan_->remaining_files() == 0 && !live_plan_->has_pending_directories()") ||
        !contains(conflict, "void MainWindow::FinalizeConflictSessionIfEmpty()") ||
        count_occurrences(conflict, "live_plan_->has_pending_directories()") < 2 ||
        !contains(queue, "FinalizeStoppedSessionIfEmpty();") ||
        !contains(queue, "FinalizeConflictSessionIfEmpty();")) {
        return fail(22, "directory-only live work must survive run, Resume, conflict and finalization states");
    }

    if (!contains(header, "pending_flow_operation_") ||
        !contains(shell, "const auto operation = job.operation") ||
        !contains(shell, "self->pending_flow_operation_ = operation") ||
        !contains(window, "pending_flow_operation_ = velocitycopy::FileOperation::Copy") ||
        !contains(append, "flow_.make_job(next_job_id_++, pending_flow_operation_)") ||
        !contains(append, "pending_flow_operation_ = velocitycopy::FileOperation::Copy")) {
        return fail(23, "Explorer Cut/Paste operation must survive the shared layout flow");
    }

    return 0;
}