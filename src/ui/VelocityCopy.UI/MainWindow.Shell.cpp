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
            auto job = std::move(*dispatch.job);
            if (!HasActiveTransfer()) {
                StartTransfer(std::move(job));
            } else if (active_destination_.lexically_normal() == job.destination.lexically_normal() &&
                       active_operation_ == job.operation) {
                AppendTransfer(std::move(job));
            } else {
                EnqueueTransfer(std::move(job));
            }
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
