#include "pch.h"
#include "IconAssets.h"
#include "DecisionSurface.h"
#include "UiTokens.h"

#include <winrt/Windows.System.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Automation;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;

namespace velocitycopy::ui {
namespace {
struct CompletionState {
    winrt::handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic_bool completed{};
    std::atomic<std::uint32_t> result{encode_decision({DecisionChoice::Cancel, false})};

    void complete(const DecisionChoice choice, const bool checked) noexcept {
        bool expected = false;
        if (!completed.compare_exchange_strong(expected, true)) return;
        result.store(encode_decision({choice, checked}));
        SetEvent(event.get());
    }
};

void center_owned_window(HWND owner, HWND dialog) noexcept {
    if (!owner || !dialog) return;
    RECT owner_rect{}, dialog_rect{};
    if (!GetWindowRect(owner, &owner_rect) || !GetWindowRect(dialog, &dialog_rect)) return;

    MONITORINFO monitor_info{sizeof(monitor_info)};
    const HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    if (!monitor || !GetMonitorInfoW(monitor, &monitor_info)) return;

    const int width = dialog_rect.right - dialog_rect.left;
    const int height = dialog_rect.bottom - dialog_rect.top;
    const int desired_x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    const int desired_y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
    const int max_x = (std::max)(monitor_info.rcWork.left, monitor_info.rcWork.right - width);
    const int max_y = (std::max)(monitor_info.rcWork.top, monitor_info.rcWork.bottom - height);
    const LONG x = (std::clamp)(static_cast<LONG>(desired_x), monitor_info.rcWork.left, static_cast<LONG>(max_x));
    const LONG y = (std::clamp)(static_cast<LONG>(desired_y), monitor_info.rcWork.top, static_cast<LONG>(max_y));
    SetWindowPos(dialog, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}
// Sizes the dialog's CLIENT area to the measured content. AppWindow::Resize
// sets the OUTER window size (frame and borders included), so the old code left
// the bottom/right edge of the content (the action buttons) clipped by exactly
// the non-client frame. The XAML rasterization scale of the dialog itself is
// used, not the owner's DPI, because the dialog may open on another monitor.
// Width grows when the localized buttons (or a large Text Size) need more than
// the token width, and both axes are capped to the work area: the content sits
// in a ScrollViewer, so an oversized message scrolls instead of being cut.
void fit_dialog_to_content(
    Window const& dialog,
    Grid const& root,
    FrameworkElement const& actions,
    Thickness const& content_padding,
    HWND owner) noexcept {
    try {
        HWND dialog_hwnd{};
        if (auto native = dialog.try_as<::IWindowNative>()) (void)native->get_WindowHandle(&dialog_hwnd);

        double scale = 0.0;
        if (const auto xaml_root = root.XamlRoot()) scale = xaml_root.RasterizationScale();
        if (!(scale > 0.0)) {
            UINT dpi = dialog_hwnd ? GetDpiForWindow(dialog_hwnd) : 0;
            if (dpi == 0 && owner) dpi = GetDpiForWindow(owner);
            scale = static_cast<double>(dpi ? dpi : USER_DEFAULT_SCREEN_DPI) / USER_DEFAULT_SCREEN_DPI;
        }

        double max_width_epx = std::numeric_limits<double>::infinity();
        double max_height_epx = std::numeric_limits<double>::infinity();
        MONITORINFO monitor_info{sizeof(monitor_info)};
        const HMONITOR monitor = MonitorFromWindow(owner ? owner : dialog_hwnd, MONITOR_DEFAULTTONEAREST);
        if (monitor && GetMonitorInfoW(monitor, &monitor_info)) {
            const double margin = token_double(L"ExpandedWorkAreaMargin", 16);
            max_width_epx = (std::max)(1.0,
                (monitor_info.rcWork.right - monitor_info.rcWork.left) / scale - margin * 2.0);
            max_height_epx = (std::max)(1.0,
                (monitor_info.rcWork.bottom - monitor_info.rcWork.top) / scale - margin * 2.0);
        }

        constexpr float unbounded = std::numeric_limits<float>::infinity();
        actions.Measure(Windows::Foundation::Size{unbounded, unbounded});
        double width_epx = (std::max)(
            static_cast<double>(token_int(L"DecisionWindowWidth", 440)),
            actions.DesiredSize().Width + content_padding.Left + content_padding.Right);
        width_epx = (std::min)(width_epx, max_width_epx);

        root.Measure(Windows::Foundation::Size{static_cast<float>(width_epx), unbounded});
        const double height_epx = (std::min)(
            static_cast<double>(root.DesiredSize().Height), max_height_epx);

        dialog.AppWindow().ResizeClient(Windows::Graphics::SizeInt32{
            static_cast<std::int32_t>(std::ceil(width_epx * scale)),
            static_cast<std::int32_t>(std::ceil(height_epx * scale))});
        center_owned_window(owner, dialog_hwnd);
    } catch (...) {}
}
} // namespace

Windows::Foundation::IAsyncOperation<std::uint32_t> show_decision_async(DecisionOptions options) {
    const auto ui_context = apartment_context{};
    auto state = std::make_shared<CompletionState>();
    if (!state->event) co_return encode_decision({DecisionChoice::Cancel, false});

    auto cancellation = co_await get_cancellation_token();
    cancellation.enable_propagation(false);
    cancellation.callback([state] { state->complete(DecisionChoice::Cancel, false); });

    Window dialog;
    dialog.Title(hstring(options.title));

    Grid root;
    root.RowDefinitions().Append(RowDefinition{});
    root.RowDefinitions().GetAt(0).Height(GridLength{0.0, GridUnitType::Auto});
    root.RowDefinitions().Append(RowDefinition{});
    root.RowDefinitions().GetAt(1).Height(GridLength{1.0, GridUnitType::Star});

    Border title_bar;
    title_bar.Height(token_double(L"AboutTitleBarHeight", 32));
    root.Children().Append(title_bar);

    const auto content_padding = token_thickness(L"DecisionContentPadding", Thickness{24, 20, 24, 20});
    StackPanel content;
    content.Spacing(token_double(L"DecisionContentSpacing", 12));
    content.Padding(content_padding);

    Grid heading;
    heading.ColumnSpacing(token_double(L"SpaceRelated", 8));
    heading.ColumnDefinitions().Append(ColumnDefinition{});
    heading.ColumnDefinitions().GetAt(0).Width(GridLength{0.0, GridUnitType::Auto});
    heading.ColumnDefinitions().Append(ColumnDefinition{});
    heading.ColumnDefinitions().GetAt(1).Width(GridLength{1.0, GridUnitType::Star});

    if (options.tone != DecisionTone::Neutral) {
        FontIcon status_icon;
        switch (options.tone) {
        case DecisionTone::Warning:
            status_icon.Glyph(L"\xE7BA");
            apply_icon_style(status_icon, L"WarningIconStyle");
            break;
        case DecisionTone::Error:
            status_icon.Glyph(L"\xEB90");
            apply_icon_style(status_icon, L"ErrorIconStyle");
            break;
        case DecisionTone::Neutral:
        default:
            break;
        }
        status_icon.FontSize(token_double(L"DecisionStatusIconSize", 20));
        status_icon.VerticalAlignment(VerticalAlignment::Center);
        status_icon.IsHitTestVisible(false);
        AutomationProperties::SetAccessibilityView(
            status_icon,
            Microsoft::UI::Xaml::Automation::Peers::AccessibilityView::Raw);
        heading.Children().Append(status_icon);
    }

    TextBlock title;
    title.Text(hstring(options.title));
    title.FontSize(token_double(L"SubtitleFontSize", 20));
    title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    title.TextWrapping(TextWrapping::Wrap);
    Grid::SetColumn(title, options.tone == DecisionTone::Neutral ? 0 : 1);
    if (options.tone == DecisionTone::Neutral) Grid::SetColumnSpan(title, 2);
    heading.Children().Append(title);
    content.Children().Append(heading);

    TextBlock message;
    message.Text(hstring(options.message));
    message.FontSize(token_double(L"BodyFontSize", 14));
    message.TextWrapping(TextWrapping::Wrap);
    content.Children().Append(message);

    if (!options.detail.empty()) {
        TextBlock detail;
        detail.Text(hstring(options.detail));
        detail.FontSize(token_double(L"CaptionFontSize", 12));
        detail.TextWrapping(TextWrapping::Wrap);
        detail.IsTextSelectionEnabled(true);
        apply_text_style(detail, L"SecondaryTextStyle");
        content.Children().Append(detail);
    }

    CheckBox verification;
    if (!options.verification_label.empty()) {
        verification.Content(box_value(hstring(options.verification_label)));
        content.Children().Append(verification);
    }

    StackPanel actions;
    actions.Orientation(Orientation::Horizontal);
    actions.HorizontalAlignment(HorizontalAlignment::Right);
    actions.Spacing(token_double(L"DecisionActionSpacing", 8));

    Button primary;
    primary.Content(box_value(hstring(options.primary_label)));
    primary.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
    AutomationProperties::SetName(primary, hstring(options.primary_label));

    Button secondary;
    secondary.Content(box_value(hstring(options.secondary_label)));
    AutomationProperties::SetName(secondary, hstring(options.secondary_label));

    Button cancel;
    if (options.include_cancel) {
        cancel.Content(box_value(hstring(options.cancel_label)));
        AutomationProperties::SetName(cancel, hstring(options.cancel_label));
    }

    auto complete = [state, verification](DecisionChoice choice) noexcept {
        bool checked = false;
        try { checked = verification && verification.IsChecked().GetBoolean(); } catch (...) {}
        state->complete(choice, checked);
    };
    primary.Click([complete](auto const&, auto const&) { complete(DecisionChoice::Primary); });
    secondary.Click([complete](auto const&, auto const&) { complete(DecisionChoice::Secondary); });
    if (options.include_cancel) cancel.Click([complete](auto const&, auto const&) { complete(DecisionChoice::Cancel); });

    actions.Children().Append(primary);
    actions.Children().Append(secondary);
    if (options.include_cancel) actions.Children().Append(cancel);
    content.Children().Append(actions);

    ScrollViewer content_viewport;
    content_viewport.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    content_viewport.HorizontalScrollMode(ScrollMode::Disabled);
    content_viewport.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    content_viewport.VerticalScrollMode(ScrollMode::Auto);
    content_viewport.Content(content);
    Grid::SetRow(content_viewport, 1);
    root.Children().Append(content_viewport);

    KeyboardAccelerator escape;
    escape.Key(Windows::System::VirtualKey::Escape);
    escape.Invoked([complete](auto const&, KeyboardAcceleratorInvokedEventArgs const& args) {
        args.Handled(true);
        complete(DecisionChoice::Cancel);
    });
    root.KeyboardAccelerators().Append(escape);

    // Loaded is the first point where WinUI templates/theme resources have been applied
    // and the dialog's own XamlRoot (monitor scale) is known. Resize again there so
    // Button/CheckBox desired sizes cannot be clipped.
    root.Loaded([primary, root, dialog, actions, content_padding, owner = options.owner](auto const&, auto const&) {
        fit_dialog_to_content(dialog, root, actions, content_padding, owner);
        (void)primary.Focus(FocusState::Programmatic);
    });

    dialog.Content(root);
    try { dialog.SystemBackdrop(MicaBackdrop{}); } catch (...) {}
    dialog.ExtendsContentIntoTitleBar(true);
    dialog.SetTitleBar(title_bar);

    HWND dialog_hwnd{};
    try {
        auto native = dialog.as<::IWindowNative>();
        (void)native->get_WindowHandle(&dialog_hwnd);
        if (dialog_hwnd && options.owner) {
            SetWindowLongPtrW(dialog_hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(options.owner));
        }
        auto app_window = dialog.AppWindow();
        if (auto presenter = app_window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
            presenter.IsMinimizable(false);
            presenter.IsMaximizable(false);
            presenter.IsResizable(false);
        }
        app_window.SetIcon(velocitycopy::ui::application_icon_path());

        fit_dialog_to_content(dialog, root, actions, content_padding, options.owner);
    } catch (...) {}

    dialog.Closed([state](auto const&, auto const&) { state->complete(DecisionChoice::Cancel, false); });
    dialog.Activate();
    center_owned_window(options.owner, dialog_hwnd);

    co_await resume_on_signal(state->event.get());
    co_await ui_context;
    try { dialog.Close(); } catch (...) {}
    co_return state->result.load();
}

Windows::Foundation::IAsyncOperation<std::uint32_t> await_decision(
    Windows::Foundation::IAsyncOperation<std::uint32_t> operation) {
    try {
        co_return co_await operation;
    } catch (const hresult_canceled&) {
        co_return encode_decision({DecisionChoice::Cancel, false});
    }
}

} // namespace velocitycopy::ui
