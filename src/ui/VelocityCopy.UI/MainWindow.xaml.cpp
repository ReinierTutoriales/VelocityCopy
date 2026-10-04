#include "pch.h"
#include "MainWindow.xaml.h"
#include "App.xaml.h"
#include "UiTokens.h"
#include "Localization.h"
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
    // External Explorer drops must reach the window even when a child control
    // (notably ListView during its own reorder gesture) class-handles the
    // routed drag event. QueueList keeps AllowDrop/CanReorderItems for its
    // independent internal reorder path.
    RootGrid().AddHandler(
        UIElement::DragEnterEvent(),
        box_value(DragEventHandler{this, &MainWindow::OnDragEnter}),
        true);
    RootGrid().AddHandler(
        UIElement::DragOverEvent(),
        box_value(DragEventHandler{this, &MainWindow::OnDragOver}),
        true);
    RootGrid().AddHandler(
        UIElement::DropEvent(),
        box_value(DragEventHandler{this, &MainWindow::OnDrop}),
        true);
    dispatcher_ = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    ErrorBar().Closed([weak = get_weak()](InfoBar const&, InfoBarClosedEventArgs const&) {
        if (auto self = weak.get()) self->ResizeWindowToContent();
    });
    PerformanceGraph().SizeChanged([weak = get_weak()](IInspectable const&, SizeChangedEventArgs const&) {
        if (auto self = weak.get()) self->UpdatePerformanceGraph();
    });
    ConfigureQueuePersistenceMenu();
    try {
        const auto pause = velocitycopy::localization::get_string(L"ActionPause");
        const auto cancel = velocitycopy::localization::get_string(L"ActionCancel");
        ToolTipService::SetToolTip(PauseButtonHost(), box_value(pause));
        ToolTipService::SetToolTip(CancelButtonHost(), box_value(cancel));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(PauseButton(), pause);
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(CancelButton(), cancel);

        const auto show_details = velocitycopy::localization::get_string(L"ActionShowDetails");
        ToolTipService::SetToolTip(DetailsButton(), box_value(show_details));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(DetailsButton(), show_details);

        const auto move_up = velocitycopy::localization::get_string(L"ActionMoveUp");
        const auto move_down = velocitycopy::localization::get_string(L"ActionMoveDown");
        const auto remove = velocitycopy::localization::get_string(L"ActionRemove");
        ToolTipService::SetToolTip(QueueMoveUpButtonHost(), box_value(move_up));
        ToolTipService::SetToolTip(QueueMoveDownButtonHost(), box_value(move_down));
        ToolTipService::SetToolTip(QueueRemoveButtonHost(), box_value(remove));
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

    // Derive the queue viewport from the single expanded-height ceiling.
    const auto queue_padding = velocitycopy::ui::token_thickness(L"QueuePanelPadding", Thickness{8, 8, 8, 12});
    const auto list_margin = velocitycopy::ui::token_thickness(L"QueueListMargin", Thickness{0, 4, 0, 0});
    const auto commands_margin = velocitycopy::ui::token_thickness(L"QueueCommandsMargin", Thickness{0, 8, 0, 0});
    const auto queue_ceiling = velocitycopy::ui::token_double(L"QueueExpandedMaxHeight", 340);
    const auto transfer_padding = velocitycopy::ui::token_thickness(L"TransferContentPadding", Thickness{8, 0, 8, 8});
    const auto caption_height = velocitycopy::ui::token_double(L"CaptionRowHeight", 32);
    const auto action_height = velocitycopy::ui::token_double(L"ActionButtonSize", 32);
    const auto normal_surface_fallback =
        caption_height + action_height + transfer_padding.Top + transfer_padding.Bottom;
    const auto header_height = velocitycopy::ui::token_double(L"QueueHeaderMinHeight", 28);
    const auto command_height = velocitycopy::ui::token_double(L"ActionButtonSize", 32);
    QueueList().MaxHeight((std::max)(0.0, queue_ceiling - normal_surface_fallback -
        queue_padding.Top - queue_padding.Bottom - header_height - list_margin.Top - list_margin.Bottom -
        commands_margin.Top - commands_margin.Bottom - command_height));

    try {
        auto app_window = AppWindow();
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

    RootGrid().Loaded([weak = get_weak()](IInspectable const&, RoutedEventArgs const&) {
        if (auto self = weak.get()) {
            try {
                const auto root = self->RootGrid().XamlRoot();
                if (root) {
                    self->last_rasterization_scale_ = root.RasterizationScale();
                    self->xaml_root_changed_revoker_ = root.Changed(auto_revoke,
                        [weak](Microsoft::UI::Xaml::XamlRoot const& changed_root, IInspectable const&) {
                            if (auto window = weak.get()) {
                                const double scale = changed_root.RasterizationScale();
                                if (!window->resize_in_progress_ &&
                                    scale > 0.0 &&
                                    std::abs(scale - window->last_rasterization_scale_) > 0.0001) {
                                    window->last_rasterization_scale_ = scale;
                                    window->ResizeWindowToContent(true);
                                }
                            }
                        });
                }

                self->ui_settings_ = Windows::UI::ViewManagement::UISettings();
                self->last_text_scale_factor_ = self->ui_settings_.TextScaleFactor();
                self->text_scale_changed_revoker_ = self->ui_settings_.TextScaleFactorChanged(auto_revoke,
                    [weak](Windows::UI::ViewManagement::UISettings const& sender, IInspectable const&) {
                        if (auto window = weak.get()) {
                            const double scale = sender.TextScaleFactor();
                            if (std::abs(scale - window->last_text_scale_factor_) <= 0.0001) return;
                            window->last_text_scale_factor_ = scale;
                            (void)window->dispatcher_.TryEnqueue([weak]() {
                                if (auto ui_window = weak.get(); ui_window && !ui_window->resize_in_progress_) {
                                    ui_window->ResizeWindowToContent();
                                }
                            });
                        }
                    });
            } catch (...) {
            }
            self->ResizeWindowToContent();
        }
    });
    ResizeWindowToContent();
    PositionInitialWindow();
}

void MainWindow::PositionInitialWindow() noexcept {
    try {
        if (hwnd_ == nullptr) return;

        RECT window_rect{};
        MONITORINFO monitor_info{sizeof(monitor_info)};
        const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitor_info) ||
            !GetWindowRect(hwnd_, &window_rect)) {
            return;
        }

        const int width = window_rect.right - window_rect.left;
        const int height = window_rect.bottom - window_rect.top;
        const int work_width = monitor_info.rcWork.right - monitor_info.rcWork.left;
        const int work_height = monitor_info.rcWork.bottom - monitor_info.rcWork.top;
        const int x = monitor_info.rcWork.left + (work_width - width) / 2;
        const int y = monitor_info.rcWork.top + (work_height - height) * 2 / 5;
        AppWindow().Move(Windows::Graphics::PointInt32{x, y});
    } catch (...) {
    }
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

        const auto title_bar = AppWindow().TitleBar();
        const double right_inset_epx =
            title_bar.RightInset() * 96.0 / static_cast<double>(dpi);
        double title_height_epx = velocitycopy::ui::token_double(L"CaptionRowHeight", 32);
        if (title_bar.Height() > 0) {
            title_height_epx = title_bar.Height() * 96.0 / static_cast<double>(dpi);
        }
        TitleBarDragRegion().Height(title_height_epx);
        CaptionRowDefinition().Height(GridLength{title_height_epx, GridUnitType::Pixel});
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

