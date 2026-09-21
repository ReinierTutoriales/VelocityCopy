#include "pch.h"
#include "MainWindow.xaml.h"
#include "Version.h"

#include <winver.h>

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
    Microsoft::Windows::ApplicationModel::Resources::ResourceLoader const& loader,
    wchar_t const* key,
    wchar_t const* fallback) {
    try {
        const auto value = loader.GetString(key);
        return value.empty() ? hstring(fallback) : value;
    } catch (...) {
        return hstring(fallback);
    }
}

} // namespace

void MainWindow::ShowAboutDialog() noexcept {
    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        const auto title = resource_or(loader, L"AboutTitle", L"VelocityCopy");
        const auto tagline = resource_or(
            loader,
            L"AboutTagline",
            L"Fast, focused file transfers for Windows 11.");
        const auto version_format = resource_or(loader, L"AboutVersionFormat", L"Version {0}");
        const auto publisher = resource_or(loader, L"AboutPublisher", L"ReinierTutoriales");
        const auto license = resource_or(loader, L"AboutLicense", L"MIT License");
        const auto repository_label = resource_or(loader, L"AboutRepositoryLabel", L"View project on GitHub");
        const auto version = executable_version();

        Flyout about;
        about.Placement(FlyoutPlacementMode::Bottom);

        StackPanel panel;
        panel.Width(300);
        panel.Spacing(12);
        panel.Padding(Thickness{16, 14, 16, 14});

        Border accent;
        accent.Height(3);
        accent.CornerRadius(CornerRadius{2});
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
        header.Spacing(12);

        Image logo;
        logo.Width(40);
        logo.Height(40);
        logo.Stretch(Stretch::Uniform);
        logo.Source(BitmapImage{Uri{L"ms-appx:///Assets/VelocityCopy.png"}});
        header.Children().Append(logo);

        StackPanel identity;
        identity.VerticalAlignment(VerticalAlignment::Center);
        identity.Spacing(2);

        TextBlock product;
        product.Text(L"VelocityCopy");
        product.FontSize(19);
        product.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        identity.Children().Append(product);

        TextBlock version_text;
        version_text.Text(replace_version_token(std::wstring(version_format.c_str()), version));
        version_text.FontSize(12);
        version_text.Opacity(0.66);
        identity.Children().Append(version_text);

        header.Children().Append(identity);
        panel.Children().Append(header);

        TextBlock description;
        description.Text(tagline);
        description.TextWrapping(TextWrapping::Wrap);
        description.FontSize(12.5);
        description.Opacity(0.82);
        panel.Children().Append(description);

        TextBlock metadata;
        metadata.Text(hstring(
            std::wstring(publisher.c_str()) + L"  •  " + std::wstring(license.c_str())));
        metadata.FontSize(11.5);
        metadata.Opacity(0.62);
        panel.Children().Append(metadata);

        HyperlinkButton repository;
        repository.Content(box_value(repository_label));
        repository.NavigateUri(Uri{kRepositoryUrl});
        repository.HorizontalAlignment(HorizontalAlignment::Left);
        repository.Padding(Thickness{0});
        panel.Children().Append(repository);

        about.Content(panel);
        about.ShowAt(OptionsButton());
        return;
    } catch (...) {
    }

    // Last-resort fallback only. The normal About experience is a themed WinUI
    // flyout; this path prevents a broken auxiliary surface from affecting copies.
    try {
        const auto version = executable_version();
        const auto message = std::format(
            L"VelocityCopy {}\nReinierTutoriales · MIT License\n{}",
            version,
            kRepositoryUrl);
        MessageBoxW(hwnd_, message.c_str(), L"VelocityCopy", MB_OK | MB_ICONINFORMATION);
    } catch (...) {
    }
}

void MainWindow::OnAboutClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) {
    ShowAboutDialog();
}

} // namespace winrt::VelocityCopyUI::implementation
