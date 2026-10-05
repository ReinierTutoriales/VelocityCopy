# VelocityCopy engine audit

## Confirmed faults and changes

- Pre-cancelled/pre-stopped directory-only jobs previously created destinations or removed empty Move sources; parked source-removal retries ignored controls. Preflight, directory creation, removal retries and cleanup now observe controls. Paused preflight waits without filesystem mutation.
- Queue snapshots shared `<archive>.tmp`, so overlapping saves could truncate each other's data. GUID staging names isolate writers while retaining flush and atomic replacement. The archive version remains 3.
- The noexcept CopyEngine boundary called allocating path helpers without exception handling. It now returns E_OUTOFMEMORY on allocation failure; path guards close newly opened handles if vector insertion fails.
- Move cleanup followed an initial source junction and accumulated/sorted all directory paths. The iterative traversal pins inspected non-reparse directories and deletes by handle, with O(depth) traversal state and no global sort. It preserves nonempty directories and checks controls between entries.
- Package validation attempted to download a removed v1.0.0 fixture (HTTP 404). It now uses the real published v1.1.0 installer with its published SHA256, retaining both write-locked shell-generation upgrade checks.

## Microsoft documentation used

- [CopyFile2](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-copyfile2): HRESULT completion/error semantics and native copy behavior.
- [CopyFile2 V2 parameters](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-copyfile2_extended_parameters_v2): requested I/O sizes can be reduced when memory is insufficient. Existing bounded strategy sizes and worker limits are retained; no extra file checksum pass or unbounded parallelism was added.
- [CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew): share-delete controls rename/deletion access; OPEN_REPARSE_POINT opens the link itself.
- [SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle): FileDispositionInfo requires DELETE access and deletion occurs when handles close.
- [CoCreateGuid](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-cocreateguid): unique 128-bit identifiers for independent staging files.

## Verification scope

New native regression cases cover cancelled/stopped source-removal retries, directory-only cancel/stop/pause, empty Move cleanup, junction rejection, concurrent complete queue snapshots and injected allocation failures with process handle-count checks. Existing tests, WinUI linking and installer validation run in Windows CI/Package.

Linux source checks cannot establish Windows runtime behavior. Integration requires green Windows CI and Package for the tested head. ARM64 CI validates compilation and packaging; real ARM64 execution, Explorer hot-loaded COM behavior, OneDrive hydration, USB disconnects and low-memory hardware benchmarks still require manual checks. The O(depth) change is an algorithmic memory improvement, not a measured throughput claim. No new public release or tag is created by this audit.
