#include "pch.h"
#include "IconAssets.h"
#include "MainWindow.xaml.h"
#include "Localization.h"
#include "UiTokens.h"
#include "Version.h"

#include <winver.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Imaging;

namespace winrt::VelocityCopyUI::implementation {
namespace {

constexpr wchar_t kRepositoryUrl[] = L"https://github.com/ReinierTutoriales/VelocityCopy";

std::wstring compiled_version() {
    if constexpr (VELOCITYCOPY_VERSION_BUILD == 0) {
        return std::format(
            L"{}.{}.{}",
            VELOCITYCOPY_VERSION_MAJOR,
            VELOCITYCOPY_VERSION_MINOR,
            VELOCITYCOPY_VERSION_PATCH);
    }
    return std::format(
        L"{}.{}.{}.{}",
        VELOCITYCOPY_VERSION_MAJOR,
        VELOCITYCOPY_VERSION_MINOR,
        VELOCITYCOPY_VERSION_PATCH,
        VELOCITYCOPY_VERSION_BUILD);
}

std::wstring executable_path() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return path;
}

std::wstring executable_version() noexcept {
    const auto fallback = compiled_version();
    try {
        const auto path = executable_path();
        if (path.empty()) return fallback;

        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
        if (size == 0) return fallback;

        std::vector<std::byte> data(size);
        if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return fallback;

        VS_FIXEDFILEINFO* info = nullptr;
        UINT info_size = 0;
        if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &info_size) ||
            info == nullptr || info_size < sizeof(VS_FIXEDFILEINFO) ||
            info->dwSignature != 0xFEEF04BD) {
            return fallback;
        }

        const auto major = HIWORD(info->dwFileVersionMS);
        const auto minor = LOWORD(info->dwFileVersionMS);
        const auto patch = HIWORD(info->dwFileVersionLS);
        const auto build = LOWORD(info->dwFileVersionLS);
        if (build == 0) {
            return std::format(L"{}.{}.{}", major, minor, patch);
        }
        return std::format(L"{}.{}.{}.{}", major, minor, patch, build);
    } catch (...) {
        return fallback;
    }
}

std::wstring replace_version_token(std::wstring format, const std::wstring& version) {
    constexpr std::wstring_view token = L"{0}";
    const auto position = format.find(token);
    if (position != std::wstring::npos) {
        format.replace(position, token.size(), version);
    }
    return format;
}

hstring resource_or(
    wchar_t const* key,
    wchar_t const* fallback) {
    try {
        const auto value = velocitycopy::localization::get_string(key);
        return value.empty() ? hstring(fallback) : value;
    } catch (...) {
        return hstring(fallback);
    }
}

} // namespace

