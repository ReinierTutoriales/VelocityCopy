# VelocityCopy Windows integration and system-impact contract

VelocityCopy integrates deeply enough to feel native on Windows 11 without replacing Explorer or installing a heavyweight always-busy service.

## Installation

- Distribution target: classic self-contained NSIS installers for Windows 11 x64 and ARM64 (`VelocityCopy-Setup-x64.exe` and `VelocityCopy-Setup-ARM64.exe`).
- Each installer copies the autonomous WinUI payload, registers Explorer `DragDropHandlers` and a conventional Add/Remove Programs entry.
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
- Minimize is a native Windows minimize operation. It keeps the transfer window in the taskbar/switcher model and never pauses, cancels or stops active work.
- Close is intentionally different from Minimize: closing an active transfer window cancels the work owned by that window and retires that transfer surface. Closing an idle window retires only that window; the resident app/tray process remains available.
- Clicking the tray icon restores the most recent live window when one exists or creates a new idle compact window when none remains.
- The tray menu exposes Open VelocityCopy and Exit. Exit is the explicit action that terminates the resident process and all remaining windows.
- Silent sign-in startup creates the tray presence without activating the compact window or creating a taskbar button.
- The tray icon negotiates `NOTIFYICON_VERSION_4` after `NIM_ADD` and handles the v4 callback layout, including keyboard selection.
- If Explorer restarts, VelocityCopy handles the registered `TaskbarCreated` message and re-adds the notification icon.
- The executable resource, tray, window icons, Start Menu shortcut, Installed Apps entry, installer and uninstaller share `Assets/VelocityCopy.ico`. It contains 16, 20, 24, 28, 32, 40, 48, 64, 80, 96, 128 and 256 pixel frames generated from the centered 512px `Logo/VelocityCopy.png` master using `tools/Generate-AppIcon.sh` (ImageMagick is a developer dependency only).
- The tray loads the embedded application resource with `LoadIconWithScaleDown` and small-icon metrics for the notification monitor, falling back to `LoadIconMetric`. It refreshes on DPI/display/settings changes and Explorer restart, without a polling timer. Replacing the icon publishes the new handle before releasing the old one; failure retains the existing handle.
- Window icon paths are absolute and relative to the installed executable, independent of the caller's working directory. NSIS uses the staged ICO for both installer and uninstaller and quotes the executable in `DisplayIcon`.
- Package CI validates every embedded icon frame byte-for-byte against the staged ICO on both architectures. The x64 install smoke also checks the installed asset, installer/uninstaller resources, Start Menu shortcut and Installed Apps registration, and launches from a different working directory. Visual verification on physical displays at 100%, 125%, 150% and 200%, including mixed-monitor setups, remains a manual check.

## Resident impact

The resident process exists only to provide near-instant Explorer handoff and state continuity.

- no directory scanning while idle
- no hashing while idle
- no network polling
- no filesystem watcher farm
- transfer snapshots come from IDataObject; no idle clipboard listener
- no global keyboard or mouse hooks
- no process injection, DLL injection, or generic keystroke capture
- no periodic benchmark
- no copy worker until work exists
- IPC waits on local named-pipe completion and shutdown events instead of polling
- the hidden startup window performs no animation
- when hidden in the tray and no transfer/planning work is active, VelocityCopy opts into Windows 11 EcoQoS with `ProcessPowerThrottling` / `PROCESS_POWER_THROTTLING_EXECUTION_SPEED`
- EcoQoS is removed before planning, copying, resuming work, showing the window or exiting, so active file I/O is never intentionally throttled

Any future idle feature must preserve this near-zero-CPU design and must not apply background I/O priority to active transfers.

## Session shutdown and recovery

- `WM_QUERYENDSESSION` returns success immediately; VelocityCopy does not delay Windows shutdown with UI.
- On a confirmed `WM_ENDSESSION`, EcoQoS is removed and the remaining live plan, deferred appends and queued sessions are snapshotted.
- The snapshot is stored as `VelocityCopy.Recovery.vcq` under `%LOCALAPPDATA%\VelocityCopy` using the existing atomic, per-writer unique staging file + `MoveFileExW` archive path. Cleanup recognizes both legacy and unique GUID staging names, keeps active owners, and preserves unrelated files.
- Recovery persistence is best-effort and must never block or veto Windows shutdown.

## File Explorer integration

Explorer integration uses an unpackaged in-process COM server:

- native COM DLL implementing `IShellExtInit` and `IContextMenu`
- classic installer registration of the CLSID and Explorer drop handlers
- architecture-matched shell DLL (x64 DLL for x64 Explorer, ARM64 DLL for ARM64 Explorer)
- synchronous menu-construction methods remain bounded and cheap
- `Invoke` packages intent into a versioned `ShellRequest` and returns
- all enumeration, planning, conflicts, queue state and I/O remain in the VelocityCopy app process
- IPC or launch failure must never destabilize Explorer

The shell uses the SuperCopier-style default drop-menu selection technique described in EXPLORER_INTEGRATION.md. The command-ID mapping is historical and not a documented Windows 11 guarantee. Automatic Ctrl+V remains a required installed-Windows validation gate. No global hooks, injection or resident clipboard staging are used.
## Process model

```text
Windows sign-in
    |
    +-- HKCU Run --startup -> VelocityCopy.WinUI.exe (hidden primary)
                               |
                               +-- local named-pipe IPC server (blocking idle)

Explorer.exe
    |
    +-- VelocityCopy.Shell.dll / IShellExtInit + IContextMenu
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
- ASan remains a RelWithDebInfo compile gate; do not block releases on MSVC ASan test execution on GitHub-hosted runners

See `docs/RELEASE_GATES.md` for the workflow tokens that architecture tests require.
