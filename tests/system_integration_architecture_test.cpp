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
    const auto shell = read_all(root / "src/shell/drop_handler.cpp");
    const auto shell_window = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Shell.cpp");
    const auto ipc = read_all(root / "src/core/ipc_transport.cpp");
    const auto window = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.xaml.cpp");
    const auto tray = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Tray.cpp");
    const auto app_tray = read_all(root / "src/ui/VelocityCopy.UI/AppTray.cpp");
    const auto persistence = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.QueuePersistence.cpp");
    const auto recovery = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Recovery.cpp");
    const auto conflict = read_all(root / "src/ui/VelocityCopy.UI/MainWindow.Conflict.cpp");
    const auto ci_workflow = read_all(root / ".github/workflows/ci.yml");
    const auto package_workflow = read_all(root / ".github/workflows/package.yml");
    const auto installer_exe = read_all(root / "tools/VelocityCopy-Test-Installer.nsi");
    const auto installer_smoke = read_all(root / "tools/Install-VelocityCopy-Test.ps1");
    const auto docs = read_all(root / "docs/SYSTEM_INTEGRATION.md");

    if (app.empty() || shell.empty() || shell_window.empty() || ipc.empty() ||
        window.empty() || tray.empty() || app_tray.empty() || persistence.empty() || recovery.empty() || conflict.empty() ||
        ci_workflow.empty() || package_workflow.empty() || installer_exe.empty() || installer_smoke.empty() || docs.empty()) {
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
        !contains(tray, "SC_MINIMIZE") ||
        !contains(tray, "WM_CLOSE") ||
        !contains(tray, "WM_ENDSESSION") ||
        contains(tray, "SetWindowsHookEx") ||
        contains(tray, "WH_KEYBOARD_LL") ||
        contains(app_tray, "SetWindowsHookEx") ||
        contains(app_tray, "WH_KEYBOARD_LL") ||
        !contains(app_tray, "Shell_NotifyIconW(NIM_ADD") ||
        !contains(app_tray, "NOTIFYICON_VERSION_4") ||
        !contains(app_tray, "TaskbarCreated") ||
        !contains(app, "ProcessPowerThrottling") ||
        contains(tray, "Shell_NotifyIconW") ||
        contains(tray, "ProcessPowerThrottling") ||
        !contains(persistence, "recovery_file(") ||
        !contains(persistence, "session_id_")) {
        return fail(6, "resident UI must enforce tray, hook-free operation, EcoQoS and shutdown recovery contracts");
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
        !contains(ci_workflow, "VELOCITYCOPY_ENABLE_ASAN=ON") ||
        !contains(ci_workflow, "RelWithDebInfo") ||
        contains(ci_workflow, "Add-AppxPackage") ||
        contains(ci_workflow, "makensis")) {
        return fail(8, "commit CI must compile x64/ARM64, run ASan, and stay free of packaging");
    }

    if (!contains(package_workflow, "workflow_dispatch:") ||
        !contains(package_workflow, "branches: [main]") ||
        !contains(package_workflow, "WindowsAppSDKSelfContained=true") ||
        !contains(package_workflow, "VelocityCopy-Setup-x64.exe") ||
        !contains(package_workflow, "VelocityCopy-Setup-ARM64.exe") ||
        !contains(package_workflow, "PAYLOAD_ARCH") ||
        !contains(package_workflow, "nsis-3.11.zip") ||
        !contains(package_workflow, "Smoke install classic x64 installer") ||
        !contains(package_workflow, "Smoke uninstall classic x64 installer") ||
        contains(package_workflow, "choco ") ||
        contains(package_workflow, "Add-AppxPackage") ||
        contains(package_workflow, "VelocityCopy.msixbundle")) {
        return fail(9, "packaging must run on main without Chocolatey and stay classic for x64 and ARM64");
    }

    if (!contains(installer_exe, "RequestExecutionLevel admin") ||
        !contains(installer_exe, "PAYLOAD_DIR") ||
        !contains(installer_exe, "PAYLOAD_ARCH") ||
        !contains(installer_exe, "IsARM64") ||
        !contains(installer_exe, "VelocityCopy.WinUI.exe") ||
        !contains(installer_exe, "MUI_FINISHPAGE_RUN_FUNCTION") ||
        contains(installer_exe, "!define MUI_FINISHPAGE_RUN \"$INSTDIR") ||
        !contains(installer_exe, "explorer.exe") ||
        !contains(installer_exe, "WriteUninstaller") ||
        !contains(installer_exe, "CreateShortcut") ||
        !contains(installer_exe, "Windows\\CurrentVersion\\Uninstall\\VelocityCopy") ||
        !contains(installer_exe, "!macro CloseRunningApp") ||
        !contains(installer_exe, "taskkill.exe") ||
        !contains(installer_exe, "/IM VelocityCopy.WinUI.exe /F") ||
        !contains(installer_smoke, "Launching VelocityCopy in startup/tray mode before uninstall smoke test") ||
        !contains(installer_smoke, "the install directory still exists") ||
        contains(installer_exe, "Add-AppxPackage")) {
        return fail(10, "classic install/uninstall must stop the resident app and prove Program Files cleanup");
    }

    if (contains(app, "\\nbool is_startup_activation")) {
        return fail(11, "startup source must not comment out is_startup_activation with a literal escape");
    }

    if (!contains(shell_window, "shell_session_.dispatch(request)") ||
        !contains(shell_window, "QueueOrStartCopy(std::move(*dispatch.job))") ||
        contains(shell_window, "BeginShellLayoutAsync") ||
        contains(shell_window, "CaptureClipboardFileSelection") ||
        contains(tray, "AddClipboardFormatListener") ||
        contains(tray, "WM_CLIPBOARDUPDATE")) {
        return fail(12, "transfer handoff must use its own shell snapshot and enter the queue directly");
    }
    if (!contains(docs, "IShellExtInit") || !contains(docs, "EcoQoS")) {
        return fail(13, "integration constraints must remain documented");
    }
    if (!contains(window, "StandardDataFormats::StorageItems()") ||
        !contains(window, "active_destination.empty()") ||
        !contains(window, "job.destination = active_destination_") ||
        !contains(window, "job.operation = active_operation_") ||
        !contains(window, "QueueOrStartCopy(std::move(job))") ||
        contains(window, "preferred_drop_operation") ||
        contains(window, "DragDropModifiers::Control") ||
        contains(window, "DragDropModifiers::Shift")) {
        return fail(14, "drag-and-drop must append storage items only to the active transfer session");
    }

    if (!contains(shell, "SHGetPathFromIDListEx") ||
        !contains(shell, "CF_HDROP") || !contains(shell, "IContextMenu") ||
        contains(shell, "IExplorerCommand") ||
        contains(installer_exe, "ExplorerCommandHandler") ||
        !contains(installer_exe, "DragDropHandlers")) {
        return fail(15, "only the drop-handler registration and data-object contract may remain");
    }

    if (contains(recovery, "ContentDialog") || contains(recovery, "XamlRoot(") ||
        contains(conflict, "ContentDialog") ||
        !contains(recovery, "ShowNativeDecisionDialog") ||
        !contains(conflict, "ShowNativeDecisionDialog") ||
        !contains(conflict, "TaskDialogIndirect")) {
        return fail(16, "modal conflict and recovery decisions must remain native top-level dialogs outside the compact XAML surface");
    }
    return 0;
}
