#include "velocitycopy/copy_engine.hpp"
#include <windows.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>

namespace { thread_local int allocations_until_failure = -1; }
void* operator new(std::size_t size) {
    if (allocations_until_failure == 0) {
        allocations_until_failure = -1;
        throw std::bad_alloc{};
    }
    if (allocations_until_failure > 0) --allocations_until_failure;
    if (auto p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int wmain() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / L"VelocityCopyAllocationTest";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const auto source = root / L"source.bin";
    { std::ofstream stream(source); stream << "data"; }
    const auto destination = root / std::wstring(80, L'x') / L"copy.bin";
    velocitycopy::CopyEngine engine;
    velocitycopy::ProgressCallback progress;
    int failures = 0;
    for (int allocation = 0; allocation < 24; ++allocation) {
        fs::remove_all(destination.parent_path(), ec);
        DWORD before{}, after{};
        GetProcessHandleCount(GetCurrentProcess(), &before);
        allocations_until_failure = allocation;
        const auto result = engine.copy_file(source, destination, progress);
        allocations_until_failure = -1;
        GetProcessHandleCount(GetCurrentProcess(), &after);
        if (result.native_code == static_cast<std::int32_t>(E_OUTOFMEMORY)) {
            if (after != before) return 1;
            ++failures;
        }
        else if (!result.success) return 2;
    }
    fs::remove_all(root, ec);
    return failures >= 3 ? 0 : 3;
}
