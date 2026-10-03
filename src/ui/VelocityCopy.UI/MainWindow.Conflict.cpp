#include "pch.h"
#include "MainWindow.xaml.h"
#include "Localization.h"
#include "DecisionSurface.h"
#include "UiTokens.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {
fire_and_forget MainWindow::ShowConflictDialogAsync(velocitycopy::JobResult conflict) {
    auto lifetime = get_strong();

    try {
        if (interrupted_session_ != InterruptedSessionState::Conflict || !live_plan_ || !conflict.destination_conflict ||
            conflict.conflict_file_id == 0) {
            co_return;
        }

        const std::wstring title = velocitycopy::localization::get_string(L"ConflictTitle").c_str();
        const std::wstring replace_label = velocitycopy::localization::get_string(L"ActionReplace").c_str();
        const std::wstring skip_label = velocitycopy::localization::get_string(L"ActionSkip").c_str();
        const std::wstring cancel_label = velocitycopy::localization::get_string(L"ActionCancel").c_str();

        const std::wstring message = velocitycopy::localization::get_string(L"ConflictMessage").c_str();
        std::wstring detail;
        if (!conflict.conflict_destination.empty()) {
            const auto filename = conflict.conflict_destination.filename();
            detail = filename.empty() ? conflict.conflict_destination.wstring() : filename.wstring();
        }

        if (decision_operation_) co_return;
        const std::wstring apply_to_all_label =
            velocitycopy::localization::get_string(L"ConflictApplyToAll").c_str();
        decision_operation_ = velocitycopy::ui::show_decision_async({
            hwnd_,
            title,
            message,
            detail,
            replace_label,
            skip_label,
            cancel_label,
            apply_to_all_label,
            true,
        });
        const auto decision = velocitycopy::ui::decode_decision(
            co_await velocitycopy::ui::await_decision(decision_operation_));
        decision_operation_ = nullptr;

        if (interrupted_session_ != InterruptedSessionState::Conflict || !live_plan_) co_return;
        const bool apply_to_all = decision.verification_checked;

        switch (decision.choice) {
        case velocitycopy::ui::DecisionChoice::Primary:
            ResumeConflictCopy(
                apply_to_all ? 0 : conflict.conflict_file_id,
                apply_to_all ? velocitycopy::ConflictPolicy::ReplaceAll : velocitycopy::ConflictPolicy::Prompt);
            co_return;
        case velocitycopy::ui::DecisionChoice::Secondary:
            if (apply_to_all) {
                ResumeConflictCopy(0, velocitycopy::ConflictPolicy::SkipAll);
                co_return;
            }
            if (!live_plan_->remove_pending_file(conflict.conflict_file_id)) {
                ShowError();
                CancelCurrentSession();
                co_return;
            }
            RefreshQueue();
            ResumeConflictCopy(0, velocitycopy::ConflictPolicy::Prompt);
            co_return;
        case velocitycopy::ui::DecisionChoice::Cancel:
        default:
            CancelCurrentSession();
            co_return;
        }
    } catch (...) {
        decision_operation_ = nullptr;
        if (interrupted_session_ == InterruptedSessionState::Conflict) CancelCurrentSession();
    }
}

void MainWindow::ResumeConflictCopy(
    const std::uint64_t replace_file_id,
    const velocitycopy::ConflictPolicy policy) {
    if (interrupted_session_ != InterruptedSessionState::Conflict || !live_plan_) return;

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
            pending_resume_ = ConflictResume{{replace_file_id, policy}};
            return;
        }
    }

    if (live_plan_->unresolved_files() == 0 && !live_plan_->has_pending_directories()) {
        pending_resume_ = {};
        FinalizeConflictSessionIfEmpty();
        return;
    }

    pending_resume_ = {};
    interrupted_session_ = InterruptedSessionState::None;
    stop_requested_ = false;
    current_file_id_ = 0;
    current_file_skippable_ = false;
    cancel_requested_.store(false, std::memory_order_relaxed);
    presenter_.reset();
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    last_queue_completed_files_ = live_plan_->completed_files();
    execution_control_ = std::make_shared<velocitycopy::ExecutionControl>();
    if (!append_gate_) append_gate_ = std::make_shared<AppendGate>();

    auto plan = live_plan_;
    auto control = execution_control_;
    auto gate = append_gate_;
    active_destination_ = plan->destination_root();
    active_operation_ = plan->operation();
    SetExecutionButtonsRunning();

    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    copy_thread_ = std::jthread(
        [this, weak, dispatcher, plan, control, gate, replace_file_id, policy](std::stop_token stop_token) {
            const auto result = RunLivePlanSession(plan, control, gate, stop_token, false, replace_file_id, policy);
            (void)dispatcher.TryEnqueue([weak, result]() {
                if (auto self = weak.get()) self->FinishCopy(result);
            });
        });
}

void MainWindow::FinalizeConflictSessionIfEmpty() {
    if (interrupted_session_ != InterruptedSessionState::Conflict || !live_plan_ || live_plan_->unresolved_files() != 0 ||
        live_plan_->has_pending_directories()) return;

    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        if (append_gate_->planning_count != 0) return;
        append_gate_->accepting = false;
        append_gate_->condition.notify_all();
    }

    interrupted_session_ = InterruptedSessionState::None;
    pending_resume_ = {};
    live_plan_.reset();
    append_gate_.reset();
    active_destination_.clear();
    RefreshQueue();
    QueueButton().IsEnabled(false);
    QueuePanel().Visibility(Visibility::Collapsed);
    QueueChevron().Glyph(L"\xE70D");
    ResizeWindow(velocitycopy::ui::token_int(L"CompactSurfaceHeight", 72));
    SetExecutionButtonsIdle();
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    SetProgressFraction(1.0);
    if (queued_sessions_.empty()) DestroyCompletedWindow();
    else StartNextQueuedSession();
}

} // namespace winrt::VelocityCopyUI::implementation
