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
                   << L"  VelocityCopyCli [--move] [--contents-only] <destination> <source> [source...]\n"
                   << L"\nOptions:\n"
                   << L"  --move           Move sources after each successful copy.\n"
                   << L"  --contents-only  Place selected contents directly in the destination.\n";
        return 1;
    }

    int first_path = 1;
    auto layout = velocitycopy::DestinationLayout::PreserveSourceFolder;
    auto operation = velocitycopy::FileOperation::Copy;

    while (first_path < argc) {
        const std::wstring_view argument(argv[first_path]);
        if (argument == L"--contents-only") {
            layout = velocitycopy::DestinationLayout::ContentsOnly;
            ++first_path;
            continue;
        }
        if (argument == L"--move") {
            operation = velocitycopy::FileOperation::Move;
            ++first_path;
            continue;
        }
        if (argument.starts_with(L"--")) {
            std::wcerr << L"Unknown option: " << argument << L"\n";
            return 1;
        }
        break;
    }

    if (argc - first_path < 2) {
        std::wcerr << L"Destination and at least one source are required.\n";
        return 1;
    }

    velocitycopy::CopyJob job{};
    job.id = 1;
    job.destination = argv[first_path];
    job.layout = layout;
    job.operation = operation;
    job.display_name = operation == velocitycopy::FileOperation::Move
        ? L"VelocityCopy CLI move"
        : L"VelocityCopy CLI copy";

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
                std::wcout << (operation == velocitycopy::FileOperation::Move
                                   ? L"\rMoving... "
                                   : L"\rCopying... ")
                           << std::setw(3) << percent << L"%  "
                           << progress.completed_files << L"/" << progress.total_files
                           << L" files"
                           << std::flush;
            }

            return velocitycopy::JobDecision::Continue;
        });

    std::wcout << L"\n";

    if (!result.success) {
        const auto action = operation == velocitycopy::FileOperation::Move ? L"Move" : L"Copy";
        std::wcerr << action
                   << (result.cancelled ? L" cancelled. " : L" failed. ")
                   << L"Windows status: 0x"
                   << std::hex
                   << static_cast<std::uint32_t>(result.native_code)
                   << L"\n";
        return 2;
    }

    std::wcout << (operation == velocitycopy::FileOperation::Move
                       ? L"Move complete.\n"
                       : L"Copy complete.\n");
    return 0;
}
