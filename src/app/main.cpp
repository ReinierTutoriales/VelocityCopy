#include "velocitycopy/ipc_transport.hpp"
#include "velocitycopy/job_executor.hpp"
#include "velocitycopy/process_activation.hpp"
#include "velocitycopy/shell_session.hpp"

#include <windows.h>

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cwchar>
#include <deque>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>

namespace {

std::optional<std::uintptr_t> parse_uintptr(const wchar_t* text) noexcept {
    if (text == nullptr || *text == L'\0') {
        return std::nullopt;
    }
    wchar_t* end = nullptr;
    const auto value = std::wcstoull(text, &end, 10);
    if (end == text || *end != L'\0') {
        return std::nullopt;
    }
    return static_cast<std::uintptr_t>(value);
}

std::optional<std::size_t> parse_size(const wchar_t* text) noexcept {
    const auto value = parse_uintptr(text);
    if (!value) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*value);
}

int execute_job(const velocitycopy::CopyJob& job) {
    velocitycopy::JobExecutor executor;
    const auto result = executor.execute(job);
    return result.success ? 0 : 2;
}

int run_shell_runtime(const std::optional<velocitycopy::ShellRequest>& initial_request) {
    velocitycopy::SingleInstance instance;
    if (!instance.valid()) {
        return 20;
    }

    if (!instance.primary()) {
        if (initial_request) {
            return velocitycopy::send_shell_request(*initial_request, 1000) ? 0 : 21;
        }
        return 0;
    }

    velocitycopy::ShellIpcServer server;
    if (!server.valid()) {
        return 22;
    }

    std::mutex mutex;
    std::condition_variable ready;
    std::deque<velocitycopy::ShellRequest> requests;
    bool receiver_failed = false;

    if (initial_request) {
        requests.push_back(*initial_request);
    }

    std::thread receiver([&] {
        for (;;) {
            auto request = server.receive();
            if (!request) {
                std::lock_guard lock(mutex);
                receiver_failed = true;
                ready.notify_one();
                return;
            }
            {
                std::lock_guard lock(mutex);
                requests.push_back(std::move(*request));
            }
            ready.notify_one();
        }
    });
    receiver.detach();

    velocitycopy::ShellSession session;
    for (;;) {
        velocitycopy::ShellRequest request;
        {
            std::unique_lock lock(mutex);
            ready.wait(lock, [&] { return receiver_failed || !requests.empty(); });
            if (requests.empty() && receiver_failed) {
                return 23;
            }
            request = std::move(requests.front());
            requests.pop_front();
        }

        const auto dispatch = session.dispatch(request);
        if (dispatch.status == velocitycopy::ShellDispatchStatus::InvalidRequest) {
            continue;
        }
        if (dispatch.job) {
            (void)execute_job(*dispatch.job);
        }
        // show_window is consumed by the future WinUI host. The shell runtime remains headless for now.
    }
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc >= 2 && std::wstring_view(argv[1]) == L"--shell-runtime") {
        std::optional<velocitycopy::ShellRequest> initial_request;

        if (argc == 6 && std::wstring_view(argv[2]) == L"--activation-handle" &&
            std::wstring_view(argv[4]) == L"--activation-size") {
            const auto handle = parse_uintptr(argv[3]);
            const auto size = parse_size(argv[5]);
            if (!handle || !size) {
                return 24;
            }
            initial_request = velocitycopy::read_inherited_shell_request(*handle, *size);
            if (!initial_request) {
                return 25;
            }
        } else if (argc != 2) {
            return 26;
        }

        return run_shell_runtime(initial_request);
    }

    if (argc < 3) {
        std::wcout << L"VelocityCopy\n\n"
                   << L"Usage:\n"
                   << L"  VelocityCopy <destination> <source> [source...]\n"
                   << L"  VelocityCopy --contents-only <destination> <source> [source...]\n";
        return 1;
    }

    int first_path = 1;
    auto layout = velocitycopy::DestinationLayout::PreserveSourceFolder;

    if (std::wstring_view(argv[1]) == L"--contents-only") {
        layout = velocitycopy::DestinationLayout::ContentsOnly;
        first_path = 2;
    }

    if (argc - first_path < 2) {
        std::wcerr << L"Destination and at least one source are required.\n";
        return 1;
    }

    velocitycopy::CopyJob job{};
    job.id = 1;
    job.destination = argv[first_path];
    job.layout = layout;
    job.display_name = L"VelocityCopy transfer";

    for (int i = first_path + 1; i < argc; ++i) {
        job.sources.emplace_back(argv[i]);
    }

    velocitycopy::JobExecutor executor;
    int last_percent = -1;

    const auto result = executor.execute(
        job,
        [&](const velocitycopy::JobProgress& progress) {
            int percent = 100;
            if (progress.total_bytes != 0) {
                percent = static_cast<int>(std::min<std::uint64_t>(
                    100,
                    (progress.transferred_bytes * 100) / progress.total_bytes));
            }

            if (percent != last_percent) {
                last_percent = percent;
                std::wcout << L"\rCopying... "
                           << std::setw(3) << percent << L"%  "
                           << progress.completed_files << L"/" << progress.total_files
                           << L" files"
                           << std::flush;
            }

            return velocitycopy::JobDecision::Continue;
        });

    std::wcout << L"\n";

    if (!result.success) {
        std::wcerr << (result.cancelled ? L"Copy cancelled. " : L"Copy failed. ")
                   << L"Windows status: 0x"
                   << std::hex
                   << static_cast<std::uint32_t>(result.native_code)
                   << L"\n";
        return 2;
    }

    std::wcout << L"Copy complete.\n";
    return 0;
}
