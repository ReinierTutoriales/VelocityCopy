#include "pch.h"
#include "IconAssets.h"
#include "MainWindow.xaml.h"
#include "App.xaml.h"
#include "UiTokens.h"
#include "Localization.h"
#include "PerformanceGraphScale.h"
#include "TelemetryLayout.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <limits>

using namespace winrt;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace Windows::Storage;
using namespace Windows::Foundation;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;

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
    // ListView handles the mouse wheel internally. handledEventsToo lets
    // Queue hand a wheel detent to the outer Narrow viewport only after the
    // internal list reaches its boundary.
    QueueList().AddHandler(
        UIElement::PointerWheelChangedEvent(),
        box_value(Microsoft::UI::Xaml::Input::PointerEventHandler{
            this, &MainWindow::OnQueuePointerWheelChanged}),
        true);
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
    // ShowNotice measures the InfoBar right after IsOpen(true), before its
    // template and wrapped message have necessarily been laid out, and a later
    // Message/Title change or Text Size change alters its height without any
    // resize request. Re-fit whenever the open notice's real height changes so
    // the banner is never cut off at the bottom of the fixed-size window.
    ErrorBar().SizeChanged([weak = get_weak()](IInspectable const&, SizeChangedEventArgs const& args) {
        if (auto self = weak.get(); self && self->ErrorBar().IsOpen() &&
            std::abs(args.NewSize().Height - args.PreviousSize().Height) > 0.5f) {
            self->ResizeWindowToContent(true);
        }
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
        DetailsButtonText().Text(velocitycopy::localization::get_string(L"ActionDetails"));
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
        app_window.SetIcon(velocitycopy::ui::application_icon_path());
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
                                    window->ScheduleTelemetryReserveSettle();
                                    window->ScheduleTelemetryGeometryProbe();
                                }
                            }
                        });
                }

                self->ui_settings_ = Windows::UI::ViewManagement::UISettings();
                self->last_text_scale_factor_ = self->ui_settings_.TextScaleFactor();
                self->text_scale_changed_revoker_ = self->ui_settings_.TextScaleFactorChanged(auto_revoke,
                    [weak, dispatcher = self->dispatcher_](Windows::UI::ViewManagement::UISettings const& sender, IInspectable const&) {
                        const double scale = sender.TextScaleFactor();
                        (void)dispatcher.TryEnqueue([weak, scale]() {
                            if (auto window = weak.get(); window && !window->tray_exit_requested_) {
                                if (std::abs(scale - window->last_text_scale_factor_) <= 0.0001) return;
                                window->last_text_scale_factor_ = scale;
                                window->UpdatePerformanceAxisWidth();
                                window->ResizeWindowToContent();
                                window->ScheduleTelemetryReserveSettle();
                                window->ScheduleTelemetryGeometryProbe();
                            }
                        });
                    });
            } catch (...) {
            }
            self->UpdatePerformanceAxisWidth();
            self->ResizeWindowToContent();
            self->ScheduleTelemetryGeometryProbe();
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

        // Keep the last valid system-reserved caption inset while minimized.
        // Caption metrics are not stable/meaningful for an iconic window.
        if (IsIconic(hwnd)) return;

        const auto dpi = GetDpiForWindow(hwnd);
        if (dpi == 0) return;

        const auto title_bar = AppWindow().TitleBar();
        const double reported_right_inset_epx =
            title_bar.RightInset() * 96.0 / static_cast<double>(dpi);
        if (reported_right_inset_epx > 0.0) {
            title_bar_right_inset_epx_ =
                (std::max)(title_bar_right_inset_epx_, reported_right_inset_epx);
        }
        const double right_inset_epx =
            (std::max)(reported_right_inset_epx, title_bar_right_inset_epx_);
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
    Microsoft::UI::Windowing::AppWindowChangedEventArgs const& args) {
    ApplyTitleBarInset();

    // Minimize/restore is reported as a presenter change. Queue one more pass so
    // the system caption cluster has settled before the filename receives its
    // final right padding.
    if (args.DidPresenterChange() || args.DidSizeChange()) {
        auto weak = get_weak();
        (void)dispatcher_.TryEnqueue([weak]() {
            if (auto self = weak.get()) {
                self->ApplyTitleBarInset();
            }
        });
    }
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

    HWND hwnd = hwnd_;
    if (hwnd == nullptr) {
        auto window_native = this->m_inner.as<::IWindowNative>();
        if (FAILED(window_native->get_WindowHandle(&hwnd)) || hwnd == nullptr) return;
    }
    const auto dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) return;

    MONITORINFO monitor_info{sizeof(monitor_info)};
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitor_info)) return;

    const double work_width_epx =
        (monitor_info.rcWork.right - monitor_info.rcWork.left) * 96.0 / static_cast<double>(dpi);
    const double work_height_epx =
        (monitor_info.rcWork.bottom - monitor_info.rcWork.top) * 96.0 / static_cast<double>(dpi);
    const double work_margin = velocitycopy::ui::token_double(L"ExpandedWorkAreaMargin", 16);
    const double normal_width = velocitycopy::ui::token_double(L"NormalWindowMinWidth", 380);
    const double preferred_width = velocitycopy::ui::token_double(L"ExpandedPreferredWidth", 880);
    const double three_column_threshold =
        velocitycopy::ui::token_double(L"ExpandedThreeColumnThreshold", 720);
    const double work_width_cap = (std::max)(normal_width, work_width_epx - work_margin * 2.0);
    const double text_scale = (std::max)(1.0, last_text_scale_factor_);
    // 380 epx scaled by Text Size is the minimum, not the final width: the window grows only
    // when the reserved bottom row (telemetry cells + actions + Details) cannot fit.
    ApplyTelemetryReserves();
    const double scaled_normal_width = normal_width * text_scale;
    const double required_normal_width = expanded_ ? 0.0 : RequiredNormalWindowWidth();
    const double normal_target_width = velocitycopy::ui::layout::normal_target_width(
        scaled_normal_width, required_normal_width, work_width_cap);
    const double target_width = expanded_
        ? (std::max)(normal_width, (std::min)(preferred_width, work_width_cap))
        : normal_target_width;

    if (expanded_) {
        const double effective_width = target_width / text_scale;
        const auto previous_layout_mode = expanded_layout_mode_;
        expanded_layout_mode_ = effective_width >= three_column_threshold
            ? ExpandedLayoutMode::ThreeColumn
            : ExpandedLayoutMode::Narrow;
        if (previous_layout_mode != expanded_layout_mode_) {
            RefreshQueue(true);
        }
        if (expanded_layout_mode_ == ExpandedLayoutMode::ThreeColumn) {
            QueueSectionIcon().Visibility(Visibility::Collapsed);
            ExpandedRow0().Height(GridLength{1.0, GridUnitType::Star});
            ExpandedRow1().Height(GridLength{0.0, GridUnitType::Pixel});
            ExpandedRow2().Height(GridLength{0.0, GridUnitType::Pixel});
            ExpandedColumn0().Width(GridLength{1.0, GridUnitType::Star});
            ExpandedColumn1().Width(GridLength{2.0, GridUnitType::Star});
            ExpandedColumn2().Width(GridLength{0.0, GridUnitType::Pixel});
            Grid::SetRow(QueuePanel(), 0);
            Grid::SetColumn(QueuePanel(), 0);
            Grid::SetRow(DetailsViewport(), 0);
            Grid::SetColumn(DetailsViewport(), 1);
            Grid::SetColumnSpan(DetailsViewport(), 1);
            DetailsRow0().Height(GridLength{1.0, GridUnitType::Star});
            DetailsRow1().Height(GridLength{0.0, GridUnitType::Pixel});
            DetailsColumn0().Width(GridLength{1.0, GridUnitType::Star});
            DetailsColumn1().Width(GridLength{1.0, GridUnitType::Star});
            Grid::SetRow(PerformancePanel(), 0);
            Grid::SetColumn(PerformancePanel(), 0);
            Grid::SetRow(InformationPanel(), 0);
            Grid::SetColumn(InformationPanel(), 1);
            DetailsGrid().ColumnSpacing(
                velocitycopy::ui::token_double(L"ExpandedColumnSpacing", 8));
            ExpandedViewport().VerticalScrollMode(ScrollMode::Disabled);
            ExpandedViewport().VerticalScrollBarVisibility(ScrollBarVisibility::Disabled);
            ExpandedViewport().IsTabStop(false);
            ExpandedRegion().ColumnSpacing(
                velocitycopy::ui::token_double(L"ExpandedColumnSpacing", 8));
            PerformancePanel().Margin(
                velocitycopy::ui::token_thickness(L"ExpandedSectionMargin", Thickness{4.0}));
        } else {
            QueueSectionIcon().Visibility(Visibility::Visible);
            ExpandedRow0().Height(GridLength{1.0, GridUnitType::Star});
            ExpandedRow1().Height(GridLength{1.0, GridUnitType::Star});
            ExpandedRow2().Height(GridLength{0.0, GridUnitType::Pixel});
            ExpandedColumn0().Width(GridLength{1.0, GridUnitType::Star});
            ExpandedColumn1().Width(GridLength{0.0, GridUnitType::Pixel});
            ExpandedColumn2().Width(GridLength{0.0, GridUnitType::Pixel});
            Grid::SetRow(QueuePanel(), 0);
            Grid::SetColumn(QueuePanel(), 0);
            Grid::SetRow(DetailsViewport(), 1);
            Grid::SetColumn(DetailsViewport(), 0);
            Grid::SetColumnSpan(DetailsViewport(), 1);
            DetailsRow0().Height(GridLength{1.0, GridUnitType::Auto});
            DetailsRow1().Height(GridLength{1.0, GridUnitType::Auto});
            DetailsColumn0().Width(GridLength{1.0, GridUnitType::Star});
            DetailsColumn1().Width(GridLength{0.0, GridUnitType::Pixel});
            Grid::SetRow(PerformancePanel(), 0);
            Grid::SetColumn(PerformancePanel(), 0);
            Grid::SetRow(InformationPanel(), 1);
            Grid::SetColumn(InformationPanel(), 0);
            DetailsGrid().ColumnSpacing(0.0);
            ExpandedViewport().VerticalScrollMode(ScrollMode::Auto);
            ExpandedViewport().VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
            ExpandedRegion().ColumnSpacing(0.0);
            PerformancePanel().Margin(
                velocitycopy::ui::token_thickness(L"ExpandedSectionMargin", Thickness{4.0}));
        }
    }

    const float measure_width = static_cast<float>(target_width);
    TransferSurface().InvalidateMeasure();
    TransferSurface().Measure({measure_width, std::numeric_limits<float>::infinity()});
    const double normal_height = TransferSurface().DesiredSize().Height;
    double notice_height = 0.0;
    if (ErrorBar().IsOpen()) {
        ErrorBar().Measure({measure_width, std::numeric_limits<float>::infinity()});
        notice_height = ErrorBar().DesiredSize().Height;
    }

    if (!expanded_) {
        ResizeWindow(
            static_cast<int>(std::ceil(target_width)),
            static_cast<int>(std::ceil(normal_height + notice_height)),
            preserve_position);
        return;
    }

    const double queue_min_height = velocitycopy::ui::token_double(L"QueueExpandedMinHeight", 176);
    const double details_min_height = velocitycopy::ui::token_double(L"DetailsExpandedMinHeight", 128);
    const double expanded_max_height = velocitycopy::ui::token_double(L"QueueExpandedMaxHeight", 340);
    const double available_expanded_height =
        (std::max)(queue_min_height, work_height_epx - work_margin * 2.0 - normal_height - notice_height);
    const double expanded_height_cap = (std::max)(
        1.0, (std::min)(expanded_max_height, available_expanded_height));
    ExpandedViewport().MaxHeight(expanded_height_cap);
    const auto expanded_padding = ExpandedRegion().Padding();
    const double expanded_column_spacing =
        velocitycopy::ui::token_double(L"ExpandedColumnSpacing", 8);
    const double expanded_content_width = (std::max)(
        1.0, target_width - expanded_padding.Left - expanded_padding.Right);
    const double panel_width = expanded_layout_mode_ == ExpandedLayoutMode::ThreeColumn
        ? (std::max)(1.0, (expanded_content_width - expanded_column_spacing * 2.0) / 3.0)
        : expanded_content_width;

    PerformancePanel().Measure({
        static_cast<float>(panel_width), std::numeric_limits<float>::infinity()});
    InformationPanel().Measure({
        static_cast<float>(panel_width), std::numeric_limits<float>::infinity()});
    const double performance_height = PerformancePanel().DesiredSize().Height;
    const double information_height = InformationPanel().DesiredSize().Height;

    double expanded_region_height = 0.0;
    if (expanded_layout_mode_ == ExpandedLayoutMode::ThreeColumn) {
        const double content_height = (std::min)(
            (std::max)({performance_height, information_height, queue_min_height}),
            (std::max)(1.0, expanded_height_cap - expanded_padding.Top - expanded_padding.Bottom));
        ExpandedRow0().Height(GridLength{content_height, GridUnitType::Pixel});
        expanded_region_height =
            content_height + expanded_padding.Top + expanded_padding.Bottom;
    } else {
        const double padding_height = expanded_padding.Top + expanded_padding.Bottom;
        const double content_cap = (std::max)(1.0, expanded_height_cap - padding_height);
        const double details_required_height = (std::max)(
            details_min_height, performance_height + information_height);
        const double required_content_height = queue_min_height + details_required_height;
        const bool constrained_height = content_cap < required_content_height;
        const double queue_height = queue_min_height;
        ExpandedRow0().Height(GridLength{queue_height, GridUnitType::Pixel});
        // Let DetailsGrid keep its measured desired height. When the combined
        // Queue + Performance + Information extent exceeds the viewport cap,
        // the outer ScrollViewer must own the overflow instead of clipping
        // Information inside a fixed 128 epx details row.
        ExpandedRow1().Height(GridLength{1.0, GridUnitType::Auto});
        ExpandedViewport().VerticalScrollMode(
            constrained_height ? ScrollMode::Auto : ScrollMode::Disabled);
        ExpandedViewport().VerticalScrollBarVisibility(
            constrained_height ? ScrollBarVisibility::Auto : ScrollBarVisibility::Disabled);
        ExpandedViewport().IsTabStop(constrained_height);
        expanded_region_height = constrained_height
            ? expanded_height_cap
            : queue_height + details_required_height + padding_height;
    }

    ResizeWindow(
        static_cast<int>(std::ceil(target_width)),
        static_cast<int>(std::ceil(normal_height + notice_height + expanded_region_height)),
        preserve_position);
    RootGrid().UpdateLayout();
}