void MainWindow::ResizeWindow(const int client_width_epx, const int client_height_epx, const bool preserve_position) {
    if (resize_in_progress_) return;
    try {
        HWND hwnd{};
        auto window_native = this->m_inner.as<::IWindowNative>();
        if (SUCCEEDED(window_native->get_WindowHandle(&hwnd)) && hwnd != nullptr) {
            const auto dpi = GetDpiForWindow(hwnd);
            if (dpi == 0) return;

            RECT window_rect{};
            RECT client_rect{};
            MONITORINFO monitor_info{sizeof(monitor_info)};
            const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (!GetWindowRect(hwnd, &window_rect) || !GetClientRect(hwnd, &client_rect) ||
                monitor == nullptr || !GetMonitorInfoW(monitor, &monitor_info)) return;
            const int frame_width =
                (window_rect.right - window_rect.left) - (client_rect.right - client_rect.left);
            const int frame_height =
                (window_rect.bottom - window_rect.top) - (client_rect.bottom - client_rect.top);
            const int safety_margin = MulDiv(
                velocitycopy::ui::token_int(L"ExpandedWorkAreaMargin", 16),
                static_cast<int>(dpi), 96);
            const int work_width = monitor_info.rcWork.right - monitor_info.rcWork.left;
            const int work_height = monitor_info.rcWork.bottom - monitor_info.rcWork.top;
            const int requested_client_width = MulDiv(client_width_epx, static_cast<int>(dpi), 96);
            const int requested_client_height = MulDiv(client_height_epx, static_cast<int>(dpi), 96);
            const int window_width = (std::min)(
                requested_client_width + frame_width,
                (std::max)(1, work_width - safety_margin * 2));
            const int window_height = (std::min)(
                requested_client_height + frame_height,
                (std::max)(1, work_height - safety_margin * 2));
            int x = window_rect.left;
            int y = window_rect.top;
            bool reposition = false;
            if (!preserve_position) {
                if (x < monitor_info.rcWork.left) {
                    x = monitor_info.rcWork.left + safety_margin;
                    reposition = true;
                } else if (x + window_width > monitor_info.rcWork.right) {
                    x = monitor_info.rcWork.right - safety_margin - window_width;
                    reposition = true;
                }
                if (y < monitor_info.rcWork.top) {
                    y = monitor_info.rcWork.top + safety_margin;
                    reposition = true;
                } else if (y + window_height > monitor_info.rcWork.bottom) {
                    y = monitor_info.rcWork.bottom - safety_margin - window_height;
                    reposition = true;
                }
            }

            resize_in_progress_ = true;
            SetWindowPos(hwnd, nullptr, x, y, window_width, window_height,
                         SWP_NOZORDER | SWP_NOACTIVATE | (reposition ? 0 : SWP_NOMOVE));
            resize_in_progress_ = false;
            ApplyTitleBarInset();
        }
    } catch (...) {
        resize_in_progress_ = false;
    }
}
void MainWindow::ResizeWindowToContent(const bool preserve_position) {
    if (resize_in_progress_) return;
    RootGrid().UpdateLayout();
    const auto measured_width = RootGrid().ActualWidth() > 0.0
        ? static_cast<float>(RootGrid().ActualWidth())
        : static_cast<float>(velocitycopy::ui::token_int(L"NormalWindowMinWidth", 380));
    TransferSurface().Measure({measured_width, std::numeric_limits<float>::infinity()});
    const auto normal_height = static_cast<double>(TransferSurface().DesiredSize().Height);
    const auto queue_min_height = velocitycopy::ui::token_int(L"QueueExpandedMinHeight", 176);
    const auto queue_max_height = velocitycopy::ui::token_int(L"QueueExpandedMaxHeight", 340);
    double notice_height = 0.0;
    double details_height = 0.0;
    if (ErrorBar().IsOpen()) {
        ErrorBar().Measure({measured_width, std::numeric_limits<float>::infinity()});
        notice_height = ErrorBar().DesiredSize().Height;
    }
    if (expanded_) {
        DetailsPanel().Measure({measured_width, std::numeric_limits<float>::infinity()});
        details_height = DetailsPanel().DesiredSize().Height;
    }
    if (!expanded_) {
        ResizeWindow(
            velocitycopy::ui::token_int(L"NormalWindowMinWidth", 380),
            static_cast<int>(std::ceil(normal_height + notice_height + details_height)),
            preserve_position);
        return;
    }

    QueuePanel().Measure({measured_width, std::numeric_limits<float>::infinity()});
    const auto desired_queue_height = static_cast<double>(QueuePanel().DesiredSize().Height);
    const auto expanded_height = static_cast<int>(std::ceil(
        normal_height + notice_height + desired_queue_height));
    const auto notice_height_epx = static_cast<int>(std::ceil(notice_height));
    ResizeWindow(
        velocitycopy::ui::token_int(L"NormalWindowMinWidth", 380),
        (std::clamp)(
            expanded_height,
            queue_min_height + notice_height_epx,
            queue_max_height + notice_height_epx),
        preserve_position);
    RootGrid().UpdateLayout();
}

