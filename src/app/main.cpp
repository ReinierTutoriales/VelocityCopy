#include "velocitycopy/copy_engine.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 3) {
        std::wcout << L"VelocityCopy\n\n"
                   << L"Usage:\n"
                   << L"  VelocityCopy <source> <destination>\n";
        return 1;
    }

    const std::filesystem::path source = argv[1];
    const std::filesystem::path destination = argv[2];

    velocitycopy::CopyEngine engine;
    int last_percent = -1;

    const auto result = engine.copy_file(
        source,
        destination,
        [&](const velocitycopy::CopyProgress& progress) {
            if (progress.total_bytes == 0) {
                return velocitycopy::CopyDecision::Continue;
            }

            const auto percent = static_cast<int>(std::min<std::uint64_t>(
                100,
                (progress.transferred_bytes * 100) / progress.total_bytes));

            if (percent != last_percent) {
                last_percent = percent;
                std::wcout << L"\rCopying... " << std::setw(3) << percent << L"%" << std::flush;
            }

            return velocitycopy::CopyDecision::Continue;
        });

    std::wcout << L"\n";

    if (!result.success) {
        std::wcerr << L"Copy failed. Windows status: 0x"
                   << std::hex
                   << static_cast<std::uint32_t>(result.native_code)
                   << L"\n";
        return 2;
    }

    std::wcout << L"Copy complete.\n";
    return 0;
}
