#include "pch.h"
#include "MainWindow.xaml.h"
#include "App.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::SetExecutionButtonsPlanning() {
    RefreshEfficiencyMode();
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    PauseIcon().Glyph(L"\xE769");
    PauseButton().IsEnabled(false);
    CancelButton().IsEnabled(true);
    current_file_id_ = 0;
    current_file_skippable_ = false;
    paused_ = false;
    RefreshExecutionMenuState();
    RefreshExecutionButtonState();
}

void MainWindow::SetExecutionButtonsRunning() {
    RefreshEfficiencyMode();
    PauseButton().IsEnabled(true);
    CancelButton().IsEnabled(true);
    current_file_id_ = 0;
    current_file_skippable_ = false;
    paused_ = false;
    PauseIcon().Glyph(L"\xE769");
    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        const auto label = loader.GetString(L"ActionPause");
        ToolTipService::SetToolTip(PauseButton(), box_value(label));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PauseButton(), label);
    } catch (...) {}
    RefreshExecutionMenuState();
    RefreshExecutionButtonState();
}

void MainWindow::SetExecutionButtonsIdle() {
    PauseButton().IsEnabled(false);
    CancelButton().IsEnabled(false);
    QueueButton().IsEnabled(true);
    current_file_id_ = 0;
    current_file_skippable_ = false;
    paused_ = false;
    resume_requested_ = false;
    PauseIcon().Glyph(L"\xE769");
    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        const auto label = loader.GetString(L"ActionPause");
        ToolTipService::SetToolTip(PauseButton(), box_value(label));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PauseButton(), label);
    } catch (...) {}
    RefreshEfficiencyMode();
    RefreshExecutionMenuState();
    RefreshExecutionButtonState();
}

void MainWindow::SetExecutionButtonsStopped() {
    PauseButton().IsEnabled(true);
    CancelButton().IsEnabled(true);
    current_file_id_ = 0;
    current_file_skippable_ = false;
    paused_ = false;
    PauseIcon().Glyph(L"\xE768");
    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        const auto label = loader.GetString(L"ActionResume");
        ToolTipService::SetToolTip(PauseButton(), box_value(label));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PauseButton(), label);
    } catch (...) {}
    RefreshEfficiencyMode();
    RefreshExecutionMenuState();
    RefreshExecutionButtonState();
}

void MainWindow::SetExecutionButtonsConflict() {
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    PauseIcon().Glyph(L"\xE769");
    PauseButton().IsEnabled(false);
    CancelButton().IsEnabled(true);
    current_file_id_ = 0;
    current_file_skippable_ = false;
    paused_ = false;
    RefreshEfficiencyMode();
    RefreshExecutionMenuState();
    RefreshExecutionButtonState();
}

