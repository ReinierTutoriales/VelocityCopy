#include "velocitycopy/destination_catalog.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

int wmain() {
    velocitycopy::DestinationCatalog catalog;

    const std::vector<fs::path> recent = {
        L"C:\\VelocityCopyRecent",
        L"C:\\VelocityCopyRecent\\",
    };
    const auto entries = catalog.enumerate(recent);
    if (entries.empty()) {
        return 1;
    }

    const std::vector<fs::path> sources = {L"C:\\Source\\Novela"};
    if (velocitycopy::DestinationCatalog::validate(sources, L"") != velocitycopy::DestinationValidation::Empty) {
        return 2;
    }
    if (velocitycopy::DestinationCatalog::validate(sources, L"C:\\Source\\Novela") != velocitycopy::DestinationValidation::SameAsSource) {
        return 3;
    }
    if (velocitycopy::DestinationCatalog::validate(sources, L"C:\\Source\\Novela\\Output") != velocitycopy::DestinationValidation::InsideSource) {
        return 4;
    }
    if (velocitycopy::DestinationCatalog::validate(sources, L"D:\\Backup") != velocitycopy::DestinationValidation::Valid) {
        return 5;
    }

    std::wcout << L"VelocityCopy destination catalog test passed.\n";
    return 0;
}
