# Stabilization audit — 2026-10-15

This record reconciles the 2026-10-14 implementation line with the available
continuation chats and current source. It does not certify visual acceptance or
recover missing chat messages: some cached assistant messages are unresolved
content references. Testing for this work is restricted to repository CI;
no interactive tests or installation in the owner's Windows session.

## Recovered requirements and current implementation

| Requirement | Source and automated coverage | Remaining evidence or implementation |
| --- | --- | --- |
| Ctrl+C/X → Ctrl+V in Explorer | `explorer_paste.cpp`, shared clipboard decoder, v3 IPC; explicit Shell commands retained | Installed keyboard/focus/tabs/restart acceptance remains separate from CI. No broad replacement of every Shell operation is claimed. |
| Queue integrity under concurrent execution | Stable IDs, bounded 256-row pages, page reorder validation, append transactions, allocation-failure tests | New synthetic concurrency test combines four workers, append, active/pending snapshots and page reorder over 22,000 items; CI must validate it. |
| Compact/expanded geometry | Content measurement, frame/work-area budgets, measured telemetry reserves, text-scale event handling, bounded active/pending viewports | Visual DPI, text-size and theme acceptance is not inferred from compilation. No new arbitrary width constants are introduced. |
| Active transfer progress | At most eight authoritative active rows; native RangeValue progress; no bars for pending files | This change keeps accessible file names stable and avoids redundant paused-state assignments. |
| Conflict, skipped, failed and completion states | `MainWindow.Conflict.cpp`, `FinishCopy`, issue list, capability-gated retries; KeepBoth/KeepBothAll already have core and UI callers | A redesign of the terminal/decision composition is not implemented by a row-template optimization. Earlier notes describing Keep both or taskbar integration as absent are historical. |
| Safe Move recovery after restart | QueueArchive v3, durable flush, separate source-removal incidents, source/destination fingerprints and unavailable-volume handling; recovery/archive tests | Physical disconnect/power-loss testing remains unverified. These existing functions must not be replaced by another copy of the same transfer. |
| Taskbar/tray/notifications | Existing ITaskbarList3 replay, TaskbarCreated handling, resident startup, conditional completion notification, multiwindow restore | Manual theme/focus/restart behavior remains a release gate. |
| Planner performance | Existing plan-only JSON mode and benchmark tools; planner retains a 250,000-entry safety ceiling | CI now records five 1,000-file planning samples and median, tied to the commit. This is not a bulk-throughput result. |

## Improvements in this block

- The Shell DLL must not launch a second activation after ambiguous IPC delivery.
  Existing transport tests establish ambiguity; the caller architecture contract
  now requires checking it before on-demand launch.
- Both queue templates remove a nested Grid per realized row. Narrow also removes
  one column definition. Bindings, native virtualization, theme resources,
  selection/reorder and the existing data bounds remain unchanged. Element-count
  reduction is established in source; frame-time savings require profiling.
- Active progress retains a stable accessible file name. Percentage changes are
  exposed by the native ProgressBar RangeValue rather than by rewriting its name
  each snapshot. Paused state is assigned only on a real change.
- The CI planner report is descriptive, uses disposable runner fixtures and does
  not select production worker counts or impose unreliable disk-speed thresholds.

Official guidance: [WinUI collection performance](https://learn.microsoft.com/en-us/windows/apps/develop/performance/optimize-gridview-and-listview),
[XAML layout](https://learn.microsoft.com/en-us/windows/apps/develop/performance/optimize-your-xaml-layout),
[accessibility](https://learn.microsoft.com/en-us/windows/apps/develop/accessibility).

## Preserve the proposed work without calling it implemented

The earlier Robocopy discussion proposed persistent mid-file restart, advanced
metadata/ACL handling, sparse/block cloning, SMB features and I/O QoS. It did not
establish a finished implementation of those capabilities. Current code already
has CopyFile2 pause/resume, requested SMB compressed traffic, topology-gated
workers and destination-folder lease reuse; these must not be discarded.

Further changes need these explicit acceptance conditions:

- **Persistent restart:** checkpoint identity, changed source/destination
  rejection, durable checkpoint writes, restart after process loss and Move
  source preservation. Pause within the same process is not this feature.
- **Metadata policy:** specify what is preserved on NTFS/ReFS/FAT/SMB and how
  unsupported metadata or insufficient privileges is reported; no silent promise
  of full ACL/owner/audit fidelity.
- **Sparse/cloning/QoS:** prove supported filesystem/device capabilities, preserve
  the current fallback and pause/cancel behavior, then compare real workloads.
- **Planner streaming:** preserve collision detection, destination reservations,
  aggregate totals, append, recovery and cancellation before relaxing the current
  materialization bound. A larger limit alone is not a memory optimization.
- **Terminal visual redesign:** define same-window completion/decision ownership,
  localization, focus, theme resources and capability-gated actions. Preserve all
  current Keep both, retry, issue-inspection and multiwindow behavior.

The open-source assessment is in [OPEN_SOURCE_ASSESSMENT.md](OPEN_SOURCE_ASSESSMENT.md).
No new runtime dependency or release publication is implied by this audit.
