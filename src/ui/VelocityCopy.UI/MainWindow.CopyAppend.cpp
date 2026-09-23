#include "pch.h"
#include "MainWindow.xaml.h"

using namespace winrt;

namespace winrt::VelocityCopyUI::implementation {

std::optional<velocitycopy::ActiveSession> MainWindow::SessionSnapshot() {
    if (tray_exit_requested_ || !HasActiveTransfer() || active_destination_.empty()) return std::nullopt;
    bool accepting = false;
    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        accepting = append_gate_->accepting;
    }
    return velocitycopy::ActiveSession{window_id_, active_destination_, active_operation_, accepting, active_destination_key_, active_source_key_};
}

bool MainWindow::IsVisibleForRouting() const noexcept {
    return !tray_exit_requested_ && hwnd_ != nullptr && !tray_window_hidden_ && IsWindowVisible(hwnd_) != FALSE;
}

void MainWindow::EnqueueTransfer(velocitycopy::CopyJob job, velocitycopy::StorageKey destination_key, velocitycopy::StorageKey source_key) {
    queued_sessions_.push_back({std::move(job), std::move(destination_key), std::move(source_key)});
    RefreshQueue();
}

void MainWindow::AppendTransfer(velocitycopy::CopyJob job) {
    RefreshEfficiencyMode();

    if (stop_requested_) {
        deferred_interrupted_jobs_.push_back(std::move(job));
        return;
    }

    if ((stopped_session_ || conflict_session_) && live_plan_ && append_gate_) {
        EnqueueAppend(std::move(job), live_plan_, nullptr, append_gate_, false);
        return;
    }

    auto target_plan = live_plan_;
    auto target_control = execution_control_;
    auto target_gate = append_gate_;
    if (!target_control || !target_gate) {
        EnqueueTransfer(std::move(job));
        return;
    }

    if (!target_plan) {
        bool reserved = false;
        {
            std::lock_guard gate_lock(target_gate->mutex);
            if (target_gate->accepting) {
                ++target_gate->planning_count;
                reserved = true;
            }
        }
        if (reserved) deferred_same_destination_jobs_.push_back(std::move(job));
        else EnqueueTransfer(std::move(job));
        return;
    }

    EnqueueAppend(std::move(job), std::move(target_plan), std::move(target_control), std::move(target_gate), false);
}

void MainWindow::EnqueueAppend(
    velocitycopy::CopyJob job,
    std::shared_ptr<velocitycopy::LiveCopyPlan> target_plan,
    std::shared_ptr<velocitycopy::ExecutionControl> target_control,
    std::shared_ptr<AppendGate> target_gate,
    const bool reservation_already_held) {
    if (!target_plan || !target_gate) {
        EnqueueTransfer(std::move(job));
        return;
    }

    if (!reservation_already_held) {
        bool reserved = false;
        {
            std::lock_guard gate_lock(target_gate->mutex);
            if (target_gate->accepting) {
                ++target_gate->planning_count;
                reserved = true;
            }
        }
        if (!reserved) {
            if (stopped_session_ || conflict_session_) deferred_interrupted_jobs_.push_back(std::move(job));
            else EnqueueTransfer(std::move(job));
            return;
        }
    }

    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    (void)append_planner_.enqueue(
        std::move(job),
        [weak, dispatcher, target_plan, target_control, target_gate](velocitycopy::JobPlanningResult result) mutable {
            auto release_reservation = [&]() {
                std::lock_guard gate_lock(target_gate->mutex);
                if (target_gate->planning_count != 0) --target_gate->planning_count;
                target_gate->condition.notify_all();
            };
            auto notify_failure = [weak, dispatcher, target_gate]() {
                (void)dispatcher.TryEnqueue([weak, target_gate]() {
                    if (auto self = weak.get(); self && self->append_gate_ == target_gate) {
                        self->ShowError();
                        if (self->conflict_session_ && self->resume_requested_)
                            self->ResumeConflictCopy(self->conflict_replace_file_id_);
                        else if (self->stopped_session_ && self->resume_requested_)
                            self->ResumeStoppedCopy();
                        else if (self->conflict_session_)
                            self->FinalizeConflictSessionIfEmpty();
                        else
                            self->FinalizeStoppedSessionIfEmpty();
                    }
                });
            };

            if (!result.plan) {
                release_reservation();
                if (result.error_code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED))) {
                    // Explicit planner cancellation is a control transition, not
                    // a transfer failure. Reservations are released above, but
                    // the compact UI must not surface a spurious error banner.
                    return;
                }
                notify_failure();
                return;
            }

            velocitycopy::LivePlanAppendResult append_result = velocitycopy::LivePlanAppendResult::Drained;
            bool committed = false;
            {
                std::lock_guard gate_lock(target_gate->mutex);
                if (target_gate->accepting) {
                    append_result = target_plan->append(std::move(*result.plan), true);
                    committed = true;
                }
                if (target_gate->planning_count != 0) --target_gate->planning_count;
                target_gate->condition.notify_all();
            }
            if (!committed) return;

            (void)dispatcher.TryEnqueue([weak, target_plan, target_control, target_gate, append_result]() mutable {
                if (auto self = weak.get()) {
                    if (self->live_plan_ != target_plan || self->execution_control_ != target_control || self->append_gate_ != target_gate)
                        return;
                    switch (append_result) {
                    case velocitycopy::LivePlanAppendResult::Appended:
                        self->QueueButton().IsEnabled(true);
                        self->RefreshQueue();
                        if (self->conflict_session_ && self->resume_requested_)
                            self->ResumeConflictCopy(self->conflict_replace_file_id_);
                        else if (self->stopped_session_ && self->resume_requested_)
                            self->ResumeStoppedCopy();
                        return;
                    case velocitycopy::LivePlanAppendResult::Drained:
                    case velocitycopy::LivePlanAppendResult::DifferentDestination:
                    case velocitycopy::LivePlanAppendResult::DifferentOperation:
                    case velocitycopy::LivePlanAppendResult::DestinationCollision:
                    case velocitycopy::LivePlanAppendResult::SizeOverflow:
                    case velocitycopy::LivePlanAppendResult::InternalFailure:
                        self->ShowError();
                        if (self->conflict_session_ && self->resume_requested_)
                            self->ResumeConflictCopy(self->conflict_replace_file_id_);
                        else if (self->stopped_session_ && self->resume_requested_)
                            self->ResumeStoppedCopy();
                        else if (self->conflict_session_)
                            self->FinalizeConflictSessionIfEmpty();
                        else
                            self->FinalizeStoppedSessionIfEmpty();
                        return;
                    }
                }
            });
        });
}

} // namespace winrt::VelocityCopyUI::implementation
