# Incremental discovery and Copy (opt in)

`VelocityCopyCLI.exe --stream <destination> <source>...` uses the existing
planner and executor through `execute_streaming_copy`. Discovery produces
batches while the consumer copies admitted batches. Defaults are 512 entries
per batch and 2048 buffered entries, including the batch being executed.
The ordinary planner and GUI retain their existing 250000-entry limit.
Streaming Move is rejected; source removal during enumeration needs a separate
protocol. This change does not enable streaming in the GUI or Explorer yet.

Pending batches are bounded, not total process memory. Destination collision
reservations, path semantics caches and traversal state remain proportional to
discovery. Reservations span all batches, including completed files. Append
inserts only new reservations and rolls them back on allocation failure rather
than copying all previous reservations. Materialized directory history is
released after each completed batch.

`JobProgress::totals_final` is false until discovery has finished and all batches
are admitted. Logical resolution and physical bytes retain their existing
meaning. Pause blocks both producer and consumer; internal cancellation can
wake a paused producer so shutdown can join it. Native synchronous filesystem
calls retain their existing cancellation boundaries.

The optional checkpoint callback runs before copying each admitted batch and
must durably accept the archive before that batch executes. The original Copy
job remains in `queued_jobs` until successful completion, alongside the current
remaining plan. On interruption, replay the original job through streaming and
apply an explicit conflict policy to already copied files. This is conservative
rediscovery, not an exact persisted enumeration cursor. No source is removed.
The existing GUI recovery path is not a streaming recovery implementation.

Tests cover overlap, pending bounds, Unicode/empty folders, Pause/Stop/Cancel,
SkipAll/ReplaceAll/KeepBothAll/Prompt and checkpoint failure/replay. Native CI
also copies and verifies 250001 files and emits benchmark JSON for one warmup
plus seven alternating static/streaming samples of 3000 files. Timings include
planning and execution, excluding fixture creation and content verification.
The benchmark uses warm filesystem caches and does not establish performance
on HDD, SMB, cold caches or other hardware.

References: [Windows enumeration semantics](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw),
[MSVC condition variables](https://learn.microsoft.com/en-us/cpp/standard-library/condition-variable?view=msvc-170).
Existing source leases and destination guards continue to validate each transfer;
an enumeration is not a transactional snapshot of a changing source tree.
