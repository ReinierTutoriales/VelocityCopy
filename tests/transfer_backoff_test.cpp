#include "velocitycopy/copy_engine.hpp"
#include "velocitycopy/execution_control.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>

using namespace std::chrono_literals;
using namespace velocitycopy;
namespace fs = std::filesystem;

int main() {
    // Exercise both notification orderings: request before and during the wait.
    for (const bool cancel : {false, true}) {
        for (const bool before : {false, true}) {
            ExecutionControl control;
            auto request = [&] {
                if (cancel) control.request_cancel();
                else control.request_stop();
            };
            if (before) request();
            auto waiting = std::async(std::launch::async, [&] { return control.wait_for_retry(30s); });
            if (!before) {
                std::this_thread::sleep_for(20ms);
                request();
            }
            if (waiting.wait_for(5s) != std::future_status::ready) return 1;
            if (waiting.get() != (cancel ? ExecutionDirective::Cancel : ExecutionDirective::Stop)) return 2;
        }
    }
    {
        ExecutionControl control;
        control.request_pause();
        auto waiting = std::async(std::launch::async, [&] { return control.wait_for_retry(0ms); });
        if (waiting.wait_for(30ms) != std::future_status::timeout) {
            control.resume();
            return 3;
        }
        control.resume();
        if (waiting.get() != ExecutionDirective::Run) return 4;
        const auto start = std::chrono::steady_clock::now();
        if (control.wait_for_retry(30ms) != ExecutionDirective::Run ||
            std::chrono::steady_clock::now() - start < 30ms) return 5;
    }
    {
        ExecutionControl control;
        auto waiting = std::async(std::launch::async, [&] { return control.wait_for_retry(100ms); });
        control.request_pause();
        if (waiting.wait_for(150ms) != std::future_status::timeout) {
            control.resume();
            return 6;
        }
        control.request_cancel();
        if (waiting.get() != ExecutionDirective::Cancel) return 7;
    }

    // A locked existing destination must retain its bytes on both Stop and Cancel.
    const auto root = fs::temp_directory_path() / L"VelocityCopyTransferBackoffTest";
    fs::create_directories(root);
    const auto source = root / L"source.txt";
    const auto target = root / L"target.txt";
    { std::ofstream stream(source); stream << "new"; }
    { std::ofstream stream(target); stream << "original"; }
    const HANDLE locked = CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (locked == INVALID_HANDLE_VALUE) return 8;
    CopyEngine engine;
    for (const bool cancel : {false, true}) {
        ExecutionControl control;
        if (cancel) control.request_cancel();
        else control.request_stop();
        CopyOptions options;
        options.existing_destination = ExistingDestinationPolicy::Replace;
        options.control = &control;
        const auto result = engine.copy_file(source, target, options);
        if (result.success || result.native_code !=
            static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_REQUEST_ABORTED))) {
            CloseHandle(locked);
            return 9;
        }
    }
    CloseHandle(locked);
    std::string content;
    { std::ifstream stream(target); stream >> content; }
    const bool retained = content == "original" && fs::exists(source);
    fs::remove_all(root);
    return retained ? 0 : 10;
}
