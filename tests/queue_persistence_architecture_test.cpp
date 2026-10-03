#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}
int fail(const int code, const char* message) {
    std::cerr << "queue persistence architecture contract " << code << ": " << message << '\n';
    return code;
}
} // namespace

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto archive_h = read_all(root / "src/core/include/velocitycopy/queue_archive.hpp");
    const auto archive_cpp = read_all(root / "src/core/queue_archive.cpp");
    const auto live_h = read_all(root / "src/core/include/velocitycopy/live_copy_plan.hpp");
    const auto live_cpp = read_all(root / "src/core/live_copy_plan.cpp");
    const auto live_export = read_all(root / "src/core/live_copy_plan_export.cpp");
    const auto app_h = read_all(root / "src/ui/VelocityCopy.UI/App.xaml.h");
    const auto app_cpp = read_all(root / "src/ui/VelocityCopy.UI/App.xaml.cpp");
    const auto window_h = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.h");
    const auto window_cpp = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto persistence = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.QueuePersistence.cpp");
    const auto recovery = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Recovery.cpp");
    const auto tray = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Tray.cpp");
    const auto project = read_all(root / "src/ui/VelocityCopy.UI/VelocityCopy.UI.vcxproj");
    const auto cmake = read_all(root / "CMakeLists.txt");

    if (archive_h.empty() || archive_cpp.empty() || live_h.empty() || live_cpp.empty() ||
        live_export.empty() || app_h.empty() || app_cpp.empty() || window_h.empty() || window_cpp.empty() || persistence.empty() ||
        recovery.empty() || tray.empty() || project.empty() || cmake.empty()) {
        return fail(1, "required production source missing");
    }

    if (!contains(archive_h, "current_plan") || !contains(archive_h, "current_append_jobs") ||
        !contains(archive_h, "queued_jobs")) {
        return fail(2, "archive must preserve current session and future sessions separately");
    }

    if (!contains(archive_cpp, "kMagic{'V','C','Q','U','E','U','E','1'}") ||
        !contains(archive_cpp, "kFormatVersion = 3") ||
        !contains(archive_cpp, "kPreviousFormatVersion = 2") ||
        !contains(archive_cpp, "kLegacyFormatVersion = 1") ||
        !contains(archive_cpp, "kMaxEntries") || !contains(archive_cpp, "TempFileGuard") ||
        !contains(archive_cpp, "MOVEFILE_REPLACE_EXISTING") ||
        !contains(archive_cpp, "MOVEFILE_WRITE_THROUGH") ||
        !contains(archive_cpp, "FlushFileBuffers") ||
        !contains(archive_cpp, "source_removals") ||
        !contains(archive_cpp, "stream.peek()")) {
        return fail(3, "archive format must be versioned, bounded and atomically replaced");
    }

    if (!contains(live_h, "export_remaining_plan() const") ||
        !contains(live_export, "plan.directories = directories_") ||
        !contains(live_export, "plan.source_roots = source_roots_") ||
        !contains(live_export, "for (const auto& file : active_files_)") ||
        !contains(live_export, "for (const auto& file : pending_files_)") ||
        live_export.find("for (const auto& file : active_files_)") >
            live_export.find("for (const auto& file : pending_files_)")) {
        return fail(4, "restartable plan must export structure and active files before pending files");
    }

    if (!contains(live_cpp, "directories_.insert(") ||
        !contains(live_cpp, "source_roots_.insert(") ||
        !contains(live_cpp, "std::make_move_iterator(plan.directories.begin())") ||
        !contains(live_cpp, "std::make_move_iterator(plan.source_roots.begin())")) {
        return fail(5, "live append must preserve directories and source roots in the same session");
    }

    if (!contains(window_cpp, "ConfigureQueuePersistenceMenu()") ||
        !contains(persistence, "FileSavePicker") || !contains(persistence, "FileOpenPicker") ||
        !contains(persistence, "resume_background()") || !contains(persistence, "ActionQueueOptions") ||
        !contains(persistence, "AutomationProperties::SetName")) {
        return fail(6, "WinUI persistence commands must be compact, native and accessible");
    }

    if (!contains(persistence, "revalidate_plan_sources") ||
        !contains(persistence, "std::filesystem::file_size") ||
        !contains(persistence, "current_append_jobs") ||
        !contains(persistence, "merged.append(std::move(append_plan), true)")) {
        return fail(7, "load must revalidate sources and restore same-session appends");
    }

    if (!contains(persistence, "StartCopyPlan") || !contains(persistence, "RunLivePlanSession") ||
        contains(persistence, "executor_.execute(")) {
        return fail(8, "loaded plans must reuse the one production execution loop");
    }

    if (!contains(persistence, "!execution_control_ && !live_plan_") ||
        !contains(persistence, "interrupted_session_ == InterruptedSessionState::None") ||
        !contains(persistence, "queued_sessions_.empty()")) {
        return fail(9, "loading must remain disabled while another session exists");
    }

    if (!contains(project, "MainWindow.QueuePersistence.cpp") ||
        !contains(project, "MainWindow.Recovery.cpp") ||
        !contains(cmake, "src/core/queue_archive.cpp") ||
        !contains(cmake, "src/core/live_copy_plan_export.cpp") ||
        !contains(cmake, "VelocityCopyQueueArchiveTest") ||
        !contains(cmake, "VelocityCopyLivePlanStructureTest")) {
        return fail(10, "persistence production units and contract tests must be compiled");
    }

    if (contains(window_h, "queue_archive_store_")) {
        return fail(11, "unused persistence state must not remain in MainWindow");
    }

    if (!contains(app_cpp, "list_recovery_files(") ||
        !contains(app_h, "pending_recovery_files_") ||
        !contains(app_h, "recovery_files_initialized_") ||
        contains(recovery, "list_recovery_files(") ||
        !contains(recovery, "TakeRecoveryFile()") ||
        !contains(recovery, "session_id_ =") ||
        !contains(recovery, "RequestDecisionAsync(") ||
        !contains(recovery, "DecisionChoice::Primary") ||
        !contains(recovery, "DecisionChoice::Secondary") ||
        !contains(recovery, "tray_exit_requested_ || session_ending_") ||
        contains(recovery, "ContentDialog") ||
        contains(recovery, "XamlRoot") ||
        !contains(recovery, "revalidate_recovery_plan") ||
        !contains(recovery, "revalidate_recovery_job") ||
        !contains(recovery, "retire_recovery_file(") ||
        !contains(recovery, "std::move(*archive->current_plan), std::move(archive->source_removals)")) {
        return fail(12, "shutdown recovery must require an explicit validated native resume/discard decision");
    }

    if (contains(tray, "MaybeOfferRecoveryAsync()") ||
        !contains(tray, "ShowWindow(hwnd_, SW_SHOW)") ||
        !contains(window_h, "void OfferRecoveryIfIdle()") ||
        !contains(window_h, "recovery_prompt_checked_") ||
        !contains(window_h, "recovery_prompt_active_") ||
        !contains(app_cpp, "implementation->OfferRecoveryIfIdle()")) {
        return fail(13, "recovery prompt must be owned by explicit app activation and guarded against duplicates");
    }

    if (!contains(archive_h, "source_removals") ||
        !contains(persistence, "parked_source_removals()") ||
        !contains(persistence, "restore_parked_source_removal") ||
        !contains(recovery, "!archive->source_removals.empty()")) {
        return fail(14, "v3 must persist and restore source-removal decisions independently of CopyPlan");
    }

    if (!contains(persistence, "live_plan_->unresolved_files() != 0 ||") ||
        !contains(persistence, "!live_plan_->parked_source_removals().empty()")) {
        return fail(15, "save queue must remain enabled for source-removal-only recovery work");
    }

    if (!contains(window_h, "[[nodiscard]] bool StartCopyPlan(") ||
        !contains(persistence, "bool MainWindow::StartCopyPlan(") ||
        !contains(recovery, "adopted = StartCopyPlan(") ||
        !contains(recovery, "if (!adopted)") ||
        !contains(recovery, "app->ReturnRecoveryFile(path)") ||
        recovery.find("if (!adopted)") > recovery.rfind("retire_recovery_file(path)")) {
        return fail(16, "recovery checkpoint must survive until live state adoption succeeds");
    }

    const auto start_copy = persistence.find("bool MainWindow::StartCopyPlan(");
    const auto restore = persistence.find("restore_parked_source_removal", start_copy);
    const auto reset = persistence.find("ResetTransferSurface()", restore);
    const auto clear_destination_key = persistence.find("active_destination_key_ = {}", restore);
    const auto clear_source_key = persistence.find("active_source_key_ = {}", restore);
    if (start_copy == std::string::npos || restore == std::string::npos ||
        reset == std::string::npos || clear_destination_key == std::string::npos ||
        clear_source_key == std::string::npos ||
        reset < restore || clear_destination_key < restore || clear_source_key < restore) {
        return fail(17, "StartCopyPlan must validate restored recovery before mutating the active surface");
    }

    const auto recovery_adopt = recovery.find("bool adopted = false;");
    const auto recovery_failure = recovery.find("if (!adopted)", recovery_adopt);
    const auto recovery_session = recovery.find("session_id_ = *recovered_session_id", recovery_adopt);
    const auto recovery_queued = recovery.find("for (auto& job : archive->queued_jobs)", recovery_adopt);
    if (recovery_adopt == std::string::npos || recovery_failure == std::string::npos ||
        recovery_session == std::string::npos || recovery_queued == std::string::npos ||
        recovery_session < recovery_failure || recovery_queued < recovery_failure) {
        return fail(18, "recovery identity and queued sessions must not publish before current-state adoption succeeds");
    }

    const auto queued_only_branch = recovery.find("A queued-only archive has no current state to adopt");
    const auto publish_queued = recovery.find("for (auto& job : archive->queued_jobs)", queued_only_branch);
    const auto start_queued = recovery.find("if (queued_only) StartNextQueuedSession();", publish_queued);
    if (queued_only_branch == std::string::npos || publish_queued == std::string::npos ||
        start_queued == std::string::npos || start_queued < publish_queued) {
        return fail(19, "queued-only recovery must publish future sessions before starting the first one");
    }

    const auto recovery_error_key = recovery.find("RecoveryRestoreError", recovery_failure);
    const auto recovery_show_error = recovery.find("ShowError(recovery_error);", recovery_failure);
    if (recovery_error_key == std::string::npos || recovery_show_error == std::string::npos ||
        recovery_error_key > recovery_show_error) {
        return fail(20, "failed recovery adoption must explain that the saved recovery could not be restored");
    }

    return 0;
}
