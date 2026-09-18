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
namespace {

DataPackageOperation preferred_drop_operation(
    const DataPackageView& data_view,
    const Microsoft::UI::Input::DragDrop::DragDropModifiers modifiers) {
    const auto requested = data_view.RequestedOperation();
    const bool allows_copy = (requested & DataPackageOperation::Copy) == DataPackageOperation::Copy;
    const bool allows_move = (requested & DataPackageOperation::Move) == DataPackageOperation::Move;
    const bool control = (modifiers & Microsoft::UI::Input::DragDrop::DragDropModifiers::Control) ==
        Microsoft::UI::Input::DragDrop::DragDropModifiers::Control;
    const bool shift = (modifiers & Microsoft::UI::Input::DragDrop::DragDropModifiers::Shift) ==
        Microsoft::UI::Input::DragDrop::DragDropModifiers::Shift;

    // Windows documents Ctrl/Shift as user overrides for drag/drop operation.
    // Never synthesize an operation the source did not advertise.
    if (control && allows_copy) return DataPackageOperation::Copy;
    if (shift && allows_move) return DataPackageOperation::Move;
    if (requested == DataPackageOperation::Move) return DataPackageOperation::Move;
    return DataPackageOperation::Copy;
}

velocitycopy::FileOperation file_operation(const DataPackageOperation operation) {
    return operation == DataPackageOperation::Move
        ? velocitycopy::FileOperation::Move
        : velocitycopy::FileOperation::Copy;
}

} // namespace

MainWindow::MainWindow() {
    InitializeComponent();
    dispatcher_ = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    ConfigureQueuePersistenceMenu();

    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        const auto pause = loader.GetString(L"ActionPause");
        const auto skip = loader.GetString(L"ActionSkip");
        const auto stop = loader.GetString(L"ActionStop");
        const auto cancel = loader.GetString(L"ActionCancel");
        ToolTipService::SetToolTip(PauseButton(), box_value(pause));
        ToolTipService::SetToolTip(SkipButton(), box_value(skip));
        ToolTipService::SetToolTip(StopButton(), box_value(stop));
        ToolTipService::SetToolTip(CancelButton(), box_value(cancel));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PauseButton(), pause);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(SkipButton(), skip);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(StopButton(), stop);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(CancelButton(), cancel);

        const auto move_up = loader.GetString(L"ActionMoveUp");
        const auto move_down = loader.GetString(L"ActionMoveDown");
        const auto remove = loader.GetString(L"ActionRemove");
        ToolTipService::SetToolTip(QueueMoveUpButton(), box_value(move_up));
        ToolTipService::SetToolTip(QueueMoveDownButton(), box_value(move_down));
        ToolTipService::SetToolTip(QueueRemoveButton(), box_value(remove));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(QueueMoveUpButton(), move_up);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(QueueMoveDownButton(), move_down);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(QueueRemoveButton(), remove);
    } catch (...) {
    }

    try {
        SystemBackdrop(Microsoft::UI::Xaml::Media::MicaBackdrop{});
    } catch (...) {
    }

    ExtendsContentIntoTitleBar(true);
    SetTitleBar(TitleBarDragRegion());

    try {
        auto app_window = AppWindow();
        app_window.IsShownInSwitchers(false);
        if (auto presenter = app_window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
            presenter.IsMinimizable(true);
            presenter.IsMaximizable(false);
        }
    } catch (...) {
        // Keep the native WinUI defaults if the presenter cannot be adjusted.
    }

    ResizeWindow(72);
    InitializeTrayIntegration();
}

