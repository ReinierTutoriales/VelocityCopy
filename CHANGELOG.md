# Changelog

## 0.20.0 — 2026-09-21 (pre-release)

First public pre-release.

- Compact WinUI 3 copier with integrated progress and expandable queue
- Pause, resume, skip, stop and cancel; save and load queues
- Explorer transfer handler; tray residency with EcoQoS while idle
- Classic self-contained installers for x64 and ARM64
- Native recovery prompt after an interrupted session
- About flyout with version read from the executable

Fixes and hardening in this cycle:
- Queue draining no longer degrades quadratically with large file counts
- Append planning waits are bounded; no indefinite freeze on slow removable drives
- Device probes limited to fixed disks
- Installer closes the running app before install/uninstall
- Skip availability shared between menu and action, covered by behavior tests
