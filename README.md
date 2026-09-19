<div align="center">

<img src="Logo/VelocityCopy.png" alt="VelocityCopy" width="170" />

# VelocityCopy

### A faster, smarter, and cleaner way to copy and move files on Windows 11.

**Simple when you need it. Powerful when you want more.**

</div>

## Status

VelocityCopy is a **pre-release** Windows 11 app. The copy engine, compact WinUI shell, Explorer commands and classic installers exist and are built on every `main` commit. It is not a 1.0 product yet.

Current automated baseline (`main`):

- x64 Release tests
- ARM64 core compile
- ASan RelWithDebInfo compile
- self-contained unpackaged WinUI for x64 and ARM64
- `VelocityCopy-Setup-x64.exe` and `VelocityCopy-Setup-ARM64.exe`

Download the latest installers from [Actions → Windows Package](https://github.com/ReinierTutoriales/VelocityCopy/actions/workflows/package.yml). Use the setup that matches the PC: x64 setup refuses ARM64 Windows, and the ARM64 setup refuses x64 Windows.

Open product gaps before calling it production:

- [#1](https://github.com/ReinierTutoriales/VelocityCopy/issues/1) shutdown recovery has no Resume/Discard UI
- [#2](https://github.com/ReinierTutoriales/VelocityCopy/issues/2) no transparent Explorer paste without a global keyboard hook

## What it does today

- Drag and drop files into a compact WinUI 3 window
- One overall progress view with an expandable queue
- Pause, resume, skip, stop, cancel
- Save and load copy queues
- Explorer `IExplorerCommand` verbs (copy, copy-to, paste, open)
- Silent startup via an installer-owned HKCU Run entry (`--startup`)
- Tray residency with EcoQoS while idle and hidden

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

Keep work on short-lived PR branches. Merge to `main` and delete the branch. Do not keep `tmp-*` or merged `fix/*` branches.

Before changing CI or Package, read [docs/RELEASE_GATES.md](docs/RELEASE_GATES.md). Those workflows are string-checked by architecture tests.

## Build locally

```bat
cmake -S . -B build/x64 -A x64
cmake --build build/x64 --config Release --parallel
ctest --test-dir build/x64 -C Release --output-on-failure
```

Installers are produced by GitHub Actions, not by the local CMake tree.

## License

VelocityCopy is available under the [MIT License](LICENSE).
