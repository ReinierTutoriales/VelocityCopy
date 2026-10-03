<div align="center">

<img src="Logo/VelocityCopy.png" alt="VelocityCopy" width="170" />

# VelocityCopy

### A faster, smarter, and cleaner way to copy and move files on Windows 11.

**Simple when you need it. Powerful when you want more.**

</div>

## Status

VelocityCopy 1.0 is the stable Windows 11 release line. The copy engine, compact WinUI shell, Explorer integration and classic installers are built and verified by the repository release pipeline.

Current automated baseline (`main`):

- x64 Release tests
- ARM64 core compile
- ASan RelWithDebInfo compile
- self-contained unpackaged WinUI for x64 and ARM64
- `VelocityCopy-Setup-x64.exe` and `VelocityCopy-Setup-ARM64.exe`

Download installers from Releases. Use the setup that matches the PC: x64 setup refuses ARM64 Windows, and the ARM64 setup refuses x64 Windows.

## What it does

- Drag and drop files into a compact WinUI 3 window
- Overall progress view with an expandable queue
- Pause, resume, skip, stop and cancel
- Save and load copy queues
- Native per-session shutdown recovery with Resume/Discard decisions
- Explorer transfer handler with SuperCopier-style default selection
- Silent startup via an installer-owned HKCU Run entry (`--startup`)
- One process-owned tray icon with EcoQoS while eligible and idle

## Repository layout

```text
src/core     copy engine, planner, queue, IPC
src/ui       unpackaged WinUI 3 shell
src/shell    Explorer in-process COM server
tools        NSIS script, install helper, benchmarks
tests        core and architecture contracts
docs         product and release-gate rules
.github      Windows CI and Windows Package
```

`main` is the only intended long-lived branch. Do not keep temporary or merged fix branches.

Before changing CI or Package, read `docs/RELEASE_GATES.md`. Those workflows are protected by architecture contracts.

## Build locally

```bat
cmake -S . -B build/x64 -A x64
cmake --build build/x64 --config Release --parallel
ctest --test-dir build/x64 -C Release --output-on-failure
```

Installers are produced by GitHub Actions, not by the local CMake tree.

## License

VelocityCopy is available under the MIT License.
