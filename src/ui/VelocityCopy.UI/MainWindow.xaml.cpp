#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

using namespace winrt;
using namespace Windows::ApplicationModel::DataTransfer;
using namespace Windows::Storage;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {

MainWindow::MainWindow() {
    InitializeComponent();
    dispatcher_ = Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

    try {
        SystemBackdrop(Microsoft::UI::Xaml::Media::MicaBackdrop{});
    } catch (...) {
    }

    ExtendsContentIntoTitleBar(true);
    SetTitleBar(TitleBarDragRegion());

    try {
        HWND hwnd{};
        auto window_native = this->m_inner.as<::IWindowNative>();
        if (SUCCEEDED(window_native->get_WindowHandle(&hwnd)) && hwnd != nullptr) {
            const auto dpi = GetDpiForWindow(hwnd);
            const int width = MulDiv(460, static_cast<int>(dpi), 96);
            const int height = MulDiv(156, static_cast<int>(dpi), 96);
            SetWindowPos(hwnd, nullptr, 0, 0, width, height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    } catch (...) {
    }
}

void MainWindow::OnDragOver(IInspectable const&, DragEventArgs const& args) {
    if (args.DataView().Contains(StandardDataFormats::StorageItems())) {
        args.AcceptedOperation(DataPackageOperation::Copy);
        DragOverlay().Visibility(Visibility::Visible);
    }
}

void MainWindow::OnDragLeave(IInspectable const&, DragEventArgs const&) {
    DragOverlay().Visibility(Visibility::Collapsed);
}

void MainWindow::OnDrop(IInspectable const&, DragEventArgs const& args) {
    DragOverlay().Visibility(Visibility::Collapsed);
    HandleDropAsync(args.DataView());
}

fire_and_forget MainWindow::HandleDropAsync(DataPackageView data_view) {
    auto lifetime = get_strong();
    try {
        auto storage_items = co_await data_view.GetStorageItemsAsync();
        std::vector<velocitycopy::DropItem> items;
        items.reserve(storage_items.Size());

        for (auto const& item : storage_items) {
            const auto path = item.Path();
            if (path.empty()) {
                continue;
            }
            const auto kind = item.IsOfType(StorageItemTypes::Folder)
                ? velocitycopy::DropItemKind::Directory
                : velocitycopy::DropItemKind::File;
            items.push_back({std::filesystem::path(path.c_str()), kind});
        }

        if (items.empty()) {
            ShowError();
            co_return;
        }

        dropped_items_ = std::move(items);
        flow_.begin(dropped_items_);
        DestinationStep().Visibility(Visibility::Visible);
        LayoutStep().Visibility(Visibility::Collapsed);
        StartCopyButton().IsEnabled(false);
        PreserveToggle().IsChecked(false);
        DirectToggle().IsChecked(false);
        ErrorBar().IsOpen(false);
        LoadDestinations();
        DropFlowFlyout().ShowAt(RootGrid());
    } catch (...) {
        ShowError();
    }
}

void MainWindow::LoadDestinations() {
    auto children = DestinationItems().Children();
    children.Clear();

    for (const auto& entry : destination_catalog_.enumerate()) {
        Button button;
        button.HorizontalAlignment(HorizontalAlignment::Stretch);
        button.HorizontalContentAlignment(HorizontalAlignment::Left);
        std::wstring display = entry.label;
        if (!entry.path.empty()) {
            display += L"  ";
            display += entry.path.wstring();
        }
        button.Content(box_value(hstring(display)));
        button.Tag(box_value(hstring(entry.path.wstring())));
        button.Click({this, &MainWindow::OnDestinationClick});
        children.Append(button);
    }
}

void MainWindow::OnDestinationClick(IInspectable const& sender, RoutedEventArgs const&) {
    try {
        const auto button = sender.as<Button>();
        const auto value = unbox_value<hstring>(button.Tag());
        SelectDestination(std::filesystem::path(value.c_str()));
    } catch (...) {
        ShowError();
    }
}

void MainWindow::OnBrowseClick(IInspectable const&, RoutedEventArgs const&) {
    BrowseAsync();
}

fire_and_forget MainWindow::BrowseAsync() {
    auto lifetime = get_strong();
    try {
        Microsoft::Windows::Storage::Pickers::FolderPicker picker(AppWindow().Id());
        auto result = co_await picker.PickSingleFolderAsync();
        if (result) {
            SelectDestination(std::filesystem::path(result.Path().c_str()));
        }
    } catch (...) {
        ShowError();
    }
}

void MainWindow::SelectDestination(std::filesystem::path destination) {
    const auto validation = flow_.choose_destination(destination);
    if (validation != velocitycopy::DestinationValidation::Valid || !flow_.menu()) {
        ShowError();
        return;
    }

    ErrorBar().IsOpen(false);
    SelectedDestinationText().Text(hstring(destination.wstring()));
    PreservePreview().Text(PreviewText(flow_.menu()->preserve));
    DirectPreview().Text(PreviewText(flow_.menu()->direct));
    DestinationStep().Visibility(Visibility::Collapsed);
    LayoutStep().Visibility(Visibility::Visible);
}

void MainWindow::OnPreserveClick(IInspectable const&, RoutedEventArgs const&) {
    PreserveToggle().IsChecked(true);
    DirectToggle().IsChecked(false);
    StartCopyButton().IsEnabled(flow_.choose_layout(velocitycopy::DestinationLayout::PreserveSourceFolder));
}

void MainWindow::OnDirectClick(IInspectable const&, RoutedEventArgs const&) {
    PreserveToggle().IsChecked(false);
    DirectToggle().IsChecked(true);
    StartCopyButton().IsEnabled(flow_.choose_layout(velocitycopy::DestinationLayout::ContentsOnly));
}

void MainWindow::OnBackClick(IInspectable const&, RoutedEventArgs const&) {
    if (!flow_.back()) {
        return;
    }
    if (flow_.stage() == velocitycopy::DropFlowStage::Destination) {
        LayoutStep().Visibility(Visibility::Collapsed);
        DestinationStep().Visibility(Visibility::Visible);
        StartCopyButton().IsEnabled(false);
        PreserveToggle().IsChecked(false);
        DirectToggle().IsChecked(false);
    }
}

void MainWindow::OnStartCopyClick(IInspectable const&, RoutedEventArgs const&) {
    auto job = flow_.make_job(next_job_id_++);
    if (!job) {
        ShowError();
        return;
    }
    DropFlowFlyout().Hide();
    StartCopy(std::move(*job));
}

void MainWindow::StartCopy(velocitycopy::CopyJob job) {
    cancel_requested_.store(false, std::memory_order_relaxed);
    presenter_.reset();
    GlobalProgress().Value(0);
    CancelButton().IsEnabled(true);
    CurrentItemText().Text(job.display_name.empty() ? hstring(L"…") : hstring(job.display_name));

    auto weak = get_weak();
    auto dispatcher = dispatcher_;

    copy_thread_ = std::jthread([this, weak, dispatcher, job = std::move(job)](std::stop_token stop_token) mutable {
        const auto result = executor_.execute(job, [this, weak, dispatcher, &stop_token](const velocitycopy::JobProgress& progress) {
            if (stop_token.stop_requested() || cancel_requested_.load(std::memory_order_relaxed)) {
                return velocitycopy::JobDecision::Cancel;
            }

            if (auto snapshot = presenter_.observe(progress, GetTickCount64())) {
                const auto value = *snapshot;
                dispatcher.TryEnqueue([weak, value]() {
                    if (auto self = weak.get()) {
                        self->ApplySnapshot(value);
                    }
                });
            }
            return velocitycopy::JobDecision::Continue;
        });

        dispatcher.TryEnqueue([weak, result]() {
            if (auto self = weak.get()) {
                self->FinishCopy(result);
            }
        });
    });
}

void MainWindow::ApplySnapshot(const velocitycopy::UiSnapshot& snapshot) {
    GlobalProgress().Value(snapshot.fraction * 100.0);
    if (!snapshot.current_source.empty()) {
        CurrentItemText().Text(hstring(snapshot.current_source.filename().wstring()));
    }
    SpeedText().Text(FormatSpeed(snapshot.bytes_per_second));
    EtaText().Text(FormatEta(snapshot.eta_seconds));
}

void MainWindow::FinishCopy(const velocitycopy::JobResult& result) {
    CancelButton().IsEnabled(false);
    if (result.success) {
        GlobalProgress().Value(100);
        SpeedText().Text(L"—");
        EtaText().Text(L"—");
    } else if (!result.cancelled) {
        ShowError();
    }
}

void MainWindow::OnCancelClick(IInspectable const&, RoutedEventArgs const&) {
    cancel_requested_.store(true, std::memory_order_relaxed);
}

void MainWindow::ShowError() {
    ErrorBar().IsOpen(true);
}

hstring MainWindow::PreviewText(const velocitycopy::DropChoicePreview& preview) {
    if (preview.destinations.empty()) {
        return {};
    }
    std::wstring text = preview.destinations.front().wstring();
    if (preview.hidden_items != 0) {
        text += L"  +";
        text += std::to_wstring(preview.hidden_items);
    }
    return hstring(text);
}

hstring MainWindow::FormatSpeed(double bytes_per_second) {
    if (bytes_per_second <= 0.0) {
        return hstring(L"—");
    }
    const double mib = bytes_per_second / (1024.0 * 1024.0);
    return hstring(std::format(L"{:.1f} MiB/s", mib));
}

hstring MainWindow::FormatEta(double seconds) {
    if (seconds <= 0.0 || !std::isfinite(seconds)) {
        return hstring(L"—");
    }
    const auto rounded = static_cast<std::uint64_t>(seconds + 0.5);
    const auto minutes = rounded / 60;
    const auto remaining = rounded % 60;
    if (minutes == 0) {
        return hstring(std::format(L"{} s", remaining));
    }
    return hstring(std::format(L"{} m {} s", minutes, remaining));
}

} // namespace winrt::VelocityCopyUI::implementation
