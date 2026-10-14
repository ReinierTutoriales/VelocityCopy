#include "pch.h"
#include "MainWindow.xaml.h"
#include "Localization.h"
#include "UiTokens.h"
#include "QueueItem.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;

namespace winrt::VelocityCopyUI::implementation {

namespace {
ScrollViewer find_scroll_viewer(DependencyObject const& root) {
    if (!root) return nullptr;
    if (auto viewer = root.try_as<ScrollViewer>()) return viewer;
    const auto child_count = Media::VisualTreeHelper::GetChildrenCount(root);
    for (int index = 0; index < child_count; ++index) {
        if (auto viewer = find_scroll_viewer(Media::VisualTreeHelper::GetChild(root, index))) {
            return viewer;
        }
    }
    return nullptr;
}
} // namespace


bool MainWindow::ApplyQueueItemStyle(const bool narrow) {
    if (narrow == queue_item_style_narrow_) return false;
    try {
        if (narrow) {
            QueueList().ItemContainerStyle(
                QueueList().Resources().Lookup(box_value(L"QueueNarrowListViewItemStyle")).as<Style>());
        } else {
            // ThreeColumn keeps the native ListViewItem container (DefaultListViewItemStyle).
            QueueList().ClearValue(ItemsControl::ItemContainerStyleProperty());
        }
        QueueList().ItemTemplate(RootGrid().Resources().Lookup(box_value(
            narrow ? L"QueueNarrowItemTemplate" : L"QueueItemTemplate")).as<DataTemplate>());
        queue_item_style_narrow_ = narrow;
        return true;
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: ApplyQueueItemStyle failed\n");
        return false;
    }
}

void MainWindow::RefreshActiveQueue() {
    constexpr std::size_t kVisibleActiveFiles = 8;
    auto rows = ActiveQueueRows().Children();
    if (!live_plan_) {
        rows.Clear();
        ActiveQueuePanel().Visibility(Visibility::Collapsed);
        return;
    }
    const auto view = live_plan_->active_queue_view(kVisibleActiveFiles);
    if (view.active_count == 0) {
        rows.Clear();
        ActiveQueuePanel().Visibility(Visibility::Collapsed);
        return;
    }
    ActiveQueuePanel().Visibility(Visibility::Visible);
    ActiveQueueHeading().Text(hstring(std::format(L"Transferring ({})", view.active_count)));
    while (rows.Size() > view.active_files.size()) rows.RemoveAtEnd();
    while (rows.Size() < view.active_files.size()) {
        StackPanel row;
        row.Spacing(2);
        TextBlock label;
        label.TextTrimming(TextTrimming::CharacterEllipsis);
        ProgressBar progress;
        progress.Minimum(0);
        progress.Maximum(100);
        row.Children().Append(label);
        row.Children().Append(progress);
        rows.Append(row);
    }
    for (std::uint32_t index = 0; index < view.active_files.size(); ++index) {
        const auto& active = view.active_files[index];
        auto row = rows.GetAt(index).as<StackPanel>();
        auto label = row.Children().GetAt(0).as<TextBlock>();
        auto progress = row.Children().GetAt(1).as<ProgressBar>();
        const double percent = active.file.size == 0 ? 0.0 :
            (std::min)(100.0, 100.0 * static_cast<double>(active.transferred_bytes) /
                static_cast<double>(active.file.size));
        const auto name = active.file.source.filename().wstring();
        label.Text(hstring(std::format(L"{} — {} / {} bytes",
            name, active.transferred_bytes, active.file.size)));
        progress.Value(percent);
        const auto accessible_percent = active.transferred_bytes != 0 && percent < 0.01
            ? std::format(L"{:.4f}", percent)
            : active.transferred_bytes != 0 && percent < 1.0
                ? std::format(L"{:.2f}", percent)
                : std::format(L"{:.0f}", percent);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
            progress, hstring(std::format(L"{}: {}% ({} of {} bytes)",
                name, accessible_percent, active.transferred_bytes, active.file.size)));
    }
}

