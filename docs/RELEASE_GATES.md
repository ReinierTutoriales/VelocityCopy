# VelocityCopy release gates

These rules exist so packaging and CI do not regress the way they did during the x64/ARM64 installer work.

## What must stay green

- Windows CI on `main`: x64 Release configure, build and `ctest`.
- Windows CI on `main`: ARM64 Release configure and build. Do not run ARM64 tests on `windows-latest`; the runner is x64.
- Windows CI on `main`: ASan is a compile-only RelWithDebInfo gate (`-DVELOCITYCOPY_ENABLE_ASAN=ON`). Do not execute the full suite under MSVC ASan on GitHub-hosted runners; those processes hang.
- Windows Package on every `main` push, `v*` tag and `workflow_dispatch`: `VelocityCopy-Setup-x64.exe` and `VelocityCopy-Setup-ARM64.exe`.
- x64 Package smoke-installs and uninstalls the classic setup. ARM64 Package only builds the installer.

## Packaging rules

- Distribution is classic self-contained NSIS, not MSIX.
- Do not add Chocolatey as a required step. NSIS comes from curl/SourceForge zip or winget.
- Do not use `${If} ${IsARM64}`. Detect native ARM64 with `IsWow64Process2` (0xAA64).
- WinUI must stay unpackaged and self-contained (`WindowsAppSDKSelfContained=true`, `AppxPackage=false`).
- Each payload must include `VelocityCopy.WinUI.exe` and `VelocityCopy.Shell.dll` and must not contain `.msix` / `.cer` / `.pfx`.
- After editing `App.xaml.cpp` or other UI sources, search for a literal `\n` in front of a declaration. That swallows functions and breaks both architectures.

## How to change workflows without breaking architecture tests

`tests/system_integration_architecture_test.cpp` inspects workflow text. Keep these tokens:

- CI: `cmake_arch: x64`, `cmake_arch: ARM64`, `ctest --test-dir build/x64`, `VELOCITYCOPY_ENABLE_ASAN=ON`, `RelWithDebInfo`
- Package: `branches: [main]`, `workflow_dispatch:`, `VelocityCopy-Setup-x64.exe`, `VelocityCopy-Setup-ARM64.exe`, `PAYLOAD_ARCH`, `nsis-3.11.zip`
- Package must not contain `choco ` or `Add-AppxPackage`

Change surrounding YAML freely. Do not drop those strings.

## One change at a time

Do not mix engine work, installer work and sanitizer work in the same PR. If Package is red, look at NSIS/download/payload first. If only ASan is red, do not rewrite the copy engine.
