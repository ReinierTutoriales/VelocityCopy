#include "velocitycopy/copy_engine.hpp"
#include "velocitycopy/job_executor.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace velocitycopy;

bool blocked_write(const fs::path& path) {
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); return false; }
    return GetLastError() == ERROR_SHARING_VIOLATION;
}

bool same_contents(const fs::path& first, const fs::path& second) {
    std::ifstream left(first, std::ios::binary), right(second, std::ios::binary);
    std::string a(64 * 1024, '\0'), b(a.size(), '\0');
    do {
        left.read(a.data(), a.size());
        right.read(b.data(), b.size());
        if (left.gcount() != right.gcount() || a != b) return false;
    } while (left.gcount());
    return left.eof() && right.eof();
}

int run_test() {
    const auto root = fs::temp_directory_path() /
        (L"VelocityCopySourceIdentityTest-" + std::to_wstring(GetCurrentProcessId()));
    const auto folder = root / L"source";
    const auto source = folder / L"file.bin";
    fs::create_directories(folder);
    {
        std::ofstream stream(source, std::ios::binary);
        const std::string block(1024 * 1024, 'v');
        for (int part = 0; part < 16; ++part) stream.write(block.data(), block.size());
        if (!stream) return 1;
    }
    CopyEngine engine;
    const auto target = root / L"copy" / L"file.bin";
    bool inspected = false;
    bool blocked = true;
    const auto copied = engine.copy_file(source, target, [&](const CopyProgress&) {
        if (!inspected) {
            inspected = true;
            blocked = blocked_write(source);
            blocked = blocked && !MoveFileExW(source.c_str(), (folder / L"swapped.bin").c_str(), 0);
            blocked = blocked && !MoveFileExW(folder.c_str(), (root / L"swapped-folder").c_str(), 0);
        }
        return CopyDecision::Continue;
    });
    if (!copied.success || !inspected || !blocked || !same_contents(source, target) ||
        !copied.source_fingerprint.has_identity || !copied.destination_fingerprint.has_identity) return 2;
    const auto before = probe_file_fingerprint(source);
    if (before.fingerprint.identity.file_id != copied.source_fingerprint.identity.file_id) return 3;

    // Retain the source throughout a genuine native pause, including when the
    // caller has disabled basic metadata restoration.
    {
        SourceLease lease;
        CopyOptions options{};
        options.source_lease = &lease;
        options.preserve_basic_metadata = false;
        bool paused_once = false;
        const auto paused_target = root / L"paused.bin";
        const auto paused = engine.copy_file(source, paused_target, options, [&](const CopyProgress& value) {
            if (!paused_once && value.transferred_bytes && value.transferred_bytes < value.total_bytes) {
                paused_once = true;
                return CopyDecision::Pause;
            }
            return CopyDecision::Continue;
        });
        if (!paused_once || paused.native_code != HRESULT_FROM_WIN32(ERROR_REQUEST_PAUSED) ||
            !blocked_write(source) || MoveFileExW(source.c_str(), (folder / L"swapped.bin").c_str(), 0)) return 4;
        options.resume_from_pause = true;
        const auto resumed = engine.copy_file(source, paused_target, options);
        if (!resumed.success || !same_contents(source, paused_target)) return 5;
        lease.reset();
    }

    // Source or destination replaced after CopyFile2 closes: actual copy
    // provenance must not authorize deletion of either replacement.
    for (const bool replace_source : {true, false}) {
        const auto src = root / (replace_source ? L"swap-source.bin" : L"swap-dest-source.bin");
        const auto dst = root / (replace_source ? L"swap-source-copy.bin" : L"swap-dest-copy.bin");
        { std::ofstream stream(src); stream << "original"; }
        const auto copy = engine.copy_file(src, dst);
        if (!copy.success) return 6;
        const auto replaced = replace_source ? src : dst;
        fs::rename(replaced, replaced.wstring() + L".saved");
        { std::ofstream stream(replaced); stream << "new-data"; }
        SourceRemovalRecovery recovery{};
        recovery.source = src;
        recovery.destination = dst;
        recovery.source_fingerprint = copy.source_fingerprint;
        recovery.destination_fingerprint = copy.destination_fingerprint;
        if (remove_verified_recovery_source(recovery) == S_OK || !fs::exists(src) || !fs::exists(dst)) return 7;
    }

    // Force the executor's copy-and-delete Move path on a single volume:
    // an external read handle admits CopyFile2 but denies rename/deletion.
    const auto move_source = root / L"move-source.bin";
    const auto move_target = root / L"move" / L"file.bin";
    { std::ofstream stream(move_source); stream << "move"; }
    const HANDLE held = CreateFileW(move_source.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (held == INVALID_HANDLE_VALUE) return 8;
    CopyPlan raw{};
    raw.operation = FileOperation::Move;
    raw.source_roots = {move_source};
    raw.destination_root = move_target.parent_path();
    raw.directories = {{raw.destination_root}};
    raw.files = {{1, move_source, move_target, 4}};
    raw.total_bytes = 4;
    LiveCopyPlan plan(std::move(raw));
    ExecutionControl control;
    const auto moved = JobExecutor{}.execute(plan, control, JobExecutionOptions{1});
    CloseHandle(held);
    if (!moved.success || moved.parked_files != 1 || !fs::exists(move_source) ||
        !same_contents(move_source, move_target)) return 9;
    auto options = JobExecutionOptions{1};
    options.retry_source_removals = true;
    const auto recovered = JobExecutor{}.execute(plan, control, options);
    if (!recovered.success || recovered.parked_files || recovered.outcomes.succeeded != 1 ||
        fs::exists(move_source) || !fs::exists(move_target)) return 10;

    // The handle-based same-volume fast path retains the original file ID.
    const auto renamed = root / L"renamed.bin";
    const auto result = engine.rename_file(source, renamed, ExistingDestinationPolicy::Fail);
    if (!result.success || fs::exists(source) ||
        probe_file_fingerprint(renamed).fingerprint.identity.file_id != before.fingerprint.identity.file_id) return 11;
    fs::remove_all(root);
    return 0;
}

int main() {
    const auto result = run_test();
    if (result) std::cerr << "Source identity failure at check " << result << '\n';
    return result;
}
