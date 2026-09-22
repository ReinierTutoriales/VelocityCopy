#include "pch.h"
#include "MainWindow.xaml.h"
#include "App.xaml.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj_core.h>

#pragma comment(lib, "dwmapi.lib")

using namespace winrt;

namespace winrt::VelocityCopyUI::implementation {
namespace {

constexpr UINT_PTR kTraySubclassId = 0x56434F50;

void sync_native_window_theme(
    HWND hwnd,
    Microsoft::UI::Xaml::ElementTheme theme) noexcept {
    if (hwnd == nullptr) return;

    const BOOL dark = theme == Microsoft::UI::Xaml::ElementTheme::Dark ? TRUE : FALSE;
    (void)DwmSetWindowAttribute(
        hwnd,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &dark,
        sizeof(dark));
}

} // namespace

MainWindow::~MainWindow() {
    RemoveTrayIntegration();
}

void MainWindow::InitializeTrayIntegration() {
    if (hwnd_ != nullptr) return;
    try {
        auto window_native = this->m_inner.as<::IWindowNative>();
        if (FAILED(window_native->get_WindowHandle(&hwnd_)) || hwnd_ == nullptr) { hwnd_ = nullptr; return; }
        if (!SetWindowSubclass(hwnd_, &MainWindow::TraySubclassProc, kTraySubclassId,
                               reinterpret_cast<DWORD_PTR>(this))) { hwnd_ = nullptr; return; }

        // Keep the native HWND theme synchronized with WinUI ActualTheme. Native-owned
        // surfaces (TaskDialog, system menu, caption/Snap chrome) can then query the
        // window's real DWM dark-mode state instead of guessing from an unset attribute.
        sync_native_window_theme(hwnd_, RootGrid().ActualTheme());
        auto weak = get_weak();
        RootGrid().ActualThemeChanged(
            [weak](Microsoft::UI::Xaml::FrameworkElement const& sender, IInspectable const&) {
                if (auto self = weak.get()) {
                    sync_native_window_theme(self->NativeOwner(), sender.ActualTheme());
                }
            });

        // Every visible transfer window is a normal taskbar/switcher window. This is
        // essential for native Windows multi-window grouping and live thumbnail previews.
        // The notification-area icon remains app-level; it is not a replacement for the
        // taskbar representation of individual transfer windows.
        try { AppWindow().IsShownInSwitchers(true); } catch (...) {}

        tray_window_hidden_ = IsWindowVisible(hwnd_) == FALSE;
        RefreshEfficiencyMode();
    } catch (...) { RemoveTrayIntegration(); }
}

void MainWindow::RemoveTrayIntegration() noexcept {
    if (window_id_ != 0) {
        if (auto* app = App::Instance()) app->RemoveEfficiencyVote(window_id_);
    }
    if (hwnd_ != nullptr) (void)RemoveWindowSubclass(hwnd_, &MainWindow::TraySubclassProc, kTraySubclassId);
    hwnd_ = nullptr;
}

bool MainWindow::HasActiveTransfer() const noexcept {
    return execution_control_ != nullptr || stopped_session_ || conflict_session_ || stop_requested_;
}

void MainWindow::DestroyCompletedWindow() noexcept {
    if (HasActiveTransfer()) return;
    tray_exit_requested_ = true;
    RefreshEfficiencyMode();
    try { Close(); } catch (...) {}
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
    RefreshEfficiencyMode();
    MaybeOfferRecoveryAsync();
}

void MainWindow::ShowRequestError() {
    ShowFromTray();
    ShowError();
}

void MainWindow::RequestAppExit() noexcept {
    tray_exit_requested_ = true;
    RefreshEfficiencyMode();
    if (hwnd_ != nullptr) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
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
    if (window_id_ == 0) return;
    if (auto* app = App::Instance()) app->ReportEfficiencyVote(window_id_, enable);
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

    switch (message) {
    case WM_SYSCOMMAND:
        // Minimize must remain a native Windows minimize operation. Do not convert it
        // into "hide to tray": Windows needs the real top-level window in order to
        // provide taskbar grouping, per-window previews and normal restore behavior.
        if ((wparam & 0xFFF0) == SC_MINIMIZE && !self->tray_exit_requested_) {
            return DefSubclassProc(hwnd, message, wparam, lparam);
        }
        break;

    case WM_CLOSE:
        if (!self->tray_exit_requested_) {
            if (self->HasActiveTransfer()) {
                self->HideToTray();
                return 0;
            }
            self->tray_exit_requested_ = true;
            self->RefreshEfficiencyMode();
            break;
        }
        break;


    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wparam != FALSE) {
            self->session_ending_ = true;
            if (auto* app = App::Instance()) app->SetShuttingDown(true);
            self->RefreshEfficiencyMode();
            self->PersistRecoveryQueueNoThrow();
        }
        break;

    case WM_DESTROY: {
        const auto id = self->window_id_;
        self->RemoveTrayIntegration();
        if (auto* app = App::Instance()) app->OnWindowDestroyed(id);
        break;
    }
    }

    return DefSubclassProc(hwnd, message, wparam, lparam);
}

} // namespace winrt::VelocityCopyUI::implementation
