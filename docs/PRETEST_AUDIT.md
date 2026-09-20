# VelocityCopy pre-test audit

Status: pre-release manual test gate

## Confirmed automated baseline

The Windows CI and Package gates must be green before using an artifact for manual testing:

- x64 Release configure/build and core `ctest`
- x64 Release configure/build and core `ctest`
- self-contained WinUI 3 Release build for x64 when packaging is requested
- classic NSIS x64 installer generation
- x64 smoke install/uninstall, including uninstall while the resident tray process is running and verification that the install directory is gone
- ARM64 follows only after the x64 stabilization gate is green; it must reuse the same recipe with architecture-specific changes only

See `docs/RELEASE_GATES.md` before changing workflows.

## Fixed during pre-test audit

### Clipboard staging on demand

Explorer integration now carries a complete transfer snapshot. See EXPLORER_INTEGRATION.md for the SuperCopier-style default-selection mechanism and the mandatory automatic Ctrl+V gate.

### One-click test installer

During the current stabilization phase, manual testing uses the classic x64 installer `VelocityCopy-Setup-x64.exe`. ARM64 packaging is re-enabled after the x64 path is stable.

The setup requests elevation through UAC and copies the self-contained WinUI payload, including `VelocityCopy.WinUI.exe` and `VelocityCopy.Shell.dll`, into Program Files. Testers do not run PowerShell, certificates, MSIX files or framework packages manually.

The installer and uninstaller must force-close the resident `VelocityCopy.WinUI.exe` before replacing or removing installed files. A normal `WM_CLOSE` is insufficient because VelocityCopy intentionally hides to tray. The package smoke gate must launch the app in `--startup` mode before uninstall and must fail if the process remains alive or `$ProgramFiles\VelocityCopy` still exists afterward.

### Shutdown recovery prompt

VelocityCopy discovers `VelocityCopy.Recovery.vcq`, validates the saved work, and offers Resume / Discard without silently restarting file I/O. The recovery decision uses the same native top-level dialog path as conflict handling; it must never use an in-surface XAML `ContentDialog` inside the compact copier HWND.

## Manual test matrix

### Installation and shell registration

- Double-click `VelocityCopy-Setup-x64.exe` and accept the UAC prompt.
- Confirm VelocityCopy appears in installed apps.
- Launch once and confirm the tray icon appears.
- Confirm Explorer transfer handling is registered for Directory/Drive/Folder drag-drop targets.
- Restart Explorer and confirm the tray icon re-registers.
- Re-run the installer while VelocityCopy is resident and confirm the installed files are replaced cleanly.
- Uninstall VelocityCopy from Windows Installed apps while it is resident in the tray.
- Confirm the VelocityCopy process is gone, shell registration is removed, and `C:\Program Files\VelocityCopy` no longer exists.

### Resident startup and tray

- Sign out/in after first launch.
- Confirm startup is silent: no compact window flashes.
- Confirm exactly one resident VelocityCopy process.
- Confirm tray icon is present.
- Confirm click restores the compact window.
- Confirm minimize and close return the window to tray without stopping work.
- Confirm tray Exit terminates the resident process.
- Confirm idle CPU remains effectively zero and Efficiency Mode appears when hidden and idle.

### Explorer Copy/Cut handoff

- With VelocityCopy resident: Explorer Ctrl+C -> ordinary Ctrl+V.
- With VelocityCopy resident: Explorer Ctrl+X -> ordinary Ctrl+V and verify source removal only after successful destination copy.
- With VelocityCopy not resident/startup disabled: Ctrl+C or Ctrl+X first, then ordinary Ctrl+V; on-demand activation must preserve the data-object snapshot.
- Repeat a Copy paste to multiple destinations.
- Attempt a stale Cut paste after the original source was moved and verify failure is contained.

### Copy engine

- one small file
- one multi-GB file
- thousands of small files
- nested directories
- empty directories
- Unicode and long names
- destination already exists
- destination disappears mid-plan
- source disappears mid-plan
- source becomes a reparse point after planning
- removable drive removal during copy
- low-disk-space failure
- access-denied source/destination

### Controls

- Pause / Resume during a large file
- Skip a safe new destination
- Stop and resume remaining queue
- Cancel and verify partial destination cleanup rules
- append files/folders while a session is running
- queue reorder/delete pending jobs

### Move safety

- successful file move
- successful directory tree move
- conflict during move
- cancel during move
- source delete failure after successful copy
- non-empty source directory cleanup
- ensure no recursive `remove_all` semantics are used

### Persistence

- manual Save queue / Load queue
- corrupted queue archive
- truncated archive
- very large declared entry counts with tiny payload
- v1 queue archive migration defaults to Copy
- v2 Copy/Move roundtrip
- create a shutdown recovery checkpoint, relaunch collapsed, and confirm Resume / Discard appears in a separate native window rather than inside the 72 epx copier

### UI

- single-surface compact window around the 460x72 target; the window itself is the copier, with no nested decorative capsule
- logo at far left
- queue disclosure at far right
- whole-body progress fill
- expanded queue
- light/dark themes
- 100/125/150/200% DPI
- multi-monitor move and restore
- long filename/path truncation
- keyboard focus/tooltips/accessibility names

## Release blockers

Manual testing should stop and return to engineering if any of these occur:

- Explorer crash/hang
- source data loss
- destination corruption not surfaced as failure
- duplicate primary processes
- startup window flash on sign-in
- tray icon cannot recover after Explorer restart
- classic install/uninstall leaves VelocityCopy running, leaves `$ProgramFiles\VelocityCopy`, or leaves broken shell registration
- a modal decision is clipped inside or forces resizing of the compact copier surface
- malformed queue/IPC input crashes the process
- active copy remains in EcoQoS after work begins
