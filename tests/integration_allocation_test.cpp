#include "velocitycopy/job_planning_worker.hpp"
#include "velocitycopy/process_activation.hpp"
#include "velocitycopy/storage_profiler.hpp"
#include "velocitycopy/transfer_router.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>

namespace { thread_local int fail_after = -1; }
void* operator new(std::size_t n) {
    if (fail_after == 0) { fail_after = -1; throw std::bad_alloc{}; }
    if (fail_after > 0) --fail_after;
    if (auto p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main() {
    using namespace velocitycopy;
    using namespace std::chrono_literals;
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / L"VelocityCopyIntegrationAllocationTest";
    fs::create_directories(root);
    const auto source = root / L"file.bin";
    { std::ofstream out(source); out << "data"; }
    const auto absent_exe = root / L"NeverExists.VelocityCopy.exe";
    ShellRequest request;
    request.action = ShellAction::Transfer;
    request.sources = {source};
    request.destination = root / L"destination";
    (void)StorageProfiler{}.inspect(root);
    (void)launch_velocitycopy_with_request(absent_exe, request);
    std::fprintf(stderr, "Storage/activation allocation sweep\n");
    for (int allocation = 0; allocation < 60; ++allocation) {
        DWORD before{}, after{};
        GetProcessHandleCount(GetCurrentProcess(), &before);
        fail_after = allocation;
        (void)StorageProfiler{}.inspect(root);
        fail_after = -1;
        GetProcessHandleCount(GetCurrentProcess(), &after);
        if (after != before) return 1;
        fail_after = allocation;
        (void)resolve_storage_key(root);
        fail_after = -1;
        GetProcessHandleCount(GetCurrentProcess(), &after);
        if (after != before) return 2;
        fail_after = allocation;
        const bool launched = launch_velocitycopy_with_request(absent_exe, request);
        fail_after = -1;
        GetProcessHandleCount(GetCurrentProcess(), &after);
        if (launched || after != before) return 3;
    }

    CopyJob job;
    job.sources = {source};
    job.destination = root / L"destination";
    std::fprintf(stderr, "Worker cancellation allocation check\n");
    std::mutex mutex;
    std::condition_variable gate;
    bool entered = false, release = false;
    JobPlanningWorker worker;
    (void)worker.enqueue(job, [&](JobPlanningResult) {
        std::unique_lock lock(mutex);
        entered = true;
        gate.notify_all();
        gate.wait(lock, [&] { return release; });
    });
    {
        std::unique_lock lock(mutex);
        if (!gate.wait_for(lock, 5s, [&] { return entered; })) return 4;
    }
    std::atomic<int> cancelled{0};
    for (int i = 0; i < 3; ++i) {
        (void)worker.enqueue(job, [&](JobPlanningResult result) {
            if (!result.plan && result.error_code == static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED))) ++cancelled;
        });
    }
    fail_after = 0;
    worker.cancel_pending();
    const bool no_allocation = fail_after == 0;
    fail_after = -1;
    {
        std::lock_guard lock(mutex);
        release = true;
    }
    gate.notify_all();
    if (!no_allocation || cancelled != 3) return 5;
    std::error_code ec;
    fs::remove_all(root, ec);
    return 0;
}
