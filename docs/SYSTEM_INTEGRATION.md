# VelocityCopy Windows integration and system-impact contract

VelocityCopy integrates deeply enough to feel native on Windows 11 without replacing Explorer or installing a heavyweight always-busy service.

## Installation

- Distribution target: signed MSIX for Windows 11 x64.
- The package owns application identity, Explorer COM registration, context-menu registration, assets and startup registration.
- Test builds use an ephemeral self-signed certificate and include only the public `.cer`; production builds must use a trusted production signing identity.
- Updating an installed build must preserve the same package identity.

## Startup behavior

- VelocityCopy declares a packaged `windows.startupTask` named `VelocityCopyStartup`.
- The task starts enabled for packaged desktop builds after the app has been launched at least once.
- Startup activation is silent: it creates the primary application instance and IPC endpoint but does not show the copy window.
- A later normal launch redirects `OpenVelocityCopy` to the existing primary instance and shows that window.
- There is never more than one primary VelocityCopy process per interactive Windows session.
- Windows and the user remain authoritative: the startup entry may be disabled from Settings or Task Manager and VelocityCopy must not fight that choice.

## Tray, taskbar and window behavior

- VelocityCopy owns a persistent notification-area icon while the resident process is running.
- When the compact window is visible it behaves as a normal Windows desktop app and may appear in the taskbar/system switchers.
- Minimize and close hide the compact window to the notification area instead of terminating the resident process.
- Minimizing or hiding never pauses, cancels or stops an active transfer; execution is owned by the core worker, not by window visibility.
- Clicking the tray icon restores the same live queue/progress state.
- The tray menu exposes Open VelocityCopy and Exit. Exit is the explicit action that terminates the resident process.
- Silent sign-in startup creates the tray presence without activating the compact window or creating a taskbar button.

## Resident impact

The resident process exists only to provide near-instant Explorer handoff and state continuity.

- no directory scanning while idle
- no hashing while idle
- no network polling
- no filesystem watcher farm
- no global keyboard or mouse hooks
- clipboard file capture uses AddClipboardFormatListener events, not polling
- no periodic benchmark
- no copy worker until work exists
- IPC blocks on a local named pipe instead of polling
- the hidden startup window performs no animation

Windows may apply normal desktop power/resource policies to the background process. Any future idle feature must preserve this near-zero-CPU design.

## File Explorer integration

Explorer integration uses the Windows 11 packaged desktop model:

- native COM DLL implementing `IExplorerCommand`
- `windows.comServer` registration in the MSIX manifest
- `windows.fileExplorerContextMenus` registration for files, folders and folder backgrounds
- x64 shell DLL for x64 Explorer
- synchronous menu-construction methods remain bounded and cheap
- `Invoke` packages intent into a versioned `ShellRequest` and returns
- all enumeration, planning, conflicts, queue state and I/O remain in the VelocityCopy app process
- IPC or launch failure must never destabilize Explorer

VelocityCopy does not hook Explorer, patch explorer.exe, install a kernel driver, or add a permanently active helper service. It observes normal file Copy/Cut clipboard updates using AddClipboardFormatListener and stages CF_HDROP paths, including Preferred DropEffect so Cut becomes Move. Global Ctrl+V remains owned by Windows; VelocityCopy paste is invoked through its Explorer command surface until a stable public interception path exists.

## Process model

```text
Windows sign-in
    |
    +-- packaged StartupTask -> VelocityCopy.WinUI.exe (hidden primary)
                               |
                               +-- local named-pipe IPC server (blocking idle)

Explorer.exe
    |
    +-- VelocityCopy.Shell.dll / IExplorerCommand
            |
            +-- ShellRequest -> existing primary instance
                or bounded on-demand launch if no primary exists

User launch
    |
    +-- secondary instance -> OpenVelocityCopy request -> primary window
```

## Test gate

A build is not ready for manual Explorer/startup testing unless CI proves all of the following:

- core tests pass
- WinUI Release builds
- MSIX package is produced
- MSIX is signed
- Explorer shell DLL is included in the package output
- install/uninstall helper is staged
- startup and Explorer registrations remain present in the manifest
