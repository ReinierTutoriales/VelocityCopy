#include "pch.h"
#include "MainWindow.xaml.h"
#include "App.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <limits>

using namespace winrt;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace Windows::Storage;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {
namespace {

constexpr int kCompactWindowWidthEpx = 380;

bool accepts_active_transfer_drop(
    const std::filesystem::path& active_destination,
    const std::shared_ptr<velocitycopy::ExecutionControl>& execution_control,
    const std::shared_ptr<velocitycopy::LiveCopyPlan>& live_plan,
    DragEventArgs const& args) {
    if (active_destination.empty() || (!execution_control && !live_plan)) {
        return false;
    }
    if (!args.DataView().Contains(StandardDataFormats::StorageItems())) {
        return false;
    }
    return (args.AllowedOperations() & DataPackageOperation::Copy) == DataPackageOperation::Copy;
}

} // namespace

MainWindow::MainWindow() {
    if (auto* app = App::Instance()) window_id_ = app->NextWindowId();
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
    base_caption_content_padding_ = CaptionContentGrid().Padding();

    try {
        auto app_window = AppWindow();
        app_window.IsShownInSwitchers(false);
        if (auto presenter = app_window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
            presenter.IsMinimizable(true);
            presenter.IsMaximizable(false);
            presenter.IsResizable(false);
        }
        auto weak = get_weak();
        app_window.Changed([weak](
            Microsoft::UI::Windowing::AppWindow const& sender,
            Microsoft::UI::Windowing::AppWindowChangedEventArgs const& args) {
            if (auto self = weak.get()) {
                self->OnAppWindowChanged(sender, args);
            }
        });
        app_window.SetIcon(L"Assets\\VelocityCopy.ico");
    } catch (...) {
    }

    InitializeTrayIntegration();
    ApplyTitleBarInset();
    ResizeWindow(72);
}

void MainWindow::MoveNativeWindow(const int x, const int y) noexcept {
    try {
        AppWindow().Move(Windows::Graphics::PointInt32{x, y});
    } catch (...) {
    }
}

void MainWindow::ApplyTitleBarInset() noexcept {
    try {
        HWND hwnd = hwnd_;
        if (hwnd == nullptr) {
            auto window_native = this->m_inner.as<::IWindowNative>();
            if (FAILED(window_native->get_WindowHandle(&hwnd)) || hwnd == nullptr) {
                return;
            }
        }

        const auto dpi = GetDpiForWindow(hwnd);
        if (dpi == 0) return;

        const double right_inset_epx =
            AppWindow().TitleBar().RightInset() * 96.0 / static_cast<double>(dpi);
        CaptionContentGrid().Padding(Thickness{
            base_caption_content_padding_.Left,
            base_caption_content_padding_.Top,
            base_caption_content_padding_.Right + right_inset_epx,
            base_caption_content_padding_.Bottom});
    } catch (...) {
    }
}

void MainWindow::OnAppWindowChanged(
    Microsoft::UI::Windowing::AppWindow const&,
    Microsoft::UI::Windowing::AppWindowChangedEventArgs const&) {
    ApplyTitleBarInset();
}

void MainWindow::OnTransferSurfaceSizeChanged(
    IInspectable const&,
    SizeChangedEventArgs const& args) {
    ProgressFill().Width(args.NewSize().Width * progress_fraction_);
}

void MainWindow::ResizeWindow(const int height_epx) {
    try {
        HWND hwnd{};
        auto window_native = this->m_inner.as<::IWindowNative>();
        if (SUCCEEDED(window_native->get_WindowHandle(&hwnd)) && hwnd != nullptr) {
            const auto dpi = GetDpiForWindow(hwnd);
            if (dpi == 0) return;
            const int width = MulDiv(kCompactWindowWidthEpx, static_cast<int>(dpi), 96);
            const int height = MulDiv(height_epx, static_cast<int>(dpi), 96);
            SetWindowPos(hwnd, nullptr, 0, 0, width, height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            ApplyTitleBarInset();
        }
    } catch (...) {
    }
}

void MainWindow::ResizeWindowToContent() {
    if (QueuePanel().Visibility() != Visibility::Visible) {
        ResizeWindow(72);
        return;
    }

    const auto measured_width = RootGrid().ActualWidth() > 0.0
        ? static_cast<float>(RootGrid().ActualWidth())
        : static_cast<float>(kCompactWindowWidthEpx);
    QueuePanel().Measure({measured_width, std::numeric_limits<float>::infinity()});
    const auto desired_queue_height = static_cast<double>(QueuePanel().DesiredSize().Height);
    const auto expanded_height = static_cast<int>(std::ceil(72.0 + desired_queue_height));
    ResizeWindow((std::clamp)(expanded_height, 176, 340));
    RootGrid().UpdateLayout();
}

void MainWindow::SetProgressFraction(const double fraction) {
    progress_fraction_ = (std::clamp)(fraction, 0.0, 1.0);
    ProgressFill().Width(TransferSurface().ActualWidth() * progress_fraction_);

    const double percent = progress_fraction_ * 100.0;
    if (progress_fraction_ > 0.0 && percent < 0.1) {
        ProgressPercentText().Text(L"<0.1%");
    } else if (percent > 0.0 && percent < 10.0) {
        ProgressPercentText().Text(hstring(std::format(L"{:.1f}%", percent)));
    } else {
        ProgressPercentText().Text(hstring(std::format(L"{:.0f}%", percent)));
    }
}

void MainWindow::OnDragEnter(IInspectable const&, DragEventArgs const& args) {
    args.AcceptedOperation(
        accepts_active_transfer_drop(active_destination_, execution_control_, live_plan_, args)
            ? DataPackageOperation::Copy
            : DataPackageOperation::None);
}

void MainWindow::OnDragOver(IInspectable const&, DragEventArgs const& args) {
    args.AcceptedOperation(
        accepts_active_transfer_drop(active_destination_, execution_control_, live_plan_, args)
            ? DataPackageOperation::Copy
            : DataPackageOperation::None);
}

void MainWindow::OnDragLeave(IInspectable const&, DragEventArgs const&) {
}

void MainWindow::OnDrop(IInspectable const&, DragEventArgs const& args) {
    if (!accepts_active_transfer_drop(active_destination_, execution_control_, live_plan_, args)) {
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
        AppendTransfer(std::move(job));
        deferral.Complete();
    } catch (...) {
        deferral.Complete();
        ShowError();
    }
}

void MainWindow::ResetTransferSurface() {
    ErrorBar().IsOpen(false);
    ErrorBar().Message(L"");
}

void MainWindow::ShowError(hstring const& message) {
    ErrorBar().Message(message);
    ErrorBar().IsOpen(true);
}

hstring MainWindow::FormatFailureReason(const std::int32_t native_code) {
    if (native_code == 0) {
        return {};
    }

    DWORD message_code = static_cast<DWORD>(native_code);
    const HRESULT hr = static_cast<HRESULT>(native_code);
    if (HRESULT_FACILITY(hr) == FACILITY_WIN32) {
        message_code = HRESULT_CODE(hr);
    }

    wchar_t* message_buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        message_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&message_buffer),
        0,
        nullptr);

    if (length != 0 && message_buffer != nullptr) {
        std::wstring message(message_buffer, length);
        LocalFree(message_buffer);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ' || message.back() == L'\t')) {
            message.pop_back();
        }
        if (!message.empty()) {
            return hstring(message);
        }
    } else if (message_buffer != nullptr) {
        LocalFree(message_buffer);
    }

    return hstring(std::format(L"0x{:08X}", static_cast<std::uint32_t>(native_code)));
}

