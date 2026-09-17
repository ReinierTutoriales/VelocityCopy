#include "pch.h"
#include "MainWindow.xaml.h"

#include <algorithm>
#include <cwctype>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::VelocityCopyUI::implementation {
namespace {

std::wstring destination_key(const std::filesystem::path& path) {
    auto value = path.lexically_normal().wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](const wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

bool same_destination(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    return !left.empty() && !right.empty() && destination_key(left) == destination_key(right);
}

bool same_session(
    const std::filesystem::path& active_destination,
    const velocitycopy::FileOperation active_operation,
    const velocitycopy::CopyJob& job) {
    return same_destination(active_destination, job.destination) &&
           active_operation == job.operation;
}

} // namespace

void MainWindow::OnQueueOrStartCopyClick(IInspectable const&, RoutedEventArgs const&) {
    auto job = flow_.make_job(next_job_id_++);
    if (!job) {
        ShowError();
        return;
    }

    DropFlowFlyout().Hide();
    QueueOrStartCopy(std::move(*job));
}

void MainWindow::QueueOrStartCopy(velocitycopy::CopyJob job) {
    if (stop_requested_) {
        if (same_session(active_destination_, active_operation_, job)) {
            deferred_interrupted_jobs_.push_back(std::move(job));
        } else {
            queued_sessions_.push_back(std::move(job));
        }
        return;
    }

    if (stopped_session_ && live_plan_ && append_gate_) {
        if (!same_session(active_destination_, active_operation_, job)) {
            queued_sessions_.push_back(std::move(job));
            return;
        }
        EnqueueAppend(std::move(job), live_plan_, nullptr, append_gate_, false);
        return;
    }

    if (conflict_session_ && live_plan_ && append_gate_) {
        if (!same_session(active_destination_, active_operation_, job)) {
            queued_sessions_.push_back(std::move(job));
            return;
        }
        EnqueueAppend(std::move(job), live_plan_, nullptr, append_gate_, false);
        return;
    }

    auto target_plan = live_plan_;
    auto target_control = execution_control_;
    auto target_gate = append_gate_;

    if (!target_control || !target_gate) {
        StartCopy(std::move(job));
        return;
    }

    if (!same_session(active_destination_, active_operation_, job)) {
        queued_sessions_.push_back(std::move(job));
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
        else queued_sessions_.push_back(std::move(job));
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
        queued_sessions_.push_back(std::move(job));
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
            if ((stopped_session_ || conflict_session_) && same_session(active_destination_, active_operation_, job))
                deferred_interrupted_jobs_.push_back(std::move(job));
            else
                queued_sessions_.push_back(std::move(job));
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
