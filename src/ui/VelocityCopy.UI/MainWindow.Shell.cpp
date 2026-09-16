#include "pch.h"
#include "MainWindow.xaml.h"

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::HandleShellRequest(const velocitycopy::ShellRequest& request) {
    const auto dispatch = shell_session_.dispatch(request);

    switch (dispatch.status) {
    case velocitycopy::ShellDispatchStatus::InvalidRequest:
        ShowError();
        return;

    case velocitycopy::ShellDispatchStatus::NoStagedSources:
        Activate();
        ShowError();
        return;

    case velocitycopy::ShellDispatchStatus::Accepted:
        break;
    }

    if (dispatch.show_window) {
        Activate();
    }

    if (dispatch.job) {
        QueueOrStartCopy(*dispatch.job);
    }
}

} // namespace winrt::VelocityCopyUI::implementation
