#include "pch.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {

fire_and_forget MainWindow::ShowConflictDialogAsync(velocitycopy::JobResult conflict) {
    auto lifetime = get_strong();

    try {
        if (!conflict_session_ || !live_plan_ || !conflict.destination_conflict ||
            conflict.conflict_file_id == 0) {
            co_return;
        }

        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        ContentDialog dialog;
        dialog.XamlRoot(RootGrid().XamlRoot());
        dialog.Title(box_value(loader.GetString(L"ConflictTitle")));

        std::wstring message = loader.GetString(L"ConflictMessage").c_str();
        if (!conflict.conflict_destination.empty()) {
            message.append(L"\n\n");
            message.append(conflict.conflict_destination.wstring());
        }
        dialog.Content(box_value(hstring(message)));
        dialog.PrimaryButtonText(loader.GetString(L"ActionReplace"));
        dialog.SecondaryButtonText(loader.GetString(L"ActionSkip"));
        dialog.CloseButtonText(loader.GetString(L"ActionCancel"));
        dialog.DefaultButton(ContentDialogButton::Primary);

        const auto choice = co_await dialog.ShowAsync();
        if (!conflict_session_ || !live_plan_) co_return;

        switch (choice) {
        case ContentDialogResult::Primary:
            ResumeConflictCopy(conflict.conflict_file_id);
            co_return;
        case ContentDialogResult::Secondary:
            if (!live_plan_->remove_pending_file(conflict.conflict_file_id)) {
                ShowError();
                CancelCurrentSession();
                co_return;
            }
            RefreshQueue();
            ResumeConflictCopy(0);
            co_return;
        case ContentDialogResult::None:
        default:
            CancelCurrentSession();
            co_return;
        }
    } catch (...) {
        if (conflict_session_) CancelCurrentSession();
    }
}

void MainWindow::ResumeConflictCopy(const std::uint64_t replace_file_id) {
    if (!conflict_session_ || !live_plan_) return;

    if (!deferred_interrupted_jobs_.empty() && append_gate_) {
        auto deferred = std::move(deferred_interrupted_jobs_);
        deferred_interrupted_jobs_.clear();
        auto plan = live_plan_;
        auto gate = append_gate_;
        for (auto& job : deferred) {
            EnqueueAppend(std::move(job), plan, nullptr, gate, false);
        }
    }

    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        if (append_gate_->planning_count != 0) {
            resume_requested_ = true;
            conflict_replace_file_id_ = replace_file_id;
            return;
        }
    }

    if (live_plan_->remaining_files() == 0 && !live_plan_->has_pending_directories()) {
        resume_requested_ = false;
        conflict_replace_file_id_ = 0;
        FinalizeConflictSessionIfEmpty();
        return;
    }

    resume_requested_ = false;
    conflict_replace_file_id_ = 0;
    conflict_session_ = false;
    stopped_session_ = false;
    stop_requested_ = false;
    current_file_id_ = 0;
    current_file_skippable_ = false;
    cancel_requested_.store(false, std::memory_order_relaxed);
    presenter_.reset();
    // Keep aggregate progress from the conflict point until the resumed executor
    // publishes a fresh snapshot; only live telemetry becomes unknown.
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    last_queue_completed_files_ = live_plan_->completed_files();
    execution_control_ = std::make_shared<velocitycopy::ExecutionControl>();
    if (!append_gate_) append_gate_ = std::make_shared<AppendGate>();

    auto plan = live_plan_;
    auto control = execution_control_;
    auto gate = append_gate_;
    active_destination_ = plan->destination_root();
    SetExecutionButtonsRunning();

    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    copy_thread_ = std::jthread(
        [this, weak, dispatcher, plan, control, gate, replace_file_id](std::stop_token stop_token) {
            const auto result = RunLivePlanSession(plan, control, gate, stop_token, false, replace_file_id);
            (void)dispatcher.TryEnqueue([weak, result]() {
                if (auto self = weak.get()) self->FinishCopy(result);
            });
        });
}

void MainWindow::FinalizeConflictSessionIfEmpty() {
    if (!conflict_session_ || !live_plan_ || live_plan_->remaining_files() != 0 ||
        live_plan_->has_pending_directories()) return;

    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        if (append_gate_->planning_count != 0) return;
        append_gate_->accepting = false;
        append_gate_->condition.notify_all();
    }

    conflict_session_ = false;
    resume_requested_ = false;
    conflict_replace_file_id_ = 0;
    live_plan_.reset();
    append_gate_.reset();
    active_destination_.clear();
    RefreshQueue();
    QueueButton().IsEnabled(false);
    QueuePanel().Visibility(Visibility::Collapsed);
    QueueButton().Content(box_value(hstring(L"▸")));
    ResizeWindow(72);
    SetExecutionButtonsIdle();
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    GlobalProgress().Value(100);
    ProgressFill().Width(TransferSurface().ActualWidth());
    ProgressPercentText().Text(L"100%");
    StartNextQueuedSession();
}

} // namespace winrt::VelocityCopyUI::implementation
