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
    const auto archive = read_all(root / "src/core/queue_archive.cpp");
    const auto ipc = read_all(root / "src/core/ipc_protocol.cpp");

    if (engine.empty() || archive.empty() || ipc.empty()) {
        return fail(1, "required core source missing");
    }

    const auto reparse_check = engine.find("source_is_unsafe_reparse_point(source)");
    const auto destination_check = engine.find("destination_chain_contains_reparse_point(destination, destination_guard)");
    const auto copy_call = engine.find("CopyFile2(");
    if (reparse_check == std::string::npos ||
        !contains(engine, "FILE_ATTRIBUTE_REPARSE_POINT") ||
        !contains(engine, "ERROR_CANT_ACCESS_FILE") ||
        copy_call == std::string::npos ||
        reparse_check > copy_call) {
        return fail(2, "source reparse point must be revalidated immediately before CopyFile2");
    }

    if (destination_check == std::string::npos ||
        !contains(engine, "FILE_FLAG_OPEN_REPARSE_POINT") ||
        !contains(engine, "FileAttributeTagInfo") ||
        !contains(engine, "FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE") ||
        contains(engine, "COPY_FILE_COPY_SYMLINK") ||
        destination_check > copy_call) {
        return fail(6, "destination reparse defenses must precede CopyFile2 without re-enabling symlink traversal");
    }

    if (!contains(engine, "COPYFILE2_EXTENDED_PARAMETERS_V2") ||
        !contains(engine, "parameters.ioDesiredSize") ||
        !contains(engine, "COPYFILE2_CALLBACK_POLL_CONTINUE")) {
        return fail(7, "interactive CopyFile2 path must keep bounded I/O cycles and heartbeat callbacks");
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

    return 0;
}
