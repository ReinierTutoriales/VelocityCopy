#include "pch.h"
#include "MainWindow.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace winrt::VelocityCopyUI::implementation {

void MainWindow::ConfigureQueuePersistenceMenu() {
    try {
        auto column = ColumnDefinition{};
        column.Width(GridLengthHelper::Auto());
        ActionStrip().ColumnDefinitions().Append(column);

        queue_options_button_ = Button{};
        Grid::SetColumn(queue_options_button_, 6);
        queue_options_button_.Margin(Thickness{6.0, 0.0, 0.0, 0.0});

        FontIcon icon;
        icon.Glyph(L"\xE712");
        queue_options_button_.Content(icon);

        MenuFlyout menu;
        save_queue_menu_item_ = MenuFlyoutItem{};
        load_queue_menu_item_ = MenuFlyoutItem{};

        try {
            Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
            save_queue_menu_item_.Text(loader.GetString(L"ActionSaveQueue"));
            load_queue_menu_item_.Text(loader.GetString(L"ActionLoadQueue"));
            ToolTipService::SetToolTip(
                queue_options_button_, box_value(loader.GetString(L"ActionQueueOptions")));
        } catch (...) {
            save_queue_menu_item_.Text(L"Save queue");
            load_queue_menu_item_.Text(L"Load queue");
            ToolTipService::SetToolTip(queue_options_button_, box_value(L"Queue options"));
        }

        save_queue_menu_item_.Click({this, &MainWindow::OnSaveQueueClick});
        load_queue_menu_item_.Click({this, &MainWindow::OnLoadQueueClick});
        menu.Items().Append(save_queue_menu_item_);
        menu.Items().Append(load_queue_menu_item_);
        queue_options_button_.Flyout(menu);
        ActionStrip().Children().Append(queue_options_button_);
        RefreshQueueCommandState();
    } catch (...) {
        // Persistence commands are auxiliary UI. Failure to construct the menu
        // must not prevent the copy engine or main window from starting.
    }
}

