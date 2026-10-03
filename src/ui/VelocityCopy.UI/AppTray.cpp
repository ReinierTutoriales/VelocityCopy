#include "pch.h"
#include "AppTray.h"
#include "Localization.h"
#include "App.xaml.h"

#include <shlobj_core.h>
#include <shellscalingapi.h>

namespace winrt::VelocityCopyUI::implementation {
namespace {
constexpr wchar_t kTrayWindowClass[] = L"VelocityCopy.AppTrayWindow";
constexpr UINT kTrayCallbackMessage = WM_APP + 0x51;
constexpr UINT kTrayIconId = 1;
}

AppTray::~AppTray() { Remove(); }

bool AppTray::Initialize(App* owner) noexcept {
    if (hwnd_ != nullptr) return true;
    owner_ = owner;
    try {
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &AppTray::WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = kTrayWindowClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        // Hidden top-level owner. Message-only windows do not receive broadcast messages,
        // so they cannot observe TaskbarCreated after Explorer starts/restarts. A real
        // top-level owner is also required by the standard notification-area menu pattern.
        hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kTrayWindowClass, L"VelocityCopy", WS_POPUP,
                                0, 0, 0, 0, nullptr, nullptr, instance, this);
        if (!hwnd_) return false;

        std::array<wchar_t, 32768> module_path{};
        SHFILEINFOW shell_info{};
        const DWORD length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
        if (length && length < module_path.size() &&
            SHGetFileInfoW(module_path.data(), FILE_ATTRIBUTE_NORMAL, &shell_info, sizeof(shell_info),
                           SHGFI_ICON | SHGFI_SMALLICON)) icon_ = shell_info.hIcon;
        if (!icon_) icon_ = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));

        data_ = {};
        data_.cbSize = sizeof(data_);
        data_.hWnd = hwnd_;
        data_.uID = kTrayIconId;
        data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data_.uCallbackMessage = kTrayCallbackMessage;
        data_.hIcon = icon_;
        wcscpy_s(data_.szTip, L"VelocityCopy");
        RestoreIcon();
        taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
        return added_;
    } catch (...) { Remove(); return false; }
}

void AppTray::RestoreIcon() noexcept {
    data_.uVersion = 0;
    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    v4_ = false;
    if (added_) {
        data_.uVersion = NOTIFYICON_VERSION_4;
        v4_ = Shell_NotifyIconW(NIM_SETVERSION, &data_) != FALSE;
    }
}

void AppTray::Remove() noexcept {
    if (added_) (void)Shell_NotifyIconW(NIM_DELETE, &data_);
    added_ = false; v4_ = false;
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    if (icon_) DestroyIcon(icon_);
    icon_ = nullptr;
    data_ = {};
    HideMenuSurface();
    menu_flyout_ = nullptr;
    menu_anchor_ = nullptr;
    menu_window_ = nullptr;
    owner_ = nullptr;
    open_dispatch_active_ = false;
}

