#include "velocitycopy/ipc_protocol.hpp"
#include "velocitycopy/ipc_transport.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

int wmain() {
    using namespace velocitycopy;

    ShellRequest request{};
    request.action = ShellAction::CopySelectionTo;
    request.sources = {
        std::filesystem::path(L"C:\\Test\\Novela\\capitulo1.mkv"),
        std::filesystem::path(L"C:\\Test\\Novela\\capitulo2.mkv")};
    request.destination = L"D:\\Backup";
    request.layout = DestinationLayout::PreserveSourceFolder;

    const auto encoded = serialize_shell_request(request);
    if (!encoded) {
        return 1;
    }
    const auto decoded = deserialize_shell_request(*encoded);
    if (!decoded || decoded->sources != request.sources ||
        decoded->destination != request.destination || decoded->action != request.action ||
        decoded->layout != request.layout) {
        return 2;
    }

    auto corrupted = *encoded;
    corrupted[0] ^= 0xffu;
    if (deserialize_shell_request(corrupted)) {
        return 3;
    }

    SingleInstance first;
    SingleInstance second;
    if (!first.valid() || !first.primary() || !second.valid() || second.primary()) {
        return 4;
    }

    ShellIpcServer server;
    if (!server.valid()) {
        return 5;
    }

    std::optional<ShellRequest> received;
    std::jthread receiver([&] {
        received = server.receive();
    });

    if (!send_shell_request(request, 2000)) {
        return 6;
    }
    receiver.join();

    if (!received || received->sources != request.sources ||
        received->destination != request.destination || received->action != request.action ||
        received->layout != request.layout) {
        return 7;
    }

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
        return 8;
    }

    server.stop();
    stop_receiver.join();
    if (!server.stopping() || !stop_receiver_returned.load(std::memory_order_acquire) || stop_result) {
        return 9;
    }

    std::wcout << L"VelocityCopy IPC test passed.\n";
    return 0;
}
