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
    std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    std::erase(text, '\r');
    return text;
}
bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}
int fail(int code, const char* message) {
    std::cerr << "storage security architecture contract " << code << ": " << message << '\n';
    return code;
}
}

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto engine = read_all(root / "src/core/copy_engine.cpp");
    const auto path_guard = read_all(root / "src/core/destination_path_guard.hpp");
    const auto planner = read_all(root / "src/core/job_planner.cpp");
    const auto archive = read_all(root / "src/core/queue_archive.cpp");
    const auto executor = read_all(root / "src/core/job_executor.cpp");
    const auto ipc = read_all(root / "src/core/ipc_protocol.cpp");

    if (engine.empty() || path_guard.empty() || planner.empty() || archive.empty() || executor.empty() || ipc.empty()) {
        return fail(1, "required core source missing");
    }

    const auto reparse_check = engine.find("source_is_unsafe_reparse_point(source)");
    const auto destination_check = engine.find("destination_guard.prepare_directory(parent");
    const auto copy_call = engine.find("CopyFile2(");
    if (reparse_check == std::string::npos ||
        !contains(engine, "ERROR_CANT_ACCESS_FILE") ||
        copy_call == std::string::npos ||
        reparse_check > copy_call) {
        return fail(2, "source reparse point must be revalidated immediately before CopyFile2");
    }

    if (destination_check == std::string::npos ||
        !contains(path_guard, "FILE_FLAG_OPEN_REPARSE_POINT") ||
        !contains(path_guard, "FileAttributeTagInfo") ||
        !contains(path_guard, "FILE_SHARE_READ | FILE_SHARE_WRITE") ||
        contains(path_guard, "FILE_SHARE_DELETE") ||
        !contains(path_guard, "CreateDirectoryW") ||
        !contains(path_guard, "IsReparseTagNameSurrogate") ||
        contains(engine, "COPY_FILE_COPY_SYMLINK") ||
        contains(engine, "create_directories") ||
        contains(executor, "create_directories") ||
        !contains(executor, "DestinationPathGuard") ||
        !contains(executor, "prepare_directory(directory.destination") ||
        destination_check > copy_call) {
        return fail(6, "all destination directory creation must use the shared non-reparse locked path guard before CopyFile2 or plan materialization");
    }

    if (!contains(engine, "COPYFILE2_EXTENDED_PARAMETERS_V2") ||
        !contains(engine, "parameters.ioDesiredSize") ||
        !contains(engine, "COPYFILE2_CALLBACK_POLL_CONTINUE")) {
        return fail(7, "interactive CopyFile2 path must keep bounded I/O cycles and heartbeat callbacks");
    }

    if (!contains(planner, "IsReparseTagNameSurrogate") ||
        contains(planner, "std::filesystem::is_symlink")) {
        return fail(8, "planner must reject only name-surrogate reparse points and leave cloud placeholders eligible");
    }

    if (!contains(planner, "kMaxPlannedEntries = 250'000") ||
        !contains(archive, "kMaxEntries = 250'000") ||
        !contains(archive, "kMaxArchiveBytes")) {
        return fail(9, "materialized plans and queue archives must have explicit memory bounds");
    }

    if (contains(archive, "source_roots.reserve(static_cast<std::size_t>(roots))") ||
        contains(archive, "directories.reserve(static_cast<std::size_t>(directories))") ||
        contains(archive, "files.reserve(static_cast<std::size_t>(files))") ||
        contains(archive, "sources.reserve(static_cast<std::size_t>(sources))") ||
        contains(archive, "jobs.reserve(static_cast<std::size_t>(count))")) {
        return fail(3, "archive parser must not preallocate from untrusted entry counts");
    }

    if (!contains(archive, "roots > kMaxEntries") ||
        !contains(archive, "directories > kMaxEntries") ||
        !contains(archive, "files > kMaxEntries") ||
        !contains(archive, "sources > kMaxEntries") ||
        !contains(archive, "count > kMaxEntries")) {
        return fail(4, "archive parser must preserve bounded entry-count validation");
    }

    if (contains(ipc, "request.sources.reserve(source_count)") ||
        !contains(ipc, "source_count > kMaxShellSources") ||
        !contains(ipc, "bytes.size() > kMaxShellMessageBytes")) {
        return fail(5, "IPC parser must bound declared counts without preallocating from them");
    }

    if (contains(executor, "E_FAIL") ||
        !contains(executor, "ERROR_INVALID_STATE") ||
        !contains(executor, "ERROR_UNHANDLED_EXCEPTION")) {
        return fail(10, "executor must expose explicit HRESULTs for invalid plan state and unknown exceptions instead of generic E_FAIL");
    }

    return 0;
}
