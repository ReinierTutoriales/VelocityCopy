#include "pch.h"
#include "AppTray.h"
#include "App.xaml.h"

#include <shlobj_core.h>

namespace winrt::VelocityCopyUI::implementation {
namespace {
constexpr wchar_t kTrayWindowClass[] = L"VelocityCopy.AppTrayWindow";
constexpr UINT kTrayCallbackMessage = WM_APP + 0x51;
constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayOpenCommand = 1;
constexpr UINT kTrayExitCommand = 2;
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
        hwnd_ = CreateWindowExW(0, kTrayWindowClass, L"VelocityCopy", 0, 0, 0, 0, 0,
                                HWND_MESSAGE, nullptr, instance, this);
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
        data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
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
    owner_ = nullptr;
}

LRESULT CALLBACK AppTray::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<AppTray*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<AppTray*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT AppTray::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) noexcept {
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
                if (owner_) owner_->ShowPrimaryWindow(); return 0;
            }
            if (notification == WM_RBUTTONUP || notification == WM_CONTEXTMENU) {
                ShowMenu(anchor); return 0;
            }
        }
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

void AppTray::ShowMenu(POINT anchor) noexcept {
    if (!hwnd_) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    std::wstring open_text = L"Open VelocityCopy", exit_text = L"Exit";
    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        open_text = loader.GetString(L"TrayOpen").c_str();
        exit_text = loader.GetString(L"TrayExit").c_str();
    } catch (...) {}
    AppendMenuW(menu, MF_STRING, kTrayOpenCommand, open_text.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExitCommand, exit_text.c_str());
    if (anchor.x == -1 && anchor.y == -1) GetCursorPos(&anchor);
    SetForegroundWindow(hwnd_);
    const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                          anchor.x, anchor.y, hwnd_, nullptr);
    DestroyMenu(menu);
    if (command == kTrayOpenCommand && owner_) owner_->ShowPrimaryWindow();
    else if (command == kTrayExitCommand && owner_) owner_->ExitFromTray();
}

} // namespace winrt::VelocityCopyUI::implementation