void MainWindow::RefreshQueueCommandState() {
    if (!save_queue_menu_item_ || !load_queue_menu_item_) {
        return;
    }

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

void MainWindow::OnSaveQueueClick(IInspectable const&, RoutedEventArgs const&) {
    SaveQueueAsync();
}

void MainWindow::OnLoadQueueClick(IInspectable const&, RoutedEventArgs const&) {
    LoadQueueAsync();
}

fire_and_forget MainWindow::SaveQueueAsync() {
    auto lifetime = get_strong();
    try {
        Microsoft::Windows::Storage::Pickers::FileSavePicker picker(AppWindow().Id());
        picker.SuggestedFileName(L"VelocityCopy Queue.vcq");
        auto result = co_await picker.PickSaveFileAsync();
        if (!result) {
            co_return;
        }

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
        current_append_jobs.reserve(
            deferred_same_destination_jobs_.size() + deferred_interrupted_jobs_.size());
        current_append_jobs.insert(
            current_append_jobs.end(),
            deferred_same_destination_jobs_.begin(),
            deferred_same_destination_jobs_.end());
        current_append_jobs.insert(
            current_append_jobs.end(),
            deferred_interrupted_jobs_.begin(),
            deferred_interrupted_jobs_.end());

        std::vector<velocitycopy::CopyJob> queued(
            queued_sessions_.begin(), queued_sessions_.end());

        if (!current_plan && current_append_jobs.empty() && queued.empty()) {
            co_return;
        }

        if (!current_plan && !current_append_jobs.empty()) {
            queued.insert(queued.begin(), current_append_jobs.begin(), current_append_jobs.end());
            current_append_jobs.clear();
        }

        std::filesystem::path path(result.Path().c_str());
        if (path.extension().empty()) {
            path += L".vcq";
        }

        auto dispatcher = dispatcher_;
        auto weak = get_weak();
        co_await resume_background();

        bool saved = false;
        try {
            velocitycopy::QueueArchive archive{};
            if (current_plan) {
                archive.current_plan = current_plan->export_remaining_plan();
                if (archive.current_plan->files.empty()) {
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
            if (auto self = weak.get(); self && !saved) {
                self->ShowError();
            }
        });
    } catch (...) {
        ShowError();
    }
}

fire_and_forget MainWindow::LoadQueueAsync() {
    auto lifetime = get_strong();
    try {
        if (execution_control_ || live_plan_ || stopped_session_ || conflict_session_ ||
            stop_requested_ || !queued_sessions_.empty()) {
            RefreshQueueCommandState();
            co_return;
        }

        Microsoft::Windows::Storage::Pickers::FileOpenPicker picker(AppWindow().Id());
        picker.FileTypeFilter().Append(L".vcq");
        auto result = co_await picker.PickSingleFileAsync();
        if (!result) {
            co_return;
        }

        const std::filesystem::path path(result.Path().c_str());
        auto dispatcher = dispatcher_;
        auto weak = get_weak();
        co_await resume_background();

        auto archive = velocitycopy::QueueArchiveStore{}.load(path);
        if (archive) {
            try {
                if (archive->current_plan) {
                    velocitycopy::LiveCopyPlan merged(std::move(*archive->current_plan));
                    velocitycopy::JobPlanner planner;
                    for (const auto& job : archive->current_append_jobs) {
                        const auto append_result = merged.append(planner.build(job), true);
                        if (append_result != velocitycopy::LivePlanAppendResult::Appended) {
                            archive.reset();
                            break;
                        }
                    }
                    if (archive) {
                        archive->current_plan = merged.export_remaining_plan();
                        archive->current_append_jobs.clear();
                    }
                } else if (!archive->current_append_jobs.empty()) {
                    archive->queued_jobs.insert(
                        archive->queued_jobs.begin(),
                        archive->current_append_jobs.begin(),
                        archive->current_append_jobs.end());
                    archive->current_append_jobs.clear();
                }
            } catch (...) {
                archive.reset();
            }
        }

        (void)dispatcher.TryEnqueue([weak, archive = std::move(archive)]() mutable {
            auto self = weak.get();
            if (!self) {
                return;
            }
            if (!archive) {
                self->ShowError();
                return;
            }
            if (self->execution_control_ || self->live_plan_ || self->stopped_session_ ||
                self->conflict_session_ || self->stop_requested_ ||
                !self->queued_sessions_.empty()) {
                self->ShowError();
                return;
            }

            for (auto& job : archive->queued_jobs) {
                job.id = self->next_job_id_++;
                job.state = velocitycopy::JobState::Pending;
                self->queued_sessions_.push_back(std::move(job));
            }

            if (archive->current_plan && !archive->current_plan->files.empty()) {
                self->StartCopyPlan(std::move(*archive->current_plan));
            } else {
                self->StartNextQueuedSession();
                self->RefreshQueueCommandState();
            }
        });
    } catch (...) {
        ShowError();
    }
}

void MainWindow::StartCopyPlan(velocitycopy::CopyPlan plan) {
    if (plan.files.empty() || plan.destination_root.empty()) {
        StartNextQueuedSession();
        RefreshQueueCommandState();
        return;
    }

    active_destination_ = plan.destination_root;
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
    ResizeWindow(156);
    GlobalProgress().Value(0);

    auto live = std::make_shared<velocitycopy::LiveCopyPlan>(std::move(plan));
    live_plan_ = live;
    PublishLivePlan(live);

    auto control = execution_control_;
    auto gate = append_gate_;
    auto weak = get_weak();
    auto dispatcher = dispatcher_;
    copy_thread_ = std::jthread(
        [this, weak, dispatcher, live, control, gate](std::stop_token stop_token) {
            const auto result = RunLivePlanSession(
                live, control, gate, stop_token, false, 0);
            (void)dispatcher.TryEnqueue([weak, result]() {
                if (auto self = weak.get()) {
                    self->FinishCopy(result);
                }
            });
        });
    RefreshQueueCommandState();
}

} // namespace winrt::VelocityCopyUI::implementation