void MainWindow::RefreshQueue(const bool force_visual_rebuild) {
    if (queue_drag_active_) {
        // The pending ListView is frozen during native drag, but active file
        // progress must continue to update independently.
        if (expanded_) RefreshActiveQueue();
        queue_refresh_force_rebuild_ = queue_refresh_force_rebuild_ || force_visual_rebuild;
        return;
    }

    // No queue enumeration or row creation while Details is collapsed.
    if (!expanded_) {
        queue_refresh_force_rebuild_ = queue_refresh_force_rebuild_ || force_visual_rebuild;
        RefreshQueueCommandState();
        return;
    }

    RefreshActiveQueue();

    // A container style only applies to containers created after it is set, so a style change
    // is handled exactly like a forced visual rebuild.
    const bool style_changed = ApplyQueueItemStyle(expanded_layout_mode_ == ExpandedLayoutMode::Narrow);
    const bool rebuild_visuals = std::exchange(queue_refresh_force_rebuild_, false) ||
        force_visual_rebuild || style_changed;
    auto items = QueueList().Items();

    auto append_item = [&](const std::filesystem::path& source, const std::optional<std::uint64_t> id) {
        items.Append(winrt::make<QueueItem>(source, id.value_or(0)));
    };

    if (!live_plan_) {
        QueuePreviousPageButton().IsEnabled(false);
        QueueNextPageButton().IsEnabled(false);
        queue_page_offset_ = 0;
        queue_rendered_page_offset_ = 0;
        queue_snapshot_.clear();
        items.Clear();

        // During initial planning there is not yet a LiveCopyPlan, but the
        // accepted top-level sources are already authoritative. Expose them as
        // a read-only preview so the queue disclosure remains useful instead of
        // appearing broken until enumeration finishes.
        if (execution_control_ && !planning_sources_.empty()) {
            constexpr std::size_t kPlanningPreviewLimit = 256;
            const auto visible = (std::min)(planning_sources_.size(), kPlanningPreviewLimit);
            for (std::size_t index = 0; index < visible; ++index) {
                append_item(planning_sources_[index], std::nullopt);
            }
            QueueCountText().Text(hstring(std::format(L"{}", planning_sources_.size())));
            RefreshQueueCommandState();
            RefreshQueueEditCommandState();
            return;
        }

        planning_sources_.clear();
        QueueCountText().Text(L"0");
        RefreshQueueCommandState();
        RefreshQueueEditCommandState();
        return;
    }

    planning_sources_.clear();

    constexpr std::size_t kVisibleQueueItems = 256;
    auto view = live_plan_->queue_view(kVisibleQueueItems, queue_page_offset_);
    if (view.pending_count == 0) queue_page_offset_ = 0;
    if (view.pending_files.empty() && view.pending_count != 0 && queue_page_offset_ != 0) {
        queue_page_offset_ = static_cast<std::size_t>(((view.pending_count - 1) / kVisibleQueueItems) * kVisibleQueueItems);
        view = live_plan_->queue_view(kVisibleQueueItems, queue_page_offset_);
    }
    QueuePreviousPageButton().IsEnabled(queue_page_offset_ != 0);
    QueueNextPageButton().IsEnabled(queue_page_offset_ + view.pending_files.size() < view.pending_count);
    // Reorder is scoped to the visible page in the core; hidden files stay in place.
    QueueList().CanReorderItems(true);
    const bool page_changed = queue_rendered_page_offset_ != queue_page_offset_;
    const bool unchanged = !page_changed && view.pending_files.size() == queue_snapshot_.size() &&
        std::equal(view.pending_files.begin(), view.pending_files.end(), queue_snapshot_.begin(),
            [](const velocitycopy::PlannedFile& left, const velocitycopy::PlannedFile& right) {
                return left.id == right.id && left.source == right.source && left.destination == right.destination && left.size == right.size;
            });
    if (unchanged && !rebuild_visuals) {
        QueueCountText().Text(hstring(queue_page_offset_ == 0 && view.pending_count <= kVisibleQueueItems
            ? std::format(L"{}", view.pending_count)
            : std::format(L"{}–{} / {}", queue_page_offset_ + 1,
                queue_page_offset_ + view.pending_files.size(), view.pending_count)));
        RefreshQueueCommandState();
        RefreshQueueEditCommandState();
        return;
    }

    const auto selected_ids = SelectedPendingIds();
    const auto queue_scroll_viewer = find_scroll_viewer(QueueList());
    const std::optional<double> previous_vertical_offset =
        queue_scroll_viewer ? std::optional<double>{page_changed ? 0.0 : queue_scroll_viewer.VerticalOffset()} : std::nullopt;
    std::optional<std::uint64_t> focused_id;
    const auto xaml_root = RootGrid().XamlRoot();
    const auto focused_element = xaml_root ? FocusManager::GetFocusedElement(xaml_root) : IInspectable{nullptr};
    if (auto focused = focused_element.try_as<FrameworkElement>()) {
        auto current = focused;
        while (current) {
            try {
                if (auto container = current.try_as<ListViewItem>()) {
                    if (auto item = container.Content().try_as<VelocityCopyUI::QueueItem>()) {
                        focused_id = item.Id();
                        break;
                    }
                }
            } catch (...) {
            }
            current = Media::VisualTreeHelper::GetParent(current).try_as<FrameworkElement>();
        }
    }

    const auto previous_snapshot = std::move(queue_snapshot_);
    queue_snapshot_ = std::move(view.pending_files);
    queue_rendered_page_offset_ = queue_page_offset_;

    std::size_t completed_prefix = 0;
    if (!previous_snapshot.empty() && items.Size() == previous_snapshot.size()) {
        while (completed_prefix < previous_snapshot.size() &&
               std::none_of(queue_snapshot_.begin(), queue_snapshot_.end(),
                   [&](const velocitycopy::PlannedFile& current) { return current.id == previous_snapshot[completed_prefix].id; })) {
            ++completed_prefix;
        }
    }

    const auto retained_count = previous_snapshot.size() - completed_prefix;
    const bool can_trim_prefix = !rebuild_visuals && completed_prefix > 0 && completed_prefix < previous_snapshot.size() &&
        queue_snapshot_.size() >= retained_count &&
        std::equal(previous_snapshot.begin() + completed_prefix, previous_snapshot.end(), queue_snapshot_.begin(),
            [](const velocitycopy::PlannedFile& left, const velocitycopy::PlannedFile& right) {
                return left.id == right.id && left.source == right.source &&
                       left.destination == right.destination && left.size == right.size;
            });

    if (can_trim_prefix) {
        for (std::size_t index = 0; index < completed_prefix; ++index) {
            items.RemoveAt(0);
        }
    } else {
        items.Clear();
    }

    const auto expected_retained_visuals = can_trim_prefix ? retained_count : 0;
    if (can_trim_prefix && items.Size() != expected_retained_visuals) {
        items.Clear();
    }
    const bool incremental_visual_state_valid = can_trim_prefix && items.Size() == expected_retained_visuals;
    const auto append_from_index = incremental_visual_state_valid ? retained_count : 0;

    for (std::size_t file_index = append_from_index; file_index < queue_snapshot_.size(); ++file_index) {
        const auto& file = queue_snapshot_[file_index];
        append_item(file.source, file.id);
    }

    const auto selected_ranges = QueueList().SelectedRanges();
    if (selected_ranges.Size() != 0 && items.Size() != 0) {
        QueueList().DeselectRange(Microsoft::UI::Xaml::Data::ItemIndexRange(
            0, static_cast<std::uint32_t>(items.Size())));
    }
    if (!selected_ids.empty()) {
        for (std::uint32_t index = 0; index < queue_snapshot_.size(); ++index) {
            if (std::find(selected_ids.begin(), selected_ids.end(), queue_snapshot_[index].id) != selected_ids.end()) {
                QueueList().SelectRange(Microsoft::UI::Xaml::Data::ItemIndexRange(index, 1));
            }
        }
    }

    if (focused_id) {
        for (std::uint32_t index = 0; index < queue_snapshot_.size(); ++index) {
            if (queue_snapshot_[index].id == *focused_id) {
                if (auto container = QueueList().ContainerFromIndex(index).try_as<Control>()) {
                    container.Focus(FocusState::Programmatic);
                }
                break;
            }
        }
    }

    if (previous_vertical_offset && queue_scroll_viewer) {
        QueueList().UpdateLayout();
        const double target_offset = (std::clamp)(
            *previous_vertical_offset, 0.0, queue_scroll_viewer.ScrollableHeight());
        (void)queue_scroll_viewer.ChangeView(
            nullptr,
            Windows::Foundation::IReference<double>{target_offset},
            nullptr,
            true);
    }

    QueueCountText().Text(hstring(queue_page_offset_ == 0 && view.pending_count <= kVisibleQueueItems
            ? std::format(L"{}", view.pending_count)
            : std::format(L"{}–{} / {}", queue_page_offset_ + 1,
                queue_page_offset_ + view.pending_files.size(), view.pending_count)));
    RefreshQueueCommandState();
    RefreshQueueEditCommandState();
}