void MainWindow::SetProgressFraction(const double fraction) {
    const double progress_fraction = (std::clamp)(fraction, 0.0, 1.0);
    const double percent = progress_fraction * 100.0;
    TransferProgress().Value(percent);
    ProgressPercentText().Text(FormatProgressPercent(progress_fraction));
    taskbar_fraction_ = progress_fraction;
    RefreshTaskbarProgress();
}

void MainWindow::SetTaskbarState(const TBPFLAG state) noexcept {
    taskbar_state_ = state;
    RefreshTaskbarProgress();
}

void MainWindow::RefreshWindowTitle() noexcept {
    // Several copies show up as separate taskbar/Alt+Tab entries; name each
    // by its progress and destination folder so they can be told apart.
    try {
        std::wstring title = L"VelocityCopy";
        if (taskbar_state_ != TBPF_NOPROGRESS && !active_destination_.empty()) {
            auto folder = active_destination_.filename().wstring();
            if (folder.empty()) folder = active_destination_.wstring();
            title = taskbar_state_ == TBPF_INDETERMINATE
                ? std::format(L"{} \u2014 VelocityCopy", folder)
                : std::format(L"{} \u00B7 {} \u2014 VelocityCopy", FormatProgressPercent(taskbar_fraction_).c_str(), folder);
        }
        if (title == window_title_) return;
        window_title_ = title;
        Title(hstring(title));
    } catch (...) {
    }
}

