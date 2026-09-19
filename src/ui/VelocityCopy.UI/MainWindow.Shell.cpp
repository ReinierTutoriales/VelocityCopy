#include "pch.h"
#include "MainWindow.xaml.h"

#include <windows.h>

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::HandleShellRequest(const velocitycopy::ShellRequest& request) {
    if (request.action == velocitycopy::ShellAction::PasteToFolder &&
        shell_session_.staged_sources().empty()) {
        // The app may have been started on demand after Explorer already placed
        // Copy/Cut files on the clipboard. Reconstruct staging at paste time so
        // Paste with VelocityCopy does not depend on startup residency.
        CaptureClipboardFileSelection();
    }

    const auto dispatch = shell_session_.dispatch(request);

    switch (dispatch.status) {
    case velocitycopy::ShellDispatchStatus::InvalidRequest:
        ShowFromTray();
        ShowError();
        return;

    case velocitycopy::ShellDispatchStatus::NoStagedSources:
        ShowFromTray();
        ShowError();
        return;

    case velocitycopy::ShellDispatchStatus::Accepted:
        break;
    }

    if (request.action == velocitycopy::ShellAction::CopySelectionPromptDestination) {
        BeginShellDestinationAsync(request.sources);
        return;
    }

    if (dispatch.job) {
        if (request.action == velocitycopy::ShellAction::PasteToFolder) {
            BeginShellLayoutAsync(std::move(*dispatch.job));
            return;
        }

        if (dispatch.show_window) {
            ShowFromTray();
        }
        QueueOrStartCopy(std::move(*dispatch.job));
        return;
    }

    if (dispatch.show_window) {
        ShowFromTray();
    }
}

fire_and_forget MainWindow::BeginShellDestinationAsync(
    std::vector<std::filesystem::path> sources) {
    auto lifetime = get_strong();
    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    const auto generation = ++shell_layout_generation_;

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

    if (classification_failed || items.empty()) {
        items.clear();
    }

    (void)dispatcher.TryEnqueue([
        weak,
        generation,
        items = std::move(items)]() mutable {
        if (auto self = weak.get()) {
            if (self->shell_layout_generation_ != generation) {
                return;
            }

            self->ShowFromTray();
            if (items.empty()) {
                self->ShowError();
                return;
            }

            self->pending_flow_operation_ = velocitycopy::FileOperation::Copy;
            self->dropped_items_ = std::move(items);
            self->flow_.begin(self->dropped_items_);
            self->DestinationStep().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
            self->LayoutStep().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
            self->StartCopyButton().IsEnabled(false);
            self->PreserveToggle().IsChecked(false);
            self->DirectToggle().IsChecked(false);
            self->ErrorBar().IsOpen(false);
            self->LoadDestinations();
            // The destination browser is application content, not a flyout. Grow the
            // compact window before opening it so the menu is never clipped by the
            // 72px transfer surface shell.
            self->ResizeWindow(430);
            self->DropFlowFlyout().ShowAt(self->RootGrid());
        }
    });
}

fire_and_forget MainWindow::BeginShellLayoutAsync(velocitycopy::CopyJob job) {
    auto lifetime = get_strong();
    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    const auto generation = ++shell_layout_generation_;
    const auto destination = job.destination;
    const auto operation = job.operation;
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
        generation,
        destination,
        operation,
        items = std::move(items)]() mutable {
        if (auto self = weak.get()) {
            if (self->shell_layout_generation_ != generation) {
                return;
            }

            self->ShowFromTray();
            if (items.empty()) {
                self->ShowError();
                return;
            }

            self->pending_flow_operation_ = operation;
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
                self->ResizeWindow(360);
                self->DropFlowFlyout().ShowAt(self->RootGrid());
            }
        }
    });
}

} // namespace winrt::VelocityCopyUI::implementation
