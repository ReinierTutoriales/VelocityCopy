#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#ifndef VELOCITYCOPY_SOURCE_DIR
#error VELOCITYCOPY_SOURCE_DIR must be defined
#endif

namespace {
std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    std::erase(text, '\r');
    return text;
}
bool contains(const std::string& text, const std::string& value) {
    return text.find(value) != std::string::npos;
}
int fail(int code, const char* message) {
    std::cerr << "system integration architecture contract " << code << ": " << message << '\n';
    return code;
}
}

int main() {
    const std::filesystem::path root{VELOCITYCOPY_SOURCE_DIR};
    const auto app = read_all(root / "src/ui/VelocityCopy.UI/App.xaml.cpp");
    const auto shell = read_all(root / "src/shell/explorer_commands.cpp");
    const auto shell_window = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Shell.cpp");
    const auto ipc = read_all(root / "src/core/ipc_transport.cpp");
    const auto window = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto tray = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Tray.cpp");
    const auto persistence = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.QueuePersistence.cpp");
    const auto ci_workflow = read_all(root / ".github/workflows/ci.yml");
    const auto package_workflow = read_all(root / ".github/workflows/package.yml");
    const auto installer_exe = read_all(root / "tools/VelocityCopy-Test-Installer.nsi");
    const auto docs = read_all(root / "docs/SYSTEM_INTEGRATION.md");

    if (app.empty() || shell.empty() || shell_window.empty() || ipc.empty() ||
        window.empty() || tray.empty() || persistence.empty() || ci_workflow.empty() || package_workflow.empty() ||
        installer_exe.empty() || docs.empty()) {
        return fail(1, "required integration source missing");
    }

    if (!contains(app, "--startup") ||
        !contains(app, "startup_activation") ||
        !contains(app, "ShellAction::OpenVelocityCopy")) {
        return fail(4, "startup must remain hidden and later launches must wake the primary instance");
    }

    if (!contains(shell, "send_shell_request") ||
        !contains(shell, "launch_velocitycopy_with_request") ||
        contains(shell, "SetWindowsHookEx") ||
        contains(shell, "ReadDirectoryChangesW")) {
        return fail(5, "Explorer extension must stay bounded and IPC-only");
    }

    if (!contains(window, "IsMinimizable(true)") ||
        !contains(window, "IsMaximizable(false)") ||
        !contains(tray, "Shell_NotifyIconW(NIM_ADD") ||
        !contains(tray, "AddClipboardFormatListener") ||
        !contains(tray, "CFSTR_PREFERREDDROPEFFECT") ||
        !contains(tray, "SC_MINIMIZE") ||
        !contains(tray, "WM_CLOSE") ||
        contains(tray, "SetWindowsHookEx") ||
        contains(tray, "WH_KEYBOARD_LL") ||
        !contains(tray, "ProcessPowerThrottling") ||
        !contains(tray, "NOTIFYICON_VERSION_4") ||
        !contains(tray, "TaskbarCreated") ||
        !contains(tray, "WM_ENDSESSION") ||
        !contains(persistence, "VelocityCopy.Recovery.vcq")) {
        return fail(6, "resident UI must enforce tray, hook-free clipboard capture, EcoQoS and shutdown recovery contracts");
    }

    if (!contains(ipc, "ConvertSidToStringSidW") ||
        !contains(ipc, "PIPE_REJECT_REMOTE_CLIENTS") ||
        !contains(ipc, "GetNamedPipeClientProcessId") ||
        !contains(ipc, "connected_client_in_same_session") ||
        !contains(ipc, "CreateNamedPipeW")) {
        return fail(7, "IPC pipe and mutex must use explicit local-user security");
    }

    if (!contains(ci_workflow, "push:") ||
        !contains(ci_workflow, "pull_request:") ||
        !contains(ci_workflow, "cmake_arch: x64") ||
        !contains(ci_workflow, "cmake_arch: ARM64") ||
        !contains(ci_workflow, "cmake --build build/") ||
        !contains(ci_workflow, "ctest --test-dir build/x64") ||
        contains(ci_workflow, "Add-AppxPackage") ||
        contains(ci_workflow, "makensis")) {
        return fail(8, "commit CI must compile x64 and ARM64 core without packaging or installation");
    }

    if (!contains(package_workflow, "workflow_dispatch:") ||
        !contains(package_workflow, "WindowsAppSDKSelfContained=true") ||
        !contains(package_workflow, "VelocityCopy-Setup-x64.exe") ||
        !contains(package_workflow, "VelocityCopy-Setup-ARM64.exe") ||
        !contains(package_workflow, "PAYLOAD_ARCH") ||
        !contains(package_workflow, "Smoke install classic x64 installer") ||
        !contains(package_workflow, "Smoke uninstall classic x64 installer") ||
        contains(package_workflow, "Add-AppxPackage") ||
        contains(package_workflow, "VelocityCopy.msixbundle")) {
        return fail(9, "release packaging must remain classic and self-contained for x64 and ARM64");
    }

    if (!contains(installer_exe, "RequestExecutionLevel admin") ||
        !contains(installer_exe, "PAYLOAD_DIR") ||
        !contains(installer_exe, "PAYLOAD_ARCH") ||
        !contains(installer_exe, "IsARM64") ||
        !contains(installer_exe, "VelocityCopy.WinUI.exe") ||
        !contains(installer_exe, "WriteUninstaller") ||
        !contains(installer_exe, "CreateShortcut") ||
        !contains(installer_exe, "Windows\\CurrentVersion\\Uninstall\\VelocityCopy") ||
        contains(installer_exe, "Add-AppxPackage")) {
        return fail(10, "classic installer must copy the autonomous payload and register a conventional uninstaller");
    }

    if (contains(app, "\\nbool is_startup_activation")) {
        return fail(11, "startup source must not comment out is_startup_activation with a literal escape");
    }

    if (!contains(shell_window, "PasteToFolder") ||
        !contains(shell_window, "staged_sources().empty()") ||
        !contains(shell_window, "CaptureClipboardFileSelection()")) {
        return fail(12, "Explorer paste must reconstruct clipboard staging when app starts on demand");
    }

    if (!contains(docs, "IExplorerCommand") ||
        !contains(docs, "AddClipboardFormatListener") ||
        !contains(docs, "EcoQoS")) {
        return fail(13, "system-impact constraints must remain documented");
    }

    if (!contains(window, "StandardDataFormats::StorageItems()") ||
        !contains(window, "active_session") ||
        !contains(window, "job.destination = active_destination_") ||
        !contains(window, "job.operation = active_operation_") ||
        !contains(window, "QueueOrStartCopy(std::move(job))") ||
        contains(window, "preferred_drop_operation") ||
        contains(window, "DragDropModifiers::Control") ||
        contains(window, "DragDropModifiers::Shift")) {
        return fail(14, "drag-and-drop must append storage items only to the active transfer session");
    }

    if (!contains(shell, "IFolderView") ||
        !contains(shell, "IShellItem* folder") ||
        !contains(shell, "SIGDN_FILESYSPATH") ||
        contains(shell, "IShellItemArray* folder_items")) {
        return fail(15, "Explorer paste target must resolve the current folder as one shell item");
    }

    return 0;
}
