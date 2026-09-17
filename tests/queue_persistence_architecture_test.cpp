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
    const auto window_h = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.h");
    const auto window_cpp = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto persistence = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.QueuePersistence.cpp");
    const auto project = read_all(root / "src/ui/VelocityCopy.UI/VelocityCopy.UI.vcxproj");
    const auto cmake = read_all(root / "CMakeLists.txt");

    if (archive_h.empty() || archive_cpp.empty() || live_h.empty() || live_cpp.empty() ||
        live_export.empty() || window_h.empty() || window_cpp.empty() || persistence.empty() ||
        project.empty() || cmake.empty()) {
        return fail(1, "required production source missing");
    }

    if (!contains(archive_h, "current_plan") || !contains(archive_h, "current_append_jobs") ||
        !contains(archive_h, "queued_jobs")) {
        return fail(2, "archive must preserve current session and future sessions separately");
    }

    if (!contains(archive_cpp, "kMagic{'V','C','Q','U','E','U','E','1'}") ||
        !contains(archive_cpp, "kFormatVersion = 2") ||
        !contains(archive_cpp, "kLegacyFormatVersion = 1") ||
        !contains(archive_cpp, "kMaxEntries") || !contains(archive_cpp, "TempFileGuard") ||
        !contains(archive_cpp, "MOVEFILE_REPLACE_EXISTING") ||
        !contains(archive_cpp, "MOVEFILE_WRITE_THROUGH") || !contains(archive_cpp, "stream.peek()")) {
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
        !contains(persistence, "!stopped_session_") || !contains(persistence, "!conflict_session_") ||
        !contains(persistence, "queued_sessions_.empty()")) {
        return fail(9, "loading must remain disabled while another session exists");
    }

    if (!contains(project, "MainWindow.QueuePersistence.cpp") ||
        !contains(cmake, "src/core/queue_archive.cpp") ||
        !contains(cmake, "src/core/live_copy_plan_export.cpp") ||
        !contains(cmake, "VelocityCopyQueueArchiveTest") ||
        !contains(cmake, "VelocityCopyLivePlanStructureTest")) {
        return fail(10, "persistence production units and contract tests must be compiled");
    }

    if (contains(window_h, "queue_archive_store_")) {
        return fail(11, "unused persistence state must not remain in MainWindow");
    }

    return 0;
}