velocitycopy::JobResult MainWindow::RunLivePlanSession(
    std::shared_ptr<velocitycopy::LiveCopyPlan> plan,
    std::shared_ptr<velocitycopy::ExecutionControl> control,
    std::shared_ptr<AppendGate> gate,
    const std::stop_token stop_token,
    const bool publish_plan,
    std::uint64_t replace_file_id) {
    auto weak = get_weak();
    auto dispatcher = dispatcher_;

    if (publish_plan) {
        (void)dispatcher.TryEnqueue([weak, plan]() {
            if (auto self = weak.get()) self->PublishLivePlan(plan);
        });
    }

    velocitycopy::JobResult result{true, false, S_OK, false};
    for (;;) {
        auto options = executor_.recommend_options(*plan);
        if (replace_file_id != 0) {
            options.worker_count = 1;
            options.replace_file_id = replace_file_id;
        }

        result = executor_.execute(
            *plan, *control, options,
            [this, weak, dispatcher, control, stop_token](const velocitycopy::JobProgress& progress) {
                if (stop_token.stop_requested() || cancel_requested_.load(std::memory_order_relaxed)) {
                    control->request_cancel();
                    return velocitycopy::JobDecision::Cancel;
                }
                if (auto snapshot = presenter_.observe(progress, GetTickCount64())) {
                    const auto value = *snapshot;
                    (void)dispatcher.TryEnqueue([weak, value]() {
                        if (auto self = weak.get()) self->ApplySnapshot(value);
                    });
                }
                return velocitycopy::JobDecision::Continue;
            });
        replace_file_id = 0;

        if (result.cancelled || (!result.success && !result.stopped)) break;

        std::unique_lock gate_lock(gate->mutex);
        if (result.stopped) {
            if (gate->planning_count != 0 && gate->accepting) {
                (void)gate->condition.wait(gate_lock, stop_token, [&] {
                    return gate->planning_count == 0 || !gate->accepting;
                });
            }
            if (stop_token.stop_requested() || cancel_requested_.load(std::memory_order_relaxed) ||
                control->directive() == velocitycopy::ExecutionDirective::Cancel) {
                control->request_cancel();
                result = {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false};
            }
            gate->accepting = false;
            gate->condition.notify_all();
            break;
        }

        if (gate->planning_count != 0 && gate->accepting) {
            (void)gate->condition.wait(gate_lock, stop_token, [&] {
                return gate->planning_count == 0 || !gate->accepting;
            });
        }

        if (stop_token.stop_requested() || cancel_requested_.load(std::memory_order_relaxed)) {
            gate->accepting = false;
            gate->condition.notify_all();
            control->request_cancel();
            result = {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false};
            break;
        }

        if (!gate->accepting) {
            const auto directive = control->directive();
            if (directive == velocitycopy::ExecutionDirective::Cancel) {
                result = {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false};
            } else if (directive == velocitycopy::ExecutionDirective::Stop) {
                result = {false, false, S_OK, true};
            }
            break;
        }

        if (plan->remaining_files() != 0 || plan->has_pending_directories()) {
            gate_lock.unlock();
            continue;
        }
        gate->accepting = false;
        gate->condition.notify_all();
        break;
    }

    {
        std::lock_guard gate_lock(gate->mutex);
        gate->accepting = false;
        gate->condition.notify_all();
    }
    return result;
}

void MainWindow::StartTransfer(velocitycopy::CopyJob job, velocitycopy::StorageKey destination_key, velocitycopy::StorageKey source_key) {
    active_destination_key_ = std::move(destination_key);
    active_source_key_ = std::move(source_key);
    planning_sources_ = job.sources; // Queue preview while a large tree is still being planned.
    ResetTransferSurface();
    active_destination_ = job.destination;
    active_operation_ = job.operation;
    stopped_session_ = false;
    conflict_session_ = false;
    conflict_replace_file_id_ = 0;
    stop_requested_ = false;
    resume_requested_ = false;
    current_file_id_ = 0;
    current_file_skippable_ = false;
    cancel_requested_.store(false, std::memory_order_relaxed);
    presenter_.reset();
    last_queue_completed_files_ = 0;
    live_plan_.reset();
    execution_control_ = std::make_shared<velocitycopy::ExecutionControl>();
    append_gate_ = std::make_shared<AppendGate>();
    queue_snapshot_.clear();
    QueueList().Items().Clear();
    QueueCountText().Text(L"0");
    QueueButton().IsEnabled(true);
    QueuePanel().Visibility(Visibility::Collapsed);
    ResizeWindow(72);
    SetProgressFraction(0.0);
    SetExecutionButtonsPlanning();
    CurrentItemText().Text(job.display_name.empty() ? hstring(L"…") : hstring(job.display_name));
    RefreshQueue();

    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    auto control = execution_control_;
    auto gate = append_gate_;
    copy_thread_ = std::jthread([this, weak, dispatcher, control, gate, job = std::move(job)](std::stop_token stop_token) mutable {
        std::shared_ptr<velocitycopy::LiveCopyPlan> plan;
        try {
            plan = std::make_shared<velocitycopy::LiveCopyPlan>(planner_.build(job, stop_token));
        } catch (...) {
            const bool cancelled = stop_token.stop_requested() ||
                cancel_requested_.load(std::memory_order_relaxed);
            {
                std::lock_guard gate_lock(gate->mutex);
                gate->accepting = false;
                gate->condition.notify_all();
            }
            (void)dispatcher.TryEnqueue([weak, cancelled]() {
                if (auto self = weak.get()) {
                    self->FinishCopy(cancelled
                        ? velocitycopy::JobResult{false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false}
                        : velocitycopy::JobResult{false, false, static_cast<std::int32_t>(E_FAIL), false});
                }
            });
            return;
        }

        if (stop_token.stop_requested() || cancel_requested_.load(std::memory_order_relaxed)) {
            control->request_cancel();
            (void)dispatcher.TryEnqueue([weak]() {
                if (auto self = weak.get()) self->FinishCopy({false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false});
            });
            return;
        }

        const auto result = RunLivePlanSession(plan, control, gate, stop_token, true, 0);
        (void)dispatcher.TryEnqueue([weak, result]() {
            if (auto self = weak.get()) self->FinishCopy(result);
        });
    });
}

