#include "pch.h"
#include "MainWindow.xaml.h"
#include "Localization.h"
#include "UiTokens.h"
#include "DecisionSurface.h"
#include "App.xaml.h"
#include "UiSnapshotMailbox.h"

#include "velocitycopy/diagnostics.hpp"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace {

std::filesystem::path current_executable() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (int attempt = 0; attempt < 6; ++attempt) {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size()) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
    return {};
}

bool is_access_denied(const std::int32_t hresult) noexcept {
    return hresult == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) ||
        hresult == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_PRIVILEGE_NOT_HELD));
}

} // namespace

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::ClearLiveTelemetry() {
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    DetailsSpeedText().Text(L"—");
    DetailsEtaText().Text(L"—");
    PerformanceCurrentSpeedText().Text(L"—");
}

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
    SetTaskbarState(TBPF_INDETERMINATE);
    performance_sampling_state_ = PerformanceSamplingState::Planning;
    ApplyTransferVisualState(TransferVisualState::Active);
    TransferProgress().ShowPaused(false);
    TransferProgress().ShowError(false);
    RefreshEfficiencyMode();
    ClearLiveTelemetry();
    PauseIcon().Glyph(L"\xE769");
    PauseButton().IsEnabled(false);
    CancelButton().IsEnabled(true);
    ResetCurrentItemState();
    RefreshExecutionMenuState();
}