void MainWindow::RefreshTaskbarProgress() noexcept {
    RefreshWindowTitle();
    if (hwnd_ == nullptr || taskbar_unavailable_) return;
    if (!taskbar_) {
        // Explorer can be restarted or absent; failing once disables the
        // feature for this window instead of retrying on every tick.
        if (FAILED(CoCreateInstance(__uuidof(TaskbarList), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(taskbar_.put()))) ||
            FAILED(taskbar_->HrInit())) {
            taskbar_ = nullptr;
            taskbar_unavailable_ = true;
            return;
        }
    }
    (void)taskbar_->SetProgressState(hwnd_, taskbar_state_);
    if (taskbar_state_ != TBPF_NOPROGRESS && taskbar_state_ != TBPF_INDETERMINATE) {
        constexpr ULONGLONG kScale = 10000;
        (void)taskbar_->SetProgressValue(hwnd_, static_cast<ULONGLONG>(taskbar_fraction_ * kScale), kScale);
    }
}

hstring MainWindow::FormatProgressPercent(const double fraction) {
    const double clamped = (std::clamp)(fraction, 0.0, 1.0);
    const double percent = clamped * 100.0;
    if (clamped > 0.0 && percent < 0.1) return hstring(L"<0.1%");
    if (percent > 0.0 && percent < 10.0) return hstring(std::format(L"{:.1f}%", percent));
    return hstring(std::format(L"{:.0f}%", percent));
}