void MainWindow::ResumeStoppedCopy() {
    if (!stopped_session_ || !live_plan_) return;
    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        if (append_gate_->planning_count != 0) {
            resume_requested_ = true;
            PauseButton().IsEnabled(false);
            return;
        }
    }
    if (live_plan_->remaining_files() == 0 && !live_plan_->has_pending_directories()) {
        resume_requested_ = false;
        FinalizeStoppedSessionIfEmpty();
        return;
    }

    resume_requested_ = false;
    stopped_session_ = false;
    conflict_session_ = false;
    stop_requested_ = false;
    current_file_id_ = 0;
    current_file_skippable_ = false;
    cancel_requested_.store(false, std::memory_order_relaxed);
    presenter_.reset();
    // Preserve the stopped session's aggregate progress until the resumed executor
    // publishes its first authoritative snapshot. Reset only live telemetry.
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    last_queue_completed_files_ = live_plan_->completed_files();
    execution_control_ = std::make_shared<velocitycopy::ExecutionControl>();
    if (!append_gate_) append_gate_ = std::make_shared<AppendGate>();

    auto plan = live_plan_;
    auto control = execution_control_;
    auto gate = append_gate_;
    active_destination_ = plan->destination_root();
    active_operation_ = plan->operation();
    SetExecutionButtonsRunning();
    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    copy_thread_ = std::jthread([this, weak, dispatcher, plan, control, gate](std::stop_token stop_token) {
        const auto result = RunLivePlanSession(plan, control, gate, stop_token, false, 0);
        (void)dispatcher.TryEnqueue([weak, result]() {
            if (auto self = weak.get()) self->FinishCopy(result);
        });
    });
}

void MainWindow::StartNextQueuedSession() {
    if (execution_control_ || stopped_session_ || conflict_session_ || stop_requested_ || queued_sessions_.empty()) return;
    auto next = std::move(queued_sessions_.front());
    queued_sessions_.pop_front();
    StartTransfer(std::move(next.job), std::move(next.destination), std::move(next.source));
}

void MainWindow::PublishLivePlan(std::shared_ptr<velocitycopy::LiveCopyPlan> plan) {
    live_plan_ = std::move(plan);
    QueueButton().IsEnabled(true);
    RefreshQueue();
    if (!stop_requested_) SetExecutionButtonsRunning();

    auto deferred = std::move(deferred_same_destination_jobs_);
    deferred_same_destination_jobs_.clear();
    auto target_plan = live_plan_;
    auto target_control = execution_control_;
    auto target_gate = append_gate_;
    for (auto& job : deferred) {
        if (target_plan && target_control && target_gate) {
            EnqueueAppend(std::move(job), target_plan, target_control, target_gate, true);
        }
    }
}

void MainWindow::OnPauseClick(IInspectable const&, RoutedEventArgs const&) {
    if (stopped_session_) {
        ResumeStoppedCopy();
        return;
    }
    if (!execution_control_) return;

    if (paused_) {
        execution_control_->resume();
        paused_ = false;
    } else {
        execution_control_->request_pause();
        paused_ = true;
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
    }
    PauseIcon().Glyph(paused_ ? L"\xE768" : L"\xE769");
    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        const auto label = loader.GetString(paused_ ? L"ActionResume" : L"ActionPause");
        ToolTipService::SetToolTip(PauseButton(), box_value(label));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PauseButton(), label);
    } catch (...) {
        // Localization failure must never mutate the execution state.
    }
    RefreshExecutionMenuState();
    RefreshExecutionButtonState();
}

