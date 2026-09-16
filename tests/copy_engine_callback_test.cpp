#include "velocitycopy/copy_engine.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

int wmain() {
    const auto base = fs::temp_directory_path() / L"VelocityCopyCallbackBoundaryTest";
    const auto source = base / L"source.bin";
    const auto destination = base / L"destination.bin";
    const auto skipped_destination = base / L"skipped.bin";

    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    if (ec) {
        return 1;
    }

    {
        std::ofstream out(source, std::ios::binary);
        std::string block(1024 * 1024, 'x');
        for (int i = 0; i < 8; ++i) {
            out.write(block.data(), static_cast<std::streamsize>(block.size()));
        }
    }

    velocitycopy::CopyEngine engine;
    const auto callback_failure = engine.copy_file(source, destination, [](const velocitycopy::CopyProgress&) -> velocitycopy::CopyDecision {
        throw std::runtime_error("intentional callback failure");
    });

    if (callback_failure.success || callback_failure.native_code != static_cast<std::int32_t>(E_FAIL)) {
        fs::remove_all(base, ec);
        return 2;
    }

    bool skip_called = false;
    const auto skipped = engine.copy_file(
        source,
        skipped_destination,
        [&](const velocitycopy::CopyProgress&) {
            skip_called = true;
            return velocitycopy::CopyDecision::Skip;
        });

    fs::remove_all(base, ec);

    if (!skip_called || skipped.success ||
        skipped.native_code != static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED))) {
        return 3;
    }

    std::wcout << L"VelocityCopy callback boundary test passed.\n";
    return 0;
}