void MainWindow::OnDragEnter(IInspectable const&, DragEventArgs const& args) {
    if (queue_drag_active_) return;
    args.AcceptedOperation(
        accepts_active_transfer_drop(active_destination_, execution_control_, live_plan_, args)
            ? DataPackageOperation::Copy
            : DataPackageOperation::None);
}

void MainWindow::OnDragOver(IInspectable const&, DragEventArgs const& args) {
    if (queue_drag_active_) return;
    args.AcceptedOperation(
        accepts_active_transfer_drop(active_destination_, execution_control_, live_plan_, args)
            ? DataPackageOperation::Copy
            : DataPackageOperation::None);
}

void MainWindow::OnDrop(IInspectable const&, DragEventArgs const& args) {
    if (queue_drag_active_) return;
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
    const auto target_gate = append_gate_;
    const auto target_operation = active_operation_;
    try {
        const auto target_destination = active_destination_;
        auto target_is_current = [&]() {
            return target_gate && append_gate_ == target_gate && !tray_exit_requested_ &&
                !cancel_requested_.load(std::memory_order_relaxed) &&
                active_destination_ == target_destination && active_operation_ == target_operation &&
                (execution_control_ || live_plan_);
        };
        if (active_destination_.empty() || (!execution_control_ && !live_plan_)) {
            deferral.Complete();
            co_return;
        }

        auto storage_items = co_await args.DataView().GetStorageItemsAsync();
        if (!target_is_current()) {
            args.AcceptedOperation(DataPackageOperation::None);
            deferral.Complete();
            co_return;
        }
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
        job.destination = target_destination;
        job.operation = target_operation;
        AppendTransfer(std::move(job));
        deferral.Complete();
    } catch (...) {
        deferral.Complete();
        args.AcceptedOperation(DataPackageOperation::None);
        if (target_gate && append_gate_ == target_gate && !tray_exit_requested_ &&
            !cancel_requested_.load(std::memory_order_relaxed)) ShowError();
    }
}