void MainWindow::RefreshExecutionButtonState() {
    const bool active = execution_control_ != nullptr;
    SkipButton().IsEnabled(velocitycopy::can_skip_current_file(
        active, current_file_id_, current_file_skippable_,
        paused_, stopped_session_, conflict_session_, stop_requested_));
    StopButton().IsEnabled(active && !stopped_session_ && !conflict_session_ && !stop_requested_);
}

void MainWindow::OnSkipClick(IInspectable const&, RoutedEventArgs const&) {
    if (!velocitycopy::can_skip_current_file(
            execution_control_ != nullptr,
            current_file_id_,
            current_file_skippable_,
            paused_,
            stopped_session_,
            conflict_session_,
            stop_requested_)) return;
    execution_control_->request_skip(current_file_id_);
    current_file_skippable_ = false;
    RefreshExecutionButtonState();
}

void MainWindow::OnStopClick(IInspectable const&, RoutedEventArgs const&) {
    if (!execution_control_ || stopped_session_ || conflict_session_ || stop_requested_) return;
    resume_requested_ = false;
    stop_requested_ = true;
    current_file_skippable_ = false;
    execution_control_->request_stop();
    PauseButton().IsEnabled(false);
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    RefreshExecutionMenuState();
    RefreshExecutionButtonState();
}

void MainWindow::OnCancelClick(IInspectable const&, RoutedEventArgs const&) {
    CancelCurrentSession();
}

void MainWindow::CancelCurrentSession() {
    cancel_requested_.store(true, std::memory_order_relaxed);
    resume_requested_ = false;
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    conflict_replace_file_id_ = 0;
    current_file_id_ = 0;
    current_file_skippable_ = false;
    deferred_same_destination_jobs_.clear();
    deferred_interrupted_jobs_.clear();
    queued_sessions_.clear();
    append_planner_.cancel_pending();

    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        append_gate_->accepting = false;
        append_gate_->condition.notify_all();
    }
    if (execution_control_) {
        copy_thread_.request_stop();
        execution_control_->request_cancel();
        return;
    }
    if (stopped_session_ || conflict_session_) {
        stopped_session_ = false;
        conflict_session_ = false;
        stop_requested_ = false;
        live_plan_.reset();
        append_gate_.reset();
        active_destination_.clear();
        queue_snapshot_.clear();
        QueueList().Items().Clear();
        QueueCountText().Text(L"0");
        QueueButton().IsEnabled(false);
        QueuePanel().Visibility(Visibility::Collapsed);
        QueueChevron().Glyph(L"\xE70D");
        ResizeWindow(72);
        SetExecutionButtonsIdle();
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
    }
}

void MainWindow::ApplySnapshot(const velocitycopy::UiSnapshot& snapshot) {
    // Progress callbacks are marshalled through DispatcherQueue. A snapshot that was
    // queued before a terminal/control transition must not repaint stale telemetry.
    if (!execution_control_ || stopped_session_ || conflict_session_ || stop_requested_ ||
        cancel_requested_.load(std::memory_order_relaxed)) return;

    const auto fraction = (std::clamp)(snapshot.fraction, 0.0, 1.0);
    SetProgressFraction(fraction);
    const bool skip_state_changed =
        snapshot.current_file_id != current_file_id_ ||
        snapshot.current_file_skippable != current_file_skippable_;
    current_file_id_ = snapshot.current_file_id;
    current_file_skippable_ = snapshot.current_file_skippable;
    // The visible Skip button has no Opening hook like the menu; it must follow
    // the current file as snapshots arrive, or it stays disabled all transfer.
    if (skip_state_changed) RefreshExecutionButtonState();

    if (!snapshot.current_source.empty()) {
        const hstring filename(snapshot.current_source.filename().wstring());
        if (CurrentItemText().Text() != filename) CurrentItemText().Text(filename);
    }

    const auto speed = FormatSpeed(snapshot.bytes_per_second);
    if (SpeedText().Text() != speed) SpeedText().Text(speed);
    const auto eta = FormatEta(snapshot.eta_seconds);
    if (EtaText().Text() != eta) EtaText().Text(eta);

    if (QueuePanel().Visibility() == Visibility::Visible && snapshot.completed_files != last_queue_completed_files_) {
        last_queue_completed_files_ = snapshot.completed_files;
        RefreshQueue();
    }
}