void MainWindow::OnTransferSurfaceSizeChanged(
    IInspectable const&,
    SizeChangedEventArgs const& args) {
    const auto fraction = (std::clamp)(GlobalProgress().Value() / 100.0, 0.0, 1.0);
    ProgressFill().Width(args.NewSize().Width * fraction);
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

void MainWindow::OnDragEnter(IInspectable const&, DragEventArgs const& args) {
    const bool accepts_storage_items = args.DataView().Contains(StandardDataFormats::StorageItems());
    const auto operation = accepts_storage_items ? preferred_drop_operation(args.DataView(), args.Modifiers()) : DataPackageOperation::None;
    args.AcceptedOperation(operation);
    if (accepts_storage_items) {
        args.DragUIOverride().IsCaptionVisible(true);
        args.DragUIOverride().IsGlyphVisible(true);
    }
    DragOverlay().Visibility(
        accepts_storage_items ? Visibility::Visible : Visibility::Collapsed);
}

void MainWindow::OnDragOver(IInspectable const&, DragEventArgs const& args) {
    const bool accepts_storage_items = args.DataView().Contains(StandardDataFormats::StorageItems());
    const auto operation = accepts_storage_items ? preferred_drop_operation(args.DataView(), args.Modifiers()) : DataPackageOperation::None;
    args.AcceptedOperation(operation);
    if (accepts_storage_items) {
        args.DragUIOverride().IsCaptionVisible(true);
        args.DragUIOverride().IsGlyphVisible(true);
    }
    DragOverlay().Visibility(
        accepts_storage_items ? Visibility::Visible : Visibility::Collapsed);
}

void MainWindow::OnDragLeave(IInspectable const&, DragEventArgs const&) {
    DragOverlay().Visibility(Visibility::Collapsed);
}

void MainWindow::OnDrop(IInspectable const&, DragEventArgs const& args) {
    DragOverlay().Visibility(Visibility::Collapsed);
    const auto operation = preferred_drop_operation(args.DataView(), args.Modifiers());
    args.AcceptedOperation(operation);
    HandleDropAsync(args.DataView(), file_operation(operation));
}

fire_and_forget MainWindow::HandleDropAsync(
    DataPackageView data_view,
    const velocitycopy::FileOperation operation) {
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

            std::optional<velocitycopy::DropItemKind> kind;
            if (item.IsOfType(StorageItemTypes::Folder)) {
                kind = velocitycopy::DropItemKind::Directory;
            } else if (item.IsOfType(StorageItemTypes::File)) {
                kind = velocitycopy::DropItemKind::File;
            }

            if (!kind) {
                continue;
            }

            items.push_back({std::filesystem::path(path.c_str()), *kind});
        }

        if (items.empty()) {
            ShowError();
            co_return;
        }

        ++shell_layout_generation_;
        pending_flow_operation_ = operation;
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
    ++destination_navigation_generation_;
    DestinationLoadingRing().IsActive(false);
    DestinationLoadingRing().Visibility(Visibility::Collapsed);
    DestinationItems().IsEnabled(true);
    DestinationBackButton().IsEnabled(true);
    ChooseCurrentFolderButton().IsEnabled(true);
    current_destination_folder_.clear();
    DestinationBrowserHeader().Visibility(Visibility::Collapsed);
    ChooseCurrentFolderButton().Visibility(Visibility::Collapsed);

    auto children = DestinationItems().Items();
    children.Clear();

    for (const auto& entry : destination_catalog_.enumerate()) {
        Button button;
        button.HorizontalAlignment(HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(HorizontalAlignment::Left);
        button.MinHeight(36);
        std::wstring display = entry.label;
        if (!entry.path.empty()) {
            display += L"  ";
            display += entry.path.wstring();
        }
        const auto accessible_name = hstring(display);
        button.Content(box_value(accessible_name));
        button.Tag(box_value(hstring(entry.path.wstring())));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(button, accessible_name);
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

    // Keep the current destination list visible while navigation runs, but prevent
    // stale targets from accepting input until the authoritative result arrives.
    DestinationItems().IsEnabled(false);
    DestinationBackButton().IsEnabled(false);
    ChooseCurrentFolderButton().IsEnabled(false);
    DestinationLoadingRing().Visibility(Visibility::Visible);
    DestinationLoadingRing().IsActive(true);

    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    destination_navigation_generation_ = destination_navigation_.navigate(
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
    if (result.generation != destination_navigation_generation_) {
        return;
    }
    DestinationLoadingRing().IsActive(false);
    DestinationLoadingRing().Visibility(Visibility::Collapsed);
    DestinationItems().IsEnabled(true);
    DestinationBackButton().IsEnabled(true);
    ChooseCurrentFolderButton().IsEnabled(result.available);
    if (!result.available) {
        current_destination_folder_.clear();
        DestinationBrowserHeader().Visibility(Visibility::Collapsed);
        ChooseCurrentFolderButton().Visibility(Visibility::Collapsed);
        DestinationItems().Items().Clear();
        ShowError();
        return;
    }
    ErrorBar().IsOpen(false);
    current_destination_folder_ = std::move(result.folder);
    DestinationBrowserHeader().Visibility(Visibility::Visible);
    ChooseCurrentFolderButton().Visibility(Visibility::Visible);
    DestinationPathText().Text(hstring(current_destination_folder_.wstring()));
    DestinationCapacityText().Text(FormatCapacity(result.capacity));

    auto children = DestinationItems().Items();
    children.Clear();
    for (const auto& entry : result.children) {
        Button button;
        button.HorizontalAlignment(HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(HorizontalAlignment::Left);
        button.MinHeight(36);
        const auto accessible_name = hstring(entry.name.wstring());
        button.Content(box_value(accessible_name));
        button.Tag(box_value(hstring(entry.path.wstring())));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(button, accessible_name);
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
