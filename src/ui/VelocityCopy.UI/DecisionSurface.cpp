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

    StackPanel root;
    root.Spacing(0);

    Border title_bar;
    title_bar.Height(token_double(L"AboutTitleBarHeight", 32));
    root.Children().Append(title_bar);

    StackPanel content;
    content.Spacing(token_double(L"DecisionContentSpacing", 12));
    content.Padding(token_thickness(L"DecisionContentPadding", Thickness{24, 20, 24, 20}));

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
    root.Children().Append(content);

    KeyboardAccelerator escape;
    escape.Key(Windows::System::VirtualKey::Escape);
    escape.Invoked([complete](auto const&, KeyboardAcceleratorInvokedEventArgs const& args) {
        args.Handled(true);
        complete(DecisionChoice::Cancel);
    });
    root.KeyboardAccelerators().Append(escape);

    // Loaded is the first point where WinUI templates/theme resources have been applied.
    // Resize again there so Button/CheckBox desired sizes cannot be clipped.
    root.Loaded([primary, root, dialog, owner = options.owner](auto const&, auto const&) {
        try {
            const int width_epx = token_int(L"DecisionWindowWidth", 440);
            root.Measure(Windows::Foundation::Size{
                static_cast<float>(width_epx),
                std::numeric_limits<float>::infinity()});
            const int height_epx = static_cast<int>(std::ceil(root.DesiredSize().Height));
            const UINT dpi = owner ? GetDpiForWindow(owner) : USER_DEFAULT_SCREEN_DPI;
            const int effective_dpi = dpi ? static_cast<int>(dpi) : USER_DEFAULT_SCREEN_DPI;
            dialog.AppWindow().Resize(Windows::Graphics::SizeInt32{
                MulDiv(width_epx, effective_dpi, USER_DEFAULT_SCREEN_DPI),
                MulDiv(height_epx, effective_dpi, USER_DEFAULT_SCREEN_DPI)});
            HWND loaded_hwnd{};
            auto native = dialog.as<::IWindowNative>();
            if (SUCCEEDED(native->get_WindowHandle(&loaded_hwnd))) {
                center_owned_window(owner, loaded_hwnd);
            }
        } catch (...) {}
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

        const UINT dpi = options.owner ? GetDpiForWindow(options.owner) : USER_DEFAULT_SCREEN_DPI;
        const int effective_dpi = dpi ? static_cast<int>(dpi) : USER_DEFAULT_SCREEN_DPI;
        const int width_epx = token_int(L"DecisionWindowWidth", 440);
        root.Measure(Windows::Foundation::Size{static_cast<float>(width_epx), std::numeric_limits<float>::infinity()});
        const int height_epx = static_cast<int>(std::ceil(root.DesiredSize().Height));
        app_window.Resize(Windows::Graphics::SizeInt32{
            MulDiv(width_epx, effective_dpi, USER_DEFAULT_SCREEN_DPI),
            MulDiv(height_epx, effective_dpi, USER_DEFAULT_SCREEN_DPI)});
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
