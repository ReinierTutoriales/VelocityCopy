#include "velocitycopy/job_planning_worker.hpp"
#include "velocitycopy/job_queue.hpp"
#include "velocitycopy/process_activation.hpp"
#include "velocitycopy/storage_profiler.hpp"
#include "velocitycopy/transfer_router.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
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
    request.sources = {source};
    request.destination = root / L"destination";
    (void)StorageProfiler{}.inspect(root);
    (void)launch_velocitycopy_with_request(absent_exe, request);
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
    JobQueue queue;
    job.id = 1;
    queue.enqueue(job);
    job.id = 2;
    queue.enqueue(job);
    fail_after = 0;
    const bool reordered = queue.move_pending(2, 0);
    const bool allocation_free = fail_after == 0;
    fail_after = -1;
    if (!reordered || !allocation_free || queue.jobs()[0].id != 2) return 6;
    fail_after = 0;
    const auto result = queue.execute_next();
    fail_after = -1;
    if (result.success || result.native_code != static_cast<std::int32_t>(E_OUTOFMEMORY) || queue.active_job_id()) return 7;
    JobPlanningWorker worker;
    std::mutex mutex;
    std::condition_variable gate;
    bool entered = false, release = false;
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