LRESULT CALLBACK AppTray::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<AppTray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<AppTray*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(hwnd, message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

void AppTray::OpenPrimaryWindow() noexcept {
    if (open_dispatch_active_ || owner_ == nullptr) return;

    // Shell_NotifyIcon can deliver more than one activation notification for one
    // physical click (for example WM_LBUTTONUP plus NIN_SELECT). Creating a WinUI
    // Window can re-enter the native message pump before App has registered it in
    // windows_, so a nested activation used to observe an empty registry and create
    // a second idle copier. Keep the whole App::ShowPrimaryWindow call guarded.
    open_dispatch_active_ = true;
    try {
        owner_->ShowPrimaryWindow();
    } catch (...) {
    }
    open_dispatch_active_ = false;
}

LRESULT AppTray::HandleMessage(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (message == taskbar_created_message_ && taskbar_created_message_ != 0) {
        RestoreIcon(); return 0;
    }
    if (message == kTrayCallbackMessage) {
        UINT notification = v4_ ? LOWORD(lparam) : static_cast<UINT>(lparam);
        UINT icon_id = v4_ ? HIWORD(lparam) : static_cast<UINT>(wparam);
        POINT anchor{-1, -1};
        if (v4_) { anchor.x = static_cast<short>(LOWORD(wparam)); anchor.y = static_cast<short>(HIWORD(wparam)); }
        if (icon_id == kTrayIconId) {
            if (notification == WM_LBUTTONUP || notification == WM_LBUTTONDBLCLK ||
                notification == NIN_SELECT || notification == NIN_KEYSELECT) {
                OpenPrimaryWindow(); return 0;
            }
            if ((v4_ && notification == WM_CONTEXTMENU) ||
                (!v4_ && notification == WM_RBUTTONUP)) {
                ShowMenu(anchor); return 0;
            }
        }
    }
    // Use the HWND supplied by Windows: hwnd_ is assigned only after CreateWindowExW returns.
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool AppTray::EnsureMenuSurface() noexcept {
    if (menu_window_) return true;
    try {
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;

        Window window;
        Grid anchor;
        anchor.Width(32);
        anchor.Height(32);
        window.Content(anchor);

        auto app_window = window.AppWindow();
        app_window.IsShownInSwitchers(false);
        if (auto presenter = app_window.Presenter().try_as<Microsoft::UI::Windowing::OverlappedPresenter>()) {
            presenter.SetBorderAndTitleBar(false, false);
            presenter.IsResizable(false);
            presenter.IsMinimizable(false);
            presenter.IsMaximizable(false);
        }
        app_window.Resize(Windows::Graphics::SizeInt32{32, 32});

        window.Closed([this](auto const&, auto const&) {
            menu_open_ = false;
            menu_flyout_ = nullptr;
            menu_anchor_ = nullptr;
            menu_window_ = nullptr;
        });
        menu_window_ = window;
        menu_anchor_ = anchor;
        return true;
    } catch (...) {
        menu_window_ = nullptr;
        menu_anchor_ = nullptr;
        return false;
    }
}

void AppTray::HideMenuSurface() noexcept {
    menu_open_ = false;
    try {
        if (menu_flyout_) menu_flyout_.Hide();
        if (menu_window_) {
            HWND menu_hwnd{};
            auto native = menu_window_.as<::IWindowNative>();
            if (SUCCEEDED(native->get_WindowHandle(&menu_hwnd)) && menu_hwnd) ShowWindow(menu_hwnd, SW_HIDE);
        }
    } catch (...) {}
}

void AppTray::ShowMenu(POINT anchor) noexcept {
    if (!hwnd_ || menu_open_ || !EnsureMenuSurface()) return;
    try {
        using namespace Microsoft::UI::Xaml;
        using namespace Microsoft::UI::Xaml::Controls;

        if (anchor.x == -1 && anchor.y == -1) GetCursorPos(&anchor);

        HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitor_info{sizeof(monitor_info)};
        if (!GetMonitorInfoW(monitor, &monitor_info)) return;
        constexpr LONG kAnchorDip = 32;
        HWND menu_hwnd{};
        auto native = menu_window_.as<::IWindowNative>();
        if (FAILED(native->get_WindowHandle(&menu_hwnd)) || !menu_hwnd) return;
        UINT dpi_x = USER_DEFAULT_SCREEN_DPI;
        UINT dpi_y = USER_DEFAULT_SCREEN_DPI;
        if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) {
            dpi_x = GetDpiForWindow(menu_hwnd);
        }
        const LONG anchor_px = MulDiv(kAnchorDip, static_cast<int>(dpi_x), USER_DEFAULT_SCREEN_DPI);
        const LONG left = std::clamp(anchor.x - anchor_px / 2,
            monitor_info.rcWork.left, monitor_info.rcWork.right - anchor_px);
        const LONG top = std::clamp(anchor.y - anchor_px,
            monitor_info.rcWork.top, monitor_info.rcWork.bottom - anchor_px);

        auto app_window = menu_window_.AppWindow();
        app_window.Move(Windows::Graphics::PointInt32{left, top});
        app_window.Resize(Windows::Graphics::SizeInt32{anchor_px, anchor_px});
        menu_window_.Activate();

        std::wstring open_text = L"Open VelocityCopy", exit_text = L"Exit";
        try {
            open_text = velocitycopy::localization::get_string(L"TrayOpen").c_str();
            exit_text = velocitycopy::localization::get_string(L"TrayExit").c_str();
        } catch (...) {}

        MenuFlyout flyout;
        MenuFlyoutItem open_item;
        open_item.Text(open_text);
        open_item.Click([this](auto const&, auto const&) { OpenPrimaryWindow(); });
        flyout.Items().Append(open_item);
        flyout.Items().Append(MenuFlyoutSeparator{});
        MenuFlyoutItem exit_item;
        exit_item.Text(exit_text);
        exit_item.Click([this](auto const&, auto const&) {
            if (owner_) owner_->ExitFromTray();
        });
        flyout.Items().Append(exit_item);

        flyout.Closed([this](auto const&, auto const&) {
            menu_open_ = false;
            menu_flyout_ = nullptr;
            try {
                if (menu_window_) {
                    HWND menu_hwnd{};
                    auto native = menu_window_.as<::IWindowNative>();
                    if (SUCCEEDED(native->get_WindowHandle(&menu_hwnd)) && menu_hwnd) ShowWindow(menu_hwnd, SW_HIDE);
                }
            } catch (...) {}
        });

        Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowOptions show_options;
        auto placement = Microsoft::UI::Xaml::Controls::Primitives::FlyoutPlacementMode::Top;
        if (monitor_info.rcWork.left > monitor_info.rcMonitor.left) placement = Microsoft::UI::Xaml::Controls::Primitives::FlyoutPlacementMode::Right;
        else if (monitor_info.rcWork.right < monitor_info.rcMonitor.right) placement = Microsoft::UI::Xaml::Controls::Primitives::FlyoutPlacementMode::Left;
        else if (monitor_info.rcWork.top > monitor_info.rcMonitor.top) placement = Microsoft::UI::Xaml::Controls::Primitives::FlyoutPlacementMode::Bottom;
        show_options.Placement(placement);
        show_options.Position(Windows::Foundation::Point{16.0f, 16.0f});

        menu_open_ = true;
        menu_flyout_ = flyout;
        flyout.ShowAt(menu_anchor_, show_options);
    } catch (...) {
        HideMenuSurface();
    }
}

} // namespace winrt::VelocityCopyUI::implementation
