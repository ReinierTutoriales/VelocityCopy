# VelocityCopy full audit — 2026-10-08

Scope: copy planning and execution, live queue state, conflict/decision UI,
in-window notices, queue persistence and recovery, routing, About window,
strategy selection, queue view. Linux source checks only: everything below
still requires green Windows CI and manual Windows validation.

## Fixed

### Copy logic
- **Loose files recreated their source folder.** Any multi-item job placed each
  loose file at `destination\<source folder>\file`. Files now land directly in
  the destination (Explorer semantics); the parent folder is used only for
  files whose top-level names collide. `drop_layout_test` covers both cases.
- **One bad entry aborted the whole job.** An unreadable subfolder, a
  junction/symlink, an unsupported entry or a file deleted mid-scan threw and
  cancelled everything. The planner now walks the tree with an explicit stack,
  records those entries as `CopyPlan::failures` and keeps planning. `LiveCopyPlan`
  turns them into terminal `Failed` items, so the final notice reports them.
  Junctions are still never traversed (`planner_safety_test` case 8).
- **Wrong error text.** Planner `std::errc` codes were reinterpreted as Win32
  codes (`errc::file_exists` read as "cannot move to a different disk drive").
  `planning_error_hresult` maps them; planning, append and queue-load failures
  now show the real reason instead of a bare "Failed".

### Performance
- **Same-volume Move copied every byte.** Move now tries an in-place rename
  (`MoveFileExW` without `MOVEFILE_COPY_ALLOWED`) behind the same destination
  path guard; any failure falls back to copy + source removal.

### UI
- **Clipped decision dialogs.** Size was applied to the outer frame with the
  owner's DPI and a fixed width. Dialogs now size the client area from their own
  XAML scale, widen for localized buttons, cap to the work area and scroll.
- **Clipped About window.** Same outer-frame defect with a fixed 388×288 size;
  it now sizes the client area and grows to the measured content.
- **Clipped in-window notice.** The InfoBar was measured before layout; the
  window now re-fits whenever the open notice changes height.
- **Uninformative outcomes.** The retry dialog lists up to three failed files
  with their reason; the completion notice lists only non-zero outcomes plus the
  first failed file and its reason.

### Security
- **Queue files were trusted.** A `.vcq` (user-selectable input) could point
  outputs anywhere or, for Move, sources at arbitrary files. Loading now
  requires absolute paths, every output inside the destination root and every
  input inside a declared source root.

## Reviewed, no change

- Queue view: already incremental, capped at 256 rows, preserves selection,
  focus and scroll.
- Routing (`App::DeliverConvertedJob`), recovery adoption, IPC bounds and
  timeouts: consistent with their contracts.
- Legacy `LiveCopyPlan` APIs (`complete_active`, `skip_active`, …) are unused by
  production code but pinned by tests; removal is cosmetic and was not done.

## Open findings (need design or measurement)

| Severity | Finding | Recommendation |
|---|---|---|
| High | 250,000-entry cap in planner and archive. | Stream planning into the live plan instead of materialising the whole tree. |
| Medium | Single worker for every topology (`suggested_queue_depth = 1`, pinned by tests). | Use `tools/Run-VelocityCopy-BenchmarkMatrix.ps1`; enable 2–4 workers only for proven-disjoint SSD/NVMe with many small files. |
| Medium | A destination conflict cancels all workers and discards partial files. | Park conflicts like other item failures and prompt at the end or on demand. |
| Medium | No "keep both / rename" conflict choice. | Add a third action generating `name (2).ext`. |
| Medium | Whole tree is planned before the first byte moves. | Same as the streaming item above. |
| Low | Same-volume Move renames file by file. | Rename a whole directory when its destination does not exist. |
| Low | Planner failures are not saved in `.vcq`/recovery archives. | Persist them if post-recovery reporting matters. |