hstring MainWindow::FormatSpeed(const double bytes_per_second) {
    if (bytes_per_second <= 0.0 || !std::isfinite(bytes_per_second)) {
        return hstring(L"—");
    }

    constexpr double kib = 1024.0;
    constexpr double mib = 1024.0 * 1024.0;
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    if (bytes_per_second >= gib) {
        return hstring(std::format(L"{:.2f} GiB/s", bytes_per_second / gib));
    }
    if (bytes_per_second >= mib) {
        return hstring(std::format(L"{:.1f} MiB/s", bytes_per_second / mib));
    }
    return hstring(std::format(L"{:.0f} KiB/s", bytes_per_second / kib));
}

hstring MainWindow::FormatEta(const double seconds) {
    if (seconds <= 0.0 || !std::isfinite(seconds)) {
        return hstring(L"—");
    }
    const auto rounded = static_cast<std::uint64_t>(seconds + 0.5);
    const auto minutes = rounded / 60;
    const auto remaining = rounded % 60;
    if (minutes >= 60) {
        return hstring(std::format(L"{} h {:02} m", minutes / 60, minutes % 60));
    }
    if (minutes == 0) {
        return hstring(std::format(L"{} s", remaining));
    }
    return hstring(std::format(L"{} m {} s", minutes, remaining));
}

} // namespace winrt::VelocityCopyUI::implementation
