# Changelog

## 1.4.0 — 2026-10-20

- Improve source identity and recovery safeguards, destination path protection, execution controls and IPC handling.
- Reduce unnecessary planner path-buffer initialization and refine the estimated time remaining display.
- Consolidate the reviewed improvements from pull requests #84–#91 without enabling the experimental Phase 3 tree-move or streaming-copy paths (#92 and #93).
- Preserve Explorer clipboard integration and existing copy/move behavior; the unsigned x64 and ARM64 installers require Windows 11.

Validation limits:
- The release must pass Windows CI and Windows Package on the exact release commit before publication. Physical-device, SMB, removable-media and ARM64 runtime coverage remains separate from hosted CI.


## 1.3.0 — 2026-10-15

- Unify the main window, About and decision dialogs with the native Acrylic policy and theme resources. Conflicts show labeled source/destination paths, available size/date metadata and explicit action consequences; metadata reads run off the UI thread.
- Keep transfer telemetry in one persistent summary, with a clearer progress hierarchy. Expanded Details focuses on the queue and speed history, removing repeated information and visual elements.
- Allow About text and its project link to wrap within the available width, and let the Details button grow with large text while preserving its minimum height. Geometry tests cover 384 combinations of scale, text size, work-area height and composition; physical visual acceptance is separate.
- Audit staged native imports, reverse dependencies, package versions, file-type sizes and SHA-256 provenance on x64/ARM64. Reject unreviewed payload files and known unused SDK components, including nested directories, with a negative fixture validating rejection. Retain the supported componentized native runtime; no unmeasured package-size or RAM reduction is claimed.
- Add real compact, expanded and About screenshots to the README. The images were captured during 1.2.3 development before the 1.3.0 version bump.

- Planning uses native bulk directory enumeration and its 64-bit size/reparse metadata instead of opening every enumerated file for metadata. Source directory chains stay pinned while enumerating; collision, cancellation and entry bounds remain.
- Strategy selection reuses device facts for sources on the same resolved volume instead of repeating hardware probes for every selected file. Allocation failures in either recommendation entry point use conservative defaults.
- Basic file metadata now preserves creation/write times and mutable attributes, including not-indexed, through the actual copied destination handle. Metadata failures remain transfer failures so Move does not delete the source; ACL/owner/SACL replication is not implied.
- The recommended CopyFile2 strategy retains sparse files on supporting destinations on Windows 11 22H2 or later; older Windows keeps the existing copy path. Native offload and SMB compression remain available.
- Options can keep a clean completed window open with a native success notice and an Open destination folder action; automatic close remains the default.
- Audit stable direct/transitive dependencies, override the WebView2 SDK to 1.0.4258.31, and update CI actions to pinned current stable releases. Existing WinUI component versions and NSIS are already current.

- While resident, VelocityCopy routes Ctrl+V from normal Explorer filesystem folder views through a dedicated keyboard thread and STA worker into the existing transfer queue; Ctrl+C/X stay native. Explicit **Copy here**, **Move here** and **Paste with VelocityCopy** commands remain available. No undocumented numeric default-menu selection is used.
- Shell dispatch no longer launches a second activation after ambiguous IPC delivery. The clipboard decoder is shared internally without a public test-only DLL export.
- Queue templates remove a repeated nested panel while preserving native virtualization and stable selection. Active bars retain stable accessible file names and native RangeValue progress.
- CI covers concurrent append/page reorder/snapshots with four workers over 22,000 synthetic items and records a repeated planner baseline without changing production tuning.
- Release publication has one authoritative path: the Release workflow reuses the exact x64/ARM64 installers from a successful Windows Package run on the selected `main` commit, generates SHA-256 checksums, and publishes without rebuilding the binaries.
- Engineering/release contracts were consolidated so exact-SHA CI, Package, source inspection and physical Windows validation remain separate evidence levels.

Compatibility and validation limits:
- Explorer keyboard routing requires VelocityCopy to be resident and a normal filesystem folder view. Native Ctrl+C/X remain Windows-owned; this does not replace every Paste entry point, virtual folder or application.
- Installers remain unsigned and architecture-specific for Windows 11 x64/ARM64. Automated gates do not certify physical ARM64 runtime, every Explorer/focus/device path or visual DPI/Text Size acceptance. See `docs/RELEASE_GATES.md`.

## 1.2.2 — 2026-10-09

- Cleaner, more compact window: a Skip button sits between Pause and Cancel; Skip is no longer in the Options menu (Stop stays there because it keeps the queue for later, unlike Pause).
- Details show two sections instead of three: Queue and Information. The speed graph moved, smaller, into Information (the separate Performance card is gone), and the expanded window is 640 instead of 880 pixels wide.
- The window uses the same translucent Windows 11 material as the system menus (acrylic) instead of the flat Mica background.
- Queue edit commands are smaller Windows 11 toolbar buttons (no frame until hovered): fine chevrons to move items up and down, and a trash icon that turns red on hover. Its tooltip, "Remove from queue (files are not deleted)", says that no file is deleted.
- Restoring one copy from the taskbar brings back every other copy that is still running and minimized, so two simultaneous copies are seen together. The clicked window stays in front.

## 1.2.1 — 2026-10-08

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
