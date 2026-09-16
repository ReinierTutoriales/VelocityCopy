#include "pch.h"
#include "MainWindow.xaml.h"

#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::OnQueueDragItemsCompleted(
    ListViewBase const&,
    DragItemsCompletedEventArgs const&) {
    if (!live_plan_) {
        return;
    }

    const auto items = QueueList().Items();
    if (items.Size() != queue_snapshot_.size()) {
        RefreshQueue();
        return;
    }

    std::vector<bool> used(queue_snapshot_.size(), false);
    std::vector<std::uint64_t> desired_order;
    desired_order.reserve(queue_snapshot_.size());

    for (std::uint32_t item_index = 0; item_index < items.Size(); ++item_index) {
        hstring source;
        try {
            source = unbox_value<hstring>(items.GetAt(item_index));
        } catch (...) {
            RefreshQueue();
            return;
        }

        bool matched = false;
        for (std::size_t snapshot_index = 0; snapshot_index < queue_snapshot_.size(); ++snapshot_index) {
            if (used[snapshot_index]) {
                continue;
            }

            if (hstring(queue_snapshot_[snapshot_index].source.wstring()) == source) {
                used[snapshot_index] = true;
                desired_order.push_back(queue_snapshot_[snapshot_index].id);
                matched = true;
                break;
            }
        }

        if (!matched) {
            RefreshQueue();
            return;
        }
    }

    for (std::size_t target_index = 0; target_index < desired_order.size(); ++target_index) {
        if (!live_plan_->move_pending_file(desired_order[target_index], target_index)) {
            RefreshQueue();
            return;
        }
    }

    RefreshQueue();
}

} // namespace winrt::VelocityCopyUI::implementation
