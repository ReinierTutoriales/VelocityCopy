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
    std::cerr << "strategy architecture contract " << code << ": " << message << '\n';
    return code;
}
} // namespace

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto selector_h = read_all(root / "src/core/include/velocitycopy/strategy_selector.hpp");
    const auto selector_cpp = read_all(root / "src/core/strategy_selector.cpp");
    const auto executor_h = read_all(root / "src/core/include/velocitycopy/job_executor.hpp");
    const auto executor_cpp = read_all(root / "src/core/job_executor.cpp");
    const auto engine_h = read_all(root / "src/core/include/velocitycopy/copy_engine.hpp");
    const auto engine_cpp = read_all(root / "src/core/copy_engine.cpp");

    if (selector_h.empty() || selector_cpp.empty() || executor_h.empty() || executor_cpp.empty() ||
        engine_h.empty() || engine_cpp.empty()) {
        return fail(1, "required production source missing");
    }

    if (!contains(selector_h, "std::uint32_t copy_flags{}") ||
        !contains(selector_cpp, "recommendation.copy_flags = COPY_FILE_NO_BUFFERING") ||
        !contains(selector_cpp, "recommendation.copy_flags = COPY_FILE_REQUEST_COMPRESSED_TRAFFIC")) {
        return fail(2, "strategy selector must emit native CopyFile2 flags");
    }

    if (!contains(executor_h, "std::uint32_t copy_flags{}") ||
        !contains(executor_cpp, "options.copy_flags = shared_copy_flags") ||
        !contains(executor_cpp, "CopyOptions{resume_from_pause, existing_policy, options.copy_flags}")) {
        return fail(3, "JobExecutor must carry selected flags into the live production copy path");
    }

    if (!contains(engine_h, "std::uint32_t copy_flags{}") ||
        !contains(engine_cpp, "parameters.dwCopyFlags = options.copy_flags")) {
        return fail(4, "CopyEngine must consume the selected native flags");
    }

    if (!contains(executor_cpp, "shared_copy_flags &= recommendation.copy_flags")) {
        return fail(5, "multi-root execution must keep only flags supported by every source root");
    }

    return 0;
}
