#include "velocitycopy/ipc_protocol.hpp"
#include "velocitycopy/process_activation.hpp"

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <iostream>

int wmain() {
    velocitycopy::ShellRequest request{};
    request.action = velocitycopy::ShellAction::CopySelectionTo;
    request.sources = {std::filesystem::path(L"C:\\Media\\Novela\\capitulo1.mkv")};
    request.destination = L"D:\\Backup";
    request.layout = velocitycopy::DestinationLayout::PreserveSourceFolder;

    const auto payload = velocitycopy::serialize_shell_request(request);
    if (!payload || payload->empty()) {
        return 1;
    }

    HANDLE mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(payload->size()),
        nullptr);
    if (mapping == nullptr) {
        return 2;
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, payload->size());
    if (view == nullptr) {
        CloseHandle(mapping);
        return 3;
    }

    std::memcpy(view, payload->data(), payload->size());
    UnmapViewOfFile(view);

    const auto decoded = velocitycopy::read_inherited_shell_request(
        reinterpret_cast<std::uintptr_t>(mapping),
        payload->size());

    // read_inherited_shell_request owns/closes the inherited mapping handle.
    if (!decoded || decoded->action != request.action ||
        decoded->sources != request.sources || decoded->destination != request.destination ||
        decoded->layout != request.layout) {
        return 4;
    }

    if (velocitycopy::read_inherited_shell_request(0, payload->size())) {
        return 5;
    }

    std::wcout << L"VelocityCopy activation mapping test passed.\n";
    return 0;
}
