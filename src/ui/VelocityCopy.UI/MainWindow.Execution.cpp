#include "pch.h"
#include "MainWindow.xaml.h"
#include "Localization.h"
#include "UiTokens.h"
#include "DecisionSurface.h"
#include "App.xaml.h"

#include "velocitycopy/diagnostics.hpp"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::ResetCurrentItemState() noexcept {
    current_file_id_ = 0;
    current_file_skippable_ = false;
    paused_ = false;
}

void MainWindow::ResetInterruptedSessionState() noexcept {
    interrupted_session_ = InterruptedSessionState::None;
    stop_requested_ = false;
    pending_resume_ = {};
}

void MainWindow::SetExecutionButtonsPlanning() {
    performance_sampling_state_ = PerformanceSamplingState::Planning;
    TransferProgress().ShowPaused(false);
    TransferProgress().ShowError(false);
    RefreshEfficiencyMode();
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    PauseIcon().Glyph(L"\xE769");
    PauseButton().IsEnabled(false);
    CancelButton().IsEnabled(true);
    ResetCurrentItemState();
    RefreshExecutionMenuState();
}

void MainWindow::SetExecutionButtonsRunning() {
    performance_sampling_state_ = PerformanceSamplingState::Copying;
    TransferProgress().ShowPaused(false);
    TransferProgress().ShowError(false);
    RefreshEfficiencyMode();
    PauseButton().IsEnabled(true);
    CancelButton().IsEnabled(true);
    ResetCurrentItemState();
    PauseIcon().Glyph(L"\xE769");
    try {
        const auto label = velocitycopy::localization::get_string(L"ActionPause");
        ToolTipService::SetToolTip(PauseButtonHost(), box_value(label));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PauseButton(), label);
    } catch (...) {}
    RefreshExecutionMenuState();
}

void MainWindow::SetExecutionButtonsIdle() {
    performance_sampling_state_ = PerformanceSamplingState::Idle;
    TransferProgress().ShowPaused(false);
    PauseButton().IsEnabled(false);
    CancelButton().IsEnabled(false);

    ResetCurrentItemState();
    pending_resume_ = {};
    PauseIcon().Glyph(L"\xE769");
    try {
        const auto label = velocitycopy::localization::get_string(L"ActionPause");
        ToolTipService::SetToolTip(PauseButtonHost(), box_value(label));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PauseButton(), label);
    } catch (...) {}
    RefreshEfficiencyMode();
    RefreshExecutionMenuState();
}

void MainWindow::SetExecutionButtonsStopped() {
    performance_sampling_state_ = PerformanceSamplingState::Stopped;
    TransferProgress().ShowPaused(true);
    TransferProgress().ShowError(false);
    PauseButton().IsEnabled(true);
    CancelButton().IsEnabled(true);
    ResetCurrentItemState();
    PauseIcon().Glyph(L"\xE768");
    try {
        const auto label = velocitycopy::localization::get_string(L"ActionResume");
        ToolTipService::SetToolTip(PauseButtonHost(), box_value(label));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PauseButton(), label);
    } catch (...) {}
    RefreshEfficiencyMode();
    RefreshExecutionMenuState();
}

void MainWindow::SetExecutionButtonsConflict() {
    performance_sampling_state_ = PerformanceSamplingState::Conflict;
    TransferProgress().ShowPaused(false);
    TransferProgress().ShowError(false);
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    PauseIcon().Glyph(L"\xE769");
    PauseButton().IsEnabled(false);
    CancelButton().IsEnabled(true);
    ResetCurrentItemState();
    RefreshEfficiencyMode();
    RefreshExecutionMenuState();
}

