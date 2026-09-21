#include "pch.h"
#include "MainWindow.xaml.h"

#include <commctrl.h>
#include <shellapi.h>
#include <winver.h>

using namespace winrt;

namespace winrt::VelocityCopyUI::implementation {
namespace {

constexpr std::wstring_view kRepositoryUrl = L"https://github.com/ReinierTutoriales/VelocityCopy";

std::wstring executable_path() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return {};
    path.resize(length);
    return path;
}

std::wstring executable_version() noexcept {
    try {
        const auto path = executable_path();
        if (path.empty()) return L"Unknown";

        DWORD ignored = 0;
        const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
        if (size == 0) return L"Unknown";

        std::vector<std::byte> data(size);
        if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return L"Unknown";

        VS_FIXEDFILEINFO* info = nullptr;
        UINT info_size = 0;
        if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &info_size) ||
            info == nullptr || info_size < sizeof(VS_FIXEDFILEINFO) ||
            info->dwSignature != 0xFEEF04BD) {
            return L"Unknown";
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
        return L"Unknown";
    }
}

std::wstring format_about_body(std::wstring format, const std::wstring& version) {
    constexpr std::wstring_view token = L"{0}";
    const auto position = format.find(token);
    if (position != std::wstring::npos) {
        format.replace(position, token.size(), version);
    }
    return format;
}

std::wstring with_repository_hyperlink(std::wstring body) {
    const auto position = body.find(kRepositoryUrl);
    if (position == std::wstring::npos) return body;

    std::wstring link = L"<a href=\"";
    link.append(kRepositoryUrl);
    link.append(L"\">");
    link.append(kRepositoryUrl);
    link.append(L"</a>");
    body.replace(position, kRepositoryUrl.size(), link);
    return body;
}

HRESULT CALLBACK about_dialog_callback(
    HWND,
    UINT notification,
    WPARAM,
    LPARAM lparam,
    LONG_PTR) noexcept {
    if (notification == TDN_HYPERLINK_CLICKED && lparam != 0) {
        ShellExecuteW(nullptr, L"open", reinterpret_cast<LPCWSTR>(lparam), nullptr, nullptr, SW_SHOWNORMAL);
    }
    return S_OK;
}

} // namespace

void MainWindow::ShowAboutDialog() noexcept {
    try {
        std::wstring title = L"About VelocityCopy";
        std::wstring body_format =
            L"Version {0}\n\nReinierTutoriales\nMIT License\nhttps://github.com/ReinierTutoriales/VelocityCopy";
        try {
            Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
            title = loader.GetString(L"AboutTitle").c_str();
            body_format = loader.GetString(L"AboutBodyFormat").c_str();
        } catch (...) {
        }

        const auto body = format_about_body(std::move(body_format), executable_version());
        const auto rich_body = with_repository_hyperlink(body);

        HMODULE module = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module != nullptr) {
            using TaskDialogIndirectFn = HRESULT (WINAPI*)(
                const TASKDIALOGCONFIG*, int*, int*, BOOL*);
            auto task_dialog = reinterpret_cast<TaskDialogIndirectFn>(
                GetProcAddress(module, "TaskDialogIndirect"));
            if (task_dialog != nullptr) {
                TASKDIALOGCONFIG config{};
                config.cbSize = sizeof(config);
                config.hwndParent = hwnd_;
                config.dwFlags = TDF_SIZE_TO_CONTENT | TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION;
                config.dwCommonButtons = TDCBF_CLOSE_BUTTON;
                config.pszWindowTitle = L"VelocityCopy";
                config.pszMainInstruction = title.c_str();
                config.pszContent = rich_body.c_str();
                config.pfCallback = about_dialog_callback;
                const HRESULT hr = task_dialog(&config, nullptr, nullptr, nullptr);
                FreeLibrary(module);
                if (SUCCEEDED(hr)) return;
            } else {
                FreeLibrary(module);
            }
        }

        MessageBoxW(hwnd_, body.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
    } catch (...) {
    }
}

void MainWindow::OnAboutClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&) {
    ShowAboutDialog();
}

} // namespace winrt::VelocityCopyUI::implementation
