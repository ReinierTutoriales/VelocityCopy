#include "velocitycopy/job_executor.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {

int percent_complete(const std::uint64_t transferred, const std::uint64_t total) noexcept {
    if (total == 0) {
        return 100;
    }

    const auto whole = transferred / total;
    const auto remainder = transferred % total;
    const auto percent = whole >= 1
        ? 100ULL
        : std::min<std::uint64_t>(100, (remainder * 100ULL) / total);
    return static_cast<int>(percent);
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 3) {
        std::wcout << L"VelocityCopy CLI\n\n"
                   << L"Usage:\n"
                   << L"  VelocityCopyCli <destination> <source> [source...]\n"
                   << L"  VelocityCopyCli --contents-only <destination> <source> [source...]\n";
        return 1;
    }

    int first_path = 1;
    auto layout = velocitycopy::DestinationLayout::PreserveSourceFolder;

    if (std::wstring_view(argv[1]) == L"--contents-only") {
        layout = velocitycopy::DestinationLayout::ContentsOnly;
        first_path = 2;
    }

    if (argc - first_path < 2) {
        std::wcerr << L"Destination and at least one source are required.\n";
        return 1;
    }

    velocitycopy::CopyJob job{};
    job.id = 1;
    job.destination = argv[first_path];
    job.layout = layout;
    job.display_name = L"VelocityCopy CLI transfer";

    for (int i = first_path + 1; i < argc; ++i) {
        job.sources.emplace_back(argv[i]);
    }

    velocitycopy::JobExecutor executor;
    int last_percent = -1;

    const auto result = executor.execute(
        job,
        [&](const velocitycopy::JobProgress& progress) {
            const int percent = percent_complete(progress.transferred_bytes, progress.total_bytes);

            if (percent != last_percent) {
                last_percent = percent;
                std::wcout << L"\rCopying... "
                           << std::setw(3) << percent << L"%  "
                           << progress.completed_files << L"/" << progress.total_files
                           << L" files"
                           << std::flush;
            }

            return velocitycopy::JobDecision::Continue;
        });

    std::wcout << L"\n";

    if (!result.success) {
        std::wcerr << (result.cancelled ? L"Copy cancelled. " : L"Copy failed. ")
                   << L"Windows status: 0x"
                   << std::hex
                   << static_cast<std::uint32_t>(result.native_code)
                   << L"\n";
        return 2;
    }

    std::wcout << L"Copy complete.\n";
    return 0;
}