velocitycopy::JobResult MainWindow::RunLivePlanSession(
    std::shared_ptr<velocitycopy::LiveCopyPlan> plan,
    std::shared_ptr<velocitycopy::ExecutionControl> control,
    std::shared_ptr<AppendGate> gate,
    const std::stop_token stop_token,
    const bool publish_plan,
    std::uint64_t replace_file_id,
    const velocitycopy::ConflictPolicy conflict_policy,
    bool retry_source_removals) {
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
        options.conflict_policy = conflict_policy;
        options.retry_source_removals = retry_source_removals;
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
        // RetrySourceRemoval is authorized by one explicit Retry All decision.
        // Appended work may make this loop execute again, but must not silently
        // repeat source deletion without another user decision.
        retry_source_removals = false;
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
    ResetInterruptedSessionState();
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

    SetExpanded(false);
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
    if (interrupted_session_ != InterruptedSessionState::Stopped || !live_plan_) return;
    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        if (append_gate_->planning_count != 0) {
            pending_resume_ = StoppedResume{};
            PauseButton().IsEnabled(false);
            return;
        }
    }
    if (live_plan_->unresolved_files() == 0 && !live_plan_->has_pending_directories()) {
        pending_resume_ = {};
        FinalizeStoppedSessionIfEmpty();
        return;
    }

    ResetInterruptedSessionState();
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
    if (execution_control_ || interrupted_session_ != InterruptedSessionState::None || stop_requested_ || queued_sessions_.empty()) return;
    auto next = std::move(queued_sessions_.front());
    queued_sessions_.pop_front();
    StartTransfer(std::move(next.job), std::move(next.destination), std::move(next.source));
}

void MainWindow::PublishLivePlan(std::shared_ptr<velocitycopy::LiveCopyPlan> plan) {
    live_plan_ = std::move(plan);

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
    if (interrupted_session_ == InterruptedSessionState::Decision) {
        ShowRetryDecisionAsync();
        return;
    }
    if (interrupted_session_ == InterruptedSessionState::Stopped) {
        ResumeStoppedCopy();
        return;
    }
    if (!execution_control_) return;

    if (paused_) {
        execution_control_->resume();
        paused_ = false;
        performance_sampling_state_ = PerformanceSamplingState::Copying;
    } else {
        execution_control_->request_pause();
        paused_ = true;
        performance_sampling_state_ = PerformanceSamplingState::Paused;
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
    }
    TransferProgress().ShowPaused(paused_);
    TransferProgress().ShowError(false);
    PauseIcon().Glyph(paused_ ? L"\xE768" : L"\xE769");
    try {
        const auto label = velocitycopy::localization::get_string(paused_ ? L"ActionResume" : L"ActionPause");
        ToolTipService::SetToolTip(PauseButtonHost(), box_value(label));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PauseButton(), label);
    } catch (...) {
        // Localization failure must never mutate the execution state.
    }
    RefreshExecutionMenuState();
    ScheduleTelemetryGeometryProbe();
}

void MainWindow::OnSkipClick(IInspectable const&, RoutedEventArgs const&) {
    if (!velocitycopy::can_skip_current_file(
            execution_control_ != nullptr,
            current_file_id_,
            current_file_skippable_,
            paused_,
            interrupted_session_ == InterruptedSessionState::Stopped,
            interrupted_session_ == InterruptedSessionState::Conflict,
            stop_requested_)) return;
    execution_control_->request_skip(current_file_id_);
    current_file_skippable_ = false;
    RefreshExecutionMenuState();
}

void MainWindow::OnStopClick(IInspectable const&, RoutedEventArgs const&) {
    if (!execution_control_ || interrupted_session_ != InterruptedSessionState::None || stop_requested_) return;
    pending_resume_ = {};
    stop_requested_ = true;
    performance_sampling_state_ = PerformanceSamplingState::Stopped;
    current_file_skippable_ = false;
    execution_control_->request_stop();
    PauseButton().IsEnabled(false);
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    RefreshExecutionMenuState();
}

void MainWindow::OnCancelClick(IInspectable const&, RoutedEventArgs const&) {
    CancelCurrentSession();
}

