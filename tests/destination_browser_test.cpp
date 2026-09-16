#include "velocitycopy/destination_browser.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int wmain() {
    const auto base = fs::temp_directory_path() / L"VelocityCopyDestinationBrowserTest";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base / L"Zulu", ec);
    fs::create_directories(base / L"alpha", ec);
    {
        std::ofstream file(base / L"not-a-folder.txt");
        file << "x";
    }

    velocitycopy::DestinationBrowser browser;
    const auto children = browser.list_children(base);
    if (children.size() != 2 || children[0].name != L"alpha" || children[1].name != L"Zulu") {
        fs::remove_all(base, ec);
        return 1;
    }

    fs::path created;
    if (!browser.create_folder(base, L"New Folder", created) || !fs::is_directory(created)) {
        fs::remove_all(base, ec);
        return 2;
    }
    if (browser.create_folder(base, L"..", created)) {
        fs::remove_all(base, ec);
        return 3;
    }
    if (browser.create_folder(base, L"bad/name", created)) {
        fs::remove_all(base, ec);
        return 4;
    }

    const auto capacity = browser.capacity(base);
    if (!capacity.available || capacity.total_bytes == 0) {
        fs::remove_all(base, ec);
        return 5;
    }

    fs::remove_all(base, ec);
    std::wcout << L"VelocityCopy destination browser test passed.\n";
    return 0;
}
