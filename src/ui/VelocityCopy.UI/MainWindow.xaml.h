#pragma once

#include "MainWindow.g.h"

#include "velocitycopy/destination_catalog.hpp"
#include "velocitycopy/destination_navigation_worker.hpp"
#include "velocitycopy/drop_flow.hpp"
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/job_planner.hpp"
#include "velocitycopy/live_copy_plan.hpp"
#include "velocitycopy/ui_snapshot.hpp"

#include <memory>

namespace winrt::VelocityCopyUI::implementation {
struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();

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
    void OnStartCopyClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnCancelClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueMoveUpClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueMoveDownClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueRemoveClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnQueueDragItemsCompleted(
        Microsoft::UI::Xaml::Controls::ListViewBase const&,
        Microsoft::UI::Xaml::Controls::DragItemsCompletedEventArgs const&);

private:
    winrt::fire_and_forget HandleDropAsync(Windows::ApplicationModel::DataTransfer::DataPackageView data_view);
    winrt::fire_and_forget BrowseAsync();
    void LoadDestinations();
    void NavigateDestination(std::filesystem::path folder);
    void ApplyDestinationNavigation(velocitycopy::DestinationNavigationResult result);
    void SelectDestination(std::filesystem::path destination);
    void StartCopy(velocitycopy::CopyJob job);
    void PublishLivePlan(std::shared_ptr<velocitycopy::LiveCopyPlan> plan);
    void RefreshQueue();
    [[nodiscard]] std::vector<std::uint64_t> SelectedPendingIds() const;
    void ResizeWindow(int height_epx);
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
    velocitycopy::JobExecutor executor_;
    velocitycopy::ProgressPresenter presenter_{100};
    std::vector<velocitycopy::DropItem> dropped_items_;
    std::filesystem::path current_destination_folder_;
    std::shared_ptr<velocitycopy::LiveCopyPlan> live_plan_;
    std::vector<velocitycopy::PlannedFile> queue_snapshot_;
    Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{nullptr};
    std::atomic_bool cancel_requested_{false};
    std::uint64_t next_job_id_{1};
    std::uint64_t last_queue_completed_files_{};
    std::jthread copy_thread_;
};
}

namespace winrt::VelocityCopyUI::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