void MainWindow::CancelCurrentSession() {
    CancelDecisionQueue();
    cancel_requested_.store(true, std::memory_order_relaxed);
    performance_sampling_state_ = PerformanceSamplingState::Cancelling;
    pending_resume_ = {};
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    current_file_id_ = 0;
    current_file_skippable_ = false;
    deferred_same_destination_jobs_.clear();
    deferred_interrupted_jobs_.clear();
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
    if (interrupted_session_ != InterruptedSessionState::None) {
        ResetInterruptedSessionState();
        live_plan_.reset();
        append_gate_.reset();
        active_destination_.clear();
        queue_snapshot_.clear();
        QueueList().Items().Clear();
        QueueCountText().Text(L"0");

        SetExpanded(false);
        SetExecutionButtonsIdle();
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
        if (queued_sessions_.empty()) DestroyCompletedWindow();
        else StartNextQueuedSession();
        return;
    }
}

void MainWindow::ApplySnapshot(const velocitycopy::UiSnapshot& snapshot) {
    // Progress callbacks are marshalled through DispatcherQueue. A snapshot that was
    // queued before a terminal/control transition must not repaint stale telemetry.
    if (!execution_control_ || interrupted_session_ != InterruptedSessionState::None || stop_requested_ ||
        cancel_requested_.load(std::memory_order_relaxed)) return;

    const auto fraction = (std::clamp)(snapshot.fraction, 0.0, 1.0);
    SetProgressFraction(fraction);
    const bool skip_state_changed =
        snapshot.current_file_id != current_file_id_ ||
        snapshot.current_file_skippable != current_file_skippable_;
    current_file_id_ = snapshot.current_file_id;
    current_file_skippable_ = snapshot.current_file_skippable;
    // Skip/Stop live only in Options. The menu also refreshes on Opening, but an
    // already-open menu must follow the current file as snapshots arrive.
    if (skip_state_changed) RefreshExecutionMenuState();

    if (!snapshot.current_source.empty()) {
        const hstring filename(snapshot.current_source.filename().wstring());
        if (CurrentItemText().Text() != filename) CurrentItemText().Text(filename);
    }

    const auto transferred = FormatBytes(snapshot.transferred_bytes);
    const auto total = FormatBytes(snapshot.total_bytes);
    const hstring bytes_text(std::format(L"{} / {}", transferred.c_str(), total.c_str()));
    if (TransferBytesText().Text() != bytes_text) TransferBytesText().Text(bytes_text);

    hstring files_text;
    try {
        const auto pattern = velocitycopy::localization::get_string(L"TransferCompletedFormat");
        files_text = hstring(std::vformat(
            std::wstring_view{pattern.c_str(), pattern.size()},
            std::make_wformat_args(snapshot.completed_files, snapshot.total_files)));
    } catch (...) {
        files_text = hstring(std::format(
            L"{} completed of {}", snapshot.completed_files, snapshot.total_files));
    }
    if (TransferFilesText().Text() != files_text) TransferFilesText().Text(files_text);

    if (!snapshot.current_source.empty()) {
        const hstring source(snapshot.current_source.wstring());
        if (SourcePathText().Text() != source) SourcePathText().Text(source);
        ToolTipService::SetToolTip(SourcePathText(), box_value(source));
    }
    if (!snapshot.current_destination.empty()) {
        const hstring destination(snapshot.current_destination.wstring());
        if (DestinationPathText().Text() != destination) DestinationPathText().Text(destination);
        ToolTipService::SetToolTip(DestinationPathText(), box_value(destination));
    }

    const auto speed = FormatSpeed(snapshot.bytes_per_second);
    if (SpeedText().Text() != speed) SpeedText().Text(speed);
    const auto eta = FormatEta(snapshot.eta_seconds);
    if (EtaText().Text() != eta) EtaText().Text(eta);
    if ((snapshot.bytes_per_second > 0.0) != geometry_probe_speed_active_) {
        geometry_probe_speed_active_ = snapshot.bytes_per_second > 0.0;
        ScheduleTelemetryGeometryProbe();
    }

    if (DetailsBytesText().Text() != bytes_text) DetailsBytesText().Text(bytes_text);
    if (DetailsFilesText().Text() != files_text) DetailsFilesText().Text(files_text);
    if (DetailsSpeedText().Text() != speed) DetailsSpeedText().Text(speed);
    if (DetailsEtaText().Text() != eta) DetailsEtaText().Text(eta);
    if (PerformanceCurrentSpeedText().Text() != speed) PerformanceCurrentSpeedText().Text(speed);
    if (!snapshot.current_source.empty()) {
        const hstring source(snapshot.current_source.wstring());
        if (DetailsSourceText().Text() != source) DetailsSourceText().Text(source);
        ToolTipService::SetToolTip(DetailsSourceText(), box_value(source));
    }
    if (!snapshot.current_destination.empty()) {
        const hstring destination(snapshot.current_destination.wstring());
        if (DetailsDestinationText().Text() != destination) DetailsDestinationText().Text(destination);
        ToolTipService::SetToolTip(DetailsDestinationText(), box_value(destination));
    }
    ObservePerformanceSample(snapshot.bytes_per_second);

    if (expanded_ && snapshot.completed_files != last_queue_completed_files_) {
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
    ResetCurrentItemState();
    PauseIcon().Glyph(L"\xE769");

    if (result.stopped) {
        stop_requested_ = false;
        interrupted_session_ = InterruptedSessionState::Stopped;
        pending_resume_ = {};
        append_gate_ = std::make_shared<AppendGate>();
        if (live_plan_) {
            active_destination_ = live_plan_->destination_root();
            active_operation_ = live_plan_->operation();
        }
        SetExecutionButtonsStopped();
        RefreshQueue();

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
        interrupted_session_ = InterruptedSessionState::Conflict;
        pending_resume_ = {};
        append_gate_ = std::make_shared<AppendGate>();
        active_destination_ = live_plan_->destination_root();
        active_operation_ = live_plan_->operation();
        SetExecutionButtonsConflict();
        RefreshQueue();

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

    if (result.parked_files != 0 && live_plan_) {
        stop_requested_ = false;
        interrupted_session_ = InterruptedSessionState::Decision;
        pending_resume_ = {};
        append_gate_ = std::make_shared<AppendGate>();
        active_destination_ = live_plan_->destination_root();
        active_operation_ = live_plan_->operation();
        SetExecutionButtonsConflict();
        PauseIcon().Glyph(L"\xE72C");
        PauseButton().IsEnabled(true);
        try {
            const auto label = velocitycopy::localization::get_string(L"ActionResolveFailures");
            ToolTipService::SetToolTip(PauseButtonHost(), box_value(label));
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PauseButton(), label);
        } catch (...) {}
        RefreshQueue();

        SpeedText().Text(L"—");
        EtaText().Text(L"—");
        ShowRetryDecisionAsync();
        return;
    }

    ResetInterruptedSessionState();
    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        append_gate_->accepting = false;
        append_gate_->condition.notify_all();
    }
    append_gate_.reset();

    if (result.cancelled) {
        SetProgressFraction(0.0);
        deferred_interrupted_jobs_.clear();
        live_plan_.reset();
        active_destination_.clear();
        RefreshQueue();

        SetExpanded(false);
        SetExecutionButtonsIdle();
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
        try {
            CurrentItemText().Text(velocitycopy::localization::get_string(L"StatusCancelled"));
        } catch (...) {
        }
        if (queued_sessions_.empty()) DestroyCompletedWindow();
        else StartNextQueuedSession();
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

        SetExpanded(false);
        SetExecutionButtonsIdle();
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
        try {
            CurrentItemText().Text(velocitycopy::localization::get_string(L"StatusFailed"));
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
        if (!queued_sessions_.empty()) {
            std::wstring diagnostic = L"transfer: failed session yielded to queued session";
            if (!reason.empty()) {
                diagnostic.append(L": ");
                diagnostic.append(reason.c_str());
            }
            velocitycopy::log_diagnostic(diagnostic);
            StartNextQueuedSession();
        }
        return;
    }

    live_plan_.reset();
    active_destination_.clear();
    RefreshQueue();

    SetExpanded(false);
    SetExecutionButtonsIdle();
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    const bool completed_with_issues = result.outcomes.failed != 0 ||
        result.outcomes.skipped != 0 || result.outcomes.copied_source_retained != 0;
    if (queued_sessions_.empty()) {
        SetProgressFraction(1.0);
        try {
            if (completed_with_issues) {
                CurrentItemText().Text(velocitycopy::localization::get_string(L"StatusCompletedWithIssues"));
                const auto failed_label = velocitycopy::localization::get_string(L"OutcomeFailed");
                const auto skipped_label = velocitycopy::localization::get_string(L"OutcomeSkipped");
                const auto retained_label = velocitycopy::localization::get_string(L"OutcomeSourceRetained");
                ShowNotice(
                    result.outcomes.failed == 0 && result.outcomes.copied_source_retained == 0
                        ? InfoBarSeverity::Warning
                        : InfoBarSeverity::Error,
                    hstring(std::format(
                    L"{}: {}, {}: {}, {}: {}",
                    failed_label.c_str(),
                    result.outcomes.failed,
                    skipped_label.c_str(),
                    result.outcomes.skipped,
                    retained_label.c_str(),
                    result.outcomes.copied_source_retained)));
            } else {
                CurrentItemText().Text(velocitycopy::localization::get_string(
                    active_operation_ == velocitycopy::FileOperation::Move
                        ? L"StatusMoveCompleted"
                        : L"StatusCompleted"));
            }
        } catch (...) {
        }
        if (completed_with_issues) {
            // Keep the terminal surface visible so per-item failures/skips cannot
            // masquerade as a clean transfer that immediately disappears.
            return;
        }
        // Clean completed transfer windows are session surfaces, not recovery owners.
        // Recovery remains available through ShowFromTray() when the app is opened
        // explicitly; a successful clean transfer releases its own window.
        DestroyCompletedWindow();
        return;
    }
    if (completed_with_issues) {
        velocitycopy::log_diagnostic(std::format(
            L"transfer: completed with issues (failed={}, skipped={}, source_retained={})",
            result.outcomes.failed,
            result.outcomes.skipped,
            result.outcomes.copied_source_retained));
    }
    StartNextQueuedSession();
}

fire_and_forget MainWindow::ShowRetryDecisionAsync() {
    auto lifetime = get_strong();
    if (interrupted_session_ != InterruptedSessionState::Decision || !live_plan_) co_return;
    try {
        const auto decision = velocitycopy::ui::decode_decision(co_await RequestDecisionAsync({
            hwnd_,
            velocitycopy::localization::get_string(L"RetryDecisionTitle").c_str(),
            velocitycopy::localization::get_string(L"RetryDecisionMessage").c_str(),
            {},
            velocitycopy::localization::get_string(L"ActionRetryAll").c_str(),
            velocitycopy::localization::get_string(L"ActionSkipAll").c_str(),
            velocitycopy::localization::get_string(L"ActionCancel").c_str(),
            {},
            true,
            velocitycopy::ui::DecisionTone::Error,
        }));
        if (tray_exit_requested_ || session_ending_) co_return;
        if (interrupted_session_ != InterruptedSessionState::Decision || !live_plan_) co_return;
        if (decision.choice == velocitycopy::ui::DecisionChoice::Primary) ResumeParkedFailures();
        else if (decision.choice == velocitycopy::ui::DecisionChoice::Secondary) ResolveParkedFailures();
        // Closing/cancelling is non-destructive; parked work remains available.
    } catch (...) {
        // Preserve Decision state and all unresolved work.
    }
}

void MainWindow::ResumeParkedFailures() {
    if (interrupted_session_ != InterruptedSessionState::Decision || !live_plan_) return;
    for (const auto& incident : live_plan_->parked_incidents()) {
        if (incident.recovery_action == velocitycopy::RecoveryAction::RetryTransfer &&
            !live_plan_->unpark(incident.file_id)) {
            ShowError();
            return;
        }
    }
    StartDecisionSession(true);
}

void MainWindow::StartDecisionSession(const bool retry_source_removals) {
    if (interrupted_session_ != InterruptedSessionState::Decision || !live_plan_) return;
    interrupted_session_ = InterruptedSessionState::None;
    cancel_requested_.store(false, std::memory_order_relaxed);
    presenter_.reset();
    execution_control_ = std::make_shared<velocitycopy::ExecutionControl>();
    if (!append_gate_) append_gate_ = std::make_shared<AppendGate>();
    auto plan = live_plan_;
    auto control = execution_control_;
    auto gate = append_gate_;
    SetExecutionButtonsRunning();
    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    copy_thread_ = std::jthread([this, weak, dispatcher, plan, control, gate, retry_source_removals](std::stop_token token) {
        const auto result = RunLivePlanSession(
            plan, control, gate, token, false, 0,
            velocitycopy::ConflictPolicy::Prompt, retry_source_removals);
        (void)dispatcher.TryEnqueue([weak, result]() {
            if (auto self = weak.get()) self->FinishCopy(result);
        });
    });
}

void MainWindow::ResolveParkedFailures() {
    if (interrupted_session_ != InterruptedSessionState::Decision || !live_plan_) return;
    for (const auto& incident : live_plan_->parked_incidents()) {
        const auto outcome = incident.recovery_action == velocitycopy::RecoveryAction::RetrySourceRemoval
            ? velocitycopy::ItemOutcome::CopiedSourceRetained
            : velocitycopy::ItemOutcome::Failed;
        if (!live_plan_->resolve_parked(
                incident.file_id, outcome, incident.hresult, incident.destination_preexisted)) {
            ShowError();
            return;
        }
    }
    bool planning = false;
    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        planning = append_gate_->planning_count != 0;
    }
    if (planning || live_plan_->remaining_files() != 0 || live_plan_->has_pending_directories()) {
        StartDecisionSession(false);
        return;
    }
    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        append_gate_->accepting = false;
        append_gate_->condition.notify_all();
    }
    velocitycopy::JobResult result{true, false, S_OK, false};
    const auto view = live_plan_->resolution_view();
    result.outcomes = view.outcomes;
    result.parked_files = view.parked_files;
    FinishCopy(result);
}

void MainWindow::FinalizeStoppedSessionIfEmpty() {
    if (interrupted_session_ != InterruptedSessionState::Stopped || !live_plan_ || live_plan_->unresolved_files() != 0 ||
        live_plan_->has_pending_directories()) return;
    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        if (append_gate_->planning_count != 0) return;
        append_gate_->accepting = false;
        append_gate_->condition.notify_all();
    }
    pending_resume_ = {};
    interrupted_session_ = InterruptedSessionState::None;
    stop_requested_ = false;
    current_file_id_ = 0;
    current_file_skippable_ = false;
    live_plan_.reset();
    append_gate_.reset();
    active_destination_.clear();
    RefreshQueue();

    SetExpanded(false);
    SetExecutionButtonsIdle();
    SetProgressFraction(1.0);
    if (queued_sessions_.empty()) DestroyCompletedWindow();
    else StartNextQueuedSession();
}

} // namespace winrt::VelocityCopyUI::implementation
