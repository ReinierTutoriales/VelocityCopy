#include "pch.h"
#include "MainWindow.xaml.h"
#include <windows.h>
namespace winrt::VelocityCopyUI::implementation {
void MainWindow::HandleShellRequest(const velocitycopy::ShellRequest& request) {
    try {
        auto dispatch = shell_session_.dispatch(request);
        if (dispatch.status != velocitycopy::ShellDispatchStatus::Accepted) {
            ShowFromTray(); ShowError(); return;
        }
        if (dispatch.job) {
            BeginShellLayoutAsync(std::move(*dispatch.job));
        } else if (dispatch.show_window) {
            ShowFromTray();
        }
    } catch (...) { ShowFromTray(); ShowError(); }
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
            // Explorer already resolved a single, unambiguous destination
            // (Copy here / Move here). Unlike multi-root drag/drop onto the
            // window, there is no real layout question to ask, so apply the
            // default layout and start immediately instead of opening the
            // destination/layout flyout: on the compact window that flyout
            // can exceed the visible surface, blocking the transfer on a
            // prompt the user cannot dismiss or answer.
            if (self->flow_.stage() != velocitycopy::DropFlowStage::Layout) {
                return; // SelectDestination already surfaced the error.
            }
            if (!self->flow_.choose_layout(velocitycopy::DestinationLayout::PreserveSourceFolder)) {
                self->ShowError();
                return;
            }
            auto shell_job = self->flow_.make_job(self->next_job_id_++, operation);
            if (!shell_job) {
                self->ShowError();
                return;
            }
            self->pending_flow_operation_ = velocitycopy::FileOperation::Copy;
            self->QueueOrStartCopy(std::move(*shell_job));
        }
    });
}

} // namespace winrt::VelocityCopyUI::implementation
