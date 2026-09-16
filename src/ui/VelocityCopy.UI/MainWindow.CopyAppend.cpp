#include "pch.h"
#include "MainWindow.xaml.h"

#include <algorithm>
#include <cwctype>

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

void MainWindow::QueueOrStartCopy(velocitycopy::CopyJob job) {
    auto target_plan = live_plan_;
    if (!target_plan || !same_destination(target_plan->destination_root(), job.destination)) {
        StartCopy(std::move(job));
        return;
    }

    auto weak = get_weak();
    auto dispatcher = dispatcher_;

    (void)append_planner_.enqueue(
        std::move(job),
        [weak, dispatcher, target_plan](velocitycopy::JobPlanningResult result) mutable {
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
                job = std::move(result.job),
                append_result]() mutable {
                if (auto self = weak.get()) {
                    if (self->live_plan_ != target_plan) {
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
                        self->StartCopy(std::move(job));
                        return;

                    case velocitycopy::LivePlanAppendResult::DifferentDestination:
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
