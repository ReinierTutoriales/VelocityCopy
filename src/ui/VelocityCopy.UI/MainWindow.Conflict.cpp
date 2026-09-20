#include "pch.h"
#include "MainWindow.xaml.h"

#include <commctrl.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {
namespace {

constexpr int kConflictReplace = 1001;
constexpr int kConflictSkip = 1002;

int ShowNativeConflictDialog(
    HWND owner,
    const std::wstring& title,
    const std::wstring& message,
    const std::wstring& replace_label,
    const std::wstring& skip_label,
    const std::wstring& cancel_label) noexcept {
    try {
        HMODULE module = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module != nullptr) {
            using TaskDialogIndirectFn = HRESULT (WINAPI*)(
                const TASKDIALOGCONFIG*, int*, int*, BOOL*);
            auto task_dialog = reinterpret_cast<TaskDialogIndirectFn>(
                GetProcAddress(module, "TaskDialogIndirect"));

            if (task_dialog != nullptr) {
                TASKDIALOG_BUTTON buttons[] = {
                    {kConflictReplace, replace_label.c_str()},
                    {kConflictSkip, skip_label.c_str()},
                    {IDCANCEL, cancel_label.c_str()},
                };

                TASKDIALOGCONFIG config{};
                config.cbSize = sizeof(config);
                config.hwndParent = owner;
                config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
                config.pszWindowTitle = L"VelocityCopy";
                config.pszMainInstruction = title.c_str();
                config.pszContent = message.c_str();
                config.cButtons = static_cast<UINT>(std::size(buttons));
                config.pButtons = buttons;
                config.nDefaultButton = kConflictReplace;

                int selected = IDCANCEL;
                const HRESULT hr = task_dialog(&config, &selected, nullptr, nullptr);
                FreeLibrary(module);
                if (SUCCEEDED(hr)) {
                    return selected;
                }
            } else {
                FreeLibrary(module);
            }
        }

        const int fallback = MessageBoxW(
            owner,
            message.c_str(),
            title.c_str(),
            MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
        if (fallback == IDYES) return kConflictReplace;
        if (fallback == IDNO) return kConflictSkip;
    } catch (...) {
    }
    return IDCANCEL;
}

} // namespace

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
            message.append(conflict.conflict_destination.wstring());
        }

        // Conflict choices are a separate native top-level dialog, not XAML
        // content constrained by the compact copier HWND. This prevents the
        // choice UI from being clipped or forcing the copier to be resized.
        const int choice = ShowNativeConflictDialog(
            hwnd_, title, message, replace_label, skip_label, cancel_label);

        if (!conflict_session_ || !live_plan_) co_return;

        switch (choice) {
        case kConflictReplace:
            ResumeConflictCopy(conflict.conflict_file_id);
            co_return;
        case kConflictSkip:
            if (!live_plan_->remove_pending_file(conflict.conflict_file_id)) {
                ShowError();
                CancelCurrentSession();
                co_return;
            }
            RefreshQueue();
            ResumeConflictCopy(0);
            co_return;
        case IDCANCEL:
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
    QueueChevron().Glyph(L"\xE70D");
    ResizeWindow(72);
    SetExecutionButtonsIdle();
    SpeedText().Text(L"—");
    EtaText().Text(L"—");
    SetProgressFraction(1.0);
    StartNextQueuedSession();
}

} // namespace winrt::VelocityCopyUI::implementation
