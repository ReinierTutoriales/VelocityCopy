# VelocityCopy implementation line

Status: canonical ordered execution line for stabilization and UI work.

This document answers one question: **what do we change next, and what evidence is required before moving on?** It complements the engineering rules; it does not replace them.

## Gate 0 — baseline before every work block

1. Read the current HEAD and the relevant canonical docs.
2. Inspect the Actions run for that exact HEAD.
3. If `main` is red, diagnose and repair it before unrelated work.
4. Inspect the production files and tests affected by the proposed change.
5. Classify the work as bug fix, refactor, optimization, behavior change, UI change, packaging change, or documentation change.
6. Do not mix unrelated classes in one commit.
7. UI-affecting pull requests must compile `VelocityCopy.UI.vcxproj` in x64 and ARM64 before merge; core-only CI is not sufficient evidence for WinUI changes.

## Gate 1 — x64 functional stabilization

Before broad UI redesign, verify and stabilize:
- copy/move correctness and data-safety paths;
- queue/append/stop/resume/cancel/conflict semantics;
- Explorer command target resolution;
- complete IDataObject transfer snapshots and explicit Copy/Move;
- IPC single-instance dispatch;
- tray/startup behavior;
- drag/drop append-to-active-session semantics.

Global keyboard hooks are not an acceptable substitute for Explorer integration.

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
- compact target remains **380x72 epx**;
- queue/destination content expands the same HWND downward;
- eliminate clipping caused by compact-shell sizing;
- do not create a second application window merely to show the queue.

### Block C — integrated progress
- no separate visible progress strip in compact mode;
- progress is the proportional left-to-right fill of the copier/window body;
- use system Accent and theme resources;
- preserve readable content, light/dark behavior and High Contrast;
- no neon/glow or continuous decorative animation.

### Block D — controls
- retain existing actions/handlers where possible;
- reorganize current controls rather than inventing controls from visual references;
- queue disclosure uses a subtle Fluent chevron;
- preserve tooltips, accessible names, focus and native button states;
- secondary commands such as Skip/Stop live in Options and must not be kept alive as hidden XAML buttons.

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
- Explorer context commands;
- Ctrl+C/Ctrl+X -> ordinary Ctrl+V through the default handler; this gate is not proven by synthetic COM tests;
- tray startup/hide/restore/Explorer restart;
- installed application/taskbar/tray icon;
- active-transfer drag/drop append;
- compact/expanded UI behavior and DPI/theme matrix;
- large removable-drive copy, Pause/Resume, Skip from Options, Cancel, and uninstall while the app is resident in the tray.

Record failures by subsystem instead of patching several layers simultaneously.

## Gate 5 — ARM64

After x64 is stable:
- enable/build ARM64 from the same recipe;
- make only architecture-specific changes where necessary;
- produce a separate ARM64 classic installer;
- do not create a mixed/universal installer;
- ARM64 failures must not invalidate the working x64 path.

## Stabilization invariant ledger

These are regressions that have already occurred or failure modes that were explicitly closed. A change touching the named subsystem must re-check the protecting test/contract before merge.

| Invariant | Production surface | Protection |
|---|---|---|
| Pending live queue uses constant-time front removal | `live_copy_plan.*` | large-queue stress/architecture tests |
| Storage buffer recommendation reaches `CopyFile2` `ioDesiredSize` | `strategy_selector`, `job_executor`, `copy_engine` | strategy/copy architecture tests |
| No dead `NoBuffering` / speculative IOCP strategy state | copy strategy/engine | architecture tests |
| Installer/uninstaller closes resident VelocityCopy first | NSIS installer | x64 smoke install/uninstall |
| No XAML `ContentDialog` in compact runtime paths | `src/ui` | architecture tests |
| Append planner foreground wait is bounded to 8 seconds | `MainWindow.xaml.h`, `Execution.cpp` | copy-append architecture test |
| Low-level device topology IOCTL probes are fixed-disk only | `storage_profiler.cpp` | storage-profiler tests |
| Skip availability has one pure behavioral predicate and Options refreshes state on open | `ui_snapshot.*`, WinUI menu/execution | `ui_snapshot_test`, `skip_architecture_test` |
| Native caption buttons remain visible and only the top filename row reserves `RightInset` | `MainWindow.xaml*` | compact UI architecture test |
| Telemetry and actions occupy separate bottom-row columns | `MainWindow.xaml` | compact UI architecture test |
| WinUI x64/ARM64 compiles on pull requests before merge | `.github/workflows/ci.yml` | required PR CI workflow |

## Optimization rule

No optimization is accepted because it merely looks cleaner or faster. State the measurable or correctness problem first, preserve the relevant invariants, and benchmark performance-sensitive engine changes before replacing the baseline.

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

`VelocityCopy.WinUI.exe` carries a native `VERSIONINFO` resource; Explorer Properties, the About flyout and Installed Apps must not expose unrelated version identities. `src/ui/VelocityCopy.UI/Version.h` owns the native four-part version and its MAJOR/MINOR/PATCH values must match `project(VelocityCopy VERSION ...)` in the root CMake file. The packaging workflow derives NSIS `DISPLAY_VERSION` from that header rather than inventing a separate run-number version. The About flyout reads the running executable's `VERSIONINFO` first and falls back to the compile-time version so a known build never displays `Unknown`.

## Documentation rule

Every future material decision or newly discovered reusable failure mode updates the owning canonical document in the same coherent change. If documentation conflicts, reconcile it before implementation. Do not allow stale documentation to become an accidental second roadmap.
