#include "pch.h"
#include "AppTray.h"
#include "Localization.h"
#include "App.xaml.h"
#include "IconResource.h"

#include <commctrl.h>
#include <shlobj_core.h>
#include <shellscalingapi.h>

namespace winrt::VelocityCopyUI::implementation {
namespace {
constexpr wchar_t kTrayWindowClass[] = L"VelocityCopy.AppTrayWindow";
constexpr UINT kTrayCallbackMessage = WM_APP + 0x51;
constexpr UINT kTrayIconId = 1;
constexpr UINT kOpenCommandId = 1;
constexpr UINT kExitCommandId = 2;
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

        data_ = {};
        data_.cbSize = sizeof(data_);
        data_.hWnd = hwnd_;
        data_.uID = kTrayIconId;
        data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data_.uCallbackMessage = kTrayCallbackMessage;
        wcscpy_s(data_.szTip, L"VelocityCopy");
        taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
        RestoreIcon();
        if (!added_) { Remove(); return false; }
        return added_;
    } catch (...) { Remove(); return false; }
}

bool AppTray::RefreshIcon() noexcept {
    if (!hwnd_) return false;
    UINT dpi = GetDpiForWindow(hwnd_);
    if (!dpi) dpi = USER_DEFAULT_SCREEN_DPI;

    // The hidden owner can be on a different monitor from the notification area.
    // Query the Shell's actual icon location rather than assuming primary-screen DPI.
    RECT rect{};
    NOTIFYICONIDENTIFIER identifier{sizeof(identifier)};
    identifier.hWnd = hwnd_;
    identifier.uID = kTrayIconId;
    if (SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &rect))) {
        DEVICE_SCALE_FACTOR scale = SCALE_100_PERCENT;
        if (SUCCEEDED(GetScaleFactorForMonitor(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &scale))) {
            dpi = static_cast<UINT>(MulDiv(USER_DEFAULT_SCREEN_DPI, static_cast<int>(scale), 100));
        }
    }

    HICON replacement{};
    const auto instance = GetModuleHandleW(nullptr);
    if (FAILED(LoadIconWithScaleDown(instance, MAKEINTRESOURCEW(IDI_APPICON),
            GetSystemMetricsForDpi(SM_CXSMICON, dpi), GetSystemMetricsForDpi(SM_CYSMICON, dpi), &replacement)) &&
        FAILED(LoadIconMetric(instance, MAKEINTRESOURCEW(IDI_APPICON), LIM_SMALL, &replacement))) return false;
    if (!replacement) return false;

    if (added_) {
        auto updated = data_;
        updated.uFlags = NIF_ICON;
        updated.hIcon = replacement;
        if (!Shell_NotifyIconW(NIM_MODIFY, &updated)) {
            DestroyIcon(replacement);
            return false; // Retain the previously published, owned icon on failure.
        }
    }
    const auto previous = icon_;
    icon_ = replacement;
    data_.hIcon = icon_;
    if (previous) DestroyIcon(previous);
    return true;
}

void AppTray::RestoreIcon() noexcept {
    added_ = false;
    v4_ = false;
    if (!RefreshIcon()) return;
    data_.uVersion = 0;
    added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
    if (added_) {
        data_.uVersion = NOTIFYICON_VERSION_4;
        v4_ = Shell_NotifyIconW(NIM_SETVERSION, &data_) != FALSE;
        // The Shell can provide the notification monitor only after NIM_ADD.
        (void)RefreshIcon();
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
    if (message == WM_DPICHANGED || message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE) {
        if (added_) (void)RefreshIcon();
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

void AppTray::ShowMenu(POINT anchor) noexcept {
    if (!hwnd_) return;

    RECT icon_rect{};
    NOTIFYICONIDENTIFIER identifier{sizeof(identifier)};
    identifier.hWnd = hwnd_;
    identifier.uID = kTrayIconId;
    if (FAILED(Shell_NotifyIconGetRect(&identifier, &icon_rect))) {
        if (anchor.x == -1 && anchor.y == -1 && !GetCursorPos(&anchor)) return;
        icon_rect = RECT{anchor.x, anchor.y, anchor.x + 1, anchor.y + 1};
    } else {
        anchor.x = icon_rect.left + (icon_rect.right - icon_rect.left) / 2;
        anchor.y = icon_rect.top + (icon_rect.bottom - icon_rect.top) / 2;
    }

    std::wstring open_text = L"Open VelocityCopy", exit_text = L"Exit";
    try {
        open_text = velocitycopy::localization::get_string(L"TrayOpen").c_str();
        exit_text = velocitycopy::localization::get_string(L"TrayExit").c_str();
    } catch (...) {}

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    if (!AppendMenuW(menu, MF_STRING, kOpenCommandId, open_text.c_str()) ||
        !AppendMenuW(menu, MF_SEPARATOR, 0, nullptr) ||
        !AppendMenuW(menu, MF_STRING, kExitCommandId, exit_text.c_str())) {
        DestroyMenu(menu);
        return;
    }
    SetMenuDefaultItem(menu, kOpenCommandId, FALSE);

    // Required notification-area shortcut-menu pattern: foreground ownership gives
    // correct click-away dismissal; WM_NULL prevents the next invocation from
    // immediately dismissing itself.
    if (!SetForegroundWindow(hwnd_)) {
        DestroyMenu(menu);
        return;
    }
    const UINT alignment = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    TPMPARAMS params{sizeof(params)};
    params.rcExclude = icon_rect;
    const UINT command = TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | alignment,
        anchor.x, anchor.y, hwnd_, &params);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (command == kOpenCommandId) OpenPrimaryWindow();
    else if (command == kExitCommandId && owner_) owner_->ExitFromTray();

    Shell_NotifyIconW(NIM_SETFOCUS, &data_);
}

} // namespace winrt::VelocityCopyUI::implementation
