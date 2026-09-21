# VelocityCopy release gates

These gates define the current stabilization/release pipeline. They must agree with `docs/ENGINEERING_RULES.md` and `docs/IMPLEMENTATION_LINE.md`.

## Current stabilization gate

- `main` must pass Windows CI: x64 Release configure, build and `ctest`.
- Windows Package runs on every main commit, on `v*` tags and on `workflow_dispatch`; commit CI (`ci.yml`) stays free of packaging.
- x64 and ARM64 classic installers are both built with payload verification; smoke install/uninstall runs on x64 only.

## Packaging rules

- Distribution is classic self-contained NSIS, not MSIX.
- No x86 and no portable distribution.
- WinUI remains unpackaged and self-contained (`WindowsAppSDKSelfContained=true`, `AppxPackage=false`).
- Each payload (x64 and ARM64) must include both `VelocityCopy.WinUI.exe` and `VelocityCopy.Shell.dll`.
- If the installer registers a file, DLL, executable or resource, the packaging workflow must assert that the referenced payload actually exists before NSIS runs.
- The x64 package gate must smoke-install and uninstall the classic setup.
- Do not add Chocolatey, AppX/MSIX deployment, certificates, or unrelated package managers to the required path.

## Workflow-change safety

Architecture tests may inspect workflow contracts, but tests must validate durable behavior rather than arbitrary YAML wording.

Required commit-CI behavior:
- x64 configure/build;
- x64 `ctest`;
- ARM64 core compile;
- ASan RelWithDebInfo compile;
- WinUI x64 build (`ui` job);
- no AppX installation;
- no packaging in commit CI (`ci.yml`);

Required package behavior:
- main-push, `v*` tag and manual triggers;
- self-contained WinUI payload;
- classic `VelocityCopy-Setup-x64.exe` and `VelocityCopy-Setup-ARM64.exe`;
- payload verification before installer construction;
- smoke install/uninstall.

## Branch and commit hygiene

- `main` is the only intended long-lived branch.
- Do not create temporary branches unless isolation is actually required.
- Remove obsolete temporary branches through the available repository administration path when possible.
- One logical change should produce one coherent commit.
- Do not push known-broken intermediate states merely to assemble a multi-file feature.

## Failure triage

If CI/package is red, classify the failure before editing:
1. configure/build;
2. production test;
3. architecture-contract test;
4. payload/package;
5. installer smoke;
6. runtime/Windows integration.

Do not rewrite unrelated engine/UI code to fix a packaging or test-contract failure.
