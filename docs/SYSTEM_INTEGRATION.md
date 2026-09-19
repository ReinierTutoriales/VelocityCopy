# VelocityCopy Windows integration and system-impact contract

VelocityCopy integrates deeply enough to feel native on Windows 11 without replacing Explorer or installing a heavyweight always-busy service.

## Installation

- Distribution target: classic self-contained NSIS installers for Windows 11 x64 and ARM64 (`VelocityCopy-Setup-x64.exe` and `VelocityCopy-Setup-ARM64.exe`).
- Each installer copies the autonomous WinUI payload, registers Explorer `IExplorerCommand` handlers and a conventional Add/Remove Programs entry.
- The installer owns startup registration through HKCU Run (`VelocityCopy`) with `--startup`. Runtime code must never create or repair that value.
- Use the installer that matches the native OS architecture. The x64 setup refuses ARM64 Windows and the ARM64 setup refuses x64 Windows.

## Startup behavior

- Classic/unpackaged builds receive startup intent explicitly via `--startup`.
- The installer-created Run entry starts enabled after installation.
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
- The tray icon negotiates `NOTIFYICON_VERSION_4` after `NIM_ADD` and handles the v4 callback layout, including keyboard selection.
- If Explorer restarts, VelocityCopy handles the registered `TaskbarCreated` message and re-adds the notification icon.

## Resident impact

The resident process exists only to provide near-instant Explorer handoff and state continuity.

- no directory scanning while idle
- no hashing while idle
- no network polling
- no filesystem watcher farm
- clipboard file capture uses AddClipboardFormatListener events, not polling
- no global keyboard or mouse hooks
- no process injection, DLL injection, or generic keystroke capture
- no periodic benchmark
- no copy worker until work exists
- IPC blocks on a local named pipe instead of polling
- the hidden startup window performs no animation
- when hidden in the tray and no transfer/planning work is active, VelocityCopy opts into Windows 11 EcoQoS with `ProcessPowerThrottling` / `PROCESS_POWER_THROTTLING_EXECUTION_SPEED`
- EcoQoS is removed before planning, copying, resuming work, showing the window or exiting, so active file I/O is never intentionally throttled

Any future idle feature must preserve this near-zero-CPU design and must not apply background I/O priority to active transfers.

## Session shutdown and recovery

- `WM_QUERYENDSESSION` returns success immediately; VelocityCopy does not delay Windows shutdown with UI.
- On a confirmed `WM_ENDSESSION`, EcoQoS is removed and the remaining live plan, deferred appends and queued sessions are snapshotted.
- The snapshot is stored as `VelocityCopy.Recovery.vcq` in the packaged app LocalState using the existing atomic temp-file + `MoveFileExW` archive path.
- Recovery persistence is best-effort and must never block or veto Windows shutdown.

## File Explorer integration

Explorer integration uses an unpackaged in-process COM server:

- native COM DLL implementing `IExplorerCommand`
- classic installer registration of the CLSID and Explorer verb handlers
- architecture-matched shell DLL (x64 DLL for x64 Explorer, ARM64 DLL for ARM64 Explorer)
- synchronous menu-construction methods remain bounded and cheap
- `Invoke` packages intent into a versioned `ShellRequest` and returns
- all enumeration, planning, conflicts, queue state and I/O remain in the VelocityCopy app process
- IPC or launch failure must never destabilize Explorer

VelocityCopy does not patch or inject into explorer.exe, install a kernel driver, add a permanently active helper service, or install a global keyboard hook. It observes normal file Copy/Cut clipboard updates using AddClipboardFormatListener and stages CF_HDROP paths, including Preferred DropEffect so Cut becomes Move.

Paste remains on supported Explorer integration surfaces: Paste with VelocityCopy, Copy to... with VelocityCopy, drag/drop and direct app flows. Global Ctrl+V remains owned by Windows until there is a supported interception path that does not require a system-wide low-level keyboard hook.

## Process model

```text
Windows sign-in
    |
    +-- HKCU Run --startup -> VelocityCopy.WinUI.exe (hidden primary)
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

A build is not ready for manual Explorer/startup testing unless packaging proves all of the following:

- core tests pass
- self-contained WinUI Release builds for x64 and ARM64
- classic NSIS installers are produced for both architectures
- Explorer shell DLL is included in each installer payload
- the x64 installer installs and uninstalls cleanly on x64 Windows