void MainWindow::SetProgressFraction(const double fraction) {
    progress_fraction_ = (std::clamp)(fraction, 0.0, 1.0);
    const double percent = progress_fraction_ * 100.0;
    TransferProgress().Value(percent);
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
    args.Handled(true);
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
    ResetPerformanceHistory();
    TransferProgress().ShowPaused(false);
    TransferProgress().ShowError(false);
    TransferBytesText().Text(L"");
    TransferFilesText().Text(L"");
    SourcePathText().Text(L"");
    DestinationPathText().Text(L"");
    ToolTipService::SetToolTip(SourcePathText(), nullptr);
    ToolTipService::SetToolTip(DestinationPathText(), nullptr);
    DetailsSourceText().Text(L"");
    DetailsDestinationText().Text(L"");
    DetailsBytesText().Text(L"");
    DetailsFilesText().Text(L"");
    DetailsSpeedText().Text(L"—");
    DetailsEtaText().Text(L"—");
    PerformanceCurrentSpeedText().Text(L"—");
    ErrorBar().IsOpen(false);
    ErrorBar().Message(L"");
}

void MainWindow::SetDetailsExpanded(const bool expanded) {
    DetailsPanel().Visibility(expanded ? Visibility::Visible : Visibility::Collapsed);
    try {
        const auto label = velocitycopy::localization::get_string(
            expanded ? L"ActionHideDetails" : L"ActionShowDetails");
        ToolTipService::SetToolTip(DetailsButton(), box_value(label));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(DetailsButton(), label);
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: SetDetailsExpanded failed\\n");
    }
    if (expanded) UpdatePerformanceGraph();
}

