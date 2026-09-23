#include "pch.h"
#include "MainWindow.xaml.h"

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

struct TaskDialogThemeContext {
    BOOL dark_mode{};
};

HRESULT CALLBACK TaskDialogThemeCallback(
    HWND hwnd,
    UINT notification,
    WPARAM,
    LPARAM,
    LONG_PTR callback_data) noexcept {
    if (notification != TDN_CREATED) return S_OK;

    const auto* context = reinterpret_cast<const TaskDialogThemeContext*>(callback_data);
    if (context == nullptr) return S_OK;

    const BOOL dark_mode = context->dark_mode;
    (void)DwmSetWindowAttribute(
        hwnd,
        DWMWA_USE_IMMERSIVE_DARK_MODE,
        &dark_mode,
        sizeof(dark_mode));

    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    (void)DwmSetWindowAttribute(
        hwnd,
        DWMWA_WINDOW_CORNER_PREFERENCE,
        &corner,
        sizeof(corner));

    // TaskDialog is a Win32 surface, but it should visually follow the same Windows 11
    // light/dark mode as the owning WinUI transfer window rather than appearing as a
    // disconnected legacy dialog.
    (void)SetWindowTheme(hwnd, dark_mode ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    return S_OK;
}

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
    if (remember_choice != nullptr) *remember_choice = false;
    constexpr int kPrimary = 1001;
    constexpr int kSecondary = 1002;

    try {
        std::wstring display_title = title;
        std::wstring display_message = message;
        std::wstring display_primary = primary_label;
        std::wstring display_secondary = secondary_label;
        std::wstring display_cancel = cancel_label;
        std::wstring remember_label = L"Remember my choice";

        // Routing prompts originate in the app-level multi-window router. Normalize them
        // here so the actual decision surface is localized even when the router uses its
        // stable English protocol text internally.
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

        HMODULE module = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module != nullptr) {
            using TaskDialogIndirectFn = HRESULT (WINAPI*)(
                const TASKDIALOGCONFIG*, int*, int*, BOOL*);
            auto task_dialog = reinterpret_cast<TaskDialogIndirectFn>(
                GetProcAddress(module, "TaskDialogIndirect"));

            if (task_dialog != nullptr) {
                TASKDIALOG_BUTTON buttons[] = {
                    {kPrimary, display_primary.c_str()},
                    {kSecondary, display_secondary.c_str()},
                    {IDCANCEL, display_cancel.c_str()},
                };

                TaskDialogThemeContext theme{};
                if (owner != nullptr) {
                    (void)DwmGetWindowAttribute(
                        owner,
                        DWMWA_USE_IMMERSIVE_DARK_MODE,
                        &theme.dark_mode,
                        sizeof(theme.dark_mode));
                }

                TASKDIALOGCONFIG config{};
                config.cbSize = sizeof(config);
                config.hwndParent = owner;
                config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
                // Keep every native decision surface in the same compact family. Routing
                // includes a verification row, so it stays slightly narrower; conflict and
                // recovery get a little more room without expanding to long-path width.
                config.cxWidth = remember_choice != nullptr ? 240 : 260;
                config.pszWindowTitle = L"VelocityCopy";
                config.pszMainInstruction = display_title.c_str();
                config.pszContent = display_message.c_str();
                config.cButtons = include_cancel ? 3u : 2u;
                config.pButtons = buttons;
                config.nDefaultButton = kPrimary;
                config.pfCallback = &TaskDialogThemeCallback;
                config.lpCallbackData = reinterpret_cast<LONG_PTR>(&theme);
                if (remember_choice != nullptr) config.pszVerificationText = remember_label.c_str();

                int selected = IDCANCEL;
                BOOL verification_checked = FALSE;
                const HRESULT hr = task_dialog(&config, &selected, nullptr, remember_choice != nullptr ? &verification_checked : nullptr);
                FreeLibrary(module);
                if (SUCCEEDED(hr)) {
                    if (remember_choice != nullptr) *remember_choice = verification_checked != FALSE;
                    if (selected == kPrimary) return NativeDialogChoice::Primary;
                    if (selected == kSecondary) return NativeDialogChoice::Secondary;
                    return NativeDialogChoice::Cancel;
                }
            } else {
                FreeLibrary(module);
            }
        }

        const UINT flags = include_cancel
            ? (MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1)
            : (MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1);
        const int fallback = MessageBoxW(owner, display_message.c_str(), display_title.c_str(), flags);
        if (fallback == IDYES) return NativeDialogChoice::Primary;
        if (fallback == IDNO) return NativeDialogChoice::Secondary;
    } catch (...) {
    }
    return NativeDialogChoice::Cancel;
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
        const auto choice = ShowNativeDecisionDialog(
            hwnd_, title, message, replace_label, skip_label, true, cancel_label);

        if (!conflict_session_ || !live_plan_) co_return;

        switch (choice) {
        case NativeDialogChoice::Primary:
            ResumeConflictCopy(conflict.conflict_file_id);
            co_return;
        case NativeDialogChoice::Secondary:
            if (!live_plan_->remove_pending_file(conflict.conflict_file_id)) {
                ShowError();
                CancelCurrentSession();
                co_return;
            }
            RefreshQueue();
            ResumeConflictCopy(0);
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
    if (queued_sessions_.empty()) DestroyCompletedWindow();
    else StartNextQueuedSession();
}

} // namespace winrt::VelocityCopyUI::implementation
