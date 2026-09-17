#include "velocitycopy/copy_engine.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {


bool write_source(const fs::path& path) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) return false;

    std::vector<char> block(1024 * 1024);
    for (std::size_t index = 0; index < block.size(); ++index) {
        block[index] = static_cast<char>((index * 31u + 17u) & 0xFFu);
    }

    for (int block_index = 0; block_index < 32; ++block_index) {
        stream.write(block.data(), static_cast<std::streamsize>(block.size()));
        if (!stream) return false;
    }
    return true;
}

bool same_contents(const fs::path& left, const fs::path& right) {
    std::ifstream a(left, std::ios::binary);
    std::ifstream b(right, std::ios::binary);
    if (!a || !b) return false;

    std::array<char, 64 * 1024> left_buffer{};
    std::array<char, 64 * 1024> right_buffer{};
    for (;;) {
        a.read(left_buffer.data(), static_cast<std::streamsize>(left_buffer.size()));
        b.read(right_buffer.data(), static_cast<std::streamsize>(right_buffer.size()));
        const auto left_count = a.gcount();
        const auto right_count = b.gcount();
        if (left_count != right_count) return false;
        if (left_count == 0) return a.eof() && b.eof();
        if (!std::equal(
                left_buffer.begin(),
                left_buffer.begin() + left_count,
                right_buffer.begin())) {
            return false;
        }
    }
}

} // namespace

int wmain() {
    const auto base = fs::temp_directory_path() /
        (L"VelocityCopyPauseResumeTest-" + std::to_wstring(GetCurrentProcessId()));
    const auto source = base / L"source.bin";
    const auto destination = base / L"destination.bin";

    std::error_code ec;
    fs::remove_all(base, ec);
    ec.clear();
    fs::create_directories(base, ec);
    if (ec || !write_source(source)) {
        fs::remove_all(base, ec);
        return 1;
    }
    velocitycopy::CopyEngine engine;
    bool pause_requested = false;
    const auto paused = engine.copy_file(
        source,
        destination,
        [&](const velocitycopy::CopyProgress& progress) {
            if (!pause_requested && progress.transferred_bytes != 0 &&
                progress.transferred_bytes < progress.total_bytes) {
                pause_requested = true;
                return velocitycopy::CopyDecision::Pause;
            }
            return velocitycopy::CopyDecision::Continue;
        });

    if (!pause_requested || paused.success ||
        paused.native_code != static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_PAUSED)) ||
        !fs::exists(destination, ec)) {
        fs::remove_all(base, ec);
        return 2;
    }
    const auto partial_size = fs::file_size(destination, ec);
    if (ec || partial_size == 0 || partial_size >= fs::file_size(source, ec)) {
        fs::remove_all(base, ec);
        return 3;
    }

    velocitycopy::CopyOptions resume_options{};
    resume_options.resume_from_pause = true;
    resume_options.existing_destination = velocitycopy::ExistingDestinationPolicy::Fail;
    const auto resumed = engine.copy_file(source, destination, resume_options);
    if (!resumed.success || resumed.native_code != S_OK) {
        fs::remove_all(base, ec);
        return 4;
    }
    const auto source_size = fs::file_size(source, ec);
    if (ec) {
        fs::remove_all(base, ec);
        return 5;
    }
    const auto destination_size = fs::file_size(destination, ec);
    if (ec || source_size != destination_size || !same_contents(source, destination)) {
        fs::remove_all(base, ec);
        return 6;
    }
    fs::remove_all(base, ec);
    return 0;
}
