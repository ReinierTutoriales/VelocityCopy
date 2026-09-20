#include "pch.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::RefreshQueue() {
    auto items = QueueList().Items();

    auto append_visual = [&](const std::filesystem::path& source, const std::optional<std::uint64_t> id) {
        StackPanel row;
        row.Spacing(2);
        row.Margin(Thickness{8, 4, 8, 4});
        row.HorizontalAlignment(HorizontalAlignment::Stretch);
        if (id) {
            row.Tag(box_value(*id));
        }

        TextBlock name;
        name.Text(hstring(source.filename().wstring()));
        name.TextTrimming(TextTrimming::CharacterEllipsis);
        name.MaxLines(1);
        name.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        name.FontSize(12);

        TextBlock location;
        location.Text(hstring(source.parent_path().wstring()));
        location.TextTrimming(TextTrimming::CharacterEllipsis);
        location.MaxLines(1);
        location.Opacity(0.56);
        location.FontSize(10.5);

        row.Children().Append(name);
        row.Children().Append(location);

        std::wstring accessible_name = source.filename().wstring();
        const auto parent = source.parent_path().wstring();
        if (!parent.empty()) {
            accessible_name += L", ";
            accessible_name += parent;
        }
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(row, hstring(accessible_name));
        items.Append(row);
    };

    if (!live_plan_) {
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
                append_visual(planning_sources_[index], std::nullopt);
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
    auto view = live_plan_->queue_view(kVisibleQueueItems);
    const bool unchanged = view.pending_files.size() == queue_snapshot_.size() &&
        std::equal(view.pending_files.begin(), view.pending_files.end(), queue_snapshot_.begin(),
            [](const velocitycopy::PlannedFile& left, const velocitycopy::PlannedFile& right) {
                return left.id == right.id && left.source == right.source && left.destination == right.destination && left.size == right.size;
            });
    if (unchanged) {
        QueueCountText().Text(hstring(std::format(L"{}", view.pending_count)));
        RefreshQueueCommandState();
        RefreshQueueEditCommandState();
        return;
    }

    const auto selected_ids = SelectedPendingIds();
    std::optional<std::uint64_t> focused_id;
    if (auto focused = FocusManager::GetFocusedElement().try_as<FrameworkElement>()) {
        auto current = focused;
        while (current) {
            try {
                if (current.Tag()) {
                    focused_id = unbox_value<std::uint64_t>(current.Tag());
                    break;
                }
            } catch (...) {
            }
            current = Media::VisualTreeHelper::GetParent(current).try_as<FrameworkElement>();
        }
    }

    const auto previous_snapshot = std::move(queue_snapshot_);
    queue_snapshot_ = std::move(view.pending_files);

    std::size_t completed_prefix = 0;
    if (!previous_snapshot.empty() && items.Size() == previous_snapshot.size()) {
        while (completed_prefix < previous_snapshot.size() &&
               std::none_of(queue_snapshot_.begin(), queue_snapshot_.end(),
                   [&](const velocitycopy::PlannedFile& current) { return current.id == previous_snapshot[completed_prefix].id; })) {
            ++completed_prefix;
        }
    }

    const auto retained_count = previous_snapshot.size() - completed_prefix;
    const bool can_trim_prefix = completed_prefix > 0 && completed_prefix < previous_snapshot.size() &&
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
        append_visual(file.source, file.id);
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
    const bool has_selection = live_plan_ && !SelectedPendingIds().empty();
    QueueMoveUpButton().IsEnabled(has_selection);
    QueueMoveDownButton().IsEnabled(has_selection);
    QueueRemoveButton().IsEnabled(has_selection);
}

void MainWindow::OnQueueSelectionChanged(IInspectable const&, SelectionChangedEventArgs const&) {
    RefreshQueueEditCommandState();
}

void MainWindow::OnQueueKeyDown(IInspectable const&, KeyRoutedEventArgs const& args) {
    if (!live_plan_ || SelectedPendingIds().empty()) return;

    switch (args.Key()) {
    case Windows::System::VirtualKey::Delete:
        OnQueueRemoveClick(nullptr, nullptr);
        args.Handled(true);
        break;
    case Windows::System::VirtualKey::Up:
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
            OnQueueMoveUpClick(nullptr, nullptr);
            args.Handled(true);
        }
        break;
    case Windows::System::VirtualKey::Down:
        if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
            OnQueueMoveDownClick(nullptr, nullptr);
            args.Handled(true);
        }
        break;
    default:
        break;
    }
}

void MainWindow::OnQueueClick(IInspectable const&, RoutedEventArgs const&) {
    const bool expanding = QueuePanel().Visibility() != Visibility::Visible;
    QueuePanel().Visibility(expanding ? Visibility::Visible : Visibility::Collapsed);
    QueueChevron().Glyph(expanding ? L"\xE70E" : L"\xE70D");

    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        const auto label = loader.GetString(expanding ? L"ActionHideQueue" : L"ActionShowQueue");
        ToolTipService::SetToolTip(QueueButton(), box_value(label));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(QueueButton(), label);
    } catch (...) {
    }

    if (expanding) {
        RefreshQueue();
        ResizeWindowToContent();
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
        }
    }

    (void)live_plan_->reorder_pending_files(ordered_ids);
    RefreshQueue();
}

} // namespace winrt::VelocityCopyUI::implementation
