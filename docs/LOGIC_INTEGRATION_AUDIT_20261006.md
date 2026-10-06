# Logic, resource and integration audit — 2026-10-06

Baseline: f4d65d829fb233fadbc7c1c0d0c272757b1e6684. Both main Windows CI and
Windows Package passed before this work (37442672197 / 37442672300).

## Confirmed defects and bounded corrections

- Injected allocation failure 372 in the Linux native live-plan regression
  reproduced a rejected append retaining new directories/roots/partial files.
  Roll back only the appended tail; publish counters/IDs/reservations last.
- acquire_next popped Pending before copying/allocating Active; release_active
  erased Active before deque growth. Remove these internal noexcept barriers and
  order mutations after successful staging. Existing executor catches report
  E_OUTOFMEMORY and recovery still owns each file.
- Single-item edit wrappers allocated a vector before reaching their guarded
  implementation. Catch those boundary allocations. Restore/unpark/retry paths
  likewise now commit counters and ownership after successful staging; rollback
  reservations/maps on rejected restoration. Retry counters saturate at uint32
  maximum rather than wrapping to an invalid archived zero.
- cancel_pending allocated a vector inside noexcept; active stop states also
  allocated inside the noexcept worker loop. A node queue detaches by splice,
  creates stop states during enqueue and retains callbacks outside the lock.
- Legacy JobQueue reordering erased then inserted into an allocating deque.
  Rotate existing entries instead. Its noexcept execution boundary catches
  callback-adapter allocation failure and restores the active-job marker.
- StorageProfiler and noexcept routing helpers allocated without a catch;
  volume extents allocated after opening a raw handle. Scope-own that handle
  and return a conservative unknown profile on failure. Activation mappings
  likewise now survive command-line/path allocation failure without leaks.
- IPC stop only opened a wake client, which cannot wake an existing client's
  partial ReadFile. Sender cancellation could also race its temporary writer
  thread. Use overlapped operations and a stop event; drain cancellation before
  destroying buffers, bound connected frames, reuse the pipe and stream through
  64 KiB buffering. No idle timer, extra service, wire or permission changes.
- The earlier unique queue staging names no longer matched recovery cleanup's
  legacy suffix. Recognize strict session/writer GUIDs in both forms and retain
  active owners. Unrelated .vcq.tmp names are now preserved.

## Review scope preserved

Reviewed CopyFile2 callbacks/latched controls, destination-chain guards,
conflict handling and Move cleanup/recovery, planner cancellation, live queue
ownership, archive serialization/atomic replacement, storage strategy/routing,
Explorer IDataObject snapshots/default-command dispatch, per-session IPC and
activation, startup/tray lifecycle and UI dispatcher handoff.

The copy strategy remains a single buffered CopyFile2 operation per session;
no unmeasured concurrency/buffer tuning or visual redesign is introduced.
Protocol limits/version, Explorer commands, CLSIDs, startup registration,
localization, SDK versions, icon resources and installer recipe are preserved.

## Regression evidence and limits

New live-plan allocation test covers append rollback/retry, failed acquisition
and release ownership, recovery restoration, unpark and edit wrappers. New
Windows integration allocation test sweeps storage/routing/activation failure
points and compares process handle counts; it also checks allocation-free
planning cancellation and queue reorder plus the queue execution boundary.
IPC tests cover partial-frame exit, frame timeout/recovery and a 12 MB frame,
in addition to existing roundtrip/malformed/sender-timeout coverage. Recovery
storage tests cover unique/legacy orphan cleanup and active/unrelated retention.

Local source contracts and pure live-plan tests supplement Windows CI. The exact
PR head must pass x64 native tests, ARM64/ASan compilation, WinUI build/launch
and both package builds, including x64 installed/upgrade/uninstall checks.
Interactive Explorer Ctrl+V/default drop mapping, physical DPI/Narrator,
OneDrive/USB/SMB fault cases and real ARM64 runtime remain device checks.
No real-hardware throughput or total resident RAM benchmark is claimed.

Official overlapped/cancellation references are in docs/IPC.md. File transfer,
Move deletion and conflict semantics were reviewed but are not redesigned.
No release/tag publication is part of this audit.
