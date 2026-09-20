#include "pch.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace Windows::Storage;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {

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
        app_window.SetIcon(L"Assets\\VelocityCopy.ico");
    } catch (...) {
    }

    ResizeWindow(72);
    InitializeTrayIntegration();
}

void MainWindow::OnTransferSurfaceSizeChanged(
    IInspectable const&,
    SizeChangedEventArgs const& args) {
    const auto previous_width = args.PreviousSize().Width;
    const auto fraction = previous_width > 0.0
        ? ProgressFill().Width() / previous_width
        : 0.0;
    ProgressFill().Width(args.NewSize().Width * (std::clamp)(fraction, 0.0, 1.0));
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

void MainWindow::ResizeWindowToContent() {
    RootGrid().UpdateLayout();
    const auto content_height = static_cast<int>(std::ceil(RootGrid().ActualHeight()));
    ResizeWindow((std::max)(72, content_height));
}

void MainWindow::SetProgressFraction(const double fraction) {
    const auto clamped = (std::clamp)(fraction, 0.0, 1.0);
    ProgressFill().Width(TransferSurface().ActualWidth() * clamped);
    ProgressPercentText().Text(hstring(std::format(L"{:.0f}%", clamped * 100.0)));
}

void MainWindow::OnDragEnter(IInspectable const&, DragEventArgs const& args) {
    const bool active_session = !active_destination_.empty() && (execution_control_ || live_plan_);
    const bool accepts_storage_items = args.DataView().Contains(StandardDataFormats::StorageItems());
    const bool source_allows_copy =
        (args.AllowedOperations() & DataPackageOperation::Copy) == DataPackageOperation::Copy;
    args.AcceptedOperation(
        active_session && accepts_storage_items && source_allows_copy
            ? DataPackageOperation::Copy
            : DataPackageOperation::None);
}

void MainWindow::OnDragOver(IInspectable const&, DragEventArgs const& args) {
    OnDragEnter(nullptr, args);
}

void MainWindow::OnDragLeave(IInspectable const&, DragEventArgs const&) {
}

void MainWindow::OnDrop(IInspectable const&, DragEventArgs const& args) {
    const bool active_session = !active_destination_.empty() && (execution_control_ || live_plan_);
    const bool accepts_storage_items = args.DataView().Contains(StandardDataFormats::StorageItems());
    const bool source_allows_copy =
        (args.AllowedOperations() & DataPackageOperation::Copy) == DataPackageOperation::Copy;
    if (!active_session || !accepts_storage_items || !source_allows_copy) {
        args.AcceptedOperation(DataPackageOperation::None);
        return;
    }

    args.AcceptedOperation(DataPackageOperation::Copy);
    HandleDropAsync(args);
}

fire_and_forget MainWindow::HandleDropAsync(DragEventArgs args) {
    auto lifetime = get_strong();
    auto deferral = args.GetDeferral();
    try {
        if (active_destination_.empty() || (!execution_control_ && !live_plan_)) {
            deferral.Complete();
            co_return;
        }

        auto storage_items = co_await args.DataView().GetStorageItemsAsync();
        std::vector<std::filesystem::path> sources;
        sources.reserve(storage_items.Size());
        for (auto const& item : storage_items) {
            const auto path = item.Path();
            if (!path.empty()) {
                sources.emplace_back(path.c_str());
            }
        }
        if (sources.empty()) {
            deferral.Complete();
            co_return;
        }

        velocitycopy::CopyJob job{};
        job.id = next_job_id_++;
        job.sources = std::move(sources);
        job.destination = active_destination_;
        job.operation = active_operation_;
        QueueOrStartCopy(std::move(job));
        deferral.Complete();
    } catch (...) {
        deferral.Complete();
        ShowError();
    }
}

} // namespace winrt::VelocityCopyUI::implementation
