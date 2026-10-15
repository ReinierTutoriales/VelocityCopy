# VelocityCopy implementation line

Status: canonical ordered execution line for stabilization and UI work.

## x64 stabilization audit — 2026-10-14

Baseline `2b9ac7ae06e6e5b29e31d075bb5f33f279450906` passed Windows
Package, x64/core compilation, ARM64 compilation, ASan compilation and WinUI
build/launch. Windows CI failed four architecture tests: queue-density scanned
the active panel as if it were header commands; visual-fidelity found two new
explicit-font pagination icons; append still required the retired whole-queue
reorder caller; compact-UI matched `ActiveQueuePanel` as `QueuePanel`.

The coherent correction preserves the executor and adds live active-row refresh,
session-bound native drag completion, measured header/active chrome with useful
pending height, separate pagination layout, localized queue navigation, compact
width preservation on expansion, and TaskbarButtonCreated state replay.
Page-reorder allocation failures and constrained-height budgets have executable
regression coverage. Existing queue-archive concurrency coverage also reproduced
a local replacement race: save serialization remains concurrent but its final
replace and same-process loads share a mutex. This does not promise cross-process
serialization or bypass an external file lock; failed replacement still preserves
the old archive and removes the caller's own staging file.

Exact-change Windows CI/Package and physical x64 geometry/system acceptance remain
required. Earlier statements describing active-row data as unavailable or outer
scrolling as Narrow-only are superseded by `UI_ARCHITECTURE.md`'s current contract.

This document answers one question: **what do we change next, and what evidence is required before moving on?** It complements the engineering rules; it does not replace them.

## Product scope and remaining work — agreed 2026-10-14

This section is the **current product priority and decision record**, not a second roadmap.
The gate order and release evidence below remain mandatory. An implementation
commit is not evidence of a successful Windows build or real-device behavior.

### Product decisions (do not regress)

- VelocityCopy is a **fast, compact Windows 11 file copier**, not a dashboard.
  Do not add a feature without identifying a real user problem, data source,
  interaction cost, scaling behavior and acceptance criteria.
- **Compact:** content-measured height and a width that accommodates the approved
  command row; one exterior contour, same HWND when expanded. The older
  approximately 460 x 72 epx sketch is not an immutable sizing contract.
  Do not add nested decorative cards, a second queue window, or per-file
  progress bars in compact mode. See the superseding phase-1 geometry in
  `docs/UI_SPEC.md`.
- **Global progress:** exactly one native WinUI `ProgressBar` in the normal
  transfer surface, as specified by the later PR #57 contract in
  `docs/UI_SPEC.md`. Do not revive the superseded proportional surface-fill
  Border or add a second progress bar. Preserve UI Automation, legibility,
  system Accent, dark/light and High Contrast.
- **Expanded:** bounded height; no growth proportional to file count.
  Display a small **active transfers** section with per-file progress only
  where the engine has authoritative per-file bytes; display pending files
  as lightweight rows in a bounded/virtualized list. Do not fabricate per-file
  percentages or speed from global counters.
- **Thousands of files:** keep a fixed UI budget, bounded queue snapshots
  and bounded refresh rate. A large number of tiny files should primarily
  update aggregate counters; do not animate a bar for every completed file.
- **Attention states:** surface conflicts, unavailable destination, failures
  and actionable recovery only when they occur. Completed/skipped counts
  belong in aggregate status unless detailed inspection is explicitly needed.
- **Windows:** taskbar progress and completion notification are conditional
  on actual execution state and user-facing utility; avoid duplicate or
  noisy notifications. Reuse existing implementations where present.
- **No speculative rewrites:** preserve copy/move safety, active worker
  concurrency, append/resume/cancel semantics, shell integration and existing
  handlers. Avoid UI-only state that can disagree with the engine.

### Verified scope vs implementation claims

- The recent Queue changes introduced bounded offset-based `queue_view`,
  page navigation and controls, selection by stable file ID, and
  `reorder_pending_page` for page-scoped drag ordering.
- Those commits **have not been established as Windows-build-green or
  hardware-tested in this decision record**. Treat them as implemented
  source changes requiring exact-SHA CI, architecture and runtime review.
