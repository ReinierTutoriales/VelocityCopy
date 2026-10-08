# Changelog

## 1.2.1 — Unreleased

- Replacing a read-only, hidden or system file at the destination works like Explorer instead of failing with "access denied" (and no longer offers a pointless "Retry as administrator").
- A file briefly locked by antivirus or the indexer right after it was written is retried a few times before it counts as failed.
- Copies of many small files into the same folder no longer reopen and re-check every destination parent folder for each file: each copy worker keeps the folder chain locked (still junction-safe) and reuses it.
- A copy that finishes while the window is minimized, in the tray or behind another app shows a Windows notification instead of disappearing silently.
- "Completed with issues" notices have a "View all" button listing every failed, skipped or kept-source item with its reason (up to 1,000, failures first), with "Copy list" to the clipboard.
- Many small files between two different solid-state drives are copied 4 at a time; hard disks, USB, network, unknown storage and copies within one disk stay one file at a time.
- Clicking the tray icon brings back every window that is still copying or waiting for a decision, not only the newest one, so two simultaneous copies hidden to the tray are both visible again.
- Each copy window is titled with its progress and destination folder ("45% · Fotos — VelocityCopy"), so simultaneous copies can be told apart in the taskbar and Alt+Tab.
- The taskbar button shows copy progress (yellow while paused or waiting for a decision, red on errors).
- Setup is DPI-aware (sharp at 125-200% scaling) and registers size, links and a silent uninstall command in Settings > Apps.
- C++/WinRT 3.0.260818.1, Windows App SDK 2.5.1 component set, NSIS 3.13 from the official distribution, and GitHub Actions on Node 24 (checkout v6, upload-artifact v6, cache v5, setup-msbuild v3).

## 1.2.0 — 2026-10-08

Copy-correctness, protected-destination and interaction release.

- Copying or moving several loose files no longer recreates their source folder at the destination; files land directly in the target folder like Explorer. A parent folder is only used to separate files whose names collide.
- Same-volume Move renames files in place instead of copying every byte and deleting the source.
- Planning failures report the real reason (missing source, duplicate destination, destination inside source) instead of a generic error.
- An unreadable subfolder, junction or file that disappears while scanning no longer aborts the whole job; it is reported as a failed item and everything else is transferred.
- Saved queue files are validated: outputs must stay inside the destination and inputs inside the declared sources.
- The retry dialog names the failed files and why; the completion notice lists only non-zero outcomes plus the first failure. The About window sizes to its content.
- Conflicts offer "Keep both" (writes `name (2).ext`) and show size and date of both files.
- A transfer that cannot fit on the destination asks before writing anything.
- Hidden/system folders keep those attributes when copied.
- Setup enables Windows long-path support so deep folder trees no longer fail, and shows a one-time "ready" notification from the tray.
- Finishing setup starts VelocityCopy resident in the notification area instead of leaving its window open.
- Removed unused code: the legacy job queue, destination catalog enumeration and copy-plan editing helpers.
- Copying to a protected destination (C:\, C:\Windows, Program Files) asks to continue as administrator before writing anything and hands the job to an elevated instance through UAC; access-denied items in the retry dialog offer "Retry as administrator".
- Questions and problem notices come to the front on their own: Explorer hands the foreground to VelocityCopy when it sends a copy, a minimized or tray-hidden window is restored before a decision (Windows hides owned dialogs with their owner), and the taskbar button flashes when Windows keeps the focus elsewhere.
- The "destination/drive in use" questions use plain choices ("Add to current copy", "Copy afterwards", "Copy at the same time") and explain when copying at the same time helps.
- The notification-area icon keeps its hover text after the setup notification.
- Setup text is compiled as UTF-8, so accented Spanish installer messages no longer show garbled characters.
- Decision dialogs size their client area (not the outer frame) from the dialog's own monitor scale and scroll instead of clipping; the in-window notice re-fits the window whenever its height changes.

## 1.1.0 — 2026-10-04

VelocityCopy stabilization release.

- Explorer-triggered transfers now start in a normal visible window instead of inheriting a minimized presentation state.
- External Explorer drag/drop reaches the full transfer surface while preserving internal queue reordering.
- Shell IPC handoff is bounded so a stalled resident receiver cannot block Explorer indefinitely.
- Installer packaging validates the PE architecture of both `VelocityCopy.WinUI.exe` and `VelocityCopy.Shell.dll`.
- Release and pre-test contracts now match the implemented window lifecycle and current 380x72 compact UI.
- Regression coverage was expanded for routed drop, activation visibility, stalled IPC and package identity.

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
