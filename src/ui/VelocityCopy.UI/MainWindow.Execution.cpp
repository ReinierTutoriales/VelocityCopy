#include "pch.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::SetExecutionButtonsPlanning() {
    PauseButton().IsEnabled(false);
    StopButton().IsEnabled(false);
    CancelButton().IsEnabled(true);
    paused_ = false;
}

void MainWindow::SetExecutionButtonsRunning() {
    PauseButton().IsEnabled(true);
    StopButton().IsEnabled(true);
    CancelButton().IsEnabled(true);
    paused_ = false;
    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        PauseButton().Content(box_value(loader.GetString(L"ActionPause")));
    } catch (...) {
    }
}

void MainWindow::SetExecutionButtonsIdle() {
    PauseButton().IsEnabled(false);
    StopButton().IsEnabled(false);
    CancelButton().IsEnabled(false);
    paused_ = false;
    resume_requested_ = false;
    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        PauseButton().Content(box_value(loader.GetString(L"ActionPause")));
    } catch (...) {
    }
}

void MainWindow::SetExecutionButtonsStopped() {
    PauseButton().IsEnabled(true);
    StopButton().IsEnabled(false);
    CancelButton().IsEnabled(true);
    paused_ = false;
    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        PauseButton().Content(box_value(loader.GetString(L"ActionResume")));
    } catch (...) {
    }
}

