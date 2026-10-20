# Consolidated audit implementation

This consolidation brings the completed, tested changes from PRs #84–#91 into
one implementation line. Issue #83 remains open for the unfinished work below.
It does not publish a release.

## Included changes

| PR | Implemented behavior |
| --- | --- |
| #84 | Avoid clearing the 64 KiB path buffer before GetFullPathNameW. The replacement ordinal path-key implementation retains this optimization. |
| #85 | Reuse guarded destination leases, interrupt transient retry waits, and preserve execution-control behavior. |
| #86 | Smooth physical throughput using elapsed time, decay stale throughput during stalls, and warm up ETA. Rename-only Move does not invent physical throughput. |
| #87 | Revalidate pinned source/destination identities before recovery deletes a retained source. |
| #88 | Retain IPC admission acknowledgements until read, with bounded shutdown/cancellation. |
| #89 | Make existing-directory guards enforce no-delete sharing and reject name-surrogate traversal. |
| #90 | Retain source identity across copy/retry/Pause, rename inspected source handles, and authorize initial Move deletion from actual copy provenance. |
| #91 | Compare Unicode names ordinally using per-directory case policies; protect reservations/KeepBoth/archive containment; handle long paths and reject stale case-policy admissions. |

The #84 merge keeps the newer path-key implementation; it does not restore the
old locale-sensitive lowercase function. The ETA merge adds its production
change and regression assertions without duplicating the shared IPC fixes.

## Validation and measured limits

Before consolidation, revision `8fd46ccfb530425cb9d55cadd37af04870b16f28`
passed all four workflows: Windows CI (78/78 x64 tests), Native path safety,
Windows Package x64/ARM64, and Incremental comparison. The four native safety
executables also passed on physical Windows build 26300 after exact-revision
and binary-hash verification. ETA revision
`0602f072146c124a84463728ebbe5970ca56960a` passed Windows CI and Package.
The consolidation must pass its own CI and Package checks before merge;
its PR records those exact results.

The hosted warm-cache benchmark against main `38def1c3` measured planning plus
live-plan construction for 100,000 files at 0.900194 seconds before and 1.811567
seconds after the safety/path changes. Move of 3,000 files measured 0.376875
versus 0.414780 seconds. These changes are not an overall performance win.
Seven samples per group and raw metadata are retained in Actions run
[39489845616](https://github.com/ReinierTutoriales/VelocityCopy/actions/runs/39489845616).

ASan checks compile; they do not execute sanitizer tests. ARM64 builds/package
checks do not establish ARM64 runtime behavior. Fingerprints use file identity,
size and timestamps, not cryptographic content hashes.

## Work remaining separately

- Profile and reduce the measured planning/live-plan overhead while retaining
  ordinal and case-sensitive collision safety.
- Publish, build and measure the phase-2 progress throttle; it is excluded here.
- Keep phase-3 structural changes separate until their implementation and tests
  justify integration.
- Complete physical Explorer copy/cut/paste/drag-drop, Explorer restart,
  OneDrive hydration, disconnect/full-media and ARM64 runtime validation.
- Case-policy checks at admission and CopyFile2 stream start do not atomically
  lock a malicious concurrent case-flag change during destination open. This
  consolidation does not claim complete TOCTOU immunity.