- The compact body, its measured size, integrated progress and expanded
  geometry have **not** been changed by that Queue sequence. Earlier UI
  work exists, but its final visual acceptance is not established.
- Taskbar, notifications, Explorer and destination recovery must be audited
  before being labeled absent or reimplemented. Do not turn "not verified"
  into "not implemented".

### Remaining implementation blocks (priority order)

1. **Queue integrity and recent-change review.** Inspect exact-HEAD Windows
   CI and fix actual compile/test failures. Exercise page navigation, stale
   drag snapshots, reorder while workers acquire files, multiple selection,
   queue shrink/drain and large-queue performance. Preserve hidden rows.
2. **Compact body and geometry.** Inspect existing XAML and window-sizing
   code, preserve the one-contour design and the single native ProgressBar,
   eliminate clipping, and measure content-derived client/outer HWND
   dimensions on Windows at supported DPI and text scales. Do not impose
   the obsolete fixed 460 x 72 epx sketch or enlarge compact mode for Queue.
3. **Expanded layout.** Same HWND, bounded queue viewport, correct responsive
   columns, scroll/focus retention and visible controls without clipping.
4. **Active transfer presentation.** First audit whether the engine exposes
   authoritative per-file progress (ID, bytes done/total, lifecycle). Only
   then add a bounded active-files projection to Queue and a thin progress
   indicator for active files. No bar for each pending file; no unbounded
   history of successful files in memory.
5. **Actionable states.** Integrate existing conflict, skipped, failed,
   destination-unavailable and completion outcomes into concise aggregate
   and exception UI. Recovery controls must map to real engine transitions.
6. **Windows shell and system behavior.** Audit before changes: Explorer
   Copy/Move/append, taskbar progress/pause/error, tray, notification on
   completion, theme, keyboard, accessibility, DPI and monitor restore.
7. **Performance and reliability.** Benchmark planner, queue snapshots and
   transfer responsiveness with realistic large trees and big files;
   optimize only measured bottlenecks, preserving data-safety invariants.
8. **Release readiness.** Exact-SHA x64 CI and Package, x64 real-Windows
   smoke and failure matrix, ARM64 compilation/installer plus separate
   physical-device sign-off. Publish only through `docs/RELEASE_GATES.md`.

### Contract reconciliation

The newer `docs/UI_SPEC.md` phase-1 contract supersedes earlier phase-0
geometry and surface-fill descriptions. Specifically, compact height is
content-driven through `ResizeWindowToContent()` and aggregate progress uses
one native WinUI `ProgressBar` with accessible RangeValue and native
paused/error states. The former approximate 460 x 72 epx target and the
proportional body-fill requirement are historical design concepts, **not**
acceptance gates. Preserve these newer contracts unless a measured defect
justifies an explicit product change. This reconciliation does not assert
that current Windows geometry or the latest commits have passed CI.

### Deliberately excluded

- A progress bar for every pending file, unbounded completed-file rows,
  a giant expanded window, decorative nested cards, per-file animations for
  tiny files, redundant controls and a second window for the queue.
- Invented throughput/ETA per file without reliable engine telemetry.
- Calling source changes "tested", "finished" or "released" before the
  corresponding evidence exists.

### Acceptance rule for every proposed enhancement

Describe the user problem; identify existing code/data and dependencies;
specify bounded behavior at 10 / 1,000 / 100,000 files; state copy/move
safety and accessibility invariants; implement a coherent block; inspect
diff and exact-SHA Windows CI; record real-Windows measurements separately.
If it adds visual noise or runtime cost without measurable utility, omit it.

## Gate 0 — baseline before every work block

1. Read the current HEAD and the relevant canonical docs.
2. Inspect the Actions run for that exact HEAD.
3. If `main` is red, diagnose and repair it before unrelated work.
4. Inspect the production files and tests affected by the proposed change.
5. Classify the work as bug fix, refactor, optimization, behavior change, UI change, packaging change, or documentation change.
6. Do not mix unrelated classes in one commit.

## Gate 1 — x64 functional stabilization

