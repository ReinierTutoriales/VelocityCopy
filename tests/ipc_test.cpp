#include "velocitycopy/ipc_protocol.hpp"
#include "velocitycopy/ipc_transport.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

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

    std::wcout << L"VelocityCopy IPC test passed.\n";
    return 0;
}
