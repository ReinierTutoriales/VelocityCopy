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
    auto target_plan = live_plan_;
    auto target_control = execution_control_;
    auto target_gate = append_gate_;

    // No active session: this job owns the copy thread.
    if (!target_control || !target_gate) {
        StartCopy(std::move(job));
        return;
    }

    // Different destinations are separate sessions and must never replace the
    // currently running jthread. Preserve arrival order and start them only
    // after the active session reaches a terminal state.
    if (!same_destination(active_destination_, job.destination)) {
        queued_sessions_.push_back(std::move(job));
        return;
    }

    // The first job may still be in its background planning phase. Reserve the
    // active session immediately so an ultra-short first batch cannot close
    // before this deferred job is transferred to the FIFO planning worker.
    if (!target_plan) {
        bool reserved = false;
        {
            std::lock_guard gate_lock(target_gate->mutex);
            if (target_gate->accepting) {
                ++target_gate->planning_count;
                reserved = true;
            }
        }
        if (reserved) {
            deferred_same_destination_jobs_.push_back(std::move(job));
        } else {
            queued_sessions_.push_back(std::move(job));
        }
        return;
    }

    EnqueueAppend(
        std::move(job),
        std::move(target_plan),
        std::move(target_control),
        std::move(target_gate),
        false);
}

void MainWindow::EnqueueAppend(
    velocitycopy::CopyJob job,
    std::shared_ptr<velocitycopy::LiveCopyPlan> target_plan,
    std::shared_ptr<velocitycopy::ExecutionControl> target_control,
    std::shared_ptr<AppendGate> target_gate,
    const bool reservation_already_held) {
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
                if (target_gate->planning_count != 0) {
                    --target_gate->planning_count;
                }
                target_gate->condition.notify_all();
            };

            if (!result.plan) {
                release_reservation();
                (void)dispatcher.TryEnqueue([weak, target_gate]() {
                    if (auto self = weak.get(); self && self->append_gate_ == target_gate) {
                        self->ShowError();
                    }
                });
                return;
            }

            // Planning and directory creation stay off the UI thread. Creating
            // directories here also preserves empty folders in appended batches.
            for (const auto& directory : result.plan->directories) {
                {
                    std::lock_guard gate_lock(target_gate->mutex);
                    if (!target_gate->accepting) {
                        if (target_gate->planning_count != 0) {
                            --target_gate->planning_count;
                        }
                        target_gate->condition.notify_all();
                        return;
                    }
                }

                std::error_code ec;
                std::filesystem::create_directories(directory.destination, ec);
                if (ec) {
                    release_reservation();
                    (void)dispatcher.TryEnqueue([weak, target_gate]() {
                        if (auto self = weak.get(); self && self->append_gate_ == target_gate) {
                            self->ShowError();
                        }
                    });
                    return;
                }
            }

            velocitycopy::LivePlanAppendResult append_result =
                velocitycopy::LivePlanAppendResult::Drained;
            bool committed = false;
            {
                // Gate -> plan is the single lock order used by the session.
                // It prevents the executor from closing the session between the
                // final planning reservation and the queue commit.
                std::lock_guard gate_lock(target_gate->mutex);
                if (target_gate->accepting) {
                    append_result = target_plan->append(std::move(*result.plan), true);
                    committed = true;
                }
                if (target_gate->planning_count != 0) {
                    --target_gate->planning_count;
                }
                target_gate->condition.notify_all();
            }

            if (!committed) {
                return;
            }

            (void)dispatcher.TryEnqueue([
                weak,
                target_plan,
                target_control,
                target_gate,
                append_result]() mutable {
                if (auto self = weak.get()) {
                    if (self->live_plan_ != target_plan ||
                        self->execution_control_ != target_control ||
                        self->append_gate_ != target_gate) {
                        return;
                    }

                    switch (append_result) {
                    case velocitycopy::LivePlanAppendResult::Appended:
                        self->QueueButton().IsEnabled(true);
                        self->RefreshQueue();
                        return;

                    case velocitycopy::LivePlanAppendResult::Drained:
                    case velocitycopy::LivePlanAppendResult::DifferentDestination:
                    case velocitycopy::LivePlanAppendResult::DestinationCollision:
                    case velocitycopy::LivePlanAppendResult::SizeOverflow:
                        self->ShowError();
                        return;
                    }
                }
            });
        });
}

} // namespace winrt::VelocityCopyUI::implementation
