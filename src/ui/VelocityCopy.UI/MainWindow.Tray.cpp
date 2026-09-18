#include "pch.h"
#include "MainWindow.xaml.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj_core.h>

using namespace winrt;

namespace winrt::VelocityCopyUI::implementation {
namespace {

constexpr UINT kTrayCallbackMessage = WM_APP + 0x51;
constexpr UINT_PTR kTraySubclassId = 0x56434F50;
constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayOpenCommand = 1;
constexpr UINT kTrayExitCommand = 2;

UINT g_taskbar_created_message = 0;

} // namespace

MainWindow::~MainWindow() {
    RemoveTrayIntegration();
}

void MainWindow::InitializeTrayIntegration() {
    if (hwnd_ != nullptr) {
        return;
    }

    try {
        auto window_native = this->m_inner.as<::IWindowNative>();
        if (FAILED(window_native->get_WindowHandle(&hwnd_)) || hwnd_ == nullptr) {
            hwnd_ = nullptr;
            return;
        }

        if (!SetWindowSubclass(
                hwnd_,
                &MainWindow::TraySubclassProc,
                kTraySubclassId,
                reinterpret_cast<DWORD_PTR>(this))) {
            hwnd_ = nullptr;
            return;
        }

        (void)AddClipboardFormatListener(hwnd_);

        std::array<wchar_t, 32768> module_path{};
        SHFILEINFOW shell_info{};
        const DWORD module_length = GetModuleFileNameW(
            nullptr,
            module_path.data(),
            static_cast<DWORD>(module_path.size()));
        if (module_length != 0 && module_length < module_path.size() &&
            SHGetFileInfoW(
                module_path.data(),
                FILE_ATTRIBUTE_NORMAL,
                &shell_info,
                sizeof(shell_info),
                SHGFI_ICON | SHGFI_SMALLICON) != 0) {
            tray_icon_ = shell_info.hIcon;
        }
        if (tray_icon_ == nullptr) {
            tray_icon_ = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
        }

        tray_data_ = {};
        tray_data_.cbSize = sizeof(tray_data_);
        tray_data_.hWnd = hwnd_;
        tray_data_.uID = kTrayIconId;
        tray_data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        tray_data_.uCallbackMessage = kTrayCallbackMessage;
        tray_data_.hIcon = tray_icon_;
        wcscpy_s(tray_data_.szTip, L"VelocityCopy");

        tray_added_ = Shell_NotifyIconW(NIM_ADD, &tray_data_) != FALSE;
        if (tray_added_) {
            tray_data_.uVersion = NOTIFYICON_VERSION_4;
            tray_v4_ = Shell_NotifyIconW(NIM_SETVERSION, &tray_data_) != FALSE;
        }
        if (g_taskbar_created_message == 0) {
            g_taskbar_created_message = RegisterWindowMessageW(L"TaskbarCreated");
        }
        tray_window_hidden_ = IsWindowVisible(hwnd_) == FALSE;
        RefreshEfficiencyMode();
    } catch (...) {
        RemoveTrayIntegration();
    }
}

void MainWindow::RemoveTrayIntegration() noexcept {
    if (hwnd_ != nullptr) {
        (void)RemoveClipboardFormatListener(hwnd_);
    }

    if (tray_added_) {
        (void)Shell_NotifyIconW(NIM_DELETE, &tray_data_);
        tray_added_ = false;
    }
    tray_v4_ = false;
    SetEfficiencyMode(false);

    if (hwnd_ != nullptr) {
        (void)RemoveWindowSubclass(hwnd_, &MainWindow::TraySubclassProc, kTraySubclassId);
    }

    if (tray_icon_ != nullptr) {
        DestroyIcon(tray_icon_);
        tray_icon_ = nullptr;
    }

    tray_data_ = {};
    hwnd_ = nullptr;
}

void MainWindow::HideToTray() noexcept {
    if (tray_exit_requested_ || hwnd_ == nullptr) {
        return;
    }

    try {
        AppWindow().IsShownInSwitchers(false);
    } catch (...) {
    }

    ShowWindow(hwnd_, SW_HIDE);
    tray_window_hidden_ = true;
    RefreshEfficiencyMode();
}

void MainWindow::ShowFromTray() {
    SetEfficiencyMode(false);
    if (hwnd_ == nullptr) {
        InitializeTrayIntegration();
    }

    try {
        AppWindow().IsShownInSwitchers(true);
    } catch (...) {
    }

    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_SHOW);
        ShowWindow(hwnd_, SW_RESTORE);
        SetForegroundWindow(hwnd_);
    }
    Activate();
    tray_window_hidden_ = false;
    MaybeOfferRecoveryAsync();
}

void MainWindow::ShowTrayMenu(POINT anchor) noexcept {
    if (hwnd_ == nullptr) {
        return;
    }

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    std::wstring open_text = L"Open VelocityCopy";
    std::wstring exit_text = L"Exit";
    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        open_text = loader.GetString(L"TrayOpen").c_str();
        exit_text = loader.GetString(L"TrayExit").c_str();
    } catch (...) {
    }

    (void)AppendMenuW(menu, MF_STRING, kTrayOpenCommand, open_text.c_str());
    (void)AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    (void)AppendMenuW(menu, MF_STRING, kTrayExitCommand, exit_text.c_str());

    if (anchor.x == -1 && anchor.y == -1) {
        GetCursorPos(&anchor);
    }
    SetForegroundWindow(hwnd_);
    const UINT command = TrackPopupMenuEx(
        menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        anchor.x,
        anchor.y,
        hwnd_,
        nullptr);
    DestroyMenu(menu);

    if (command == kTrayOpenCommand) {
        ShowFromTray();
    } else if (command == kTrayExitCommand) {
        ExitFromTray();
    }
}

