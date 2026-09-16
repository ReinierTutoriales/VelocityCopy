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
    const auto result = engine.copy_file(source, destination, [](const velocitycopy::CopyProgress&) -> velocitycopy::CopyDecision {
        throw std::runtime_error("intentional callback failure");
    });

    fs::remove_all(base, ec);

    if (result.success || result.native_code != static_cast<std::int32_t>(E_FAIL)) {
        return 2;
    }

    std::wcout << L"VelocityCopy callback boundary test passed.\n";
    return 0;
}