velocitycopy::JobResult MainWindow::RunLivePlanSession(
    std::shared_ptr<velocitycopy::LiveCopyPlan> plan,
    std::shared_ptr<velocitycopy::ExecutionControl> control,
    std::shared_ptr<AppendGate> gate,
    const std::stop_token stop_token,
    const bool publish_plan) {
    auto weak = get_weak();
    auto dispatcher = dispatcher_;

    if (publish_plan) {
        (void)dispatcher.TryEnqueue([weak, plan]() {
            if (auto self = weak.get()) {
                self->PublishLivePlan(plan);
            }
        });
    }

    velocitycopy::JobResult result{true, false, S_OK, false};
    for (;;) {
        result = executor_.execute(
            *plan,
            *control,
            [this, weak, dispatcher, control, stop_token](const velocitycopy::JobProgress& progress) {
                if (stop_token.stop_requested() ||
                    cancel_requested_.load(std::memory_order_relaxed)) {
                    control->request_cancel();
                    return velocitycopy::JobDecision::Cancel;
                }

                if (auto snapshot = presenter_.observe(progress, GetTickCount64())) {
                    const auto value = *snapshot;
                    (void)dispatcher.TryEnqueue([weak, value]() {
                        if (auto self = weak.get()) {
                            self->ApplySnapshot(value);
                        }
                    });
                }
                return velocitycopy::JobDecision::Continue;
            });

        if (result.cancelled || (!result.success && !result.stopped)) {
            break;
        }

        std::unique_lock gate_lock(gate->mutex);
        if (result.stopped) {
            // Stop freezes the session only after work accepted before Stop has
            // finished planning and atomically committed into LiveCopyPlan.
            if (gate->planning_count != 0 && gate->accepting) {
                (void)gate->condition.wait(
                    gate_lock,
                    stop_token,
                    [&] { return gate->planning_count == 0 || !gate->accepting; });
            }

            // Cancel has precedence over Stop. This closes the race where the
            // user presses Cancel while Stop is waiting for an accepted planner.
            if (stop_token.stop_requested() ||
                cancel_requested_.load(std::memory_order_relaxed) ||
                control->directive() == velocitycopy::ExecutionDirective::Cancel) {
                control->request_cancel();
                result = {
                    false,
                    true,
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)),
                    false};
            }
            gate->accepting = false;
            gate->condition.notify_all();
            break;
        }

        if (gate->planning_count != 0 && gate->accepting) {
            (void)gate->condition.wait(
                gate_lock,
                stop_token,
                [&] { return gate->planning_count == 0 || !gate->accepting; });
        }

        if (stop_token.stop_requested() ||
            cancel_requested_.load(std::memory_order_relaxed)) {
            gate->accepting = false;
            gate->condition.notify_all();
            control->request_cancel();
            result = {
                false,
                true,
                static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)),
                false};
            break;
        }

        if (!gate->accepting) {
            const auto directive = control->directive();
            if (directive == velocitycopy::ExecutionDirective::Cancel) {
                result = {
                    false,
                    true,
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)),
                    false};
            } else if (directive == velocitycopy::ExecutionDirective::Stop) {
                result = {false, false, S_OK, true};
            }
            break;
        }

        if (plan->remaining_files() != 0) {
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

void MainWindow::StartCopy(velocitycopy::CopyJob job) {
    active_destination_ = job.destination;
    stopped_session_ = false;
    stop_requested_ = false;
    resume_requested_ = false;
    cancel_requested_.store(false, std::memory_order_relaxed);
    presenter_.reset();
    last_queue_completed_files_ = 0;
    live_plan_.reset();
    execution_control_ = std::make_shared<velocitycopy::ExecutionControl>();
    append_gate_ = std::make_shared<AppendGate>();
    queue_snapshot_.clear();
    QueueList().Items().Clear();
    QueueCountText().Text(L"0");
    QueueButton().IsEnabled(false);
    QueuePanel().Visibility(Visibility::Collapsed);
    ResizeWindow(156);
    GlobalProgress().Value(0);
    SetExecutionButtonsPlanning();
    CurrentItemText().Text(job.display_name.empty() ? hstring(L"…") : hstring(job.display_name));

    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    auto control = execution_control_;
    auto gate = append_gate_;

    copy_thread_ = std::jthread(
        [this, weak, dispatcher, control, gate, job = std::move(job)](std::stop_token stop_token) mutable {
            std::shared_ptr<velocitycopy::LiveCopyPlan> plan;
            try {
                plan = std::make_shared<velocitycopy::LiveCopyPlan>(planner_.build(job));
            } catch (...) {
                {
                    std::lock_guard gate_lock(gate->mutex);
                    gate->accepting = false;
                    gate->condition.notify_all();
                }
                (void)dispatcher.TryEnqueue([weak]() {
                    if (auto self = weak.get()) {
                        self->FinishCopy({false, false, static_cast<std::int32_t>(E_FAIL), false});
                    }
                });
                return;
            }

            if (stop_token.stop_requested() ||
                cancel_requested_.load(std::memory_order_relaxed)) {
                control->request_cancel();
                (void)dispatcher.TryEnqueue([weak]() {
                    if (auto self = weak.get()) {
                        self->FinishCopy({
                            false,
                            true,
                            static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)),
                            false});
                    }
                });
                return;
            }

            const auto result = RunLivePlanSession(
                plan, control, gate, stop_token, true);
            (void)dispatcher.TryEnqueue([weak, result]() {
                if (auto self = weak.get()) {
                    self->FinishCopy(result);
                }
            });
        });
}

void MainWindow::ResumeStoppedCopy() {
    if (!stopped_session_ || !live_plan_) {
        return;
    }

    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        if (append_gate_->planning_count != 0) {
            resume_requested_ = true;
            PauseButton().IsEnabled(false);
            return;
        }
    }

    if (live_plan_->remaining_files() == 0) {
        resume_requested_ = false;
        FinalizeStoppedSessionIfEmpty();
        return;
    }

    resume_requested_ = false;
    stopped_session_ = false;
    stop_requested_ = false;
    cancel_requested_.store(false, std::memory_order_relaxed);
    presenter_.reset();
    last_queue_completed_files_ = live_plan_->completed_files();
    execution_control_ = std::make_shared<velocitycopy::ExecutionControl>();
    if (!append_gate_) {
        append_gate_ = std::make_shared<AppendGate>();
    }

    auto plan = live_plan_;
    auto control = execution_control_;
    auto gate = append_gate_;
    active_destination_ = plan->destination_root();
    SetExecutionButtonsRunning();

    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    copy_thread_ = std::jthread(
        [this, weak, dispatcher, plan, control, gate](std::stop_token stop_token) {
            const auto result = RunLivePlanSession(
                plan, control, gate, stop_token, false);
            (void)dispatcher.TryEnqueue([weak, result]() {
                if (auto self = weak.get()) {
                    self->FinishCopy(result);
                }
            });
        });
}

