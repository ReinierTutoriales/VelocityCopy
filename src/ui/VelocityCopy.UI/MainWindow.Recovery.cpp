#include "pch.h"
#include "MainWindow.xaml.h"
#include "App.xaml.h"

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::VelocityCopyUI::implementation {
namespace {

bool revalidate_recovery_plan(velocitycopy::CopyPlan& plan) noexcept {
    try {
        if (plan.destination_root.empty()) return false;
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

bool revalidate_recovery_job(const velocitycopy::CopyJob& job) noexcept {
    try {
        if (job.destination.empty() || job.sources.empty()) return false;
        for (const auto& source : job.sources) {
            std::error_code ec;
            if (!std::filesystem::exists(source, ec) || ec) return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool merge_recovery_append_jobs(velocitycopy::QueueArchive& archive) {
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

fire_and_forget MainWindow::MaybeOfferRecoveryAsync() {
    auto lifetime = get_strong();

    if (recovery_prompt_checked_ || recovery_prompt_active_) co_return;
    if (execution_control_ || live_plan_ || stopped_session_ || conflict_session_ ||
        stop_requested_ || !queued_sessions_.empty()) {
        co_return;
    }

    auto* app = App::Instance();
    if (app == nullptr) {
        recovery_prompt_checked_ = true;
        co_return;
    }
    const auto recovery_file = app->TakeRecoveryFile();
    if (!recovery_file) {
        recovery_prompt_checked_ = true;
        co_return;
    }
    const auto path = *recovery_file;
    const auto recovered_session_id = velocitycopy::recovery_session_id(path);
    if (!recovered_session_id) {
        recovery_prompt_checked_ = true;
        co_return;
    }

    recovery_prompt_active_ = true;
    auto ui_thread = apartment_context{};

    std::optional<velocitycopy::QueueArchive> archive;
    co_await resume_background();
    try {
        archive = velocitycopy::QueueArchiveStore{}.load(path);
        if (archive) {
            if (archive->current_plan && !revalidate_recovery_plan(*archive->current_plan)) {
                archive.reset();
            }
            if (archive && !merge_recovery_append_jobs(*archive)) {
                archive.reset();
            }
            if (archive) {
                for (const auto& job : archive->queued_jobs) {
                    if (!revalidate_recovery_job(job)) {
                        archive.reset();
                        break;
                    }
                }
            }
        }
    } catch (...) {
        archive.reset();
    }

    if (!archive) {
        velocitycopy::retire_recovery_file(path, L".invalid");
        co_await ui_thread;
        recovery_prompt_active_ = false;
        recovery_prompt_checked_ = false;
        MaybeOfferRecoveryAsync();
        co_return;
    }

    const bool has_current = archive->current_plan &&
        (!archive->current_plan->files.empty() || !archive->current_plan->directories.empty());
    if (!has_current && archive->queued_jobs.empty()) {
        velocitycopy::retire_recovery_file(path);
        co_await ui_thread;
        recovery_prompt_active_ = false;
        recovery_prompt_checked_ = false;
        MaybeOfferRecoveryAsync();
        co_return;
    }

    co_await ui_thread;

    if (execution_control_ || live_plan_ || stopped_session_ || conflict_session_ ||
        stop_requested_ || !queued_sessions_.empty()) {
        recovery_prompt_active_ = false;
        co_return;
    }

    hstring title = L"Resume interrupted transfer?";
    hstring message = L"VelocityCopy found work saved during Windows shutdown. Nothing will resume until you choose Resume.";
    hstring resume = L"Resume";
    hstring discard = L"Discard";
    try {
        Microsoft::Windows::ApplicationModel::Resources::ResourceLoader loader;
        title = loader.GetString(L"RecoveryTitle");
        message = loader.GetString(L"RecoveryMessage");
        resume = loader.GetString(L"RecoveryResume");
        discard = loader.GetString(L"RecoveryDiscard");
    } catch (...) {
    }

    // Recovery is a modal decision just like conflict handling. Keep it in a
    // native top-level dialog owned by the copier HWND so the 72 epx XAML root
    // never clips or resizes itself to host the prompt.
    const auto choice = ShowNativeDecisionDialog(
        hwnd_,
        std::wstring(title.c_str()),
        std::wstring(message.c_str()),
        std::wstring(resume.c_str()),
        std::wstring(discard.c_str()),
        false);

    if (choice == NativeDialogChoice::Secondary) {
        velocitycopy::retire_recovery_file(path);
        recovery_prompt_active_ = false;
        recovery_prompt_checked_ = false;
        MaybeOfferRecoveryAsync();
        co_return;
    }

    if (choice != NativeDialogChoice::Primary) {
        app->ReturnRecoveryFile(path);
        recovery_prompt_active_ = false;
        co_return;
    }

    if (execution_control_ || live_plan_ || stopped_session_ || conflict_session_ ||
        stop_requested_ || !queued_sessions_.empty()) {
        recovery_prompt_active_ = false;
        co_return;
    }

    session_id_ = *recovered_session_id;

    for (auto& job : archive->queued_jobs) {
        job.id = next_job_id_++;
        job.state = velocitycopy::JobState::Pending;
        queued_sessions_.push_back(std::move(job));
    }

    velocitycopy::retire_recovery_file(path);
    recovery_prompt_active_ = false;
    recovery_prompt_checked_ = true;

    if (archive->current_plan &&
        (!archive->current_plan->files.empty() || !archive->current_plan->directories.empty())) {
        StartCopyPlan(std::move(*archive->current_plan));
    } else {
        StartNextQueuedSession();
        RefreshQueueCommandState();
    }
}

} // namespace winrt::VelocityCopyUI::implementation
