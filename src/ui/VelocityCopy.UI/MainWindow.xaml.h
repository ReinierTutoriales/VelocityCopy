#pragma once

#include "MainWindow.g.h"

#include "velocitycopy/app_storage.hpp"
#include "velocitycopy/execution_control.hpp"
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/job_planner.hpp"
#include "velocitycopy/job_planning_worker.hpp"
#include "velocitycopy/live_copy_plan.hpp"
#include "velocitycopy/queue_archive.hpp"
#include "velocitycopy/shell_request.hpp"
#include "velocitycopy/shell_session.hpp"
#include "velocitycopy/transfer_router.hpp"
#include "velocitycopy/ui_snapshot.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>

namespace winrt::VelocityCopyUI::implementation {
struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    ~MainWindow();

    void ShowFromTray();
    void ShowRequestError();
    void RequestAppExit() noexcept;
    [[nodiscard]] bool HasActiveTransfer() const noexcept;
    [[nodiscard]] std::uint64_t WindowId() const noexcept { return window_id_; }
    void StartTransfer(velocitycopy::CopyJob job, velocitycopy::StorageKey destination_key = {}, velocitycopy::StorageKey source_key = {});
    void AppendTransfer(velocitycopy::CopyJob job);
    void EnqueueTransfer(velocitycopy::CopyJob job);
    [[nodiscard]] const std::filesystem::path& ActiveDestination() const noexcept { return active_destination_; }
    [[nodiscard]] velocitycopy::FileOperation ActiveOperation() const noexcept { return active_operation_; }

