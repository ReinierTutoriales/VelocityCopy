#include "pch.h"
#include "MainWindow.xaml.h"
#include "App.xaml.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj_core.h>

using namespace winrt;

namespace winrt::VelocityCopyUI::implementation {
namespace {

constexpr UINT_PTR kTraySubclassId = 0x56434F50;

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
        tray_window_hidden_ = IsWindowVisible(hwnd_) == FALSE;
        RefreshEfficiencyMode();
    } catch (...) { RemoveTrayIntegration(); }
}

void MainWindow::RemoveTrayIntegration() noexcept {
    if (window_id_ != 0) {
        if (auto app = Application::Current().try_as<VelocityCopyUI::App>()) {
            if (auto* implementation = get_self<App>(app)) implementation->RemoveEfficiencyVote(window_id_);
        }
    }
    if (hwnd_ != nullptr) (void)RemoveWindowSubclass(hwnd_, &MainWindow::TraySubclassProc, kTraySubclassId);
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
    if (auto app = Application::Current().try_as<VelocityCopyUI::App>()) {
        if (auto* implementation = get_self<App>(app)) implementation->ReportEfficiencyVote(window_id_, enable);
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


    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wparam != FALSE) {
            self->session_ending_ = true;
            if (auto app = Application::Current().try_as<VelocityCopyUI::App>()) {
                if (auto* implementation = get_self<App>(app)) implementation->SetShuttingDown(true);
            }
            self->RefreshEfficiencyMode();
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