void MainWindow::ApplyTransferVisualState(const TransferVisualState state) noexcept {
    try {
        auto icon = CurrentItemIcon();
        switch (state) {
        case TransferVisualState::Warning:
            icon.Glyph(L"\xE7BA");
            velocitycopy::ui::apply_icon_style(icon, L"WarningIconStyle");
            break;
        case TransferVisualState::Error:
            SetTaskbarState(TBPF_ERROR);
            icon.Glyph(L"\xEB90");
            velocitycopy::ui::apply_icon_style(icon, L"ErrorIconStyle");
            break;
        case TransferVisualState::Active:
        default:
            icon.Glyph(L"\xE8A5");
            velocitycopy::ui::apply_icon_style(icon, L"AccentIconStyle");
            break;
        }
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: ApplyTransferVisualState failed\n");
    }
}

void MainWindow::ResetTransferSurface() {
    ResetPerformanceHistory();
    ApplyTransferVisualState(TransferVisualState::Active);
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
    ErrorBar().Title(L"");
    ErrorBar().Message(L"");
}

void MainWindow::SetExpanded(const bool expanded) {
    expanded_ = expanded;
    ExpandedRegion().Visibility(expanded ? Visibility::Visible : Visibility::Collapsed);
    try {
        const auto details_label = velocitycopy::localization::get_string(
            expanded ? L"ActionHideDetails" : L"ActionShowDetails");
        const auto details_button_label = expanded
            ? details_label
            : velocitycopy::localization::get_string(L"ActionDetails");
        DetailsButtonText().Text(details_button_label);
        DetailsChevronIcon().Glyph(expanded ? L"\uE70E" : L"\uE70D");
        ToolTipService::SetToolTip(DetailsButton(), box_value(details_label));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(DetailsButton(), details_label);
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: SetExpanded failed\\n");
    }
    if (expanded) {
        RefreshQueue();
        UpdatePerformanceGraph();
    }
    ResizeWindowToContent();
    ScheduleTelemetryGeometryProbe();
}
void MainWindow::OnDetailsClick(IInspectable const&, RoutedEventArgs const&) {
    SetExpanded(!expanded_);
    if (expanded_) {
        RootGrid().UpdateLayout();
        UpdatePerformanceGraph();
    }
}

