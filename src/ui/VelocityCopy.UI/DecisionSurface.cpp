#include "pch.h"
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
    std::atomic<std::int32_t> choice{static_cast<std::int32_t>(DecisionChoice::Cancel)};
    std::atomic_bool verification{};

    void complete(DecisionChoice value, bool checked) noexcept {
        bool expected = false;
        if (!completed.compare_exchange_strong(expected, true)) return;
        choice.store(static_cast<std::int32_t>(value));
        verification.store(checked);
        SetEvent(event.get());
    }
};

void center_owned_window(HWND owner, HWND dialog) noexcept {
    if (!owner || !dialog) return;
    RECT owner_rect{}, dialog_rect{};
    if (!GetWindowRect(owner, &owner_rect) || !GetWindowRect(dialog, &dialog_rect)) return;
    const int width = dialog_rect.right - dialog_rect.left;
    const int height = dialog_rect.bottom - dialog_rect.top;
    const int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
    const int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
    SetWindowPos(dialog, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}
} // namespace

IAsyncOperation<std::int32_t> show_decision_async(
    const DecisionOptions& options,
    bool* verification_checked) {
    if (verification_checked) *verification_checked = false;
    const auto ui_context = apartment_context{};
    auto state = std::make_shared<CompletionState>();
    if (!state->event) co_return static_cast<std::int32_t>(DecisionChoice::Cancel);

    Window dialog;
    dialog.Title(hstring(options.title));

    StackPanel root;
    root.Spacing(token_double(L"DecisionContentSpacing", 12));
    root.Padding(token_thickness(L"DecisionContentPadding", Thickness{24, 20, 24, 20}));

    TextBlock title;
    title.Text(hstring(options.title));
    title.FontSize(token_double(L"SubtitleFontSize", 20));
    title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    title.TextWrapping(TextWrapping::Wrap);
    root.Children().Append(title);

    TextBlock message;
    message.Text(hstring(options.message));
    message.FontSize(token_double(L"BodyFontSize", 14));
    message.TextWrapping(TextWrapping::Wrap);
    root.Children().Append(message);

    if (!options.detail.empty()) {
        TextBlock detail;
        detail.Text(hstring(options.detail));
        detail.FontSize(token_double(L"CaptionFontSize", 12));
        detail.TextWrapping(TextWrapping::Wrap);
        apply_text_style(detail, L"SecondaryTextStyle");
        root.Children().Append(detail);
    }

    CheckBox verification;
    if (!options.verification_label.empty()) {
        verification.Content(box_value(hstring(options.verification_label)));
        root.Children().Append(verification);
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
    root.Children().Append(actions);

    root.KeyDown([complete](auto const&, KeyRoutedEventArgs const& args) {
        if (args.Key() == Windows::System::VirtualKey::Escape) {
            args.Handled(true);
            complete(DecisionChoice::Cancel);
        } else if (args.Key() == Windows::System::VirtualKey::Enter) {
            args.Handled(true);
            complete(DecisionChoice::Primary);
        }
    });

    dialog.Content(root);
    try { dialog.SystemBackdrop(MicaBackdrop{}); } catch (...) {}

    HWND dialog_hwnd{};
    try {
        auto native = dialog.as<::IWindowNative>();
        (void)native->get_WindowHandle(&dialog_hwnd);
        if (dialog_hwnd && options.owner) SetWindowLongPtrW(dialog_hwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(options.owner));
        auto app_window = dialog.AppWindow();
        if (auto presenter = app_window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
            presenter.IsMinimizable(false);
            presenter.IsMaximizable(false);
            presenter.IsResizable(false);
        }
        app_window.SetIcon(L"Assets\\VelocityCopy.ico");
        const UINT dpi = options.owner ? GetDpiForWindow(options.owner) : USER_DEFAULT_SCREEN_DPI;
        app_window.Resize(Windows::Graphics::SizeInt32{
            MulDiv(token_int(L"DecisionWindowWidth", 440), dpi ? static_cast<int>(dpi) : 96, 96),
            MulDiv(token_int(L"DecisionWindowHeight", 250), dpi ? static_cast<int>(dpi) : 96, 96)});
    } catch (...) {}

    dialog.Closed([state](auto const&, auto const&) { state->complete(DecisionChoice::Cancel, false); });
    dialog.Activate();
    center_owned_window(options.owner, dialog_hwnd);
    primary.Focus(FocusState::Programmatic);

    co_await resume_on_signal(state->event.get());
    co_await ui_context;
    try { dialog.Close(); } catch (...) {}
    if (verification_checked) *verification_checked = state->verification.load();
    co_return state->choice.load();
}

} // namespace velocitycopy::ui
