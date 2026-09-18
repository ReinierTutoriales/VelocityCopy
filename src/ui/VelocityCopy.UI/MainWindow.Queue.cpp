#include "pch.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::RefreshQueue() {
    if (!live_plan_) {
        queue_snapshot_.clear();
        QueueList().Items().Clear();
        QueueCountText().Text(L"0");
        RefreshQueueCommandState();
        RefreshQueueEditCommandState();
        return;
    }

    constexpr std::size_t kVisibleQueueItems = 256;
    auto view = live_plan_->queue_view(kVisibleQueueItems);
    const bool unchanged = view.pending_files.size() == queue_snapshot_.size() &&
        std::equal(view.pending_files.begin(), view.pending_files.end(), queue_snapshot_.begin(),
            [](const velocitycopy::PlannedFile& left, const velocitycopy::PlannedFile& right) {
                return left.id == right.id && left.source == right.source && left.destination == right.destination && left.size == right.size;
            });
    if (unchanged) {
        QueueCountText().Text(hstring(std::format(L"{}", view.pending_count)));
        RefreshQueueCommandState();
        return;
    }
    const auto selected_ids = SelectedPendingIds();
    queue_snapshot_ = std::move(view.pending_files);

    auto items = QueueList().Items();
    items.Clear();
    for (const auto& file : queue_snapshot_) {
        StackPanel row;
        row.Spacing(1);
        row.HorizontalAlignment(HorizontalAlignment::Stretch);
        row.Tag(box_value(file.id));

        TextBlock name;
        name.Text(hstring(file.source.filename().wstring()));
        name.TextTrimming(TextTrimming::CharacterEllipsis);
        name.FontWeight(Windows::UI::Text::FontWeights::SemiBold());

        TextBlock location;
        location.Text(hstring(file.source.parent_path().wstring()));
        location.TextTrimming(TextTrimming::CharacterEllipsis);
        location.Opacity(0.58);
        location.FontSize(11);

        row.Children().Append(name);
        row.Children().Append(location);
        items.Append(row);
    }

    if (!selected_ids.empty()) {
        for (std::uint32_t index = 0; index < queue_snapshot_.size(); ++index) {
            if (std::find(selected_ids.begin(), selected_ids.end(), queue_snapshot_[index].id) != selected_ids.end()) {
                QueueList().SelectRange(Windows::Foundation::IndexRange(index, index));
            }
        }
    }

    QueueCountText().Text(hstring(std::format(L"{}", view.pending_count)));
    RefreshQueueCommandState();
    RefreshQueueEditCommandState();
}

std::vector<std::uint64_t> MainWindow::SelectedPendingIds() {
    std::vector<std::uint64_t> ids;
    const auto ranges = QueueList().SelectedRanges();
    for (const auto& range : ranges) {
        const auto first = static_cast<std::size_t>(range.FirstIndex());
        const auto last = static_cast<std::size_t>(range.LastIndex());
        if (first >= queue_snapshot_.size()) {
            continue;
        }
        const auto bounded_last = std::min(last, queue_snapshot_.size() - 1);
        for (std::size_t index = first; index <= bounded_last; ++index) {
            ids.push_back(queue_snapshot_[index].id);
        }
    }
    return ids;
}

void MainWindow::RefreshQueueEditCommandState() {
    const bool has_selection = !SelectedPendingIds().empty();
    QueueMoveUpButton().IsEnabled(has_selection);
    QueueMoveDownButton().IsEnabled(has_selection);
    QueueRemoveButton().IsEnabled(has_selection);
}

void MainWindow::OnQueueSelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) {
    RefreshQueueEditCommandState();
}

void MainWindow::OnQueueClick(IInspectable const&, RoutedEventArgs const&) {
    const bool expanding = QueuePanel().Visibility() != Visibility::Visible;
    QueuePanel().Visibility(expanding ? Visibility::Visible : Visibility::Collapsed);
    QueueButton().Content(box_value(hstring(expanding ? L"▾" : L"▸")));
    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        const auto label = loader.GetString(expanding ? L"ActionHideQueue" : L"ActionShowQueue");
        ToolTipService::SetToolTip(QueueButton(), box_value(label));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(QueueButton(), label);
    } catch (...) {
    }
    if (expanding) {
        RefreshQueue();
        ResizeWindow(300);
    } else {
        ResizeWindow(72);
    }
}

void MainWindow::OnQueueMoveUpClick(IInspectable const&, RoutedEventArgs const&) {
    if (!live_plan_) {
        return;
    }
    (void)live_plan_->move_pending_files_up(SelectedPendingIds());
    RefreshQueue();
}

void MainWindow::OnQueueMoveDownClick(IInspectable const&, RoutedEventArgs const&) {
    if (!live_plan_) {
        return;
    }
    (void)live_plan_->move_pending_files_down(SelectedPendingIds());
    RefreshQueue();
}

void MainWindow::OnQueueRemoveClick(IInspectable const&, RoutedEventArgs const&) {
    if (!live_plan_) {
        return;
    }
    (void)live_plan_->remove_pending_files(SelectedPendingIds());
    RefreshQueue();
    FinalizeStoppedSessionIfEmpty();
    FinalizeConflictSessionIfEmpty();
}

void MainWindow::OnQueueDragItemsCompleted(
    ListViewBase const&,
    DragItemsCompletedEventArgs const&) {
    if (!live_plan_) {
        return;
    }

    const auto items = QueueList().Items();
    std::vector<std::uint64_t> ordered_ids;
    ordered_ids.reserve(items.Size());
    for (std::uint32_t index = 0; index < items.Size(); ++index) {
        try {
            const auto row = items.GetAt(index).as<FrameworkElement>();
            ordered_ids.push_back(unbox_value<std::uint64_t>(row.Tag()));
        } catch (...) {
            // A malformed visual item must not corrupt the live queue order.
        }
    }

    (void)live_plan_->reorder_pending_files(ordered_ids);
    RefreshQueue();
}

} // namespace winrt::VelocityCopyUI::implementation
