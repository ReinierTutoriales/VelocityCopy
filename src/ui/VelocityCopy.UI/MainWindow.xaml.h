#pragma once

#include "MainWindow.g.h"

#include "velocitycopy/destination_catalog.hpp"
#include "velocitycopy/destination_navigation_worker.hpp"
#include "velocitycopy/drop_flow.hpp"
#include "velocitycopy/execution_control.hpp"
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/job_planner.hpp"
#include "velocitycopy/job_planning_worker.hpp"
#include "velocitycopy/live_copy_plan.hpp"
#include "velocitycopy/queue_archive.hpp"
#include "velocitycopy/shell_request.hpp"
#include "velocitycopy/shell_session.hpp"
#include "velocitycopy/ui_snapshot.hpp"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

namespace winrt::VelocityCopyUI::implementation {
struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();
    ~MainWindow();

    void ShowFromTray();
    void HandleShellRequest(const velocitycopy::ShellRequest& request);

    void OnDragEnter(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
    void OnDragOver(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
    void OnDragLeave(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
    void OnDrop(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
    void OnDestinationClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnDestinationFolderClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnDestinationBackClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnChooseCurrentFolderClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnBrowseClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnPreserveClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnDirectClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnBackClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueOrStartCopyClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnPauseClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSkipClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnStopClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnCancelClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnSaveQueueClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnLoadQueueClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnMenuPauseClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnMenuStopClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnMenuCancelClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
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
    struct AppendGate {
        std::mutex mutex;
        std::condition_variable_any condition;
        bool accepting{true};
        std::size_t planning_count{};
    };

    winrt::fire_and_forget HandleDropAsync(Microsoft::UI::Xaml::DragEventArgs args);
    winrt::fire_and_forget BeginShellDestinationAsync(std::vector<std::filesystem::path> sources);
    winrt::fire_and_forget BeginShellLayoutAsync(velocitycopy::CopyJob job);
    winrt::fire_and_forget BrowseAsync();
    winrt::fire_and_forget ShowConflictDialogAsync(velocitycopy::JobResult conflict);
    winrt::fire_and_forget SaveQueueAsync();
    winrt::fire_and_forget LoadQueueAsync();
    winrt::fire_and_forget MaybeOfferRecoveryAsync();
    void ConfigureQueuePersistenceMenu();
    void InitializeTrayIntegration();
    void RemoveTrayIntegration() noexcept;
    void HideToTray() noexcept;
    void ShowTrayMenu(POINT anchor) noexcept;
    void ExitFromTray() noexcept;
    void SetEfficiencyMode(bool enabled) noexcept;
    void RefreshEfficiencyMode() noexcept;
    [[nodiscard]] bool HasActiveWorkForEfficiencyMode() noexcept;
    void PersistRecoveryQueueNoThrow() noexcept;
    void CaptureClipboardFileSelection() noexcept;
    static LRESULT CALLBACK TraySubclassProc(
        HWND hwnd,
        UINT message,
        WPARAM wparam,
        LPARAM lparam,
        UINT_PTR subclass_id,
        DWORD_PTR ref_data);
    void LoadDestinations();
    void NavigateDestination(std::filesystem::path folder);
    void ApplyDestinationNavigation(velocitycopy::DestinationNavigationResult result);
    void SelectDestination(std::filesystem::path destination);
    void QueueOrStartCopy(velocitycopy::CopyJob job);
    void EnqueueAppend(
        velocitycopy::CopyJob job,
        std::shared_ptr<velocitycopy::LiveCopyPlan> target_plan,
        std::shared_ptr<velocitycopy::ExecutionControl> target_control,
        std::shared_ptr<AppendGate> target_gate,
        bool reservation_already_held);
    void StartCopy(velocitycopy::CopyJob job);
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
    void RefreshQueueEditCommandState();
    [[nodiscard]] std::vector<std::uint64_t> SelectedPendingIds();
    void ResizeWindow(int height_epx);
    void ResizeWindowToContent();
    void SetExecutionButtonsPlanning();
    void SetExecutionButtonsRunning();
    void SetExecutionButtonsIdle();
    void SetExecutionButtonsStopped();
    void SetExecutionButtonsConflict();
    void FinalizeStoppedSessionIfEmpty();
    void FinalizeConflictSessionIfEmpty();
    void ApplySnapshot(const velocitycopy::UiSnapshot& snapshot);
    void FinishCopy(const velocitycopy::JobResult& result);
    void ShowError();
    static hstring PreviewText(const velocitycopy::DropChoicePreview& preview);
    static hstring FormatCapacity(const velocitycopy::DestinationCapacity& capacity);
    static hstring FormatSpeed(double bytes_per_second);
    static hstring FormatEta(double seconds);

    velocitycopy::DropFlowController flow_;
    velocitycopy::DestinationCatalog destination_catalog_;
    velocitycopy::DestinationNavigationWorker destination_navigation_;
    velocitycopy::JobPlanner planner_;
    velocitycopy::JobPlanningWorker append_planner_;
    velocitycopy::JobExecutor executor_;
    std::shared_ptr<velocitycopy::ExecutionControl> execution_control_;
    std::shared_ptr<AppendGate> append_gate_;
    velocitycopy::ShellSession shell_session_;
    velocitycopy::ProgressPresenter presenter_{100};
    std::vector<velocitycopy::DropItem> dropped_items_;
    std::filesystem::path current_destination_folder_;
    std::filesystem::path active_destination_;
    velocitycopy::FileOperation pending_flow_operation_{velocitycopy::FileOperation::Copy};
    velocitycopy::FileOperation active_operation_{velocitycopy::FileOperation::Copy};
    std::deque<velocitycopy::CopyJob> deferred_same_destination_jobs_;
    std::deque<velocitycopy::CopyJob> deferred_interrupted_jobs_;
    std::deque<velocitycopy::CopyJob> queued_sessions_;
    std::shared_ptr<velocitycopy::LiveCopyPlan> live_plan_;
    std::vector<velocitycopy::PlannedFile> queue_snapshot_;
    Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{nullptr};
    Microsoft::UI::Xaml::Controls::Button queue_options_button_{nullptr};
    Microsoft::UI::Xaml::Controls::MenuFlyoutItem save_queue_menu_item_{nullptr};
    Microsoft::UI::Xaml::Controls::MenuFlyoutItem load_queue_menu_item_{nullptr};
    Microsoft::UI::Xaml::Controls::MenuFlyoutItem pause_menu_item_{nullptr};
    Microsoft::UI::Xaml::Controls::MenuFlyoutItem stop_menu_item_{nullptr};
    Microsoft::UI::Xaml::Controls::MenuFlyoutItem cancel_menu_item_{nullptr};
    std::atomic_bool cancel_requested_{false};
    bool paused_{};
    bool stopped_session_{};
    bool conflict_session_{};
    bool stop_requested_{};
    bool resume_requested_{};
    bool initial_size_applied_{};
    bool current_file_skippable_{};
    bool recovery_prompt_checked_{};
    bool recovery_prompt_active_{};
    std::uint64_t next_job_id_{1};
    std::uint64_t last_queue_completed_files_{};
    std::uint64_t shell_layout_generation_{};
    std::uint64_t destination_navigation_generation_{};
    std::uint64_t current_file_id_{};
    std::uint64_t conflict_replace_file_id_{};
    HWND hwnd_{};
    HICON tray_icon_{};
    NOTIFYICONDATAW tray_data_{};
    bool tray_added_{};
    bool tray_v4_{};
    bool tray_exit_requested_{};
    bool tray_window_hidden_{};
    bool efficiency_mode_enabled_{};
    bool session_ending_{};
    std::jthread copy_thread_;
};
}

namespace winrt::VelocityCopyUI::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
