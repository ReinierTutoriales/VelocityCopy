#include "architecture_support.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
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
    const auto app = read_source(root / "src/ui/VelocityCopy.UI/App.xaml.cpp");
    const auto xaml = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml");
    const auto header = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.h");
    const auto window = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto append = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.CopyAppend.cpp");
    const auto conflict = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.Conflict.cpp");
    const auto execution = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.Execution.cpp");
    const auto queue = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.Queue.cpp");
    const auto project = read_source(root / "src/ui/VelocityCopy.UI/VelocityCopy.UI.vcxproj");
    const auto manifest = read_source(root / "tools/VelocityCopy-Test-Installer.nsi");
    const auto explorer = read_source(root / "src/shell/drop_handler.cpp");
    const auto cli = read_source(root / "src/app/main.cpp");
    const auto cmake = read_source(root / "CMakeLists.txt");
    const auto engine_h = read_source(root / "src/core/include/velocitycopy/copy_engine.hpp");
    const auto engine_cpp = read_source(root / "src/core/copy_engine.cpp");
    const auto live_h = read_source(root / "src/core/include/velocitycopy/live_copy_plan.hpp");
    const auto executor_h = read_source(root / "src/core/include/velocitycopy/job_executor.hpp");
    const auto executor_cpp = read_source(root / "src/core/job_executor.cpp");

    if (app.empty() || xaml.empty() || header.empty() || window.empty() ||
        append.empty() || conflict.empty() || execution.empty() || queue.empty() || project.empty() ||
        manifest.empty() || explorer.empty() || cli.empty() || cmake.empty() ||
        engine_h.empty() || engine_cpp.empty() || live_h.empty() || executor_h.empty() || executor_cpp.empty()) {
        return fail(1, "required production source missing");
    }

    if (!contains(app, "shell_session_.dispatch(request)") ||
        !contains(app, "StartTransfer(std::move(job))") ||
        !contains(app, "AppendTransfer(std::move(job))") ||
        !contains(app, "EnqueueTransfer(std::move(job))") ||
        !contains(window, "AppendTransfer(std::move(job))") ||
        contains(xaml, "OnQueueOrStartCopyClick") || contains(xaml, "OnStartCopyClick") ||
        contains(header, "OnQueueOrStartCopyClick") || contains(header, "OnStartCopyClick") ||
        contains(append, "OnQueueOrStartCopyClick") || contains(append, "OnStartCopyClick")) {
        return fail(2, "Explorer must choose one explicit transfer action and active-session drop must append directly without chooser handlers");
    }

    if (!contains(append, "append_planner_.enqueue") || !contains(append, "planning_count") ||
        !contains(append, "target_plan->append") || !contains(append, "deferred_interrupted_jobs_") ||
        !contains(append, "stopped_session_") || !contains(append, "conflict_session_")) {
        return fail(3, "same-destination append pipeline missing");
    }

    if (count_occurrences(execution, "RunLivePlanSession(") < 3 ||
        !contains(execution, "ResumeStoppedCopy()") || contains(window, "executor_.execute(")) {
        return fail(4, "Start and Resume must share one executor loop");
    }

    const auto stop_pos = execution.find("void MainWindow::OnStopClick");
    const auto cancel_pos = execution.find("void MainWindow::OnCancelClick", stop_pos);
    if (stop_pos == std::string::npos || cancel_pos == std::string::npos) return fail(5, "Stop/Cancel handlers missing");
    const auto stop_body = execution.substr(stop_pos, cancel_pos - stop_pos);
    if (!contains(stop_body, "stop_requested_") || !contains(stop_body, "request_stop()") ||
        contains(stop_body, "append_planner_.cancel_pending()") || contains(stop_body, "queued_sessions_.clear()")) {
        return fail(6, "Stop must preserve accepted/future work semantics");
    }

    if (!contains(execution, "ExecutionDirective::Cancel") || !contains(execution, "request_cancel()")) {
        return fail(7, "Cancel must have precedence over Stop");
    }

    if (!contains(execution, "ResumeStoppedCopy") || !contains(execution, "resume_requested_") ||
        !contains(execution, "SetExecutionButtonsStopped") || !contains(header, "bool resume_requested_{}")) {
        return fail(8, "stopped-session Resume state missing");
    }

    if (!contains(append, "resume_requested_") || !contains(append, "ResumeStoppedCopy") ||
        !contains(append, "release_reservation")) {
        return fail(9, "append planner must consume remembered Resume intent");
    }

    if (!contains(execution, "SetExecutionButtonsPlanning") ||
        !contains(execution, "PauseButton().IsEnabled(false)") ||
        !contains(execution, "CancelButton().IsEnabled(true)") ||
        !contains(execution, "RefreshExecutionMenuState()")) {
        return fail(10, "initial planning controls unsafe");
    }

    if (!contains(header, "queued_sessions_") || !contains(append, "queued_sessions_.push_back") ||
        !contains(app, "velocitycopy::same_destination(") ||
        !contains(append, "EnqueueAppend") || !contains(execution, "StartNextQueuedSession")) {
        return fail(11, "compatible live drops must append while different sessions remain serialized");
    }

    if (!contains(queue, "kVisibleQueueItems") || !contains(queue, "file.id") ||
        !contains(queue, "reorder_pending_files") || !contains(queue, "move_pending_files_up") ||
        !contains(queue, "move_pending_files_down") || !contains(queue, "remove_pending_files")) {
        return fail(12, "bounded stable-id bulk queue contract missing");
    }

    if (!contains(project, "MainWindow.Execution.cpp") || !contains(project, "MainWindow.Queue.cpp") ||
        !contains(project, "MainWindow.Conflict.cpp") ||
        std::filesystem::exists(root / "src/ui/VelocityCopy.UI/MainWindow.QueueDrag.cpp") ||
        contains(project, "MainWindow.QueueDrag.cpp")) {
        return fail(13, "WinUI translation-unit cutover incomplete");
    }

    if (!contains(app, "SingleInstance") || !contains(app, "ShellIpcServer") || contains(cli, "--shell-runtime")) {
        return fail(14, "WinUI must be the sole Explorer activation host");
    }

    if (contains(app, "GetFileAttributesW") || contains(app, "flow_.") || contains(app, "SelectDestination") || contains(app, "BeginShellLayoutAsync")) {
        return fail(15, "Explorer jobs are already resolved and must not re-enter the removed chooser/layout pipeline");
    }

    if (!contains(explorer, "VelocityCopy.WinUI.exe") ||
        contains(explorer, "parent_path() / L\"VelocityCopy.exe\"") ||
        !contains(explorer, "send_shell_request") || !contains(explorer, "launch_velocitycopy_with_request")) {
        return fail(16, "Explorer DLL must dispatch to the WinUI executable");
    }

    if (!contains(manifest, "VelocityCopy.Shell.dll") || !contains(manifest, "DragDropHandlers") ||
        !contains(cmake, "project(VelocityCopy VERSION")) {
        return fail(17, "package/Explorer registration version contract drifted");
    }

    if (!contains(engine_h, "ExistingDestinationPolicy") || !contains(engine_cpp, "COPY_FILE_FAIL_IF_EXISTS") ||
        !contains(executor_h, "destination_conflict") || !contains(executor_h, "replace_file_id") ||
        !contains(executor_cpp, "ExistingDestinationPolicy::Replace")) {
        return fail(18, "existing destinations must fail safely and replacement must be one-shot");
    }

    if (!contains(execution, "destination_conflict") || !contains(execution, "SetExecutionButtonsConflict") ||
        !contains(execution, "ShowConflictDialogAsync") || !contains(conflict, "TaskDialogIndirect") ||
        contains(conflict, "ContentDialog") || contains(conflict, ".XamlRoot(") ||
        !contains(conflict, "ActionReplace") || !contains(conflict, "ActionSkip") ||
        !contains(conflict, "ResumeConflictCopy") || !contains(conflict, "CancelCurrentSession")) {
        return fail(19, "conflict resolution must use a separate native dialog and preserve replace/skip/cancel semantics");
    }

    if (!contains(live_h, "LiveDirectoryBatch") || !contains(live_h, "pending_directories() const") ||
        !contains(live_h, "mark_directories_materialized") || !contains(live_h, "has_pending_directories() const noexcept") ||
        !contains(executor_cpp, "pending_directories()") || !contains(executor_cpp, "mark_directories_materialized") ||
        contains(append, "create_directories") || contains(execution, "create_directories") ||
        contains(queue, "create_directories") || contains(conflict, "create_directories")) {
        return fail(20, "JobExecutor must be the sole live-directory materializer");
    }

    if (!contains(execution, "has_pending_directories") || !contains(conflict, "has_pending_directories") ||
        !contains(queue, "FinalizeStoppedSessionIfEmpty") || !contains(queue, "FinalizeConflictSessionIfEmpty")) {
        return fail(21, "directory-only live work must survive run, Resume, conflict and finalization states");
    }

    if (!contains(window, "accepts_active_transfer_drop") || !contains(window, "DataPackageOperation::Copy") ||
        contains(window, "preferred_drop_operation") || contains(window, "DragDropModifiers::Control") ||
        contains(window, "DragDropModifiers::Shift") || !contains(window, "GetDeferral()") ||
        contains(header, "pending_flow_operation_") || contains(append, "pending_flow_operation_") ||
        contains(header, "flow_") || contains(append, "flow_.make_job")) {
        return fail(22, "window drag/drop must be append-only and chooser state must be fully removed");
    }

    if (contains(app, "ShowAt(") || contains(app, "choose_layout") || contains(app, "flow_.make_job") ||
        !contains(app, "StartTransfer(")) {
        return fail(23, "Explorer transfer must start directly and never block on destination/layout UI");
    }

    const auto start_transfer = [&] {
        const auto start = execution.find("void MainWindow::StartTransfer(");
        if (start == std::string::npos) return std::string{};
        const auto end = execution.find("\n}\n", start);
        return execution.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }();
    if (!contains(header, "planning_sources_") || !contains(start_transfer, "planning_sources_ = job.sources") ||
        !contains(start_transfer, "RefreshQueue();") ||
        !contains(queue, "kPlanningPreviewLimit") ||
        !contains(queue, "execution_control_ && !planning_sources_.empty()")) {
        return fail(24, "accepted sources must be inspectable from the queue while initial planning is still running");
    }

    if (!contains(header, "struct BoundedCondition") ||
        !contains(header, "value.wait_for(") ||
        !contains(header, "std::chrono::seconds(8)") ||
        !contains(header, "*accepting = false") ||
        count_occurrences(execution, "gate->condition.wait(") != 2) {
        return fail(25, "append-planner waits must be time-bounded so a blocked filesystem enumeration cannot freeze transfer finalization");
    }

    const auto pending_start = app.find("void App::StartNextPendingRequest()");
    const auto pending_end = app.find("\n}\n", pending_start);
    const auto pending_body = pending_start == std::string::npos ? std::string{} : app.substr(pending_start, pending_end - pending_start);
    if (!contains(pending_body, "catch (...)") || pending_body.find("request_in_flight_ = false") < pending_body.find("catch (...)") || !contains(app, "ShowPrimaryWindowError();")) return fail(27, "Explorer FIFO must recover from delivery failures and surface rejected requests");

    const auto ui_root = root / "src/ui";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(ui_root)) {
        if (!entry.is_regular_file()) continue;
        const auto source = read_source(entry.path());
        if (contains(source, "QueueOrStartCopy") || contains(source, "same_session") || contains(source, "StartCopy(")) {
            return fail(26, "retired transfer decision names must not return anywhere under src/ui");
        }
    }

    return 0;
}
