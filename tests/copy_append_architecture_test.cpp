#include "architecture_support.hpp"

#include <filesystem>
#include <iostream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
bool contains(const std::string& text, const std::string& value) { return text.find(value) != std::string::npos; }
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
    const auto auxiliary = read_source(root / "src/ui/VelocityCopy.UI/AuxiliarySurface.cpp");
    const auto execution = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.Execution.cpp");
    const auto queue = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.Queue.cpp");
    const auto persistence = read_source(root / "src/ui/VelocityCopy.UI/MainWindow.QueuePersistence.cpp");
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
        append.empty() || conflict.empty() || auxiliary.empty() || execution.empty() || queue.empty() || persistence.empty() || project.empty() ||
        manifest.empty() || explorer.empty() || cli.empty() || cmake.empty() ||
        engine_h.empty() || engine_cpp.empty() || live_h.empty() || executor_h.empty() || executor_cpp.empty()) {
        return fail(1, "required production source missing");
    }

    if (!contains(app, "shell_session_.dispatch(request)") ||
        !contains(app, "StartTransfer(std::move(job), std::move(destination_key), std::move(source_key))") ||
        !contains(app, "AppendTransfer(std::move(job))") ||
        !contains(app, "EnqueueTransfer(std::move(job), std::move(destination_key), std::move(source_key))") ||
        !contains(window, "AppendTransfer(std::move(job))") ||
        contains(xaml, "OnQueueOrStartCopyClick") || contains(xaml, "OnStartCopyClick") ||
        contains(header, "OnQueueOrStartCopyClick") || contains(header, "OnStartCopyClick") ||
        contains(append, "OnQueueOrStartCopyClick") || contains(append, "OnStartCopyClick")) {
        return fail(2, "Explorer must choose one explicit transfer action and active-session drop must append directly without chooser handlers");
    }

    if (!contains(append, "append_planner_.enqueue") || !contains(append, "planning_count") ||
        !contains(append, "target_plan->append") || !contains(append, "deferred_interrupted_jobs_") ||
        !contains(append, "interrupted_session_ != InterruptedSessionState::None")) {
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

    if (!contains(execution, "ResumeStoppedCopy") || !contains(execution, "pending_resume_ = StoppedResume{};") ||
        !contains(execution, "SetExecutionButtonsStopped") || !contains(header, "PendingResume pending_resume_{};")) {
        return fail(8, "stopped-session Resume state missing");
    }

    if (!contains(append, "std::holds_alternative<StoppedResume>(pending_resume_)") || !contains(append, "ResumeStoppedCopy") ||
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
        !contains(app, "velocitycopy::route_transfer(") ||
        !contains(append, "EnqueueAppend") || !contains(execution, "StartNextQueuedSession")) {
        return fail(11, "router decisions must append compatible work while WaitFor sessions remain serialized");
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
        !contains(execution, "ShowConflictDialogAsync") || !contains(conflict, "show_native_decision") ||
        !contains(auxiliary, "TaskDialogIndirect") ||
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

    const auto start_transfer = body_of(execution, "void MainWindow::StartTransfer(");
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

    const auto resolve_body = body_of(app, "App::ResolveStorageKeysAsync(");
    if (resolve_body.empty() || !contains(resolve_body, "catch (...)") ||
        resolve_body.find("request_in_flight_ = false") < resolve_body.find("catch (...)") ||
        !contains(resolve_body, "StartNextPendingRequest();") ||
        !contains(app, "ShowPrimaryWindowError();")) {
        return fail(27, "Explorer FIFO must recover from asynchronous resolution/delivery failures and continue with the next request");
    }

    const auto start_next = body_of(execution, "void MainWindow::StartNextQueuedSession(");
    const auto deliver_job = body_of(app, "void App::DeliverConvertedJob(");
    if (!contains(header, "struct QueuedTransfer") || !contains(header, "StorageKey destination") ||
        !contains(header, "StorageKey source") || !contains(start_next, "StartTransfer(std::move(next.job), std::move(next.destination), std::move(next.source))"))
        return fail(28, "queued routed transfers must preserve resolved storage keys until they start");
    if (deliver_job.empty() || !contains(deliver_job, "velocitycopy::route_transfer(") || contains(deliver_job, "same_destination("))
        return fail(29, "App must route resolved Explorer work without the provisional destination decision");

    const auto start_copy_plan = body_of(persistence, "void MainWindow::StartCopyPlan(");
    if (start_copy_plan.empty() || !contains(start_copy_plan, "active_destination_key_ = {}") ||
        !contains(start_copy_plan, "active_source_key_ = {}"))
        return fail(30, "loaded/recovered plans must clear storage keys inherited from the previous session");

    const auto ask_pos = deliver_job.find("RouteDecision::Ask");
    if (ask_pos == std::string::npos || !contains(deliver_job, "ShowNativeDecisionDialog(") ||
        !contains(deliver_job, "route_preferences_") || !contains(deliver_job, "routing decision cancelled") ||
        contains(deliver_job, "route.recommended =="))
        return fail(32, "Ask routing must use the native decision dialog and process-local preferences instead of the provisional recommendation");
    if (!contains(auxiliary, "pszVerificationText") || !contains(auxiliary, "verification_checked") ||
        !contains(auxiliary, "verification_checked != nullptr ? &checked : nullptr") ||
        !contains(auxiliary, "MessageBoxW("))
        return fail(33, "native routing decisions must support TaskDialog verification while MessageBox fallback cannot remember choices");

    const auto run_live = body_of(execution, "velocitycopy::JobResult MainWindow::RunLivePlanSession(");
    const auto resume_conflict = body_of(conflict, "void MainWindow::ResumeConflictCopy(");
    const auto show_conflict = body_of(conflict, "fire_and_forget MainWindow::ShowConflictDialogAsync(");
    if (run_live.empty() || !contains(run_live, "options.conflict_policy = conflict_policy") ||
        !contains(executor_cpp, "options.conflict_policy == ConflictPolicy::ReplaceAll") ||
        !contains(executor_cpp, "options.conflict_policy == ConflictPolicy::SkipAll") ||
        show_conflict.empty() || !contains(show_conflict, "ConflictApplyToAll") ||
        !contains(show_conflict, "ConflictPolicy::ReplaceAll") ||
        !contains(show_conflict, "ConflictPolicy::SkipAll") ||
        resume_conflict.empty() || !contains(header, "struct ConflictResumeIntent") ||
        !contains(resume_conflict, "pending_resume_ = ConflictResume{{replace_file_id, policy}};")) {
        return fail(34, "apply-to-all conflict decisions must flow from the native dialog through the session into JobExecutionOptions");
    }

    const auto ui_root = root / "src/ui";
    for (const auto& entry : std::filesystem::recursive_directory_iterator(ui_root)) {
        if (!entry.is_regular_file()) continue;
        const auto source = read_source(entry.path());
        if (entry.path().filename().string().rfind("MainWindow.", 0) == 0 && entry.path().extension() == ".cpp" && contains(source, "route_transfer("))
            return fail(31, "route_transfer must remain owned by App and never move into MainWindow");
        if (contains(source, "QueueOrStartCopy") || contains(source, "same_session") || contains(source, "StartCopy(")) {
            return fail(26, "retired transfer decision names must not return anywhere under src/ui");
        }
    }

    const auto continue_interrupted = body_of(append, "void MainWindow::ContinueInterruptedSessionAfterPlanning()");
    if (continue_interrupted.empty() ||
        !contains(continue_interrupted, "ResumeConflictCopy(") ||
        !contains(continue_interrupted, "ResumeStoppedCopy()") ||
        !contains(continue_interrupted, "FinalizeConflictSessionIfEmpty()") ||
        !contains(continue_interrupted, "FinalizeStoppedSessionIfEmpty()") ||
        count_occurrences(append, "std::get_if<ConflictResume>(&pending_resume_)") != 1 ||
        count_occurrences(append, "ContinueInterruptedSessionAfterPlanning();") != 3) {
        return fail(35, "planner completion must route interrupted-session continuation through one decision point");
    }

    // Contract 36: Stopped and Conflict are one mutually exclusive state. The
    // retired booleans must never return anywhere under src/ui, and the header
    // must declare exactly one interrupted-session field of the enum type.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root / "src/ui")) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension();
        if (ext != ".cpp" && ext != ".h" && ext != ".xaml" && ext != ".idl") continue;
        const auto source = read_source(entry.path());
        if (contains(source, "stopped_session_") || contains(source, "conflict_session_"))
            return fail(36, "retired stopped_session_/conflict_session_ booleans must not return");
        if (contains(source, "resume_requested_") || contains(source, "conflict_resume_intent_"))
            return fail(37, "retired resume_requested_/conflict_resume_intent_ fields must not return");
    }
    if (!contains(header, "enum class InterruptedSessionState : std::uint8_t { None, Stopped, Conflict, Decision };") ||
        count_occurrences(header, "InterruptedSessionState interrupted_session_{InterruptedSessionState::None};") != 1 ||
        !contains(header, "bool stop_requested_{};")) {
        return fail(36, "interrupted session must be one enum field; stop_requested_ stays a separate transition flag");
    }

    // Contract 37: a deferred Resume is one variant. The conflict intent exists
    // only inside ConflictResume; each alternative is created only by its own
    // resume function, and only while append planning is still running.
    if (!contains(header, "#include <variant>") ||
        !contains(header, "using PendingResume = std::variant<std::monostate, StoppedResume, ConflictResume>;") ||
        count_occurrences(header, "PendingResume pending_resume_{};") != 1) {
        return fail(37, "pending resume must be declared as the three-alternative variant");
    }
    const auto resume_stopped = body_of(execution, "void MainWindow::ResumeStoppedCopy(");
    std::size_t stopped_builders = 0, conflict_builders = 0;
    for (const auto* file : {&append, &execution, &conflict}) {
        stopped_builders += count_occurrences(*file, "StoppedResume{}");
        conflict_builders += count_occurrences(*file, "ConflictResume{");
    }
    const auto deferred_branch = [](const std::string& body) {
        const auto at = body.find("planning_count != 0");
        return at == std::string::npos ? std::string{} : body.substr(at, 160);
    };
    if (stopped_builders != 1 || conflict_builders != 1 ||
        !contains(deferred_branch(resume_stopped), "pending_resume_ = StoppedResume{};") ||
        !contains(deferred_branch(resume_conflict), "pending_resume_ = ConflictResume{{replace_file_id, policy}};")) {
        return fail(37, "StoppedResume/ConflictResume must be built only in their resume function's deferred branch");
    }
    const auto continue_dispatch = body_of(append, "void MainWindow::ContinueInterruptedSessionAfterPlanning()");
    if (!contains(continue_dispatch, "interrupted_session_ == InterruptedSessionState::Conflict") ||
        !contains(continue_dispatch, "std::get_if<ConflictResume>(&pending_resume_)") ||
        !contains(continue_dispatch, "interrupted_session_ == InterruptedSessionState::Stopped") ||
        !contains(continue_dispatch, "std::holds_alternative<StoppedResume>(pending_resume_)")) {
        return fail(37, "deferred resume must dispatch on interrupted state and matching variant alternative");
    }
    // Entering either interrupted state starts with no pending resume.
    for (const char* entry : {"interrupted_session_ = InterruptedSessionState::Stopped;\n        pending_resume_ = {};",
                              "interrupted_session_ = InterruptedSessionState::Conflict;\n        pending_resume_ = {};"})
        if (!contains(execution, entry)) return fail(37, "entering an interrupted state must clear pending resume");

    // Contract 38: both Decision outcomes that may leave executable work use
    // one launcher. Skip-all must account for concurrent append planning before
    // finalizing, and close the gate only after the session is truly drained.
    const auto resume_parked = body_of(execution, "void MainWindow::ResumeParkedFailures()");
    const auto resolve_parked = body_of(execution, "void MainWindow::ResolveParkedFailures()");
    const auto start_decision = body_of(execution, "void MainWindow::StartDecisionSession(");
    if (resume_parked.empty() || resolve_parked.empty() || start_decision.empty() ||
        !contains(resume_parked, "StartDecisionSession(true);") ||
        !contains(resolve_parked, "planning_count != 0") ||
        !contains(resolve_parked, "remaining_files() != 0") ||
        !contains(resolve_parked, "has_pending_directories()") ||
        !contains(resolve_parked, "StartDecisionSession(false);") ||
        !contains(resolve_parked, "append_gate_->accepting = false") ||
        count_occurrences(start_decision, "RunLivePlanSession(") != 1 ||
        contains(resume_parked, "RunLivePlanSession(") ||
        contains(resolve_parked, "RunLivePlanSession(")) {
        return fail(38, "Decision retry/skip paths must preserve appended work and share one session launcher");
    }

    // Contract 39: source-removal retries are explicit Decision work, and
    // dismissing the decision dialog must not cancel or destroy the session.
    const auto retry_dialog = body_of(execution, "void MainWindow::ShowRetryDecisionAsync()");
    const auto decision_launcher = body_of(execution, "void MainWindow::StartDecisionSession(");
    if (retry_dialog.empty() || decision_launcher.empty() ||
        contains(retry_dialog, "CancelCurrentSession()") ||
        !contains(resume_parked, "StartDecisionSession(true)") ||
        !contains(resolve_parked, "StartDecisionSession(false)") ||
        !contains(decision_launcher, "retry_source_removals") ||
        !contains(execution, "options.retry_source_removals = retry_source_removals")) {
        return fail(39, "Decision dismissal must preserve work and source-removal retry must be explicit");
    }

    // Contract 40: dismissing the Decision dialog must leave an in-window
    // affordance to reopen it; RetrySourceRemoval attempts are advanced only
    // by the explicit retry execution path.
    const auto pause_click = body_of(execution, "void MainWindow::OnPauseClick(");
    if (pause_click.empty() ||
        !contains(pause_click, "InterruptedSessionState::Decision") ||
        !contains(pause_click, "ShowRetryDecisionAsync();") ||
        !contains(execution, "PauseButton().IsEnabled(true);") ||
        !contains(execution, "ActionRetryAll")) {
        return fail(40, "Decision dismissal must leave a retry affordance in the transfer window");
    }

    const auto reset_item = body_of(execution, "void MainWindow::ResetCurrentItemState() noexcept");
    const auto reset_interrupted = body_of(execution, "void MainWindow::ResetInterruptedSessionState() noexcept");
    if (reset_item.empty() || !contains(reset_item, "current_file_id_ = 0") ||
        !contains(reset_item, "current_file_skippable_ = false") ||
        !contains(reset_item, "paused_ = false") ||
        contains(reset_item, "ResetCurrentItemState()")) {
        return fail(28, "current-item reset must remain concrete and non-recursive");
    }
    if (reset_interrupted.empty() ||
        !contains(reset_interrupted, "interrupted_session_ = InterruptedSessionState::None") ||
        !contains(reset_interrupted, "stop_requested_ = false") ||
        !contains(reset_interrupted, "pending_resume_ = {}")) {
        return fail(29, "interrupted-session reset must clear its state as one unit");
    }

    return 0;
}