Before broad UI redesign, verify and stabilize:
- copy/move correctness and data-safety paths;
- queue/append/stop/resume/cancel/conflict semantics;
- Explorer right-button DragDropHandlers target resolution;
- complete IDataObject transfer snapshots and explicit Copy/Move commands;
- IPC single-instance dispatch;
- tray/startup behavior;
- drag/drop append-to-active-session semantics.

Explorer keyboard routing is implemented by the resident adapter described in EXPLORER_INTEGRATION.md; the shell DLL remains bounded and IPC-only.

Exit evidence: exact x64 CI SHA green plus source-level contract review. Windows-only behavior remains explicitly marked unverified until exercised on Windows.

## Gate 2 — packaging integrity

Before relying on a new installer:
- verify the staged payload contains `VelocityCopy.WinUI.exe`;
- verify it contains `VelocityCopy.Shell.dll` before installer shell registration;
- build classic x64 NSIS setup;
- smoke install/uninstall;
- verify installed launch;
- keep packaging separate from normal commit CI.

Application icon integration is atomic: valid multi-resolution ICO + RC/resource definition + project compilation + installed/tray behavior in one coherent change. Never commit a project reference to a missing binary resource.

## Gate 3 — UI structural redesign

Only start from a green x64 baseline.

### Block A — geometry
- the top-level window is the copier;
- one exterior contour only;
- remove nested decorative capsule/card/surface;
- preserve existing functional handlers and engine state;
- no progress redesign or unrelated logic rewrite in this block.

### Block B — sizing and expansion
- compact height is content-driven; width must fit the approved controls and accessibility requirements (see `docs/UI_SPEC.md`);
- queue/destination content expands the same HWND downward;
- eliminate clipping caused by compact-shell sizing;
- do not create a second application window merely to show the queue.

### Block C — integrated progress
- keep exactly one native WinUI `ProgressBar` in the compact transfer surface;
- do not revive the superseded proportional body-fill implementation;
- use system Accent and theme resources;
- preserve readable content, light/dark behavior and High Contrast;
- no neon/glow or continuous decorative animation.

### Block D — controls
- retain existing actions/handlers where possible;
- reorganize current controls rather than inventing controls from visual references;
- queue disclosure uses a subtle Fluent chevron;
- preserve tooltips, accessible names, focus and native button states.

### Block E — queue
- same HWND expands downward;
- no nested decorative card;
- keep bounded/virtualized queue behavior and existing queue operations;
- do not couple core queue state to concrete WinUI controls.

### Block F — auxiliary destination/layout flow
- destination/layout UI is separate from drag/drop;
- remove stale DropFlow naming only as a controlled refactor;
- drag/drop must never open this flow;
- validate sizing so menus/content are not clipped.

### Block G — visual/system validation
- Windows light/dark;
- inherited Accent;
- High Contrast;
- 100/125/150/200% DPI;
- multi-monitor restore;
- long-name truncation;
- keyboard focus/tooltips/accessibility;
- tray hide/restore while active.

Each UI block follows: `green HEAD -> inspect -> complete block -> review diff -> one commit -> exact-SHA CI -> continue only if green`.

## Gate 4 — real Windows validation

Automated tests do not prove Explorer/taskbar/tray integration. On an installed x64 build verify:
- Explorer right-button drop commands (Copy here / Move here);
- Explorer Ctrl+C/Ctrl+X -> Ctrl+V must be tested through the resident adapter into JobExecutor;
- tray startup/hide/restore/Explorer restart;
- installed application/taskbar/tray icon;
- active-transfer drag/drop append;
- compact/expanded UI behavior and DPI/theme matrix.

Record failures by subsystem instead of patching several layers simultaneously.

## Gate 5 — ARM64

After x64 is stable:
- enable/build ARM64 from the same recipe;
- make only architecture-specific changes where necessary;
- produce a separate ARM64 classic installer;
- do not create a mixed/universal installer;
- ARM64 failures must not invalidate the working x64 path.

## Optimization rule

No optimization is accepted because it merely looks cleaner or faster. State the measurable or correctness problem first, preserve the relevant invariants, and benchmark performance-sensitive engine changes before replacing the baseline.