void MainWindow::ShowAboutDialog() noexcept {
    try {
        if (about_window_) {
            about_window_.Activate();
            try {
                HWND about_hwnd{};
                auto native = about_window_.as<::IWindowNative>();
                if (SUCCEEDED(native->get_WindowHandle(&about_hwnd)) && about_hwnd != nullptr) {
                    ShowWindow(about_hwnd, SW_RESTORE);
                    SetForegroundWindow(about_hwnd);
                }
            } catch (...) {
            }
            return;
        }

        const auto title = resource_or(L"AboutTitle", L"VelocityCopy");
        const auto tagline = resource_or(L"AboutTagline",
            L"Fast, focused file transfers for Windows 11.");
        const auto version_format = resource_or(L"AboutVersionFormat", L"Version {0}");
        const auto publisher = resource_or(L"AboutPublisher", L"ReinierTutoriales");
        const auto license = resource_or(L"AboutLicense", L"MIT License");
        const auto repository_label = resource_or(L"AboutRepositoryLabel", L"View project on GitHub");
        const auto version = executable_version();

        Window about;
        about.Title(title);

        StackPanel root;
        root.Spacing(0);
        try { root.RequestedTheme(RootGrid().ActualTheme()); } catch (...) {}

        Border title_bar;
        title_bar.Height(velocitycopy::ui::token_double(L"AboutTitleBarHeight", 32));
        title_bar.Padding(velocitycopy::ui::token_thickness(L"AboutTitleBarPadding", Thickness{12, 0, 112, 0}));

        StackPanel title_identity;
        title_identity.Orientation(Orientation::Horizontal);
        title_identity.Spacing(velocitycopy::ui::token_double(L"AboutTitleIdentitySpacing", 8));
        title_identity.VerticalAlignment(VerticalAlignment::Center);

        Image title_logo;
        title_logo.Width(velocitycopy::ui::token_double(L"AboutTitleLogoSize", 16));
        title_logo.Height(velocitycopy::ui::token_double(L"AboutTitleLogoSize", 16));
        title_logo.Stretch(Stretch::Uniform);
        BitmapImage title_logo_source;
        title_logo_source.UriSource(Uri{L"ms-appx:///Assets/VelocityCopy.png"});
        title_logo.Source(title_logo_source);
        title_identity.Children().Append(title_logo);

        TextBlock title_text;
        title_text.Text(L"VelocityCopy");
        title_text.FontSize(velocitycopy::ui::token_double(L"CaptionFontSize", 12));
        title_text.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        title_text.VerticalAlignment(VerticalAlignment::Center);
        title_identity.Children().Append(title_text);
        title_bar.Child(title_identity);
        root.Children().Append(title_bar);

        StackPanel panel;
        panel.Spacing(0);
        panel.Padding(velocitycopy::ui::token_thickness(L"AboutPanelPadding", Thickness{24, 20, 24, 24}));

        Border accent;
        accent.Width(velocitycopy::ui::token_double(L"AboutAccentWidth", 48));
        accent.Height(velocitycopy::ui::token_double(L"AboutAccentHeight", 4));
        accent.HorizontalAlignment(HorizontalAlignment::Left);
        accent.CornerRadius(CornerRadius{velocitycopy::ui::token_double(L"AboutAccentRadius", 2)});
        accent.Margin(velocitycopy::ui::token_thickness(L"AboutAccentMargin", Thickness{0, 0, 0, 16}));
        try {
            accent.Background(
                Application::Current().Resources()
                    .Lookup(box_value(L"AccentFillColorDefaultBrush"))
                    .as<Brush>());
        } catch (...) {
        }
        panel.Children().Append(accent);

        StackPanel header;
        header.Orientation(Orientation::Horizontal);
        header.Spacing(velocitycopy::ui::token_double(L"AboutHeaderSpacing", 12));
        header.Margin(velocitycopy::ui::token_thickness(L"AboutHeaderMargin", Thickness{0, 0, 0, 16}));

        Image logo;
        logo.Width(velocitycopy::ui::token_double(L"AboutHeroLogoSize", 44));
        logo.Height(velocitycopy::ui::token_double(L"AboutHeroLogoSize", 44));
        logo.Stretch(Stretch::Uniform);
        BitmapImage logo_source;
        logo_source.UriSource(Uri{L"ms-appx:///Assets/VelocityCopy.png"});
        logo.Source(logo_source);
        header.Children().Append(logo);

        StackPanel identity;
        identity.VerticalAlignment(VerticalAlignment::Center);
        identity.Spacing(velocitycopy::ui::token_double(L"AboutIdentitySpacing", 4));

        TextBlock product;
        product.Text(title);
        product.FontSize(velocitycopy::ui::token_double(L"SubtitleFontSize", 20));
        product.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        identity.Children().Append(product);

        TextBlock version_text;
        version_text.Text(replace_version_token(std::wstring(version_format.c_str()), version));
        version_text.FontSize(velocitycopy::ui::token_double(L"CaptionFontSize", 12));
        velocitycopy::ui::apply_text_style(version_text, L"SecondaryTextStyle");
        identity.Children().Append(version_text);

        header.Children().Append(identity);
        panel.Children().Append(header);

        TextBlock description;
        description.Text(tagline);
        description.TextWrapping(TextWrapping::Wrap);
        description.FontSize(velocitycopy::ui::token_double(L"BodyFontSize", 14));
        description.LineHeight(velocitycopy::ui::token_double(L"AboutBodyLineHeight", 20));
        description.Margin(velocitycopy::ui::token_thickness(L"AboutBodyMargin", Thickness{0, 0, 0, 16}));
        panel.Children().Append(description);

        TextBlock metadata;
        metadata.Text(hstring(
            std::wstring(publisher.c_str()) + L"  •  " + std::wstring(license.c_str())));
        metadata.FontSize(velocitycopy::ui::token_double(L"CaptionFontSize", 12));
        velocitycopy::ui::apply_text_style(metadata, L"TertiaryTextStyle");
        metadata.Margin(velocitycopy::ui::token_thickness(L"AboutMetadataMargin", Thickness{0, 0, 0, 8}));
        panel.Children().Append(metadata);

        HyperlinkButton repository;
        repository.Content(box_value(repository_label));
        repository.NavigateUri(Uri{kRepositoryUrl});
        repository.HorizontalAlignment(HorizontalAlignment::Left);
        repository.FontSize(velocitycopy::ui::token_double(L"BodyFontSize", 14));
        repository.Padding(Thickness{0});
        panel.Children().Append(repository);

        root.Children().Append(panel);
        about.Content(root);

        try {
            about.SystemBackdrop(Microsoft::UI::Xaml::Media::MicaBackdrop{});
        } catch (...) {
        }

        // Use the same extended title-bar model as the compact transfer surface. This
        // gives About a real Windows caption close button and a draggable title region
        // instead of a transient Flyout that cannot be moved.
        about.ExtendsContentIntoTitleBar(true);
        about.SetTitleBar(title_bar);

        HWND about_hwnd{};
        try {
            auto native = about.as<::IWindowNative>();
            (void)native->get_WindowHandle(&about_hwnd);
        } catch (...) {
            about_hwnd = nullptr;
        }

        try {
            auto app_window = about.AppWindow();
            if (auto presenter = app_window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
                presenter.IsMinimizable(false);
                presenter.IsMaximizable(false);
                presenter.IsResizable(false);
            }
            app_window.SetIcon(velocitycopy::ui::application_icon_path());

            const UINT dpi = hwnd_ != nullptr ? GetDpiForWindow(hwnd_) : USER_DEFAULT_SCREEN_DPI;
            const int width = MulDiv(velocitycopy::ui::token_int(L"AboutWindowWidth", 388), dpi == 0 ? USER_DEFAULT_SCREEN_DPI : static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
            const int height = MulDiv(velocitycopy::ui::token_int(L"AboutWindowHeight", 288), dpi == 0 ? USER_DEFAULT_SCREEN_DPI : static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
            app_window.Resize(Windows::Graphics::SizeInt32{width, height});

            // About is an independent top-level auxiliary window. Do not attach or
            // position it relative to the compact transfer surface.
        } catch (...) {
        }

        auto weak = get_weak();
        about.Closed([weak](auto const&, auto const&) {
            if (auto self = weak.get()) self->about_window_ = nullptr;
        });
        about_window_ = about;
        about.Activate();
        if (about_hwnd != nullptr) SetForegroundWindow(about_hwnd);
        return;
    } catch (...) {
        about_window_ = nullptr;
    }

    // Last-resort fallback only. The normal About experience is now its own themed,
    // movable WinUI window; this path prevents an auxiliary-surface failure from
    // affecting active copies.
    try {
        const auto version = executable_version();
        const auto message = std::format(
            L"VelocityCopy {}\nReinierTutoriales · MIT License\n{}",
            version,
            kRepositoryUrl);
        MessageBoxW(nullptr, message.c_str(), L"VelocityCopy", MB_OK | MB_ICONINFORMATION);
    } catch (...) {
    }
}

void MainWindow::OnAboutClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) {
    ShowAboutDialog();
}

} // namespace winrt::VelocityCopyUI::implementation
