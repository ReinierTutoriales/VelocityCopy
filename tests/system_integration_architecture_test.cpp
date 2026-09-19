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

    if (!contains(manifest, "Category=\"windows.startupTask\"") ||
        !contains(manifest, "TaskId=\"VelocityCopyStartup\"") ||
        !contains(manifest, "Enabled=\"true\"")) {
        return fail(2, "MSIX must register the enabled packaged startup task");
    }

    if (!contains(manifest, "Category=\"windows.comServer\"") ||
        !contains(manifest, "Category=\"windows.fileExplorerContextMenus\"") ||
        !contains(manifest, "VelocityCopy.Shell.dll") ||
        !contains(manifest, "VelocityCopyCopyTo") ||
        !contains(manifest, "VelocityCopyOpenBackground")) {
        return fail(3, "MSIX must own modern Explorer COM/context-menu registration");
    }

    if (!contains(app, "ExtendedActivationKind::StartupTask") ||
        !contains(app, "!startup_activation && !is_stage_only_activation") ||
        !contains(app, "ShellAction::OpenVelocityCopy")) {
        return fail(4, "startup must remain hidden and later launches must wake the primary instance");
    }

    if (!contains(shell, "send_shell_request(request, 25)") ||
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
        !contains(tray, "PROCESS_POWER_THROTTLING_EXECUTION_SPEED") ||
        !contains(tray, "NOTIFYICON_VERSION_4") ||
        !contains(tray, "NIM_SETVERSION") ||
        !contains(tray, "TaskbarCreated") ||
        !contains(tray, "WM_QUERYENDSESSION") ||
        !contains(tray, "WM_ENDSESSION") ||
        !contains(persistence, "VelocityCopy.Recovery.vcq") ||
        !contains(persistence, "QueueArchiveStore{}.save")) {
        return fail(6, "resident UI must enforce tray, hook-free clipboard capture, EcoQoS and shutdown recovery contracts");
    }

    if (!contains(ipc, "ConvertSidToStringSidW") ||
        !contains(ipc, "D:P(A;;GA;;;SY)(A;;GA;;;") ||
        !contains(ipc, "PIPE_REJECT_REMOTE_CLIENTS") ||
        !contains(ipc, "GetNamedPipeClientProcessId") ||
        !contains(ipc, "connected_client_in_same_session") ||
        !contains(ipc, "CreateNamedPipeW") ||
        contains(ipc, "0, nullptr);")) {
        return fail(7, "IPC pipe and mutex must use explicit local-user security");
    }

    if (!contains(ci_workflow, "push:") ||
        !contains(ci_workflow, "pull_request:") ||
        !contains(ci_workflow, "cmake --build build/x64") ||
        !contains(ci_workflow, "ctest --test-dir build/x64") ||
        contains(ci_workflow, "GenerateAppxPackageOnBuild=true") ||
        contains(ci_workflow, "signtool") ||
        contains(ci_workflow, "Add-AppxPackage")) {
        return fail(8, "commit CI must stay fast and validate the x64 core without packaging or installation");
    }

    if (!contains(package_workflow, "workflow_dispatch:") ||
        !contains(package_workflow, "Build self-contained WinUI x64") ||
        !contains(package_workflow, "WindowsAppSDKSelfContained=true") ||
        !contains(package_workflow, "Stage autonomous x64 payload") ||
        !contains(package_workflow, "VelocityCopy-Portable-x64.zip") ||
        !contains(package_workflow, "VelocityCopy-Setup-x64.exe") ||
        !contains(package_workflow, "Smoke install classic x64 installer") ||
        !contains(package_workflow, "Smoke uninstall classic x64 installer") ||
        contains(package_workflow, "Add-AppxPackage") ||
        contains(package_workflow, "GenerateAppxPackageOnBuild=true") ||
        contains(package_workflow, "VelocityCopy.msixbundle")) {
        return fail(9, "release packaging must build an autonomous portable payload and validate classic install/uninstall without AppX deployment");
    }

    if (!contains(installer_exe, "RequestExecutionLevel admin") ||
        !contains(installer_exe, "PAYLOAD_DIR") ||
        !contains(installer_exe, "VelocityCopy.WinUI.exe") ||
        !contains(installer_exe, "WriteUninstaller") ||
        !contains(installer_exe, "CreateShortcut") ||
        !contains(installer_exe, "Windows\\\\CurrentVersion\\\\Uninstall\\\\VelocityCopy") ||
        contains(installer_exe, "Install-VelocityCopy-Test.ps1") ||
        contains(installer_exe, "Add-AppxPackage")) {
        return fail(10, "classic installer must copy the autonomous payload, create shortcuts and register a conventional uninstaller");
    }

    if (!contains(shell_window, "PasteToFolder") ||
        !contains(shell_window, "staged_sources().empty()") ||
        !contains(shell_window, "CaptureClipboardFileSelection()")) {
        return fail(12, "Explorer paste must reconstruct clipboard staging when app starts on demand");
    }

    if (!contains(docs, "near-zero-CPU") || !contains(docs, "IExplorerCommand") ||
        !contains(docs, "notification-area icon") ||
        !contains(docs, "AddClipboardFormatListener") ||
        !contains(docs, "EcoQoS") ||
        !contains(docs, "WM_ENDSESSION") ||
        !contains(docs, "NOTIFYICON_VERSION_4")) {
        return fail(13, "system-impact constraints must remain documented");
    }

    if (!contains(window, "item.IsOfType(StorageItemTypes::Folder)") ||
        !contains(window, "item.IsOfType(StorageItemTypes::File)") ||
        !contains(window, "if (!kind)") ||
        contains(window, "? velocitycopy::DropItemKind::Directory\n                : velocitycopy::DropItemKind::File")) {
        return fail(14, "drag-and-drop must classify only supported files and folders explicitly");
    }

    return 0;
}
