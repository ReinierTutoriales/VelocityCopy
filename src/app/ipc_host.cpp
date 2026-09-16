#include "velocitycopy/ipc_transport.hpp"
#include "velocitycopy/shell_request.hpp"

#include <iostream>
#include <string_view>

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 2 || std::wstring_view(argv[1]) != L"--ipc-once") {
        std::wcout << L"VelocityCopy IPC host\nUsage: VelocityCopyIpcHost --ipc-once\n";
        return 1;
    }

    velocitycopy::SingleInstance instance;
    if (!instance.valid() || !instance.primary()) {
        return 2;
    }

    velocitycopy::ShellIpcServer server;
    if (!server.valid()) {
        return 3;
    }

    const auto request = server.receive();
    if (!request) {
        return 4;
    }

    std::wcout << L"Received shell request with " << request->sources.size() << L" source(s).\n";
    return 0;
}