std::vector<std::uint64_t> MainWindow::SelectedPendingIds() {
    std::vector<std::uint64_t> ids;
    // Selection follows the QueueItem identity, not a snapshot index. During
    // native ListView drag/reorder the visual indexes can change before the
    // authoritative pending snapshot is refreshed.
    for (const auto& selected : QueueList().SelectedItems()) {
        if (auto item = selected.try_as<VelocityCopyUI::QueueItem>()) {
            if (item.Id() != 0) ids.push_back(item.Id());
        }
    }
    return ids;
}

void MainWindow::RefreshQueueEditCommandState() {
    const bool has_selection = live_plan_ && !SelectedPendingIds().empty();
    QueueMoveUpButton().IsEnabled(has_selection);
    QueueMoveDownButton().IsEnabled(has_selection);
    QueueRemoveButton().IsEnabled(has_selection);
}

void MainWindow::OnQueueSelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) {
    RefreshQueueEditCommandState();
}

void MainWindow::OnQueuePreviousPageClick(IInspectable const&, RoutedEventArgs const&) {
    constexpr std::size_t kPageSize = 256;
    if (!live_plan_ || queue_page_offset_ == 0) return;
    queue_page_offset_ = queue_page_offset_ >= kPageSize ? queue_page_offset_ - kPageSize : 0;
    RefreshQueue(true);
}

