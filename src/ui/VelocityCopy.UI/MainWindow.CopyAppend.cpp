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

    // The first job may still be in its background planning phase. Preserve the
    // user's FIFO intent instead of starting a second copy merely because the
    // LiveCopyPlan has not been published to the UI yet.
    if (!target_plan && target_control &&
        same_destination(active_destination_, job.destination)) {
        deferred_same_destination_jobs_.push_back(std::move(job));
        return;
    }

    if (!target_plan || !target_control ||
        !same_destination(target_plan->destination_root(), job.destination)) {
        active_destination_ = job.destination;
        deferred_same_destination_jobs_.clear();
        StartCopy(std::move(job));
        return;
    }

    auto weak = get_weak();
    auto dispatcher = dispatcher_;

    (void)append_planner_.enqueue(
        std::move(job),
        [weak, dispatcher, target_plan, target_control](velocitycopy::JobPlanningResult result) mutable {
            if (!result.plan) {
                (void)dispatcher.TryEnqueue([weak]() {
                    if (auto self = weak.get()) {
                        self->ShowError();
                    }
                });
                return;
            }

            // Planning and directory creation stay off the UI thread. Creating
            // directories here also preserves empty folders in appended batches.
            for (const auto& directory : result.plan->directories) {
                std::error_code ec;
                std::filesystem::create_directories(directory.destination, ec);
                if (ec) {
                    (void)dispatcher.TryEnqueue([weak]() {
                        if (auto self = weak.get()) {
                            self->ShowError();
                        }
                    });
                    return;
                }
            }

            const auto append_result = target_plan->append(std::move(*result.plan));
            (void)dispatcher.TryEnqueue([
                weak,
                target_plan,
                target_control,
                job = std::move(result.job),
                append_result]() mutable {
                if (auto self = weak.get()) {
                    // The active execution may have changed while this batch was
                    // being planned. Never append into a stale/stopped session.
                    if (self->live_plan_ != target_plan ||
                        self->execution_control_ != target_control) {
                        self->QueueOrStartCopy(std::move(job));
                        return;
                    }

                    switch (append_result) {
                    case velocitycopy::LivePlanAppendResult::Appended:
                        self->QueueButton().IsEnabled(true);
                        self->RefreshQueue();
                        return;

                    case velocitycopy::LivePlanAppendResult::Drained:
                        // The previous operation crossed the drain boundary while
                        // this batch was being planned. Start it normally rather
                        // than leaving files stranded in a dead live plan.
                        self->active_destination_ = job.destination;
                        self->StartCopy(std::move(job));
                        return;

                    case velocitycopy::LivePlanAppendResult::DifferentDestination:
                        self->active_destination_ = job.destination;
                        self->StartCopy(std::move(job));
                        return;

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
