#include "pch.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::VelocityCopyUI::implementation {

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
                if (stop_token.stop_requested()) {
                    control->request_cancel();
                    return velocitycopy::JobDecision::Cancel;
                }
                if (cancel_requested_.load(std::memory_order_relaxed)) {
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
            // Stop accepts work that was already reserved before the request.
            // Wait for those planners to commit before freezing the session.
            if (gate->planning_count != 0 && gate->accepting) {
                (void)gate->condition.wait(
                    gate_lock,
                    stop_token,
                    [&] { return gate->planning_count == 0 || !gate->accepting; });
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

        if (stop_token.stop_requested()) {
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

void MainWindow::ResumeStoppedCopy() {
    if (!stopped_session_ || !live_plan_) {
        return;
    }

    // If the user removed the final pending item, the stopped session is done
    // and the next destination can start instead of launching an empty thread.
    if (live_plan_->remaining_files() == 0) {
        FinalizeStoppedSessionIfEmpty();
        return;
    }

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
    GlobalProgress().Value(100);
    StartNextQueuedSession();
}

} // namespace winrt::VelocityCopyUI::implementation
