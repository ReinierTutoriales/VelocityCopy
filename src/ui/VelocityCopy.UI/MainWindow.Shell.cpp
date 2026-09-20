#include "pch.h"
#include "MainWindow.xaml.h"

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::HandleShellRequest(const velocitycopy::ShellRequest& request) {
    try {
        auto dispatch = shell_session_.dispatch(request);
        if (dispatch.status != velocitycopy::ShellDispatchStatus::Accepted) {
            ShowFromTray();
            ShowError();
            return;
        }

        if (dispatch.job) {
            ShowFromTray();
            QueueOrStartCopy(std::move(*dispatch.job));
            return;
        }

        if (dispatch.show_window) {
            ShowFromTray();
        }
    } catch (...) {
        ShowFromTray();
        ShowError();
    }
}

} // namespace winrt::VelocityCopyUI::implementation
