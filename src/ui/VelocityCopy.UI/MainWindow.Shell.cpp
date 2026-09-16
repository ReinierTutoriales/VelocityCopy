#include "pch.h"
#include "MainWindow.xaml.h"

#include <windows.h>

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::HandleShellRequest(const velocitycopy::ShellRequest& request) {
    const auto dispatch = shell_session_.dispatch(request);

    switch (dispatch.status) {
    case velocitycopy::ShellDispatchStatus::InvalidRequest:
        Activate();
        ShowError();
        return;

    case velocitycopy::ShellDispatchStatus::NoStagedSources:
        Activate();
        ShowError();
        return;

    case velocitycopy::ShellDispatchStatus::Accepted:
        break;
    }

    if (dispatch.job) {
        if (request.action == velocitycopy::ShellAction::PasteToFolder) {
            BeginShellLayoutAsync(std::move(*dispatch.job));
            return;
        }

        if (dispatch.show_window) {
            Activate();
        }
        QueueOrStartCopy(std::move(*dispatch.job));
        return;
    }

    if (dispatch.show_window) {
        Activate();
    }
}

fire_and_forget MainWindow::BeginShellLayoutAsync(velocitycopy::CopyJob job) {
    auto lifetime = get_strong();
    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    const auto destination = job.destination;
    auto sources = std::move(job.sources);

    co_await winrt::resume_background();

    std::vector<velocitycopy::DropItem> items;
    bool classification_failed = false;
    try {
        items.reserve(sources.size());
        for (auto& source : sources) {
            const DWORD attributes = GetFileAttributesW(source.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) {
                classification_failed = true;
                break;
            }
            const auto kind = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
                ? velocitycopy::DropItemKind::Directory
                : velocitycopy::DropItemKind::File;
            items.push_back({std::move(source), kind});
        }
    } catch (...) {
        classification_failed = true;
    }

    if (classification_failed || items.size() != sources.size()) {
        items.clear();
    }

    (void)dispatcher.TryEnqueue([
        weak,
        destination,
        items = std::move(items)]() mutable {
        if (auto self = weak.get()) {
            self->Activate();
            if (items.empty()) {
                self->ShowError();
                return;
            }

            self->dropped_items_ = std::move(items);
            self->flow_.begin(self->dropped_items_);
            self->DestinationStep().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
            self->LayoutStep().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
            self->StartCopyButton().IsEnabled(false);
            self->PreserveToggle().IsChecked(false);
            self->DirectToggle().IsChecked(false);
            self->ErrorBar().IsOpen(false);

            self->SelectDestination(destination);
            if (self->flow_.stage() == velocitycopy::DropFlowStage::Layout) {
                self->DropFlowFlyout().ShowAt(self->RootGrid());
            }
        }
    });
}

} // namespace winrt::VelocityCopyUI::implementation