void MainWindow::ExitFromTray() noexcept {
    tray_exit_requested_ = true;
    SetEfficiencyMode(false);
    if (tray_added_) {
        (void)Shell_NotifyIconW(NIM_DELETE, &tray_data_);
        tray_added_ = false;
    }
    if (hwnd_ != nullptr) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
}

void MainWindow::SetEfficiencyMode(const bool enabled) noexcept {
    if (efficiency_mode_enabled_ == enabled) {
        return;
    }

    PROCESS_POWER_THROTTLING_STATE state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = enabled ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;

    if (SetProcessInformation(
            GetCurrentProcess(),
            ProcessPowerThrottling,
            &state,
            sizeof(state))) {
        efficiency_mode_enabled_ = enabled;
    }
}

bool MainWindow::HasActiveWorkForEfficiencyMode() noexcept {
    if (execution_control_) {
        return true;
    }

    if (append_gate_) {
        std::lock_guard lock(append_gate_->mutex);
        if (append_gate_->planning_count != 0) {
            return true;
        }
    }

    return false;
}

void MainWindow::RefreshEfficiencyMode() noexcept {
    const bool enable =
        tray_window_hidden_ &&
        !tray_exit_requested_ &&
        !session_ending_ &&
        !HasActiveWorkForEfficiencyMode();
    SetEfficiencyMode(enable);
}

void MainWindow::CaptureClipboardFileSelection() noexcept {
    if (hwnd_ == nullptr || !OpenClipboard(hwnd_)) {
        return;
    }

    std::vector<std::filesystem::path> sources;
    velocitycopy::FileOperation operation = velocitycopy::FileOperation::Copy;

    if (const auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP)); drop != nullptr) {
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        try {
            sources.reserve(count);
            for (UINT index = 0; index < count; ++index) {
                const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                if (length == 0) {
                    continue;
                }
                std::wstring path(length + 1, L'\0');
                if (DragQueryFileW(drop, index, path.data(), length + 1) != 0) {
                    path.resize(length);
                    sources.emplace_back(std::move(path));
                }
            }
        } catch (...) {
            sources.clear();
        }
    }

    const UINT preferred_effect_format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
    if (preferred_effect_format != 0) {
        if (const auto effect_data = static_cast<HGLOBAL>(GetClipboardData(preferred_effect_format));
            effect_data != nullptr) {
            if (const auto effect = static_cast<const DWORD*>(GlobalLock(effect_data)); effect != nullptr) {
                if ((*effect & DROPEFFECT_MOVE) != 0) {
                    operation = velocitycopy::FileOperation::Move;
                }
                GlobalUnlock(effect_data);
            }
        }
    }

    CloseClipboard();

    if (!sources.empty()) {
        try {
            shell_session_.stage_sources(std::move(sources), operation);
        } catch (...) {
        }
    }
}

LRESULT CALLBACK MainWindow::TraySubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam,
    UINT_PTR,
    DWORD_PTR ref_data) {
    auto* self = reinterpret_cast<MainWindow*>(ref_data);
    if (self == nullptr) {
        return DefSubclassProc(hwnd, message, wparam, lparam);
    }

    if (message == kTrayCallbackMessage) {
        UINT notification = 0;
        UINT icon_id = 0;
        POINT anchor{-1, -1};

        if (self->tray_v4_) {
            notification = LOWORD(lparam);
            icon_id = HIWORD(lparam);
            anchor.x = static_cast<short>(LOWORD(wparam));
            anchor.y = static_cast<short>(HIWORD(wparam));
        } else {
            notification = static_cast<UINT>(lparam);
            icon_id = static_cast<UINT>(wparam);
        }

        if (icon_id == kTrayIconId) {
            if (notification == WM_LBUTTONUP ||
                notification == WM_LBUTTONDBLCLK ||
                notification == NIN_SELECT ||
                notification == NIN_KEYSELECT) {
                self->ShowFromTray();
                return 0;
            }
            if (notification == WM_RBUTTONUP || notification == WM_CONTEXTMENU) {
                self->ShowTrayMenu(anchor);
                return 0;
            }
        }
    }

    if (g_taskbar_created_message != 0 && message == g_taskbar_created_message) {
        self->tray_data_.uVersion = 0;
        self->tray_added_ = Shell_NotifyIconW(NIM_ADD, &self->tray_data_) != FALSE;
        self->tray_v4_ = false;
        if (self->tray_added_) {
            self->tray_data_.uVersion = NOTIFYICON_VERSION_4;
            self->tray_v4_ = Shell_NotifyIconW(NIM_SETVERSION, &self->tray_data_) != FALSE;
        }
        return 0;
    }

    switch (message) {
    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0) == SC_MINIMIZE && !self->tray_exit_requested_) {
            self->HideToTray();
            return 0;
        }
        break;

    case WM_CLOSE:
        if (!self->tray_exit_requested_) {
            self->HideToTray();
            return 0;
        }
        break;

    case WM_CLIPBOARDUPDATE:
        self->CaptureClipboardFileSelection();
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wparam != FALSE) {
            self->session_ending_ = true;
            self->SetEfficiencyMode(false);
            self->PersistRecoveryQueueNoThrow();
        }
        break;

    case WM_DESTROY:
        self->RemoveTrayIntegration();
        break;
    }

    return DefSubclassProc(hwnd, message, wparam, lparam);
}

} // namespace winrt::VelocityCopyUI::implementation