    void OnDragEnter(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
    void OnDragOver(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
    void OnDragLeave(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
    void OnDrop(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
    void OnPauseClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSkipClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnStopClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnCancelClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSaveQueueClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnLoadQueueClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnMenuSkipClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnMenuStopClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnAboutClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueMoveUpClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueMoveDownClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueRemoveClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueSelectionChanged(IInspectable const&, Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const&);
    void OnQueueKeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const&);
    void OnQueueDragItemsCompleted(
        Microsoft::UI::Xaml::Controls::ListViewBase const&,
        Microsoft::UI::Xaml::Controls::DragItemsCompletedEventArgs const&);
    void OnTransferSurfaceSizeChanged(
        IInspectable const&,
        Microsoft::UI::Xaml::SizeChangedEventArgs const&);

private:
    enum class NativeDialogChoice : std::uint8_t {
        Cancel,
        Primary,
        Secondary,
    };

    struct AppendGate {
        struct BoundedCondition {
            explicit BoundedCondition(bool* accepting_state) noexcept
                : accepting(accepting_state) {}

            std::condition_variable_any value;
            bool* accepting{};

            template <typename Lock, typename Predicate>
            bool wait(Lock& lock, const std::stop_token token, Predicate predicate) {
                const bool ready = value.wait_for(
                    lock,
                    token,
                    std::chrono::seconds(8),
                    std::move(predicate));
                if (!ready && accepting != nullptr) {
                    *accepting = false;
                    value.notify_all();
                }
                return ready;
            }

            void notify_all() noexcept {
                value.notify_all();
            }
        };

        std::mutex mutex;
        bool accepting{true};
        BoundedCondition condition{&accepting};
        std::size_t planning_count{};
    };

    winrt::fire_and_forget HandleDropAsync(Microsoft::UI::Xaml::DragEventArgs args);
    winrt::fire_and_forget ShowConflictDialogAsync(velocitycopy::JobResult conflict);
    winrt::fire_and_forget SaveQueueAsync();
    winrt::fire_and_forget LoadQueueAsync();
    winrt::fire_and_forget MaybeOfferRecoveryAsync();
    static NativeDialogChoice ShowNativeDecisionDialog(
        HWND owner,
        const std::wstring& title,
        const std::wstring& message,
        const std::wstring& primary_label,
        const std::wstring& secondary_label,
        bool include_cancel,
        const std::wstring& cancel_label = {}) noexcept;
    void ShowAboutDialog() noexcept;
    void ConfigureQueuePersistenceMenu();
    void InitializeTrayIntegration();
    void RemoveTrayIntegration() noexcept;
    void HideToTray() noexcept;
    void DestroyCompletedWindow() noexcept;
    void RefreshEfficiencyMode() noexcept;
    [[nodiscard]] bool HasActiveWorkForEfficiencyMode() noexcept;
    void PersistRecoveryQueueNoThrow() noexcept;
    void ApplyTitleBarInset() noexcept;
    void OnAppWindowChanged(
        Microsoft::UI::Windowing::AppWindow const&,
        Microsoft::UI::Windowing::AppWindowChangedEventArgs const& args);
    static LRESULT CALLBACK TraySubclassProc(
        HWND hwnd,
        UINT message,
        WPARAM wparam,
        LPARAM lparam,
        UINT_PTR subclass_id,
        DWORD_PTR ref_data);
    void EnqueueAppend(
        velocitycopy::CopyJob job,
        std::shared_ptr<velocitycopy::LiveCopyPlan> target_plan,
        std::shared_ptr<velocitycopy::ExecutionControl> target_control,
        std::shared_ptr<AppendGate> target_gate,
        bool reservation_already_held);
    void StartCopyPlan(velocitycopy::CopyPlan plan);
    void ResumeStoppedCopy();
    void ResumeConflictCopy(std::uint64_t replace_file_id);
    void CancelCurrentSession();
    void StartNextQueuedSession();
    [[nodiscard]] velocitycopy::JobResult RunLivePlanSession(
        std::shared_ptr<velocitycopy::LiveCopyPlan> plan,
        std::shared_ptr<velocitycopy::ExecutionControl> control,
        std::shared_ptr<AppendGate> gate,
        std::stop_token stop_token,
        bool publish_plan,
        std::uint64_t replace_file_id = 0);
    void PublishLivePlan(std::shared_ptr<velocitycopy::LiveCopyPlan> plan);
    void RefreshQueue();
    void RefreshQueueCommandState();
    void RefreshExecutionMenuState();
    void RefreshExecutionButtonState();
    void RefreshQueueEditCommandState();
    [[nodiscard]] std::vector<std::uint64_t> SelectedPendingIds();
    void ResizeWindow(int height_epx);
    void ResizeWindowToContent();
    void SetProgressFraction(double fraction);
    void SetExecutionButtonsPlanning();
    void SetExecutionButtonsRunning();
    void SetExecutionButtonsIdle();
    void SetExecutionButtonsStopped();
    void SetExecutionButtonsConflict();
    void FinalizeStoppedSessionIfEmpty();
    void FinalizeConflictSessionIfEmpty();
    void ApplySnapshot(const velocitycopy::UiSnapshot& snapshot);
    void FinishCopy(const velocitycopy::JobResult& result);
    void ResetTransferSurface();
    void ShowError(hstring const& message = {});
    static hstring FormatFailureReason(std::int32_t native_code);
    static hstring FormatSpeed(double bytes_per_second);
    static hstring FormatEta(double seconds);

    std::wstring session_id_{velocitycopy::new_session_id()};
    std::uint64_t window_id_{};
    velocitycopy::JobPlanner planner_;
    velocitycopy::JobPlanningWorker append_planner_;
    velocitycopy::JobExecutor executor_;
    std::shared_ptr<velocitycopy::ExecutionControl> execution_control_;
    std::shared_ptr<AppendGate> append_gate_;
    velocitycopy::ProgressPresenter presenter_{100};
    std::filesystem::path active_destination_;
    velocitycopy::StorageKey active_destination_key_;
    velocitycopy::StorageKey active_source_key_;
    velocitycopy::FileOperation active_operation_{velocitycopy::FileOperation::Copy};
    std::deque<velocitycopy::CopyJob> deferred_same_destination_jobs_;
    std::deque<velocitycopy::CopyJob> deferred_interrupted_jobs_;
    std::deque<velocitycopy::CopyJob> queued_sessions_;
    std::shared_ptr<velocitycopy::LiveCopyPlan> live_plan_;
    std::vector<velocitycopy::PlannedFile> queue_snapshot_;
    std::vector<std::filesystem::path> planning_sources_;
    Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{nullptr};
    Microsoft::UI::Xaml::Controls::Button queue_options_button_{nullptr};
    Microsoft::UI::Xaml::Controls::MenuFlyoutItem save_queue_menu_item_{nullptr};
    Microsoft::UI::Xaml::Controls::MenuFlyoutItem load_queue_menu_item_{nullptr};
    Microsoft::UI::Xaml::Controls::MenuFlyoutItem skip_menu_item_{nullptr};
    Microsoft::UI::Xaml::Controls::MenuFlyoutItem stop_menu_item_{nullptr};
    Microsoft::UI::Xaml::Controls::MenuFlyoutItem about_menu_item_{nullptr};
    Microsoft::UI::Xaml::Thickness base_caption_content_padding_{};
    std::atomic_bool cancel_requested_{false};
    double progress_fraction_{};
    bool paused_{};
    bool stopped_session_{};
    bool conflict_session_{};
    bool stop_requested_{};
    bool resume_requested_{};
    bool current_file_skippable_{};
    bool recovery_prompt_checked_{};
    bool recovery_prompt_active_{};
    std::uint64_t next_job_id_{1};
    std::uint64_t last_queue_completed_files_{};
    std::uint64_t current_file_id_{};
    std::uint64_t conflict_replace_file_id_{};
    HWND hwnd_{};
    bool tray_exit_requested_{};
    bool tray_window_hidden_{};
    bool session_ending_{};
    std::jthread copy_thread_;
};
}

namespace winrt::VelocityCopyUI::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
