# File metadata policy

CopyEngine and JobExecutor preserve basic file metadata by default. Native
CopyFile2 remains the only data-copy engine; no byte loop or backup engine is
introduced. `CopyOptions::preserve_basic_metadata` and the corresponding
execution option allow a caller to deliberately select CopyFile2-only metadata
behavior instead.

| Property | Policy |
| --- | --- |
| File contents and alternate streams | CopyFile2 handles the streams; the target filesystem must support them |
| Creation and last-write times | Capture from the opened source and apply after successful copy, with destination filesystem timestamp precision |
| Read-only, hidden, system, archive, temporary, not-indexed attributes | Preserve mutable basic attributes on the copied destination object |
| Sparse state | Request CopyFile2 sparse retention for a known supporting target on Windows 11 22H2+; retain the existing fallback on older Windows |
| Compression/encryption/cloud state | Delegate supported native behavior to CopyFile2; do not force compressed/encrypted/reparse flags with a basic-attribute setter or silently opt into decryption |
| Last access and metadata change time | Destination/system managed; do not claim source last-access or change-time fidelity |
| DACL, owner, SACL/audit and backup privileges | Not replicated by the basic policy; destination security remains Windows managed |
| Directory timestamps/ACLs | Not covered by this file policy; existing newly-created-directory attribute handling remains |

The first stream callback captures source basic information and duplicates the
destination handle. After CopyFile2 succeeds, a single `FileBasicInfo` update
applies the requested fields to that handle. There is no path-based reopening
that could update a replacement file. The duplicate is not inherited and is
closed on success, pause, stop, cancellation, retry and exception exits. Only
one is retained per active file, irrespective of its number of streams.

A failure to capture or apply requested metadata is returned as a transfer
failure with the native HRESULT. JobExecutor therefore cannot proceed to Move
source deletion on that failure. A newly created incomplete output can still be
discarded under the existing ownership rules; a pre-existing replacement is
not silently deleted. Retry/incident handling remains the existing mechanism.

CI validates contents, ADS, creation/write times and attributes on NTFS,
including an empty file, alongside sparse contents/allocation. This is not
certification of FAT/ReFS/SMB timestamp precision, full security metadata,
physical disconnects or power-loss recovery. Same-volume rename keeps the
existing file object's metadata through the native Move path.

Official contracts:
[CopyFile2](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-copyfile2),
[stream handles](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-copyfile2_message),
[DuplicateHandle](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-duplicatehandle),
[FileBasicInfo](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_basic_info),
[filesystem timestamp differences](https://learn.microsoft.com/en-us/windows/win32/fileio/setting-and-getting-the-timestamp-of-a-file).
