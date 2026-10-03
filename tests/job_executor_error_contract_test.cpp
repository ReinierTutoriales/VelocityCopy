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
}

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto source = read_all(root / "src/core/job_executor.cpp");
    if (source.empty()) return 1;
    if (source.find("E_FAIL") != std::string::npos) return 2;
    if (source.find("HRESULT_FROM_WIN32(ERROR_INVALID_STATE)") == std::string::npos) return 3;
    if (source.find("E_UNEXPECTED") == std::string::npos) return 4;
    if (source.find("native_hresult(error.code())") == std::string::npos) return 5;
    return 0;
}