void MainWindow::FinishCopy(const velocitycopy::JobResult& original_result) {
    auto result = original_result;
    auto deferred_initial = std::move(deferred_same_destination_jobs_);
    deferred_same_destination_jobs_.clear();

    if (result.stopped && cancel_requested_.load(std::memory_order_relaxed)) {
        result = {false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false};
    }

    execution_control_.reset();
    current_file_id_ = 0;
    current_file_skippable_ = false;
    paused_ = false;
    PauseIcon().Glyph(L"\xE769");

    if (result.stopped) {
        stop_requested_ = false;
        stopped_session_ = true;
        conflict_session_ = false;
        append_gate_ = std::make_shared<AppendGate>();
        if (live_plan_) {
            active_destination_ = live_plan_->destination_root();
            active_operation_ = live_plan_->operation();
        }
        SetExecutionButtonsStopped();
        RefreshQueue();
        QueueButton().IsEnabled(live_plan_ && (live_plan_->remaining_files() != 0 || live_plan_->has_pending_directories()));
        SpeedText().Text(L"—");
        EtaText().Text(L"—");

        auto deferred = std::move(deferred_interrupted_jobs_);
        deferred_interrupted_jobs_.clear();
        auto plan = live_plan_;
        auto gate = append_gate_;
        for (auto& job : deferred) {
            if (plan && gate) EnqueueAppend(std::move(job), plan, nullptr, gate, false);
        }
        FinalizeStoppedSessionIfEmpty();
        return;
    }

    if (result.destination_conflict && live_plan_ && result.conflict_file_id != 0) {
        stop_requested_ = false;
        stopped_session_ = false;
        conflict_session_ = true;
        resume_requested_ = false;
        conflict_replace_file_id_ = 0;
        append_gate_ = std::make_shared<AppendGate>();
        active_destination_ = live_plan_->destination_root();
        active_operation_ = live_plan_->operation();
        SetExecutionButtonsConflict();
        RefreshQueue();
        QueueButton().IsEnabled(live_plan_->remaining_files() != 0 || live_plan_->has_pending_directories());
        SpeedText().Text(L"—");
        EtaText().Text(L"—");

        auto plan = live_plan_;
        auto gate = append_gate_;
        for (auto& job : deferred_initial) {
            if (plan && gate) EnqueueAppend(std::move(job), plan, nullptr, gate, false);
        }
        auto interrupted = std::move(deferred_interrupted_jobs_);
        deferred_interrupted_jobs_.clear();
        for (auto& job : interrupted) {
            if (plan && gate) EnqueueAppend(std::move(job), plan, nullptr, gate, false);
        }
        ShowConflictDialogAsync(result);
        return;
    }

    resume_requested_ = false;
    conflict_replace_file_id_ = 0;
    stop_requested_ = false;
    stopped_session_ = false;
    conflict_session_ = false;
    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        append_gate_->accepting = false;
        append_gate_->condition.notify_all();
    }
    append_gate_.reset();

    if (result.cancelled) {
        SetProgressFraction(0.0);
        deferred_interrupted_jobs_.clear();
        queued_sessions_.clear();
        live_plan_.reset();
        active_destination_.clear();
        RefreshQueue();
        QueueButton().IsEnabled(false);
        QueuePanel().Visibility(Visibility::Collapsed);
        QueueChevron().Glyph(L"\xE70D");
        ResizeWindow(72);
        SetExecutionButtonsIdle();
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
        try {
            Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
            CurrentItemText().Text(loader.GetString(L"StatusCancelled"));
        } catch (...) {
        }
        // Cancellation is a terminal user decision. Once its session state is
        // cleared there is no attention left for this transfer window to own.
        DestroyCompletedWindow();
        return;
    }

    while (!deferred_initial.empty()) {
        queued_sessions_.push_front({std::move(deferred_initial.back()), {}, {}});
        deferred_initial.pop_back();
    }

    if (!result.success) {
        SetProgressFraction(0.0);
        live_plan_.reset();
        active_destination_.clear();
        RefreshQueue();
        QueueButton().IsEnabled(false);
        QueuePanel().Visibility(Visibility::Collapsed);
        QueueChevron().Glyph(L"\xE70D");
        ResizeWindow(72);
        SetExecutionButtonsIdle();
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
        try {
            Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
            CurrentItemText().Text(loader.GetString(L"StatusFailed"));
        } catch (...) {
        }
        // result.native_code carries the actual HRESULT/Win32 error the copy
        // engine recorded (see ConcurrentResultState::record_error in
        // job_executor.cpp) but it was being discarded here: ShowError() opened
        // an InfoBar with no Message at all. Decode it so a failed transfer
        // (including an Explorer Cut/Move that failed mid-copy) tells the
        // person why, not just that it failed.
        //
        // record_error now also captures which file the FIRST failure
        // happened on for any error, not only an actual destination
        // conflict (conflict_file_id/conflict_source stay repurposed as
        // "the failing file", destination_conflict itself is unchanged and
        // still gates the dedicated conflict dialog). A decoded HRESULT
        // with no path was nearly useless for a queue of more than one
        // file: "file not found" doesn't say which of possibly hundreds of
        // queued files vanished.
        auto reason = FormatFailureReason(result.native_code);
        if (result.conflict_file_id != 0 && !result.conflict_source.empty()) {
            reason = reason.empty()
                ? hstring(result.conflict_source.wstring())
                : reason + L" — " + hstring(result.conflict_source.wstring());
        }
        ShowError(reason);
        return;
    }

    live_plan_.reset();
    active_destination_.clear();
    RefreshQueue();
    QueueButton().IsEnabled(false);
    QueuePanel().Visibility(Visibility::Collapsed);
    QueueChevron().Glyph(L"\xE70D");
    ResizeWindow(72);
    SetExecutionButtonsIdle();
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    if (queued_sessions_.empty()) {
        SetProgressFraction(1.0);
        try {
            Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
            CurrentItemText().Text(loader.GetString(
                active_operation_ == velocitycopy::FileOperation::Move
                    ? L"StatusMoveCompleted"
                    : L"StatusCompleted"));
        } catch (...) {
        }
        // Completed transfer windows are session surfaces, not recovery owners.
        // Recovery remains available through ShowFromTray() when the app is opened
        // explicitly; a successful transfer must always release its own window.
        DestroyCompletedWindow();
        return;
    }
    StartNextQueuedSession();
}

void MainWindow::FinalizeStoppedSessionIfEmpty() {
    if (!stopped_session_ || !live_plan_ || live_plan_->remaining_files() != 0 ||
        live_plan_->has_pending_directories()) return;
    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        if (append_gate_->planning_count != 0) return;
        append_gate_->accepting = false;
        append_gate_->condition.notify_all();
    }
    resume_requested_ = false;
    stopped_session_ = false;
    conflict_session_ = false;
    stop_requested_ = false;
    current_file_id_ = 0;
    current_file_skippable_ = false;
    live_plan_.reset();
    append_gate_.reset();
    active_destination_.clear();
    RefreshQueue();
    QueueButton().IsEnabled(false);
    QueuePanel().Visibility(Visibility::Collapsed);
    QueueChevron().Glyph(L"\xE70D");
    ResizeWindow(72);
    SetExecutionButtonsIdle();
    SetProgressFraction(1.0);
    StartNextQueuedSession();
}

} // namespace winrt::VelocityCopyUI::implementation