void MainWindow::SetExecutionButtonsRunning() {
    SetTaskbarState(TBPF_NORMAL);
    performance_sampling_state_ = PerformanceSamplingState::Copying;
    ApplyTransferVisualState(TransferVisualState::Active);
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
    SetTaskbarState(TBPF_NOPROGRESS);
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
    SetTaskbarState(TBPF_PAUSED);
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
    SetTaskbarState(TBPF_PAUSED);
    performance_sampling_state_ = PerformanceSamplingState::Conflict;
    ApplyTransferVisualState(TransferVisualState::Warning);
    TransferProgress().ShowPaused(false);
    TransferProgress().ShowError(false);
    ClearLiveTelemetry();
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

    auto mailbox = std::make_shared<velocitycopy::ui::UiSnapshotMailbox>();
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
            [this, weak, dispatcher, control, stop_token, mailbox](const velocitycopy::JobProgress& progress) {
                if (stop_token.stop_requested() || cancel_requested_.load(std::memory_order_relaxed)) {
                    control->request_cancel();
                    return velocitycopy::JobDecision::Cancel;
                }
                if (auto snapshot = presenter_.observe(progress, GetTickCount64())) {
                    if (mailbox->publish(std::move(*snapshot))) {
                        if (!dispatcher.TryEnqueue([weak, control, mailbox]() {
                            const auto value = mailbox->consume();
                            if (auto self = weak.get(); self && value && self->execution_control_ == control) {
                                self->ApplySnapshot(*value);
                            }
                        })) mailbox->discard();
                    }
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
    const auto owner = hwnd_;
    copy_thread_ = std::jthread([this, weak, dispatcher, control, gate, owner, job = std::move(job)](std::stop_token stop_token) mutable {
        std::shared_ptr<velocitycopy::LiveCopyPlan> plan;
        bool needs_elevation = false;
        // The planner reports *why* a job cannot run (destination inside the
        // source, two entries resolving to the same file, missing source...).
        // Carry that reason to FinishCopy instead of a generic E_FAIL so the
        // notice tells the user what to fix.
        std::int32_t planning_error = static_cast<std::int32_t>(E_FAIL);
        try {
            auto built = planner_.build(job, stop_token);
            // Probe before anything is written: a protected destination
            // (C:\, C:\Windows, Program Files) would otherwise fail item by item.
            needs_elevation = velocitycopy::destination_requires_elevation(built);
            plan = std::make_shared<velocitycopy::LiveCopyPlan>(std::move(built));
        } catch (const std::system_error& error) {
            planning_error = velocitycopy::planning_error_hresult(error.code());
        } catch (const std::bad_alloc&) {
            planning_error = static_cast<std::int32_t>(E_OUTOFMEMORY);
        } catch (...) {
        }
        if (!plan) {
            const bool cancelled = stop_token.stop_requested() ||
                cancel_requested_.load(std::memory_order_relaxed);
            {
                std::lock_guard gate_lock(gate->mutex);
                gate->accepting = false;
                gate->condition.notify_all();
            }
            (void)dispatcher.TryEnqueue([weak, cancelled, planning_error]() {
                if (auto self = weak.get()) {
                    self->FinishCopy(cancelled
                        ? velocitycopy::JobResult{false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false}
                        : velocitycopy::JobResult{false, false, planning_error, false});
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

        const auto finish_without_copy = [&](const velocitycopy::JobResult& result) {
            control->request_cancel();
            {
                std::lock_guard gate_lock(gate->mutex);
                gate->accepting = false;
                gate->condition.notify_all();
            }
            (void)dispatcher.TryEnqueue([weak, result]() {
                if (auto self = weak.get()) self->FinishCopy(result);
            });
        };
        const velocitycopy::JobResult cancelled_result{
            false, true, static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)), false};

        // Windows denies this destination to a standard token. Offer to hand
        // the job to an elevated VelocityCopy (UAC) instead of failing every
        // item; the elevated instance plans and copies it on its own.
        if (needs_elevation) {
            auto answer = std::make_shared<PreflightAnswer>();
            if (!dispatcher.TryEnqueue([weak, answer]() {
                    if (auto self = weak.get()) self->AskElevationAsync(answer);
                    else answer->set(false);
                })) {
                answer->set(false);
            }
            std::int32_t outcome = static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_CANCELLED));
            if (answer->wait(stop_token)) {
                velocitycopy::QueueArchive archive{};
                archive.queued_jobs.push_back(job);
                const auto directory = velocitycopy::app_data_directory();
                const auto handoff = directory
                    ? velocitycopy::write_elevated_handoff(*directory, archive)
                    : std::nullopt;
                outcome = handoff
                    ? velocitycopy::launch_elevated_handoff(current_executable(), *handoff, owner)
                    : static_cast<std::int32_t>(E_FAIL);
            }
            // Handed over or declined: this window copies nothing of this
            // job. Pastes that arrived meanwhile are separate work and run
            // next here (asking again if they target the same place).
            const auto result =
                outcome == S_OK || outcome == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_CANCELLED))
                    ? cancelled_result
                    : velocitycopy::JobResult{false, false, outcome, false};
            control->request_cancel();
            {
                std::lock_guard gate_lock(gate->mutex);
                gate->accepting = false;
                gate->condition.notify_all();
            }
            (void)dispatcher.TryEnqueue([weak, result]() {
                auto self = weak.get();
                if (!self) return;
                while (!self->deferred_same_destination_jobs_.empty()) {
                    self->queued_sessions_.push_front({std::move(self->deferred_same_destination_jobs_.back()), {}, {}});
                    self->deferred_same_destination_jobs_.pop_back();
                }
                self->FinishCopy(result);
            });
            return;
        }

        // Ask before writing anything when the destination clearly cannot
        // hold the transfer, instead of discovering a full disk half-way.
        if (const auto shortage = velocitycopy::find_space_shortage(
                plan->source_roots(), plan->destination_root(), plan->operation(), plan->total_bytes())) {
            auto answer = std::make_shared<PreflightAnswer>();
            const auto required = shortage->required_bytes;
            const auto available = shortage->available_bytes;
            if (!dispatcher.TryEnqueue([weak, answer, required, available]() {
                    if (auto self = weak.get()) self->AskLowSpaceAsync(answer, required, available);
                    else answer->set(false);
                })) {
                answer->set(false);
            }
            if (!answer->wait(stop_token)) {
                finish_without_copy(cancelled_result);
                return;
            }
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
    ClearLiveTelemetry();
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
        ClearLiveTelemetry();
    }
    TransferProgress().ShowPaused(paused_);
    TransferProgress().ShowError(false);
    SetTaskbarState(paused_ ? TBPF_PAUSED : TBPF_NORMAL);
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
    ClearLiveTelemetry();
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
    ClearLiveTelemetry();
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
        ClearLiveTelemetry();
        if (queued_sessions_.empty()) DestroyCompletedWindow();
        else StartNextQueuedSession();
        return;
    }
}

void MainWindow::ApplySnapshot(const velocitycopy::UiSnapshot& snapshot) {
    // Progress callbacks are marshalled through DispatcherQueue. A snapshot that was
    // queued before a terminal/control transition must not repaint stale telemetry.
    if (paused_ || !execution_control_ || interrupted_session_ != InterruptedSessionState::None || stop_requested_ ||
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
        const auto pattern = velocitycopy::localization::get_string(
            snapshot.completed_files == 1
                ? L"TransferCompletedSingularFormat"
                : L"TransferCompletedFormat");
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
        if (SourcePathText().Text() != source) {
            SourcePathText().Text(source);
            ToolTipService::SetToolTip(SourcePathText(), box_value(source));
        }
    }
    if (!snapshot.current_destination.empty()) {
        const hstring destination(snapshot.current_destination.wstring());
        if (DestinationPathText().Text() != destination) {
            DestinationPathText().Text(destination);
            ToolTipService::SetToolTip(DestinationPathText(), box_value(destination));
        }
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
        if (DetailsSourceText().Text() != source) {
            DetailsSourceText().Text(source);
            ToolTipService::SetToolTip(DetailsSourceText(), box_value(source));
        }
    }
    if (!snapshot.current_destination.empty()) {
        const hstring destination(snapshot.current_destination.wstring());
        if (DetailsDestinationText().Text() != destination) {
            DetailsDestinationText().Text(destination);
            ToolTipService::SetToolTip(DetailsDestinationText(), box_value(destination));
        }
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

        ClearLiveTelemetry();

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

        ClearLiveTelemetry();

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
        ApplyTransferVisualState(TransferVisualState::Error);
        PauseIcon().Glyph(L"\xE72C");
        PauseButton().IsEnabled(true);
        try {
            const auto label = velocitycopy::localization::get_string(L"ActionResolveFailures");
            ToolTipService::SetToolTip(PauseButtonHost(), box_value(label));
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PauseButton(), label);
        } catch (...) {}
        RefreshQueue();

        ClearLiveTelemetry();
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
        ClearLiveTelemetry();
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
        ClearLiveTelemetry();
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

    // Keep the first failed/skipped item before the plan is released so the
    // notice can say which file needs attention and why.
    std::optional<velocitycopy::ItemResult> first_issue;
    if (live_plan_) {
        const auto issues = live_plan_->retained_results();
        const auto failed = std::find_if(issues.begin(), issues.end(), [](const velocitycopy::ItemResult& item) {
            return item.outcome != velocitycopy::ItemOutcome::Skipped;
        });
        if (failed != issues.end()) first_issue = *failed;
    }
    live_plan_.reset();
    active_destination_.clear();
    RefreshQueue();

    SetExpanded(false);
    SetExecutionButtonsIdle();
    ClearLiveTelemetry();
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
                const auto completed_with_issues_title =
                    velocitycopy::localization::get_string(L"StatusCompletedWithIssues");
                // List only the outcomes that actually happened ("Skipped: 3"
                // instead of "Failed: 0, Skipped: 3, Source retained: 0").
                std::wstring summary;
                const auto append_outcome = [&summary](hstring const& label, const std::uint64_t count) {
                    if (count == 0) return;
                    if (!summary.empty()) summary.append(L", ");
                    summary.append(std::format(L"{}: {}", label.c_str(), count));
                };
                append_outcome(failed_label, result.outcomes.failed);
                append_outcome(skipped_label, result.outcomes.skipped);
                append_outcome(retained_label, result.outcomes.copied_source_retained);
                if (first_issue) {
                    summary.append(L"\n");
                    summary.append(first_issue->source.wstring());
                    const auto reason = FormatFailureReason(first_issue->hresult);
                    if (!reason.empty()) {
                        summary.append(L" — ");
                        summary.append(reason.c_str());
                    }
                }
                ShowNotice(
                    result.outcomes.failed == 0 && result.outcomes.copied_source_retained == 0
                        ? InfoBarSeverity::Warning
                        : InfoBarSeverity::Error,
                    completed_with_issues_title,
                    hstring(summary));
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
    // While a UAC prompt for this work is open, no other choice may start it.
    if (elevation_in_flight_) co_return;
    if (interrupted_session_ != InterruptedSessionState::Decision || !live_plan_) co_return;
    try {
        // Name the files that need attention and why; a bare "some items
        // failed" gives no basis for choosing Retry over Skip.
        constexpr std::size_t kListedIncidents = 3;
        const auto incidents = live_plan_->parked_incidents();
        std::wstring detail;
        for (std::size_t index = 0; index < incidents.size() && index < kListedIncidents; ++index) {
            if (!detail.empty()) detail.push_back(L'\n');
            const auto name = incidents[index].source.filename();
            detail.append(name.empty() ? incidents[index].source.wstring() : name.wstring());
            const auto reason = FormatFailureReason(incidents[index].hresult);
            if (!reason.empty()) {
                detail.append(L" — ");
                detail.append(reason.c_str());
            }
        }
        if (incidents.size() > kListedIncidents) {
            detail.append(std::format(L"\n(+{})", incidents.size() - kListedIncidents));
        }
        // Access denied by Windows (protected folder, existing file owned by
        // the system) can only be retried with administrator rights.
        const bool offer_elevation = !velocitycopy::process_is_elevated() &&
            std::any_of(incidents.begin(), incidents.end(), [](const auto& incident) {
                return is_access_denied(incident.hresult);
            });
        const auto elevate_label = offer_elevation
            ? std::wstring(velocitycopy::localization::get_string(L"ActionRetryAsAdmin").c_str())
            : std::wstring{};
        const auto decision = velocitycopy::ui::decode_decision(co_await RequestDecisionAsync({
            hwnd_,
            velocitycopy::localization::get_string(L"RetryDecisionTitle").c_str(),
            velocitycopy::localization::get_string(L"RetryDecisionMessage").c_str(),
            detail,
            velocitycopy::localization::get_string(L"ActionRetryAll").c_str(),
            velocitycopy::localization::get_string(L"ActionSkipAll").c_str(),
            velocitycopy::localization::get_string(L"ActionCancel").c_str(),
            {},
            true,
            velocitycopy::ui::DecisionTone::Error,
            elevate_label,
        }));
        if (tray_exit_requested_ || session_ending_) co_return;
        if (interrupted_session_ != InterruptedSessionState::Decision || !live_plan_) co_return;
        if (decision.choice == velocitycopy::ui::DecisionChoice::Primary) ResumeParkedFailures();
        else if (decision.choice == velocitycopy::ui::DecisionChoice::Secondary) ResolveParkedFailures();
        else if (decision.choice == velocitycopy::ui::DecisionChoice::Tertiary) ElevateParkedFailuresAsync();
        // Closing/cancelling is non-destructive; parked work remains available.
    } catch (...) {
        // Preserve Decision state and all unresolved work.
    }
}

fire_and_forget MainWindow::AskLowSpaceAsync(
    std::shared_ptr<PreflightAnswer> answer,
    const std::uint64_t required_bytes,
    const std::uint64_t available_bytes) {
    auto lifetime = get_strong();
    bool proceed = false;
    try {
        const auto pattern = velocitycopy::localization::get_string(L"LowSpaceMessageFormat");
        const auto required_text = FormatBytes(required_bytes);
        const auto available_text = FormatBytes(available_bytes);
        // make_wformat_args binds lvalues only (C++23).
        const std::wstring_view required{required_text.c_str(), required_text.size()};
        const std::wstring_view available{available_text.c_str(), available_text.size()};
        const std::wstring message = std::vformat(
            std::wstring_view{pattern.c_str(), pattern.size()},
            std::make_wformat_args(required, available));
        const auto decision = velocitycopy::ui::decode_decision(co_await RequestDecisionAsync({
            hwnd_,
            velocitycopy::localization::get_string(L"LowSpaceTitle").c_str(),
            message,
            active_destination_.wstring(),
            velocitycopy::localization::get_string(L"ActionContinue").c_str(),
            velocitycopy::localization::get_string(L"ActionCancel").c_str(),
            {},
            {},
            false,
            velocitycopy::ui::DecisionTone::Warning,
        }));
        proceed = decision.choice == velocitycopy::ui::DecisionChoice::Primary &&
            !tray_exit_requested_ && !session_ending_;
    } catch (...) {
        proceed = false;
    }
    answer->set(proceed);
}

fire_and_forget MainWindow::AskElevationAsync(std::shared_ptr<PreflightAnswer> answer) {
    auto lifetime = get_strong();
    bool proceed = false;
    try {
        const auto decision = velocitycopy::ui::decode_decision(co_await RequestDecisionAsync({
            hwnd_,
            velocitycopy::localization::get_string(L"ElevationTitle").c_str(),
            velocitycopy::localization::get_string(L"ElevationMessage").c_str(),
            active_destination_.wstring(),
            velocitycopy::localization::get_string(L"ActionContinueAsAdmin").c_str(),
            velocitycopy::localization::get_string(L"ActionCancel").c_str(),
            {},
            {},
            false,
            velocitycopy::ui::DecisionTone::Warning,
        }));
        proceed = decision.choice == velocitycopy::ui::DecisionChoice::Primary &&
            !tray_exit_requested_ && !session_ending_;
    } catch (...) {
        proceed = false;
    }
    answer->set(proceed);
}

fire_and_forget MainWindow::ElevateParkedFailuresAsync() {
    auto lifetime = get_strong();
    if (elevation_in_flight_ || interrupted_session_ != InterruptedSessionState::Decision || !live_plan_) co_return;
    velocitycopy::QueueArchive archive{};
    try {
        // Everything still unresolved moves to the elevated instance, the
        // same set a saved queue holds; nothing already copied is repeated.
        archive.source_removals = live_plan_->parked_source_removals();
        archive.current_plan = live_plan_->export_remaining_plan();
        if (archive.current_plan->files.empty() && archive.current_plan->directories.empty()) {
            archive.current_plan.reset();
        }
        archive.current_append_jobs.assign(
            deferred_same_destination_jobs_.begin(), deferred_same_destination_jobs_.end());
        archive.current_append_jobs.insert(archive.current_append_jobs.end(),
            deferred_interrupted_jobs_.begin(), deferred_interrupted_jobs_.end());
    } catch (...) {
        ShowError();
        co_return;
    }
    elevation_in_flight_ = true;
    PauseButton().IsEnabled(false);
    const auto plan = live_plan_;
    const auto owner = hwnd_;
    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    // The UAC prompt blocks ShellExecuteEx until it is answered.
    co_await resume_background();
    std::int32_t outcome = static_cast<std::int32_t>(E_FAIL);
    if (const auto directory = velocitycopy::app_data_directory()) {
        if (const auto handoff = velocitycopy::write_elevated_handoff(*directory, archive)) {
            outcome = velocitycopy::launch_elevated_handoff(current_executable(), *handoff, owner);
        }
    }
    (void)dispatcher.TryEnqueue([weak, plan, outcome]() {
        auto self = weak.get();
        if (!self) return;
        self->elevation_in_flight_ = false;
        if (self->tray_exit_requested_ || self->session_ending_) return;
        if (self->interrupted_session_ != InterruptedSessionState::Decision || self->live_plan_ != plan) return;
        self->PauseButton().IsEnabled(true);
        if (outcome == S_OK) {
            // The elevated window owns that work now; retire this session.
            self->CancelCurrentSession();
        } else if (outcome != static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_CANCELLED))) {
            self->ShowError(self->FormatFailureReason(outcome));
        }
        // Declined UAC prompt: the Decision stays open with all work parked.
    });
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
