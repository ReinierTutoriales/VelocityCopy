#include "velocitycopy/ipc_protocol.hpp"
#include "velocitycopy/ipc_transport.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

namespace {
bool send_malformed_message() {
    const auto name = velocitycopy::shell_pipe_name();
    if (name.empty() || !WaitNamedPipeW(name.c_str(), 2000)) return false;
    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return false;

    const std::uint32_t size = 4;
    const std::uint32_t garbage = 0xdeadbeefu;
    DWORD written = 0;
    const bool ok = WriteFile(pipe, &size, sizeof(size), &written, nullptr) && written == sizeof(size) &&
        WriteFile(pipe, &garbage, sizeof(garbage), &written, nullptr) && written == sizeof(garbage);
    FlushFileBuffers(pipe);
    CloseHandle(pipe);
    return ok;
}
} // namespace

int wmain() {
    using namespace velocitycopy;

    ShellRequest request{};
    request.action = ShellAction::Transfer;
    request.operation = FileOperation::Move;
    request.sources = {
        std::filesystem::path(L"C:\\Test\\Novela\\capitulo1.mkv"),
        std::filesystem::path(L"C:\\Test\\Novela\\capitulo2.mkv")};
    request.destination = L"D:\\Backup";
    request.layout = DestinationLayout::PreserveSourceFolder;

    const auto encoded = serialize_shell_request(request);
    if (!encoded) return 1;
    const auto decoded = deserialize_shell_request(*encoded);
    if (!decoded || decoded->sources != request.sources || decoded->destination != request.destination ||
        decoded->operation != request.operation || decoded->action != request.action || decoded->layout != request.layout) return 2;

    auto corrupted = *encoded;
    corrupted[0] ^= 0xffu;
    if (deserialize_shell_request(corrupted)) return 3;

    SingleInstance first;
    SingleInstance second;
    if (!first.valid() || !first.primary() || !second.valid() || second.primary()) return 4;

    ShellIpcServer server;
    if (!server.valid()) return 5;

    std::optional<ShellRequest> received;
    std::jthread receiver([&] { received = server.receive(); });
    if (!send_shell_request(request, 2000)) return 6;
    receiver.join();
    if (!received || received->sources != request.sources || received->destination != request.destination ||
        received->operation != request.operation || received->action != request.action || received->layout != request.layout) return 7;

    // Malformed client data is a protocol rejection, not a transport shutdown.
    std::optional<ShellRequest> malformed_result;
    std::jthread malformed_receiver([&] { malformed_result = server.receive(); });
    if (!send_malformed_message()) return 8;
    malformed_receiver.join();
    if (malformed_result || server.stopping() || !server.valid()) return 9;

    // The same server must accept the next valid activation after malformed input.
    std::optional<ShellRequest> recovered;
    std::jthread recovery_receiver([&] { recovered = server.receive(); });
    if (!send_shell_request(request, 2000)) return 10;
    recovery_receiver.join();
    if (!recovered || recovered->sources != request.sources || recovered->destination != request.destination) return 11;

    // Shutdown contract: stop() must wake a thread blocked in ConnectNamedPipe
    // without polling or detaching the receiver thread.
    std::atomic_bool stop_receiver_returned{false};
    std::optional<ShellRequest> stop_result;
    std::jthread stop_receiver([&] {
        stop_result = server.receive();
        stop_receiver_returned.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(25ms);
    if (stop_receiver_returned.load(std::memory_order_acquire)) {
        stop_receiver.join();
        return 12;
    }

    server.stop();
    stop_receiver.join();
    if (!server.stopping() || !stop_receiver_returned.load(std::memory_order_acquire) || stop_result) return 13;

    std::wcout << L"VelocityCopy IPC test passed.\n";
    return 0;
}
