# Changelog

## 2.1.2 — 2026-10-03

RepartoCopier release hardening.

- Explorer handoff now uses a bounded IPC deadline so a stalled resident receiver cannot block the shell indefinitely.
- New Explorer-triggered transfers explicitly start in a normal visible window instead of inheriting a minimized presentation state.
- Full-window external drag/drop reaches the active transfer surface while preserving queue reordering.
- Installer payloads validate PE architecture for both the WinUI executable and shell DLL before packaging.
- Public product identity, About, tray, Explorer commands and installer are aligned to RepartoCopier 2.1.2 while technical VelocityCopy identifiers remain stable for upgrade compatibility.
- Window lifecycle documentation and pre-release checks now match the implemented Minimize/Close behavior.

## 1.0.0 — 2026-09-22

First stable VelocityCopy release.

- Compact WinUI 3 copier with integrated progress and expandable queue
- Pause, resume, skip, stop and cancel; save and load queues
- Explorer transfer handler; tray residency with process-wide EcoQoS coordination
- Classic self-contained installers for x64 and ARM64
- Per-session native recovery with explicit Resume/Discard decisions
- About flyout with version read from the executable

Hardening included in 1.0:
- Queue draining avoids quadratic behavior with large file counts
- Append planning waits are bounded
- Device probes are limited to fixed disks
- Installer closes the running app before install/uninstall
- Skip availability is shared between menu and primary action
- Recovery storage works for the unpackaged application model
- App-owned tray and window lifecycle keep the resident process usable after completed windows close
