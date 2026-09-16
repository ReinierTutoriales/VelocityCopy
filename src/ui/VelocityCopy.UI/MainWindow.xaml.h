#pragma once

#include "MainWindow.g.h"

#include "velocitycopy/destination_catalog.hpp"
#include "velocitycopy/drop_flow.hpp"
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/ui_snapshot.hpp"

namespace winrt::VelocityCopyUI::implementation {
struct MainWindow : MainWindowT<MainWindow> {
    MainWindow();

    void OnDragOver(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
    void OnDragLeave(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
    void OnDrop(IInspectable const&, Microsoft::UI::Xaml::DragEventArgs const&);
    void OnDestinationClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnBrowseClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnPreserveClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnDirectClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnBackClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnStartCopyClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);
    void OnCancelClick(IInspectable const&, Microsoft::UI::Xaml::RoutedEventArgs const&);

private:
    winrt::fire_and_forget HandleDropAsync(Windows::ApplicationModel::DataTransfer::DataPackageView data_view);
    winrt::fire_and_forget BrowseAsync();
    void LoadDestinations();
    void SelectDestination(std::filesystem::path destination);
    void StartCopy(velocitycopy::CopyJob job);
    void ApplySnapshot(const velocitycopy::UiSnapshot& snapshot);
    void FinishCopy(const velocitycopy::JobResult& result);
    void ShowError();
    static hstring PreviewText(const velocitycopy::DropChoicePreview& preview);
    static hstring FormatSpeed(double bytes_per_second);
    static hstring FormatEta(double seconds);

    velocitycopy::DropFlowController flow_;
    velocitycopy::DestinationCatalog destination_catalog_;
    velocitycopy::JobExecutor executor_;
    velocitycopy::ProgressPresenter presenter_{100};
    std::vector<velocitycopy::DropItem> dropped_items_;
    Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{nullptr};
    std::atomic_bool cancel_requested_{false};
    std::uint64_t next_job_id_{1};
    std::jthread copy_thread_;
};
}

namespace winrt::VelocityCopyUI::factory_implementation {
struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};
}