void MainWindow::StartNextQueuedSession() {
    if (execution_control_ || stopped_session_ || stop_requested_ || queued_sessions_.empty()) {
        return;
    }
    auto next = std::move(queued_sessions_.front());
    queued_sessions_.pop_front();
    StartCopy(std::move(next));
}

void MainWindow::PublishLivePlan(std::shared_ptr<velocitycopy::LiveCopyPlan> plan) {
    live_plan_ = std::move(plan);
    QueueButton().IsEnabled(true);
    RefreshQueue();
    if (!stop_requested_) {
        SetExecutionButtonsRunning();
    }

    auto deferred = std::move(deferred_same_destination_jobs_);
    deferred_same_destination_jobs_.clear();
    auto target_plan = live_plan_;
    auto target_control = execution_control_;
    auto target_gate = append_gate_;
    for (auto& job : deferred) {
        if (target_plan && target_control && target_gate) {
            EnqueueAppend(
                std::move(job), target_plan, target_control, target_gate, true);
        }
    }
}

void MainWindow::OnPauseClick(IInspectable const&, RoutedEventArgs const&) {
    if (stopped_session_) {
        ResumeStoppedCopy();
        return;
    }
    if (!execution_control_) {
        return;
    }

    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        if (paused_) {
            execution_control_->resume();
            paused_ = false;
            PauseButton().Content(box_value(loader.GetString(L"ActionPause")));
        } else {
            execution_control_->request_pause();
            paused_ = true;
            PauseButton().Content(box_value(loader.GetString(L"ActionResume")));
            SpeedText().Text(L"—");
            EtaText().Text(L"—");
        }
    } catch (...) {
        if (paused_) {
            execution_control_->resume();
            paused_ = false;
        } else {
            execution_control_->request_pause();
            paused_ = true;
        }
    }
}

void MainWindow::OnStopClick(IInspectable const&, RoutedEventArgs const&) {
    if (!execution_control_ || stopped_session_ || stop_requested_) {
        return;
    }

    // Do not cancel the append planner here. Work accepted before Stop remains
    // part of the session and RunLivePlanSession waits for its reservations.
    resume_requested_ = false;
    stop_requested_ = true;
    execution_control_->request_stop();
    PauseButton().IsEnabled(false);
    StopButton().IsEnabled(false);
}

void MainWindow::OnCancelClick(IInspectable const&, RoutedEventArgs const&) {
    cancel_requested_.store(true, std::memory_order_relaxed);
    resume_requested_ = false;
    deferred_same_destination_jobs_.clear();
    deferred_after_stop_jobs_.clear();
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

    // A stopped session has no executor thread. Cancel must therefore dispose
    // it synchronously instead of waiting for a FinishCopy callback that cannot occur.
    if (stopped_session_) {
        stopped_session_ = false;
        stop_requested_ = false;
        live_plan_.reset();
        append_gate_.reset();
        active_destination_.clear();
        queue_snapshot_.clear();
        QueueList().Items().Clear();
        QueueCountText().Text(L"0");
        QueueButton().IsEnabled(false);
        SetExecutionButtonsIdle();
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
    }
}

