#include "pch.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {
namespace {

bool revalidate_plan_sources(velocitycopy::CopyPlan& plan) noexcept {
    try {
        plan.total_bytes = 0;
        plan.largest_file_bytes = 0;
        for (auto& file : plan.files) {
            std::error_code ec;
            const auto size = std::filesystem::file_size(file.source, ec);
            if (ec) return false;
            if (std::numeric_limits<std::uint64_t>::max() - plan.total_bytes < size) return false;
            file.size = size;
            plan.total_bytes += size;
            plan.largest_file_bytes = (std::max)(plan.largest_file_bytes, size);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool merge_current_append_jobs(velocitycopy::QueueArchive& archive) {
    if (archive.current_append_jobs.empty()) return true;

    velocitycopy::JobPlanner planner;
    std::size_t first_append = 0;
    if (!archive.current_plan) {
        archive.current_plan = planner.build(archive.current_append_jobs.front());
        first_append = 1;
    }

    velocitycopy::LiveCopyPlan merged(std::move(*archive.current_plan));
    for (std::size_t index = first_append; index < archive.current_append_jobs.size(); ++index) {
        auto append_plan = planner.build(archive.current_append_jobs[index]);
        const auto append_result = merged.append(std::move(append_plan), true);
        if (append_result != velocitycopy::LivePlanAppendResult::Appended) return false;
    }
    archive.current_plan = merged.export_remaining_plan();
    archive.current_append_jobs.clear();
    return true;
}

} // namespace

void MainWindow::PersistRecoveryQueueNoThrow() noexcept {
    try {
        velocitycopy::QueueArchive archive{};

        if (live_plan_) {
            archive.current_plan = live_plan_->export_remaining_plan();
            if (archive.current_plan->files.empty() && archive.current_plan->directories.empty()) {
                archive.current_plan.reset();
            }
        }

        archive.current_append_jobs.reserve(
            deferred_same_destination_jobs_.size() + deferred_interrupted_jobs_.size());
        archive.current_append_jobs.insert(
            archive.current_append_jobs.end(),
            deferred_same_destination_jobs_.begin(),
            deferred_same_destination_jobs_.end());
        archive.current_append_jobs.insert(
            archive.current_append_jobs.end(),
            deferred_interrupted_jobs_.begin(),
            deferred_interrupted_jobs_.end());
        archive.queued_jobs.assign(queued_sessions_.begin(), queued_sessions_.end());

        const auto dir = velocitycopy::app_data_directory();
        if (!dir || session_id_.empty()) return;
        const auto path = velocitycopy::recovery_file(*dir, session_id_);

        if (!archive.current_plan &&
            archive.current_append_jobs.empty() &&
            archive.queued_jobs.empty()) {
            velocitycopy::retire_recovery_file(path);
            return;
        }

        (void)velocitycopy::QueueArchiveStore{}.save(path, archive);
    } catch (...) {
    }
}

void MainWindow::ConfigureQueuePersistenceMenu() {
    try {
        queue_options_button_ = OptionsButton();

        MenuFlyout menu;
        auto weak = get_weak();
        menu.Opening([weak](IInspectable const&, IInspectable const&) {
            if (auto self = weak.get()) {
                self->RefreshExecutionMenuState();
                self->RefreshQueueCommandState();
            }
        });
        skip_menu_item_ = MenuFlyoutItem{};
        stop_menu_item_ = MenuFlyoutItem{};
        save_queue_menu_item_ = MenuFlyoutItem{};
        load_queue_menu_item_ = MenuFlyoutItem{};
        about_menu_item_ = MenuFlyoutItem{};
        MenuFlyoutItem hide_to_tray_menu_item;

        try {
            Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
            const auto options_label = loader.GetString(L"ActionQueueOptions");
            skip_menu_item_.Text(loader.GetString(L"ActionSkip"));
            stop_menu_item_.Text(loader.GetString(L"ActionStop"));
            save_queue_menu_item_.Text(loader.GetString(L"ActionSaveQueue"));
            load_queue_menu_item_.Text(loader.GetString(L"ActionLoadQueue"));
            about_menu_item_.Text(loader.GetString(L"ActionAbout"));
            hide_to_tray_menu_item.Text(loader.GetString(L"ActionHideToTray"));
            ToolTipService::SetToolTip(queue_options_button_, box_value(options_label));
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(queue_options_button_, options_label);
        } catch (...) {
            skip_menu_item_.Text(L"Skip");
            stop_menu_item_.Text(L"Stop");
            save_queue_menu_item_.Text(L"Save queue");
            load_queue_menu_item_.Text(L"Load queue");
            about_menu_item_.Text(L"About VelocityCopy");
            hide_to_tray_menu_item.Text(L"Hide to tray");
            ToolTipService::SetToolTip(queue_options_button_, box_value(L"Options"));
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(queue_options_button_, L"Options");
        }

        skip_menu_item_.Click({this, &MainWindow::OnMenuSkipClick});
        stop_menu_item_.Click({this, &MainWindow::OnMenuStopClick});
        save_queue_menu_item_.Click({this, &MainWindow::OnSaveQueueClick});
        load_queue_menu_item_.Click({this, &MainWindow::OnLoadQueueClick});
        about_menu_item_.Click({this, &MainWindow::OnAboutClick});
        hide_to_tray_menu_item.Click([weak](IInspectable const&, RoutedEventArgs const&) {
            if (auto self = weak.get()) self->HideToTray();
        });

        menu.Items().Append(skip_menu_item_);
        menu.Items().Append(stop_menu_item_);
        menu.Items().Append(MenuFlyoutSeparator{});
        menu.Items().Append(save_queue_menu_item_);
        menu.Items().Append(load_queue_menu_item_);
        menu.Items().Append(MenuFlyoutSeparator{});
        menu.Items().Append(about_menu_item_);
        menu.Items().Append(hide_to_tray_menu_item);
        queue_options_button_.Flyout(menu);

        try {
            Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
            const auto queue_label = loader.GetString(L"ActionShowQueue");
            ToolTipService::SetToolTip(QueueButton(), box_value(queue_label));
            Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(QueueButton(), queue_label);
        } catch (...) {
        }

        RefreshQueueCommandState();
        RefreshExecutionMenuState();
    } catch (...) {
    }
}

void MainWindow::RefreshQueueCommandState() {
    if (!save_queue_menu_item_ || !load_queue_menu_item_) return;

    bool planning = false;
    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        planning = append_gate_->planning_count != 0;
    }

    const bool has_work =
        (live_plan_ && live_plan_->remaining_files() != 0) ||
        !deferred_same_destination_jobs_.empty() ||
        !deferred_interrupted_jobs_.empty() ||
        !queued_sessions_.empty();

    save_queue_menu_item_.IsEnabled(has_work && !planning);
    load_queue_menu_item_.IsEnabled(
        !execution_control_ && !live_plan_ && !stopped_session_ &&
        !conflict_session_ && !stop_requested_ && queued_sessions_.empty());
}

void MainWindow::OnMenuSkipClick(IInspectable const& sender, RoutedEventArgs const& args) {
    OnSkipClick(sender, args);
    RefreshExecutionMenuState();
}

void MainWindow::OnMenuStopClick(IInspectable const& sender, RoutedEventArgs const& args) {
    OnStopClick(sender, args);
    RefreshExecutionMenuState();
}

void MainWindow::RefreshExecutionMenuState() {
    if (!skip_menu_item_ || !stop_menu_item_) return;
    const bool active = execution_control_ != nullptr;
    skip_menu_item_.IsEnabled(velocitycopy::can_skip_current_file(
        active, current_file_id_, current_file_skippable_,
        paused_, stopped_session_, conflict_session_, stop_requested_));
    stop_menu_item_.IsEnabled(active && !stopped_session_ && !conflict_session_ && !stop_requested_);
}

void MainWindow::OnSaveQueueClick(IInspectable const&, RoutedEventArgs const&) {
    SaveQueueAsync();
}

void MainWindow::OnLoadQueueClick(IInspectable const&, RoutedEventArgs const&) {
    LoadQueueAsync();
}

fire_and_forget MainWindow::SaveQueueAsync() {
    auto lifetime = get_strong();

    std::optional<std::filesystem::path> selected_path;
    try {
        Microsoft::Windows::Storage::Pickers::FileSavePicker picker(AppWindow().Id());
        picker.SuggestedFileName(L"VelocityCopy Queue.vcq");
        auto result = co_await picker.PickSaveFileAsync();
        if (result) selected_path = std::filesystem::path(result.Path().c_str());
    } catch (...) {
        ShowError();
        co_return;
    }
    if (!selected_path) co_return;

    bool planning = false;
    if (append_gate_) {
        std::lock_guard gate_lock(append_gate_->mutex);
        planning = append_gate_->planning_count != 0;
    }
    if (planning) {
        RefreshQueueCommandState();
        co_return;
    }

    auto current_plan = live_plan_;
    std::vector<velocitycopy::CopyJob> current_append_jobs;
    current_append_jobs.reserve(deferred_same_destination_jobs_.size() + deferred_interrupted_jobs_.size());
    current_append_jobs.insert(current_append_jobs.end(), deferred_same_destination_jobs_.begin(), deferred_same_destination_jobs_.end());
    current_append_jobs.insert(current_append_jobs.end(), deferred_interrupted_jobs_.begin(), deferred_interrupted_jobs_.end());
    std::vector<velocitycopy::CopyJob> queued(queued_sessions_.begin(), queued_sessions_.end());

    if (!current_plan && current_append_jobs.empty() && queued.empty()) co_return;

    auto path = std::move(*selected_path);
    if (path.extension().empty()) path += L".vcq";

    auto dispatcher = dispatcher_;
    auto weak = get_weak();
    co_await resume_background();

    bool saved = false;
    try {
        velocitycopy::QueueArchive archive{};
        if (current_plan) {
            archive.current_plan = current_plan->export_remaining_plan();
            if (archive.current_plan->files.empty() && archive.current_plan->directories.empty()) {
                archive.current_plan.reset();
            }
        }
        archive.current_append_jobs = std::move(current_append_jobs);
        archive.queued_jobs = std::move(queued);
        saved = velocitycopy::QueueArchiveStore{}.save(path, archive);
    } catch (...) {
        saved = false;
    }

    (void)dispatcher.TryEnqueue([weak, saved]() {
        if (auto self = weak.get(); self && !saved) self->ShowError();
    });
}

fire_and_forget MainWindow::LoadQueueAsync() {
    auto lifetime = get_strong();

    if (execution_control_ || live_plan_ || stopped_session_ || conflict_session_ ||
        stop_requested_ || !queued_sessions_.empty()) {
        RefreshQueueCommandState();
        co_return;
    }

    std::optional<std::filesystem::path> selected_path;
    try {
        Microsoft::Windows::Storage::Pickers::FileOpenPicker picker(AppWindow().Id());
        picker.FileTypeFilter().Append(L".vcq");
        auto result = co_await picker.PickSingleFileAsync();
        if (result) selected_path = std::filesystem::path(result.Path().c_str());
    } catch (...) {
        ShowError();
        co_return;
    }
    if (!selected_path) co_return;

    auto dispatcher = dispatcher_;
    auto weak = get_weak();
    const auto path = std::move(*selected_path);
    co_await resume_background();

    auto archive = velocitycopy::QueueArchiveStore{}.load(path);
    if (archive) {
        try {
            if (archive->current_plan && !revalidate_plan_sources(*archive->current_plan)) archive.reset();
            if (archive && !merge_current_append_jobs(*archive)) archive.reset();
        } catch (...) {
            archive.reset();
        }
    }

    (void)dispatcher.TryEnqueue([weak, archive = std::move(archive)]() mutable {
        auto self = weak.get();
        if (!self) return;
        if (!archive) {
            self->ShowError();
            return;
        }
        if (self->execution_control_ || self->live_plan_ || self->stopped_session_ ||
            self->conflict_session_ || self->stop_requested_ || !self->queued_sessions_.empty()) {
            self->ShowError();
            return;
        }

        for (auto& job : archive->queued_jobs) {
            job.id = self->next_job_id_++;
            job.state = velocitycopy::JobState::Pending;
            self->queued_sessions_.push_back(std::move(job));
        }

        if (archive->current_plan &&
            (!archive->current_plan->files.empty() || !archive->current_plan->directories.empty())) {
            self->StartCopyPlan(std::move(*archive->current_plan));
        } else {
            self->StartNextQueuedSession();
            self->RefreshQueueCommandState();
        }
    });
}

void MainWindow::StartCopyPlan(velocitycopy::CopyPlan plan) {
    ResetTransferSurface();
    if (plan.destination_root.empty() || (plan.files.empty() && plan.directories.empty())) {
        StartNextQueuedSession();
        RefreshQueueCommandState();
        return;
    }

    active_destination_ = plan.destination_root;
    active_operation_ = plan.operation;
    stopped_session_ = false;
    conflict_session_ = false;
    conflict_replace_file_id_ = 0;
    stop_requested_ = false;
    resume_requested_ = false;
    current_file_id_ = 0;
    current_file_skippable_ = false;
    cancel_requested_.store(false, std::memory_order_relaxed);
    presenter_.reset();
    last_queue_completed_files_ = 0;
    execution_control_ = std::make_shared<velocitycopy::ExecutionControl>();
    append_gate_ = std::make_shared<AppendGate>();
    queue_snapshot_.clear();
    QueueList().Items().Clear();
    QueueCountText().Text(L"0");
    QueuePanel().Visibility(Visibility::Collapsed);
    ResizeWindow(72);
    SetProgressFraction(0.0);

    auto live = std::make_shared<velocitycopy::LiveCopyPlan>(std::move(plan));
    live_plan_ = live;
    PublishLivePlan(live);

    auto control = execution_control_;
    auto gate = append_gate_;
    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    copy_thread_ = std::jthread([this, weak, dispatcher, live, control, gate](std::stop_token stop_token) {
        const auto result = RunLivePlanSession(live, control, gate, stop_token, false, 0);
        (void)dispatcher.TryEnqueue([weak, result]() {
            if (auto self = weak.get()) self->FinishCopy(result);
        });
    });
    RefreshQueueCommandState();
}

} // namespace winrt::VelocityCopyUI::implementation
