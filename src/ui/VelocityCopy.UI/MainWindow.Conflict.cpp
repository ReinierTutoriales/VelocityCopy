#include "pch.h"
#include "MainWindow.xaml.h"
#include "Localization.h"
#include "DecisionSurface.h"
#include "UiTokens.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {
Windows::Foundation::IAsyncOperation<std::uint32_t> MainWindow::RequestDecisionAsync(
    velocitycopy::ui::DecisionOptions options) {
    auto lifetime = get_strong();
    auto ui_thread = apartment_context{};
    auto request = std::make_shared<PendingDecision>();
    if (!request->turn) co_return velocitycopy::ui::encode_decision({velocitycopy::ui::DecisionChoice::Cancel, false});

    decision_queue_.push_back(request);
    if (decision_queue_.size() == 1) SetEvent(request->turn);

    co_await resume_on_signal(request->turn);
    co_await ui_thread;
    if (request->cancelled.load(std::memory_order_relaxed) || tray_exit_requested_ || session_ending_) {
        co_return velocitycopy::ui::encode_decision({velocitycopy::ui::DecisionChoice::Cancel, false});
    }

    // A decision is owned by this window, and Windows hides owned windows
    // while their owner is minimized or in the tray: bring the owner back
    // first or the question stays invisible until the person restores it.
    RequestAttention();
    options.owner = hwnd_;
    decision_operation_ = velocitycopy::ui::show_decision_async(std::move(options));
    const auto result = co_await velocitycopy::ui::await_decision(decision_operation_);
    decision_operation_ = nullptr;

    if (!decision_queue_.empty() && decision_queue_.front() == request) decision_queue_.pop_front();
    if (!decision_queue_.empty()) SetEvent(decision_queue_.front()->turn);
    co_return result;
}

void MainWindow::CancelDecisionQueue() noexcept {
    try {
        if (decision_operation_) decision_operation_.Cancel();
        decision_operation_ = nullptr;
        for (auto& request : decision_queue_) {
            request->cancelled.store(true, std::memory_order_relaxed);
            if (request->turn) SetEvent(request->turn);
        }
        decision_queue_.clear();
    } catch (...) {}
}

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
        // Size and last-modified time of both files, like Explorer's conflict
        // dialog, so Replace / Keep both is an informed choice.
        auto describe = [](const std::filesystem::path& path) -> std::wstring {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return {};
            ULARGE_INTEGER size{};
            size.LowPart = data.nFileSizeLow;
            size.HighPart = data.nFileSizeHigh;
            std::wstring text = FormatBytes(size.QuadPart).c_str();
            SYSTEMTIME utc{};
            SYSTEMTIME local{};
            if (FileTimeToSystemTime(&data.ftLastWriteTime, &utc) &&
                SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
                wchar_t date[80]{};
                wchar_t time[80]{};
                if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, date, 80, nullptr) &&
                    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &local, nullptr, time, 80)) {
                    text.append(L" · ");
                    text.append(date);
                    text.append(L" ");
                    text.append(time);
                }
            }
            return text;
        };
        const auto existing = describe(conflict.conflict_destination);
        const auto incoming = describe(conflict.conflict_source);
        if (!existing.empty() && !incoming.empty()) {
            detail.append(L"\n");
            detail.append(velocitycopy::localization::get_string(L"ConflictExistingLabel").c_str());
            detail.append(L": ");
            detail.append(existing);
            detail.append(L"\n");
            detail.append(velocitycopy::localization::get_string(L"ConflictIncomingLabel").c_str());
            detail.append(L": ");
            detail.append(incoming);
        }
        const std::wstring keep_both_label = velocitycopy::localization::get_string(L"ActionKeepBoth").c_str();

        const std::wstring apply_to_all_label =
            velocitycopy::localization::get_string(L"ConflictApplyToAll").c_str();
        const auto decision = velocitycopy::ui::decode_decision(co_await RequestDecisionAsync({
            hwnd_, title, message, detail, replace_label, skip_label, cancel_label,
            apply_to_all_label, true, velocitycopy::ui::DecisionTone::Warning, keep_both_label,
        }));

        if (tray_exit_requested_ || session_ending_) co_return;
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
        case velocitycopy::ui::DecisionChoice::Tertiary:
            ResumeConflictCopy(
                apply_to_all ? 0 : conflict.conflict_file_id,
                apply_to_all ? velocitycopy::ConflictPolicy::KeepBothAll : velocitycopy::ConflictPolicy::KeepBoth);
            co_return;
        case velocitycopy::ui::DecisionChoice::Cancel:
        default:
            CancelCurrentSession();
            co_return;
        }
    } catch (...) {
        if (!tray_exit_requested_ && !session_ending_ && interrupted_session_ == InterruptedSessionState::Conflict) CancelCurrentSession();
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
    ClearLiveTelemetry();
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

    SetExpanded(false);
    SetExecutionButtonsIdle();
    ClearLiveTelemetry();
    SetProgressFraction(1.0);
    if (queued_sessions_.empty()) DestroyCompletedWindow();
    else StartNextQueuedSession();
}

} // namespace winrt::VelocityCopyUI::implementation
