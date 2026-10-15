#include "velocitycopy/job_executor.hpp"

#include <windows.h>
#include <winioctl.h>

#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {
struct Fixture {
    fs::path root = fs::temp_directory_path() /
        (L"VelocityCopyMetadata-" + std::to_wstring(GetCurrentProcessId()));
    ~Fixture() {
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            SetFileAttributesW(it->path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        fs::remove_all(root, ec);
    }
};

std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

bool same_bytes(const fs::path& a, const fs::path& b) {
    std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
    if (!left || !right) return false;
    std::array<char, 65536> x{}, y{};
    do {
        left.read(x.data(), x.size());
        right.read(y.data(), y.size());
        if (left.gcount() != right.gcount() ||
            !std::equal(x.begin(), x.begin() + left.gcount(), y.begin())) return false;
    } while (left.gcount());
    return left.eof() && right.eof();
}
}

int wmain() {
    Fixture fixture;
    const auto source = fixture.root / L"source";
    const auto destination = fixture.root / L"destination";
    fs::create_directories(source / L"empty");
    fs::create_directories(destination);
    const auto file = source / L"attributes.txt";
    const auto empty_file = source / L"empty-file.txt";
    { std::ofstream out(file); out << "payload"; }
    { std::ofstream out(empty_file); }
    { std::ofstream out(fs::path(file.wstring() + L":metadata")); out << "alternate stream"; }

    FILETIME creation{0x12340000, 0x01d90000}, write{0x56780000, 0x01d90000};
    const auto handle = CreateFileW(file.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return 1;
    const auto times_set = SetFileTime(handle, &creation, nullptr, &write);
    CloseHandle(handle);
    constexpr DWORD carried = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_READONLY |
        FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;
    if (!times_set || !SetFileAttributesW(file.c_str(), carried)) return 2;
    const auto empty_handle = CreateFileW(empty_file.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (empty_handle == INVALID_HANDLE_VALUE) return 10;
    const auto empty_times_set = SetFileTime(empty_handle, &creation, nullptr, &write);
    CloseHandle(empty_handle);
    if (!empty_times_set || !SetFileAttributesW(empty_file.c_str(), carried)) return 11;

    const auto sparse_source = source / L"sparse.bin";
    const auto sparse = CreateFileW(sparse_source.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (sparse == INVALID_HANDLE_VALUE) return 3;
    DWORD returned{};
    LARGE_INTEGER end{};
    end.QuadPart = 64ll * 1024 * 1024;
    bool created = DeviceIoControl(sparse, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) &&
        SetFilePointerEx(sparse, end, nullptr, FILE_BEGIN) && SetEndOfFile(sparse);
    end.QuadPart -= 4;
    const char tail[] = "tail";
    created = created && SetFilePointerEx(sparse, end, nullptr, FILE_BEGIN) &&
        WriteFile(sparse, tail, 4, &returned, nullptr) && returned == 4;
    CloseHandle(sparse);
    if (!created) return 4;

    velocitycopy::CopyJob job{};
    job.sources = {source};
    job.destination = destination;
    job.layout = velocitycopy::DestinationLayout::ContentsOnly;
    const auto result = velocitycopy::JobExecutor{}.execute(job);
    std::cerr << "Copy result: success=" << result.success << " HRESULT=" << result.native_code
        << " succeeded=" << result.outcomes.succeeded << " failed=" << result.outcomes.failed
        << " parked=" << result.parked_files << '\n';
    if (!result.success || result.outcomes.succeeded != 3 ||
        !fs::is_directory(destination / L"empty")) return 5;
    const auto copied = destination / file.filename();
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    const auto inspected = GetFileAttributesExW(copied.c_str(), GetFileExInfoStandard, &attributes);
    std::cerr << "Native metadata: inspected=" << inspected << " attributes=" << attributes.dwFileAttributes
        << " expected=" << carried << " creation comparison=" << CompareFileTime(&attributes.ftCreationTime, &creation)
        << " write comparison=" << CompareFileTime(&attributes.ftLastWriteTime, &write)
        << " payload=" << read(copied) << " ADS=" << read(fs::path(copied.wstring() + L":metadata")) << '\n';
    if (!inspected ||
        (attributes.dwFileAttributes & carried) != carried ||
        CompareFileTime(&attributes.ftCreationTime, &creation) != 0 ||
        CompareFileTime(&attributes.ftLastWriteTime, &write) != 0 ||
        read(copied) != "payload" ||
        read(fs::path(copied.wstring() + L":metadata")) != "alternate stream") return 6;
    WIN32_FILE_ATTRIBUTE_DATA empty_attributes{};
    if (!GetFileAttributesExW((destination / empty_file.filename()).c_str(), GetFileExInfoStandard, &empty_attributes) ||
        (empty_attributes.dwFileAttributes & carried) != carried ||
        CompareFileTime(&empty_attributes.ftCreationTime, &creation) != 0 ||
        CompareFileTime(&empty_attributes.ftLastWriteTime, &write) != 0 ||
        empty_attributes.nFileSizeHigh != 0 || empty_attributes.nFileSizeLow != 0) return 12;
    const auto sparse_copy = destination / sparse_source.filename();
    if (!same_bytes(sparse_source, sparse_copy)) return 7;
    // The CI Windows 11/Server 2025 runner supports sparse CopyFile2. Exercise
    // actual allocation, not just flag selection or logical file length.
    DWORD high{};
    SetLastError(ERROR_SUCCESS);
    const auto low = GetCompressedFileSizeW(sparse_copy.c_str(), &high);
    const auto allocation_error = GetLastError();
    using VersionQuery = LONG (WINAPI*)(OSVERSIONINFOW*);
    const auto query = reinterpret_cast<VersionQuery>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW version{sizeof(version)};
    if (!query || query(&version) != 0) return 8;
    std::cerr << "Sparse allocation=" << ((static_cast<std::uint64_t>(high) << 32) | low)
        << " attributes=" << GetFileAttributesW(sparse_copy.c_str()) << " OS=" << version.dwBuildNumber << '\n';
    if ((version.dwMajorVersion > 10 || version.dwBuildNumber >= 22621) &&
        ((low == INVALID_FILE_SIZE && allocation_error != ERROR_SUCCESS) ||
        (GetFileAttributesW(sparse_copy.c_str()) & FILE_ATTRIBUTE_SPARSE_FILE) == 0 ||
        ((static_cast<std::uint64_t>(high) << 32) | low) >= 2ull * 1024 * 1024)) return 9;
    std::cout << "Native metadata, ADS, sparse allocation and complete contents verified\n";
    return 0;
}