void MainWindow::ApplySnapshot(const velocitycopy::UiSnapshot& snapshot) {
    GlobalProgress().Value(snapshot.fraction * 100.0);
    if (!snapshot.current_source.empty()) {
        CurrentItemText().Text(hstring(snapshot.current_source.filename().wstring()));
    }
    SpeedText().Text(FormatSpeed(snapshot.bytes_per_second));
    EtaText().Text(FormatEta(snapshot.eta_seconds));

    if (QueuePanel().Visibility() == Visibility::Visible &&
        snapshot.completed_files != last_queue_completed_files_) {
        last_queue_completed_files_ = snapshot.completed_files;
        RefreshQueue();
    }
}

void MainWindow::FinishCopy(const velocitycopy::JobResult& original_result) {
    auto result = original_result;
    auto deferred_initial = std::move(deferred_same_destination_jobs_);
    deferred_same_destination_jobs_.clear();

    // A Cancel pressed after the worker produced a stopped result but before the
    // UI callback runs must not resurrect the stopped session.
    if (result.stopped && cancel_requested_.load(std::memory_order_relaxed)) {
        result = {
            false,
            true,
            static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED)),
            false};
    }

    execution_control_.reset();
    paused_ = false;

    if (result.stopped) {
        stop_requested_ = false;
        stopped_session_ = true;
        append_gate_ = std::make_shared<AppendGate>();
        if (live_plan_) {
            active_destination_ = live_plan_->destination_root();
        }
        SetExecutionButtonsStopped();
        RefreshQueue();
        QueueButton().IsEnabled(live_plan_ && live_plan_->remaining_files() != 0);
        SpeedText().Text(L"—");
        EtaText().Text(L"—");

        auto deferred_stop = std::move(deferred_after_stop_jobs_);
        deferred_after_stop_jobs_.clear();
        auto plan = live_plan_;
        auto gate = append_gate_;
        for (auto& job : deferred_stop) {
            if (plan && gate) {
                EnqueueAppend(std::move(job), plan, nullptr, gate, false);
            }
        }
        FinalizeStoppedSessionIfEmpty();
        return;
    }

    resume_requested_ = false;
    stop_requested_ = false;
    stopped_session_ = false;
    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        append_gate_->accepting = false;
        append_gate_->condition.notify_all();
    }
    append_gate_.reset();

    if (result.cancelled) {
        deferred_after_stop_jobs_.clear();
        queued_sessions_.clear();
        live_plan_.reset();
        active_destination_.clear();
        RefreshQueue();
        QueueButton().IsEnabled(false);
        SetExecutionButtonsIdle();
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
        return;
    }

    while (!deferred_initial.empty()) {
        queued_sessions_.push_front(std::move(deferred_initial.back()));
        deferred_initial.pop_back();
    }

    if (!result.success) {
        live_plan_.reset();
        active_destination_.clear();
        RefreshQueue();
        QueueButton().IsEnabled(false);
        SetExecutionButtonsIdle();
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
        ShowError();
        return;
    }

    live_plan_.reset();
    active_destination_.clear();
    RefreshQueue();
    QueueButton().IsEnabled(false);
    SetExecutionButtonsIdle();
    SpeedText().Text(L"—");
    EtaText().Text(L"—");

    if (queued_sessions_.empty()) {
        GlobalProgress().Value(100);
        return;
    }
    StartNextQueuedSession();
}

void MainWindow::FinalizeStoppedSessionIfEmpty() {
    if (!stopped_session_ || !live_plan_ || live_plan_->remaining_files() != 0) {
        return;
    }

    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        if (append_gate_->planning_count != 0) {
            return;
        }
        append_gate_->accepting = false;
        append_gate_->condition.notify_all();
    }

    resume_requested_ = false;
    stopped_session_ = false;
    stop_requested_ = false;
    live_plan_.reset();
    append_gate_.reset();
    active_destination_.clear();
    RefreshQueue();
    QueueButton().IsEnabled(false);
    SetExecutionButtonsIdle();
    GlobalProgress().Value(100);
    StartNextQueuedSession();
}

} // namespace winrt::VelocityCopyUI::implementation