void MainWindow::OnDetailsClick(IInspectable const&, RoutedEventArgs const&) {
    const bool expanding = DetailsPanel().Visibility() != Visibility::Visible;
    if (expanding && QueuePanel().Visibility() == Visibility::Visible) {
        QueuePanel().Visibility(Visibility::Collapsed);
        QueueChevron().Glyph(L"\xE70D");
        try {
            const auto label = velocitycopy::localization::get_string(L"ActionShowQueue");
            ToolTipService::SetToolTip(QueueButton(), box_value(label));
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(QueueButton(), label);
        } catch (...) {
        }
    }
    SetDetailsExpanded(expanding);
    ResizeWindowToContent();
    if (expanding) {
        RootGrid().UpdateLayout();
        UpdatePerformanceGraph();
    }
}

void MainWindow::ResetPerformanceHistory() noexcept {
    last_performance_sample_ms_ = 0;
    performance_speed_samples_.clear();
    try {
        PerformanceGraph().Children().Clear();
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: ResetPerformanceHistory failed\\n");
    }
}

void MainWindow::ObservePerformanceSample(const double bytes_per_second) {
    if (performance_sampling_state_ != PerformanceSamplingState::Copying) return;
    const auto now = GetTickCount64();
    if (last_performance_sample_ms_ != 0 && now - last_performance_sample_ms_ < 500) return;
    last_performance_sample_ms_ = now;
    performance_speed_samples_.push_back(
        std::isfinite(bytes_per_second) && bytes_per_second > 0.0 ? bytes_per_second : 0.0);
    while (performance_speed_samples_.size() > 60) performance_speed_samples_.pop_front();
    if (DetailsPanel().Visibility() == Visibility::Visible) UpdatePerformanceGraph();
}

void MainWindow::UpdatePerformanceGraph() {
    try {
        auto canvas = PerformanceGraph();
        const double width = canvas.ActualWidth();
        const double height = canvas.ActualHeight();
        if (width <= 0.0 || height <= 0.0) return;

        auto children = canvas.Children();
        children.Clear();
        if (performance_speed_samples_.empty()) return;

        double peak = 1.0;
        for (const double sample : performance_speed_samples_) peak = (std::max)(peak, sample);

        auto brush = PerformanceGraphBrushSource().Background();
        const std::size_t count = performance_speed_samples_.size();
        const double slot = width / static_cast<double>(count);
        const double bar_width = (std::max)(1.0, slot - 1.0);
        std::size_t index = 0;
        for (const double sample : performance_speed_samples_) {
            const double normalized = (std::clamp)(sample / peak, 0.0, 1.0);
            const double bar_height = sample <= 0.0 ? 1.0 : (std::max)(1.0, normalized * height);
            Border bar;
            bar.Width(bar_width);
            bar.Height(bar_height);
            if (brush) bar.Background(brush);
            Canvas::SetLeft(bar, static_cast<double>(index) * slot);
            Canvas::SetTop(bar, height - bar_height);
            children.Append(bar);
            ++index;
        }
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: UpdatePerformanceGraph failed\\n");
    }
}

void MainWindow::ShowNotice(InfoBarSeverity const severity, hstring const& message) {
    TransferProgress().ShowError(severity == InfoBarSeverity::Error);
    ErrorBar().Severity(severity);
    ErrorBar().Message(message);
    ErrorBar().IsOpen(true);
    ResizeWindowToContent();

    auto weak = get_weak();
    (void)dispatcher_.TryEnqueue([weak]() {
        if (auto self = weak.get(); self && self->ErrorBar().IsOpen()) {
            self->ResizeWindowToContent();
        }
    });
}

void MainWindow::ShowError(hstring const& message) {
    ShowNotice(InfoBarSeverity::Error, message);
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

hstring MainWindow::FormatBytes(const std::uint64_t bytes) {
    constexpr double kib = 1024.0;
    constexpr double mib = 1024.0 * 1024.0;
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    const double value = static_cast<double>(bytes);
    if (value >= gib) return hstring(std::format(L"{:.2f} GiB", value / gib));
    if (value >= mib) return hstring(std::format(L"{:.1f} MiB", value / mib));
    if (value >= kib) return hstring(std::format(L"{:.0f} KiB", value / kib));
    return hstring(std::format(L"{} B", bytes));
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