void MainWindow::ResetPerformanceHistory() noexcept {
    last_performance_sample_ms_ = 0;
    performance_speed_samples_.clear();
    performance_scale_state_ = {};
    try {
        PerformanceGraphLine().Points(PointCollection{});
        PerformanceGraphArea().Points(PointCollection{});
        constexpr double mib = 1024.0 * 1024.0;
        PerformanceScaleMaxText().Text(FormatPerformanceScaleSpeed(mib, mib));
        PerformanceScaleMidText().Text(FormatPerformanceScaleSpeed(mib / 2.0, mib));
        PerformanceScaleZeroText().Text(FormatPerformanceScaleSpeed(0.0, mib));
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: ResetPerformanceHistory failed\n");
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

    const std::vector<double> samples(performance_speed_samples_.begin(), performance_speed_samples_.end());
    const auto stats = velocitycopy::ui::performance_window_stats(samples);
    if (stats.scale_reference_ready) {
        (void)velocitycopy::ui::update_performance_scale(
            performance_scale_state_, stats.scale_reference_bytes_per_second);
    }

    if (expanded_) UpdatePerformanceGraph();
}

void MainWindow::UpdatePerformanceAxisWidth() {
    try {
        auto label = PerformanceScaleMaxText();
        const auto previous_text = label.Text();
        label.Text(L"1000 MiB/s");
        label.Measure({std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()});
        const double measured_width = label.DesiredSize().Width;
        label.Text(previous_text);
        const auto axis_margin = PerformanceScaleLabels().Margin();
        const double required_width = measured_width + axis_margin.Left + axis_margin.Right;
        const double token_min_width = velocitycopy::ui::token_double(L"PerformanceAxisLabelMinWidth", 88.0);
        PerformanceScaleColumn().MinWidth((std::max)(token_min_width, required_width));
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: UpdatePerformanceAxisWidth failed\\n");
    }
}

void MainWindow::UpdatePerformanceGraph() {
    try {
        auto canvas = PerformanceGraph();
        const double width = canvas.ActualWidth();
        const double height = canvas.ActualHeight();
        if (width <= 0.0 || height <= 0.0) return;

        if (performance_speed_samples_.empty()) {
            PerformanceGraphLine().Points(PointCollection{});
            PerformanceGraphArea().Points(PointCollection{});
            constexpr double mib = 1024.0 * 1024.0;
            PerformanceScaleMaxText().Text(FormatPerformanceScaleSpeed(mib, mib));
            PerformanceScaleMidText().Text(FormatPerformanceScaleSpeed(mib / 2.0, mib));
            PerformanceScaleZeroText().Text(FormatPerformanceScaleSpeed(0.0, mib));
            return;
        }

        constexpr double sample_capacity = 60.0;
        const auto scale = velocitycopy::ui::current_performance_scale(performance_scale_state_);
        const double scale_max = scale.ceiling_bytes_per_second;
        PerformanceScaleMaxText().Text(FormatPerformanceScaleSpeed(scale_max, scale.unit_bytes));
        PerformanceScaleMidText().Text(FormatPerformanceScaleSpeed(scale_max / 2.0, scale.unit_bytes));
        PerformanceScaleZeroText().Text(FormatPerformanceScaleSpeed(0.0, scale.unit_bytes));

        const double stroke_thickness = PerformanceGraphLine().StrokeThickness();
        const double stroke_inset = stroke_thickness / 2.0;
        const double baseline = height - stroke_inset;
        const double plot_height = (std::max)(1.0, height - stroke_thickness);
        const double slot = width / (sample_capacity - 1.0);
        const double first_x = (std::max)(
            0.0, width - slot * static_cast<double>(performance_speed_samples_.size() - 1));

        PointCollection line_points;
        PointCollection area_points;
        area_points.Append({static_cast<float>(first_x), static_cast<float>(baseline)});
        std::size_t index = 0;
        for (const double sample : performance_speed_samples_) {
            const double x = first_x + static_cast<double>(index) * slot;
            const double normalized = (std::clamp)(sample / scale_max, 0.0, 1.0);
            const double y = baseline - normalized * plot_height;
            const Point point{static_cast<float>(x), static_cast<float>(y)};
            line_points.Append(point);
            area_points.Append(point);
            ++index;
        }
        const double last_x = first_x + static_cast<double>(performance_speed_samples_.size() - 1) * slot;
        area_points.Append({static_cast<float>(last_x), static_cast<float>(baseline)});

        PerformanceGraphLine().Points(line_points);
        PerformanceGraphArea().Points(area_points);
    } catch (...) {
        OutputDebugStringW(L"VelocityCopy: UpdatePerformanceGraph failed\n");
    }
}

void MainWindow::ShowNotice(
    InfoBarSeverity const severity,
    hstring const& title,
    hstring const& message) {
    TransferProgress().ShowError(severity == InfoBarSeverity::Error);
    if (severity == InfoBarSeverity::Error) {
        ApplyTransferVisualState(TransferVisualState::Error);
    } else if (severity == InfoBarSeverity::Warning) {
        ApplyTransferVisualState(TransferVisualState::Warning);
    }
    ErrorBar().Severity(severity);
    ErrorBar().Title(title);
    ErrorBar().Message(message);
    ErrorBar().IsOpen(true);
    ResizeWindowToContent();
    // A notice inside a hidden or minimized window is never seen.
    if (severity == InfoBarSeverity::Error || severity == InfoBarSeverity::Warning) RequestAttention();

    auto weak = get_weak();
    (void)dispatcher_.TryEnqueue([weak]() {
        if (auto self = weak.get(); self && self->ErrorBar().IsOpen()) {
            self->ResizeWindowToContent();
        }
    });
}

void MainWindow::ShowError(hstring const& message) {
    hstring title;
    try {
        title = velocitycopy::localization::get_string(L"StatusFailed");
    } catch (...) {
    }
    ShowNotice(InfoBarSeverity::Error, title, message);
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

namespace {

hstring format_speed_value(const double bytes_per_second) {
    constexpr double kib = 1024.0;
    constexpr double mib = 1024.0 * 1024.0;
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    if (bytes_per_second <= 0.0 || !std::isfinite(bytes_per_second)) {
        return hstring(L"—");
    }
    if (bytes_per_second >= gib) return hstring(std::format(L"{:.2f} GiB/s", bytes_per_second / gib));
    if (bytes_per_second >= mib) return hstring(std::format(L"{:.1f} MiB/s", bytes_per_second / mib));
    return hstring(std::format(L"{:.0f} KiB/s", bytes_per_second / kib));
}
}

hstring MainWindow::FormatSpeed(const double bytes_per_second) {
    return format_speed_value(bytes_per_second);
}

hstring MainWindow::FormatPerformanceScaleSpeed(
    const double bytes_per_second,
    const double unit_bytes) {
    constexpr double mib = 1024.0 * 1024.0;
    constexpr double gib = 1024.0 * 1024.0 * 1024.0;
    const double safe_unit = unit_bytes >= gib ? gib : mib;
    const double value = std::isfinite(bytes_per_second) && bytes_per_second > 0.0
        ? bytes_per_second / safe_unit
        : 0.0;
    auto text = std::format(L"{:.2f}", value);
    while (text.size() > 1 && text.back() == L'0') text.pop_back();
    if (!text.empty() && text.back() == L'.') text.pop_back();
    return hstring(text + (safe_unit >= gib ? L" GiB/s" : L" MiB/s"));
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