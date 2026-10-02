#include "pch.h"
#include "MainWindow.xaml.h"
#include "AuxiliarySurface.h"
#include "UiTokens.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {
namespace {

std::wstring localized_or(
    Microsoft::Windows::ApplicationModel::Resources::ResourceLoader const& loader,
    wchar_t const* key,
    wchar_t const* fallback) {
    try {
        const auto value = loader.GetString(key);
        return value.empty() ? std::wstring(fallback) : std::wstring(value.c_str());
    } catch (...) {
        return std::wstring(fallback);
    }
}

} // namespace

MainWindow::NativeDialogChoice MainWindow::ShowNativeDecisionDialog(
    HWND owner,
    const std::wstring& title,
    const std::wstring& message,
    const std::wstring& primary_label,
    const std::wstring& secondary_label,
    const bool include_cancel,
    const std::wstring& cancel_label,
    bool* remember_choice) noexcept {
    std::wstring display_title = title;
    std::wstring display_message = message;
    std::wstring display_primary = primary_label;
    std::wstring display_secondary = secondary_label;
    std::wstring display_cancel = cancel_label;
    std::wstring remember_label = L"Remember my choice";

    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        remember_label = localized_or(loader, L"DialogRememberChoice", L"Remember my choice");

        if (title == L"Destination already in use") {
            display_title = localized_or(loader, L"RoutingDestinationInUseTitle", L"Destination already in use");
            display_message = localized_or(
                loader,
                L"RoutingDestinationInUseMessage",
                L"A transfer to this destination is already running. Add these files to it or wait?");
            display_primary = localized_or(loader, L"RoutingActionAdd", L"Add");
            display_secondary = localized_or(loader, L"RoutingActionWait", L"Wait");
        } else if (title == L"Storage device already in use") {
            display_title = localized_or(loader, L"RoutingStorageInUseTitle", L"Storage device already in use");
            display_message = localized_or(
                loader,
                L"RoutingStorageInUseMessage",
                L"Another transfer is using the same storage device. Wait or run this transfer in parallel?");
            display_primary = localized_or(loader, L"RoutingActionWait", L"Wait");
            display_secondary = localized_or(loader, L"RoutingActionParallel", L"Parallel");
        }

        if (include_cancel && display_cancel.empty()) {
            display_cancel = localized_or(loader, L"ActionCancel", L"Cancel");
        }
    } catch (...) {
    }

    const auto decision = velocitycopy::ui::show_native_decision(
        velocitycopy::ui::NativeDecisionOptions{
            owner,
            std::move(display_title),
            std::move(display_message),
            std::move(display_primary),
            std::move(display_secondary),
            std::move(display_cancel),
            std::move(remember_label),
            include_cancel,
        },
        remember_choice);

    switch (decision) {
    case velocitycopy::ui::NativeDecision::Primary:
        return NativeDialogChoice::Primary;
    case velocitycopy::ui::NativeDecision::Secondary:
        return NativeDialogChoice::Secondary;
    case velocitycopy::ui::NativeDecision::Cancel:
    default:
        return NativeDialogChoice::Cancel;
    }
}

fire_and_forget MainWindow::ShowConflictDialogAsync(velocitycopy::JobResult conflict) {
    auto lifetime = get_strong();

    try {
        if (!conflict_session_ || !live_plan_ || !conflict.destination_conflict ||
            conflict.conflict_file_id == 0) {
            co_return;
        }

        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        const std::wstring title = loader.GetString(L"ConflictTitle").c_str();
        const std::wstring replace_label = loader.GetString(L"ActionReplace").c_str();
        const std::wstring skip_label = loader.GetString(L"ActionSkip").c_str();
        const std::wstring cancel_label = loader.GetString(L"ActionCancel").c_str();

        std::wstring message = loader.GetString(L"ConflictMessage").c_str();
        if (!conflict.conflict_destination.empty()) {
            message.append(L"\n\n");
            const auto filename = conflict.conflict_destination.filename();
            message.append(filename.empty()
                ? conflict.conflict_destination.wstring()
                : filename.wstring());
        }

        // All modal decisions use a separate native top-level dialog owned by
        // VelocityCopy. Never constrain a modal choice to the compact XAML root.
        bool apply_to_all = false;
        const auto choice = ShowNativeDecisionDialog(
            hwnd_, title, message, replace_label, skip_label, true, cancel_label, &apply_to_all);

        if (!conflict_session_ || !live_plan_) co_return;

        switch (choice) {
        case NativeDialogChoice::Primary:
            ResumeConflictCopy(
                apply_to_all ? 0 : conflict.conflict_file_id,
                apply_to_all ? velocitycopy::ConflictPolicy::ReplaceAll : velocitycopy::ConflictPolicy::Prompt);
            co_return;
        case NativeDialogChoice::Secondary:
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
        case NativeDialogChoice::Cancel:
        default:
            CancelCurrentSession();
            co_return;
        }
    } catch (...) {
        if (conflict_session_) CancelCurrentSession();
    }
}

void MainWindow::ResumeConflictCopy(
    const std::uint64_t replace_file_id,
    const velocitycopy::ConflictPolicy policy) {
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
            conflict_policy_ = policy;
            return;
        }
    }

    if (live_plan_->remaining_files() == 0 && !live_plan_->has_pending_directories()) {
        resume_requested_ = false;
        conflict_replace_file_id_ = 0;
    conflict_policy_ = velocitycopy::ConflictPolicy::Prompt;
        FinalizeConflictSessionIfEmpty();
        return;
    }

    resume_requested_ = false;
    conflict_replace_file_id_ = 0;
    conflict_policy_ = velocitycopy::ConflictPolicy::Prompt;
    conflict_policy_ = policy;
    conflict_session_ = false;
    stopped_session_ = false;
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
    conflict_policy_ = velocitycopy::ConflictPolicy::Prompt;
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