void MainWindow::OnQueueNextPageClick(IInspectable const&, RoutedEventArgs const&) {
    constexpr std::size_t kPageSize = 256;
    if (!live_plan_) return;
    const auto count = live_plan_->queue_view(0).pending_count;
    if (count == 0) return;
    const auto last_page = static_cast<std::size_t>(((count - 1) / kPageSize) * kPageSize);
    if (queue_page_offset_ >= last_page) return;
    queue_page_offset_ = (std::min)(queue_page_offset_ + kPageSize, last_page);
    RefreshQueue(true);
}

void MainWindow::OnQueueKeyDown(IInspectable const&, KeyRoutedEventArgs const& args) {
    if (!live_plan_) return;
    if (args.Key() == Windows::System::VirtualKey::PageDown ||
        args.Key() == Windows::System::VirtualKey::PageUp) {
        constexpr std::size_t kPageSize = 256;
        const auto count = live_plan_->queue_view(0).pending_count;
        const auto last_page = count == 0 ? 0 : static_cast<std::size_t>(((count - 1) / kPageSize) * kPageSize);
        const auto next = args.Key() == Windows::System::VirtualKey::PageDown
            ? (std::min)(queue_page_offset_ + kPageSize, last_page)
            : queue_page_offset_ >= kPageSize ? queue_page_offset_ - kPageSize : 0;
        if (next != queue_page_offset_) {
            queue_page_offset_ = next;
            RefreshQueue(true);
        }
        args.Handled(true);
        return;
    }
    // Keep native ListView navigation intact: only Alt+Up/Down are
    // queue-edit shortcuts, while plain arrows belong to keyboard focus.
    const bool alt_pressed = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool queue_edit_key = args.Key() == Windows::System::VirtualKey::Delete ||
        (alt_pressed && (args.Key() == Windows::System::VirtualKey::Up ||
                         args.Key() == Windows::System::VirtualKey::Down));
    if (!queue_edit_key || SelectedPendingIds().empty()) return;

    switch (args.Key()) {
    case Windows::System::VirtualKey::Delete:
        OnQueueRemoveClick(nullptr, nullptr);
        args.Handled(true);
        break;
    case Windows::System::VirtualKey::Up:
        if (alt_pressed) {
            OnQueueMoveUpClick(nullptr, nullptr);
            args.Handled(true);
        }
        break;
    case Windows::System::VirtualKey::Down:
        if (alt_pressed) {
            OnQueueMoveDownClick(nullptr, nullptr);
            args.Handled(true);
        }
        break;
    default:
        break;
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

void MainWindow::OnQueuePointerWheelChanged(IInspectable const&, PointerRoutedEventArgs const& args) {
    // ListView consumes PointerWheelChanged, so the outer Narrow viewport never
    // receives the wheel while the pointer is over Queue. Preserve native Queue
    // scrolling until its internal viewer reaches the requested boundary, then
    // hand the same detent to the outer viewport.
    try {
        const auto outer = ExpandedViewport();
        if (!outer || outer.ScrollableHeight() <= 0.5) return;

        const auto properties = args.GetCurrentPoint(QueueList()).Properties();
        if (properties.IsHorizontalMouseWheel()) return;
        const int wheel_delta = properties.MouseWheelDelta();
        if (wheel_delta == 0) return;

        const auto inner = find_scroll_viewer(QueueList());
        if (inner && inner.ScrollableHeight() > 0.5) {
            const bool inner_can_scroll_up = inner.VerticalOffset() > 0.5;
            const bool inner_can_scroll_down =
                inner.VerticalOffset() < inner.ScrollableHeight() - 0.5;
            if ((wheel_delta > 0 && inner_can_scroll_up) ||
                (wheel_delta < 0 && inner_can_scroll_down)) {
                return;
            }
        }

        // Microsoft documents 120 as one wheel detent and 48 DIP as the
        // default three-line (3 x 16 DIP) vertical translation example.
        const double notch_step =
            velocitycopy::ui::token_double(L"QueueWheelForwardStep", 48);
        const double step = notch_step * (static_cast<double>(wheel_delta) / 120.0);
        const double target = (std::clamp)(
            outer.VerticalOffset() - step, 0.0, outer.ScrollableHeight());
        (void)outer.ChangeView(
            nullptr,
            Windows::Foundation::IReference<double>{target},
            nullptr,
            true);
        args.Handled(true);
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: OnQueuePointerWheelChanged failed\n");
    }
}

void MainWindow::OnQueueDragItemsStarting(
    IInspectable const&,
    DragItemsStartingEventArgs const&) {
    queue_drag_active_ = true;
}

void MainWindow::OnQueueDragItemsCompleted(
    ListViewBase const&,
    DragItemsCompletedEventArgs const&) {
    queue_drag_active_ = false;
    const bool force_visual_rebuild = std::exchange(queue_refresh_force_rebuild_, false);

    if (!live_plan_) {
        RefreshQueue(force_visual_rebuild);
        return;
    }
    const auto items = QueueList().Items();
    // Native drag must preserve the exact set of IDs displayed at drag start.
    // If a worker acquired a file while dragging, the core rejects the stale
    // page atomically instead of applying the new order to a shifted page.
    std::vector<std::uint64_t> ordered_ids;
    ordered_ids.reserve(items.Size());
    bool valid_drag_page = items.Size() == queue_snapshot_.size();
    for (std::uint32_t index = 0; valid_drag_page && index < items.Size(); ++index) {
        try {
            const auto item = items.GetAt(index).as<VelocityCopyUI::QueueItem>();
            if (item.Id() == 0) {
                valid_drag_page = false;
                break;
            }
            ordered_ids.push_back(item.Id());
        } catch (...) {
            valid_drag_page = false;
        }
    }

    // ListView may have reordered its visual Items collection even when the
    // core rejects a stale drag (an item was acquired or removed meanwhile).
    // Always reconcile from the authoritative pending plan after a drag.
    if (valid_drag_page) {
        (void)live_plan_->reorder_pending_page(queue_page_offset_, ordered_ids);
    }
    RefreshQueue(true);
}

} // namespace winrt::VelocityCopyUI::implementation