Planner work follows the same rule. The production destination registry stops walking ancestor directories once it reaches an already-registered directory: registering a directory establishes its complete ancestor chain, so re-normalizing that chain once per sibling file is redundant. Preserve destination-collision semantics and measure planning separately from copy execution. `VelocityCopyBenchmark --json` reports `planning_seconds` plus file/directory counts so any future incremental-planning redesign can be justified against measured large-tree data rather than assumption.

## Copy-engine responsiveness rule

Windows 11 `CopyFile2` is allowed to own the actual file/stream semantics, but VelocityCopy must bound requested I/O-cycle size instead of leaving large transfers entirely to the OS default. Large ISO/image transfers must continue to emit useful progress/control opportunities rather than appearing frozen for long intervals. `COPYFILE2_CALLBACK_POLL_CONTINUE` is also treated as a control heartbeat using the last authoritative byte count so Pause/Stop/Cancel do not depend exclusively on the next completed chunk.

Storage-profile tuning is production data, not decorative metadata. A non-zero `StrategyRecommendation::suggested_buffer_bytes` must flow through `JobExecutionOptions` and `CopyOptions` into `COPYFILE2_EXTENDED_PARAMETERS_V2::ioDesiredSize`. Do not calculate tuning values that the copy engine silently discards.

Interactive production uses buffered `CopyFile2`. Do not keep unreachable strategy enums or speculative async/IOCP flags merely for a possible future implementation. If a second execution strategy is introduced later, add its enum/state in the same change that adds a real executable path and tests for it.

Stop is an active-file control, not only a between-files check. An in-flight `CopyFile2` callback must observe `ExecutionDirective::Stop`; if CopyFile2 aborts that new destination, discard the partial destination, release the file back to the live plan and return a stopped session so Resume restarts it cleanly.

Do not reintroduce unconditional `COPY_FILE_COPY_SYMLINK`: source reparse points are rejected before execution, so asking CopyFile2 to preserve symlinks contradicts the storage-safety contract.

Append planning is part of the same cancellation contract as the primary planner. `JobPlanningWorker::cancel_pending()` must cancel the currently enumerating request as well as queued requests, and explicit cancellation must still release caller reservations through completion callbacks. A cancelled planner is a control transition, not an error banner.

Append-planner completion waits must also be bounded. Filesystem enumeration can block inside an individual OS call even after a stop token has been requested, so the copy/session thread must not wait forever for `planning_count` to reach zero. After the bounded wait expires, close that append gate and discard any eventual late result for that gate instead of freezing transfer finalization. This guard does not make a blocked filesystem syscall interruptible; it prevents that blocked planner from owning the foreground transfer lifetime indefinitely.

Low-level topology probes (`CreateFile` on a volume plus `DeviceIoControl` for device number, disk extents and seek penalty) belong only on local fixed disks. Removable, network, optical and other non-fixed sources use conservative strategy defaults and must not perform those synchronous hardware probes on the transfer path, because sleeping/disconnected media can block them for an unbounded interval.

A no-progress interval inside the synchronous `CopyFile2` call is a separate failure class from planner waiting. The append-gate timeout and storage-profiler guard do not prove that kernel/filesystem I/O itself cannot stall. If a real-Windows build still stops advancing bytes while the current file remains active, capture that case separately; do not misdiagnose it as queue/UI deadlock.

Large live queues must not remove from the front of a contiguous `std::vector`. The production pending queue uses constant-time front removal semantics; tests must exercise a large synthetic drain so an accidental O(n^2) front-erasure implementation is caught before release.

## Version identity rule

`VelocityCopy.WinUI.exe` carries a native `VERSIONINFO` resource; Explorer Properties, the About dialog and Installed Apps must not expose unrelated version identities. `src/ui/VelocityCopy.UI/Version.h` owns the native four-part version and its MAJOR/MINOR/PATCH values must match `project(VelocityCopy VERSION ...)` in the root CMake file. The packaging workflow derives NSIS `DISPLAY_VERSION` from that header rather than inventing a separate run-number version. The About dialog reads the running executable's `VERSIONINFO`, not a duplicated display constant, and remains a native top-level dialog outside the compact XAML surface.

## Documentation rule

Every future material decision or newly discovered reusable failure mode updates the owning canonical document in the same coherent change. If documentation conflicts, reconcile it before implementation. Do not allow stale documentation to become an accidental second roadmap.
