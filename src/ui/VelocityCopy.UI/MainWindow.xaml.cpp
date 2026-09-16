#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace Windows::Storage;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {

MainWindow::MainWindow() {
    InitializeComponent();
    dispatcher_ = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

    try {
        SystemBackdrop(Microsoft::UI::Xaml::Media::MicaBackdrop{});
    } catch (...) {
    }

    ExtendsContentIntoTitleBar(true);
    SetTitleBar(TitleBarDragRegion());
    ResizeWindow(156);
}

void MainWindow::ResizeWindow(const int height_epx) {
    try {
        HWND hwnd{};
        auto window_native = this->m_inner.as<::IWindowNative>();
        if (SUCCEEDED(window_native->get_WindowHandle(&hwnd)) && hwnd != nullptr) {
            const auto dpi = GetDpiForWindow(hwnd);
            RECT rect{};
            const bool have_rect = GetWindowRect(hwnd, &rect) != FALSE;
            const int initial_width = MulDiv(460, static_cast<int>(dpi), 96);
            const int current_width = have_rect ? rect.right - rect.left : initial_width;
            const int width = initial_size_applied_ ? current_width : initial_width;
            const int height = MulDiv(height_epx, static_cast<int>(dpi), 96);
            SetWindowPos(hwnd, nullptr, 0, 0, width, height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            initial_size_applied_ = true;
        }
    } catch (...) {
    }
}

void MainWindow::OnDragOver(IInspectable const&, DragEventArgs const& args) {
    if (args.DataView().Contains(StandardDataFormats::StorageItems())) {
        args.AcceptedOperation(DataPackageOperation::Copy);
        DragOverlay().Visibility(Visibility::Visible);
    }
}

void MainWindow::OnDragLeave(IInspectable const&, DragEventArgs const&) {
    DragOverlay().Visibility(Visibility::Collapsed);
}

void MainWindow::OnDrop(IInspectable const&, DragEventArgs const& args) {
    DragOverlay().Visibility(Visibility::Collapsed);
    HandleDropAsync(args.DataView());
}

fire_and_forget MainWindow::HandleDropAsync(DataPackageView data_view) {
    auto lifetime = get_strong();
    try {
        auto storage_items = co_await data_view.GetStorageItemsAsync();
        std::vector<velocitycopy::DropItem> items;
        items.reserve(storage_items.Size());

        for (auto const& item : storage_items) {
            const auto path = item.Path();
            if (path.empty()) {
                continue;
            }
            const auto kind = item.IsOfType(StorageItemTypes::Folder)
                ? velocitycopy::DropItemKind::Directory
                : velocitycopy::DropItemKind::File;
            items.push_back({std::filesystem::path(path.c_str()), kind});
        }

        if (items.empty()) {
            ShowError();
            co_return;
        }

        dropped_items_ = std::move(items);
        flow_.begin(dropped_items_);
        DestinationStep().Visibility(Visibility::Visible);
        LayoutStep().Visibility(Visibility::Collapsed);
        StartCopyButton().IsEnabled(false);
        PreserveToggle().IsChecked(false);
        DirectToggle().IsChecked(false);
        ErrorBar().IsOpen(false);
        LoadDestinations();
        DropFlowFlyout().ShowAt(RootGrid());
    } catch (...) {
        ShowError();
    }
}

void MainWindow::LoadDestinations() {
    destination_navigation_.cancel();
    current_destination_folder_.clear();
    DestinationBrowserHeader().Visibility(Visibility::Collapsed);
    ChooseCurrentFolderButton().Visibility(Visibility::Collapsed);

    auto children = DestinationItems().Children();
    children.Clear();

    for (const auto& entry : destination_catalog_.enumerate()) {
        Button button;
        button.HorizontalAlignment(HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(HorizontalAlignment::Left);
        std::wstring display = entry.label;
        if (!entry.path.empty()) {
            display += L"  ";
            display += entry.path.wstring();
        }
        button.Content(box_value(hstring(display)));
        button.Tag(box_value(hstring(entry.path.wstring())));
        button.Click({this, &MainWindow::OnDestinationClick});
        children.Append(button);
    }
}

void MainWindow::OnDestinationClick(IInspectable const& sender, RoutedEventArgs const&) {
    try {
        const auto button = sender.as<Button>();
        const auto value = unbox_value<hstring>(button.Tag());
        NavigateDestination(std::filesystem::path(value.c_str()));
    } catch (...) {
        ShowError();
    }
}

void MainWindow::OnDestinationFolderClick(IInspectable const& sender, RoutedEventArgs const&) {
    OnDestinationClick(sender, nullptr);
}

void MainWindow::NavigateDestination(std::filesystem::path folder) {
    if (folder.empty()) {
        return;
    }

    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    (void)destination_navigation_.navigate(
        std::move(folder),
        true,
        [weak, dispatcher](velocitycopy::DestinationNavigationResult result) mutable {
            (void)dispatcher.TryEnqueue([weak, result = std::move(result)]() mutable {
                if (auto self = weak.get()) {
                    self->ApplyDestinationNavigation(std::move(result));
                }
            });
        });
}

void MainWindow::ApplyDestinationNavigation(velocitycopy::DestinationNavigationResult result) {
    current_destination_folder_ = std::move(result.folder);
    DestinationBrowserHeader().Visibility(Visibility::Visible);
    ChooseCurrentFolderButton().Visibility(Visibility::Visible);
    DestinationPathText().Text(hstring(current_destination_folder_.wstring()));
    DestinationCapacityText().Text(FormatCapacity(result.capacity));

    auto children = DestinationItems().Children();
    children.Clear();
    for (const auto& entry : result.children) {
        Button button;
        button.HorizontalAlignment(HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(HorizontalAlignment::Left);
        button.Content(box_value(hstring(entry.name.wstring())));
        button.Tag(box_value(hstring(entry.path.wstring())));
        button.Click({this, &MainWindow::OnDestinationFolderClick});
        children.Append(button);
    }
}

void MainWindow::OnDestinationBackClick(IInspectable const&, RoutedEventArgs const&) {
    if (current_destination_folder_.empty()) {
        LoadDestinations();
        return;
    }

    const auto parent = current_destination_folder_.parent_path();
    if (parent.empty() || parent == current_destination_folder_) {
        LoadDestinations();
    } else {
        NavigateDestination(parent);
    }
}

void MainWindow::OnChooseCurrentFolderClick(IInspectable const&, RoutedEventArgs const&) {
    if (!current_destination_folder_.empty()) {
        SelectDestination(current_destination_folder_);
    }
}

void MainWindow::OnBrowseClick(IInspectable const&, RoutedEventArgs const&) {
    BrowseAsync();
}

fire_and_forget MainWindow::BrowseAsync() {
    auto lifetime = get_strong();
    try {
        Microsoft::Windows::Storage::Pickers::FolderPicker picker(AppWindow().Id());
        auto result = co_await picker.PickSingleFolderAsync();
        if (result) {
            SelectDestination(std::filesystem::path(result.Path().c_str()));
        }
    } catch (...) {
        ShowError();
    }
}

void MainWindow::SelectDestination(std::filesystem::path destination) {
    const auto validation = flow_.choose_destination(destination);
    if (validation != velocitycopy::DestinationValidation::Valid || !flow_.menu()) {
        ShowError();
        return;
    }

    ErrorBar().IsOpen(false);
    SelectedDestinationText().Text(hstring(destination.wstring()));
    PreservePreview().Text(PreviewText(flow_.menu()->preserve));
    DirectPreview().Text(PreviewText(flow_.menu()->direct));
    DestinationStep().Visibility(Visibility::Collapsed);
    LayoutStep().Visibility(Visibility::Visible);
}

void MainWindow::OnPreserveClick(IInspectable const&, RoutedEventArgs const&) {
    PreserveToggle().IsChecked(true);
    DirectToggle().IsChecked(false);
    StartCopyButton().IsEnabled(flow_.choose_layout(velocitycopy::DestinationLayout::PreserveSourceFolder));
}

void MainWindow::OnDirectClick(IInspectable const&, RoutedEventArgs const&) {
    PreserveToggle().IsChecked(false);
    DirectToggle().IsChecked(true);
    StartCopyButton().IsEnabled(flow_.choose_layout(velocitycopy::DestinationLayout::ContentsOnly));
}

void MainWindow::OnBackClick(IInspectable const&, RoutedEventArgs const&) {
    if (!flow_.back()) {
        return;
    }
    if (flow_.stage() == velocitycopy::DropFlowStage::Destination) {
        LayoutStep().Visibility(Visibility::Collapsed);
        DestinationStep().Visibility(Visibility::Visible);
        StartCopyButton().IsEnabled(false);
        PreserveToggle().IsChecked(false);
        DirectToggle().IsChecked(false);
    }
}

void MainWindow::SetExecutionButtonsRunning() {
    PauseButton().IsEnabled(true);
    StopButton().IsEnabled(true);
    CancelButton().IsEnabled(true);
}

void MainWindow::SetExecutionButtonsIdle() {
    PauseButton().IsEnabled(false);
    StopButton().IsEnabled(false);
    CancelButton().IsEnabled(false);
    paused_ = false;

    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        PauseButton().Content(box_value(loader.GetString(L"ActionPause")));
    } catch (...) {
    }
}

void MainWindow::StartCopy(velocitycopy::CopyJob job) {
    active_destination_ = job.destination;
    cancel_requested_.store(false, std::memory_order_relaxed);
    presenter_.reset();
    last_queue_completed_files_ = 0;
    live_plan_.reset();
    execution_control_ = std::make_shared<velocitycopy::ExecutionControl>();
    queue_snapshot_.clear();
    QueueList().Items().Clear();
    QueueButton().IsEnabled(false);
    QueuePanel().Visibility(Visibility::Collapsed);
    ResizeWindow(156);
    GlobalProgress().Value(0);
    paused_ = false;
    SetExecutionButtonsRunning();
    CurrentItemText().Text(job.display_name.empty() ? hstring(L"…") : hstring(job.display_name));

    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    auto control = execution_control_;

    copy_thread_ = std::jthread([this, weak, dispatcher, control, job = std::move(job)](std::stop_token stop_token) mutable {
        std::shared_ptr<velocitycopy::LiveCopyPlan> plan;
        try {
            plan = std::make_shared<velocitycopy::LiveCopyPlan>(planner_.build(job));
        } catch (...) {
            (void)dispatcher.TryEnqueue([weak]() {
                if (auto self = weak.get()) {
                    self->FinishCopy({false, false, static_cast<std::int32_t>(E_FAIL), false});
                }
            });
            return;
        }

        (void)dispatcher.TryEnqueue([weak, plan]() {
            if (auto self = weak.get()) {
                self->PublishLivePlan(plan);
            }
        });

        const auto result = executor_.execute(*plan, *control, [this, weak, dispatcher, control, &stop_token](const velocitycopy::JobProgress& progress) {
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

        (void)dispatcher.TryEnqueue([weak, result]() {
            if (auto self = weak.get()) {
                self->FinishCopy(result);
            }
        });
    });
}

void MainWindow::PublishLivePlan(std::shared_ptr<velocitycopy::LiveCopyPlan> plan) {
    live_plan_ = std::move(plan);
    QueueButton().IsEnabled(true);
    RefreshQueue();

    auto deferred = std::move(deferred_same_destination_jobs_);
    deferred_same_destination_jobs_.clear();
    for (auto& job : deferred) {
        QueueOrStartCopy(std::move(job));
    }
}

void MainWindow::RefreshQueue() {
    if (!live_plan_) {
        return;
    }

    const auto snapshot = live_plan_->snapshot();
    queue_snapshot_ = snapshot.pending_files;

    auto items = QueueList().Items();
    items.Clear();
    for (const auto& file : queue_snapshot_) {
        items.Append(box_value(hstring(file.source.wstring())));
    }

    QueueCountText().Text(hstring(std::format(L"{}", queue_snapshot_.size())));
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

void MainWindow::OnQueueClick(IInspectable const&, RoutedEventArgs const&) {
    const bool expanding = QueuePanel().Visibility() != Visibility::Visible;
    QueuePanel().Visibility(expanding ? Visibility::Visible : Visibility::Collapsed);
    if (expanding) {
        RefreshQueue();
        ResizeWindow(380);
    } else {
        ResizeWindow(156);
    }
}

void MainWindow::OnQueueMoveUpClick(IInspectable const&, RoutedEventArgs const&) {
    if (!live_plan_) {
        return;
    }
    const auto ids = SelectedPendingIds();
    for (const auto id : ids) {
        (void)live_plan_->move_pending_file_up(id);
    }
    RefreshQueue();
}

void MainWindow::OnQueueMoveDownClick(IInspectable const&, RoutedEventArgs const&) {
    if (!live_plan_) {
        return;
    }
    auto ids = SelectedPendingIds();
    for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
        (void)live_plan_->move_pending_file_down(*it);
    }
    RefreshQueue();
}

void MainWindow::OnQueueRemoveClick(IInspectable const&, RoutedEventArgs const&) {
    if (!live_plan_) {
        return;
    }
    const auto ids = SelectedPendingIds();
    for (const auto id : ids) {
        (void)live_plan_->remove_pending_file(id);
    }
    RefreshQueue();
}

void MainWindow::OnQueueDragItemsCompleted(
    ListViewBase const&,
    DragItemsCompletedEventArgs const&) {
    if (!live_plan_ || queue_snapshot_.empty()) {
        return;
    }

    const auto items = QueueList().Items();
    std::vector<bool> used(queue_snapshot_.size(), false);
    std::vector<std::uint64_t> ordered_ids;
    ordered_ids.reserve(items.Size());

    for (std::uint32_t visual_index = 0; visual_index < items.Size(); ++visual_index) {
        hstring source;
        try {
            source = unbox_value<hstring>(items.GetAt(visual_index));
        } catch (...) {
            continue;
        }

        for (std::size_t index = 0; index < queue_snapshot_.size(); ++index) {
            if (used[index]) {
                continue;
            }
            if (source == hstring(queue_snapshot_[index].source.wstring())) {
                used[index] = true;
                ordered_ids.push_back(queue_snapshot_[index].id);
                break;
            }
        }
    }

    for (std::size_t index = 0; index < ordered_ids.size(); ++index) {
        (void)live_plan_->move_pending_file(ordered_ids[index], index);
    }
    RefreshQueue();
}

void MainWindow::OnPauseClick(IInspectable const&, RoutedEventArgs const&) {
    if (!execution_control_) {
        return;
    }

    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        if (paused_) {
            execution_control_->resume();
            paused_ = false;
            PauseButton().Content(box_value(loader.GetString(L"ActionPause")));
        } else {
            execution_control_->request_pause();
            paused_ = true;
            PauseButton().Content(box_value(loader.GetString(L"ActionResume")));
            SpeedText().Text(L"—");
            EtaText().Text(L"—");
        }
    } catch (...) {
        if (paused_) {
            execution_control_->resume();
            paused_ = false;
        } else {
            execution_control_->request_pause();
            paused_ = true;
        }
    }
}

void MainWindow::OnStopClick(IInspectable const&, RoutedEventArgs const&) {
    if (!execution_control_) {
        return;
    }
    execution_control_->request_stop();
    PauseButton().IsEnabled(false);
    StopButton().IsEnabled(false);
}

void MainWindow::OnCancelClick(IInspectable const&, RoutedEventArgs const&) {
    cancel_requested_.store(true, std::memory_order_relaxed);
    if (execution_control_) {
        execution_control_->request_cancel();
    }
}

void MainWindow::ApplySnapshot(const velocitycopy::UiSnapshot& snapshot) {
    GlobalProgress().Value(snapshot.fraction * 100.0);
    if (!snapshot.current_source.empty()) {
        CurrentItemText().Text(hstring(snapshot.current_source.filename().wstring()));
    }
    SpeedText().Text(FormatSpeed(snapshot.bytes_per_second));
    EtaText().Text(FormatEta(snapshot.eta_seconds));

    if (QueuePanel().Visibility() == Visibility::Visible &&
        snapshot.completed_files != last_queue_completed_files_) {
        last_queue_completed_files_ = snapshot.completed_files;
        RefreshQueue();
    }
}

void MainWindow::FinishCopy(const velocitycopy::JobResult& result) {
    auto deferred = std::move(deferred_same_destination_jobs_);
    deferred_same_destination_jobs_.clear();

    SetExecutionButtonsIdle();
    RefreshQueue();
    execution_control_.reset();
    active_destination_.clear();

    if (result.stopped) {
        QueueButton().IsEnabled(live_plan_ && !live_plan_->snapshot().pending_files.empty());
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
        return;
    }

    QueueButton().IsEnabled(false);
    if (result.success) {
        GlobalProgress().Value(100);
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
    } else if (!result.cancelled) {
        ShowError();
    }

    // A planning failure can happen before PublishLivePlan had a chance to
    // drain same-destination requests. Preserve those user requests unless the
    // user explicitly cancelled/stopped the session.
    if (!result.cancelled) {
        for (auto& job : deferred) {
            QueueOrStartCopy(std::move(job));
        }
    }
}

void MainWindow::ShowError() {
    ErrorBar().IsOpen(true);
}

hstring MainWindow::PreviewText(const velocitycopy::DropChoicePreview& preview) {
    if (preview.destinations.empty()) {
        return {};
    }
    std::wstring text = preview.destinations.front().wstring();
    if (preview.hidden_items != 0) {
        text += L"  +";
        text += std::to_wstring(preview.hidden_items);
    }
    return hstring(text);
}

hstring MainWindow::FormatCapacity(const velocitycopy::DestinationCapacity& capacity) {
    if (!capacity.available || capacity.total_bytes == 0) {
        return {};
    }
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    return hstring(std::format(
        L"{:.1f} / {:.1f} GiB",
        static_cast<double>(capacity.free_bytes) / gib,
        static_cast<double>(capacity.total_bytes) / gib));
}

hstring MainWindow::FormatSpeed(double bytes_per_second) {
    if (bytes_per_second <= 0.0) {
        return hstring(L"—");
    }
    const double mib = bytes_per_second / (1024.0 * 1024.0);
    return hstring(std::format(L"{:.1f} MiB/s", mib));
}

hstring MainWindow::FormatEta(double seconds) {
    if (seconds <= 0.0 || !std::isfinite(seconds)) {
        return hstring(L"—");
    }
    const auto rounded = static_cast<std::uint64_t>(seconds + 0.5);
    const auto minutes = rounded / 60;
    const auto remaining = rounded % 60;
    if (minutes == 0) {
        return hstring(std::format(L"{} s", remaining));
    }
    return hstring(std::format(L"{} m {} s", minutes, remaining));
}

} // namespace winrt::VelocityCopyUI::implementation
