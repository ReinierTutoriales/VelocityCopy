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

    void HandleShellRequest(const velocitycopy::ShellRequest& request);

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
    void OnQueueMoveUpClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueMoveDownClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueRemoveClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueDragItemsCompleted(
        Microsoft::UI::Xaml::Controls::ListViewBase const&,
        Microsoft::UI::Xaml::Controls::DragItemsCompletedEventArgs const&);

private:
    struct AppendGate {
        std::mutex mutex;
        std::condition_variable_any condition;
        bool accepting{true};
        std::size_t planning_count{};
    };

    winrt::fire_and_forget HandleDropAsync(Windows::ApplicationModel::DataTransfer::DataPackageView data_view);
    winrt::fire_and_forget BeginShellLayoutAsync(velocitycopy::CopyJob job);
    winrt::fire_and_forget BrowseAsync();
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
    void ResumeStoppedCopy();
    void StartNextQueuedSession();
    [[nodiscard]] velocitycopy::JobResult RunLivePlanSession(
        std::shared_ptr<velocitycopy::LiveCopyPlan> plan,
        std::shared_ptr<velocitycopy::ExecutionControl> control,
        std::shared_ptr<AppendGate> gate,
        std::stop_token stop_token,
        bool publish_plan);
    void PublishLivePlan(std::shared_ptr<velocitycopy::LiveCopyPlan> plan);
    void RefreshQueue();
    [[nodiscard]] std::vector<std::uint64_t> SelectedPendingIds();
    void ResizeWindow(int height_epx);
    void SetExecutionButtonsPlanning();
    void SetExecutionButtonsRunning();
    void SetExecutionButtonsIdle();
    void SetExecutionButtonsStopped();
    void FinalizeStoppedSessionIfEmpty();
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
    std::deque<velocitycopy::CopyJob> deferred_same_destination_jobs_;
    std::deque<velocitycopy::CopyJob> deferred_after_stop_jobs_;
    std::deque<velocitycopy::CopyJob> queued_sessions_;
    std::shared_ptr<velocitycopy::LiveCopyPlan> live_plan_;
    std::vector<velocitycopy::PlannedFile> queue_snapshot_;
    Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{nullptr};
    std::atomic_bool cancel_requested_{false};
    bool paused_{};
    bool stopped_session_{};
    bool stop_requested_{};
    bool resume_requested_{};
    bool initial_size_applied_{};
    bool current_file_skippable_{};
    std::uint64_t next_job_id_{1};
    std::uint64_t last_queue_completed_files_{};
    std::uint64_t shell_layout_generation_{};
    std::uint64_t current_file_id_{};
    std::jthread copy_thread_;
};
}

namespace winrt::VelocityCopyUI::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
