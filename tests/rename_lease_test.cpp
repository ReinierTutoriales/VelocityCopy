#include "velocitycopy/copy_engine.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace velocitycopy;

int main() {
    const auto root = fs::temp_directory_path() / L"VelocityCopyRenameLeaseTest";
    const auto folder = root / L"target" / L"nested";
    const auto renamed_folder = root / L"renamed";
    fs::create_directories(folder);
    CopyEngine engine;
    {
        DestinationLease lease;
        for (int index = 0; index < 3; ++index) {
            const auto source = root / (std::to_wstring(index) + L".txt");
            const auto target = folder / source.filename();
            { std::ofstream stream(source); stream << index; }
            const auto moved = engine.rename_file(source, target, ExistingDestinationPolicy::Fail, &lease);
            if (!moved.success || fs::exists(source) || !fs::exists(target)) return 1;
            // Every ancestor stays pinned between files, preventing a swap.
            if (MoveFileExW((root / L"target").c_str(), renamed_folder.c_str(), 0)) return 2;
        }
        const auto source = root / L"conflict.txt";
        const auto target = folder / L"0.txt";
        { std::ofstream stream(source); stream << "replacement"; }
        if (engine.rename_file(source, target, ExistingDestinationPolicy::Fail, &lease).success ||
            !fs::exists(source)) return 3;
        if (!engine.rename_file(source, target, ExistingDestinationPolicy::Replace, &lease).success ||
            fs::exists(source)) return 4;
        std::string content;
        { std::ifstream stream(target); stream >> content; }
        if (content != "replacement") return 5;
        // Changing parents drops the old lease and pins the new chain.
        const auto next = root / L"next" / L"item.txt";
        { std::ofstream stream(source); stream << "next"; }
        if (!engine.rename_file(source, next, ExistingDestinationPolicy::Fail, &lease).success) return 6;
        if (!MoveFileExW((root / L"target").c_str(), renamed_folder.c_str(), 0)) return 7;
        if (MoveFileExW((root / L"next").c_str(), (root / L"next-moved").c_str(), 0)) return 8;
    }
    if (!MoveFileExW((root / L"next").c_str(), (root / L"next-moved").c_str(), 0)) return 9;
    fs::remove_all(root);
    return 0;
}
