# VelocityCopy Architecture

VelocityCopy is built around one rule: the interface must never become the copy engine.

## Product target

- Windows 11 x64 first.
- Compact, native-feeling user experience.
- One overall progress view.
- Expandable queue and remaining-file details.
- Drag-and-drop as a primary workflow.

## Core rules

1. The copy engine stays independent from the UI.
2. There is one production copy path at a time. Experimental engines must not silently coexist with the active path.
3. UI updates are snapshots of engine state; file I/O never waits for the UI.
4. Performance changes must be benchmarked before replacing the active path.
5. Correctness comes before throughput: paths, metadata, conflicts, cancellation, and recovery must remain predictable.
6. Windows-native facilities are preferred when they provide a reliable baseline; custom I/O is introduced only when measurements justify it.

## Initial layout

```text
src/
  app/                  Application entry point and, later, WinUI 3 UI
  core/                 Copy jobs, scheduling and copy engine
    include/velocitycopy/
```

## Development sequence

1. Establish a correct Windows-native copy baseline.
2. Add directory enumeration and multi-file jobs.
3. Add queue state, pause/cancel/skip behavior and persistence.
4. Add the compact WinUI 3 shell and drag-and-drop experience.
5. Benchmark the baseline across HDD, SATA SSD, NVMe, USB and network paths.
6. Introduce adaptive asynchronous I/O only where it produces a measured improvement.
7. Add Windows Explorer integration after the core behavior is stable.

## Current baseline

The first engine uses the Windows `CopyFile2` path and exposes progress through the core API. It exists as the correctness and performance baseline that future engines must beat without reducing reliability.
