# VelocityCopy release gates

These gates define the current stabilization/release pipeline. They must agree with `docs/ENGINEERING_RULES.md` and `docs/IMPLEMENTATION_LINE.md`.

## Current stabilization gate

- `main` must pass Windows CI: x64 Release configure, build and `ctest`.
- Normal source commits must not trigger full installer packaging.
- Packaging is intentionally separated from commit CI and is invoked by `workflow_dispatch` or a `v*` tag.
- x64 is the mandatory stabilization path.
- ARM64 is the next architecture after x64 is healthy. It must use the same source/build/package recipe with architecture-specific parameters only and must not destabilize x64.
- Do not claim ARM64 packaging is available until its workflow and artifact have actually been restored and verified.

## Packaging rules

- Distribution is classic self-contained NSIS, not MSIX.
- No x86 and no portable distribution.
- WinUI remains unpackaged and self-contained (`WindowsAppSDKSelfContained=true`, `AppxPackage=false`).
- The x64 payload must include both `VelocityCopy.WinUI.exe` and `VelocityCopy.Shell.dll`.
- If the installer registers a file, DLL, executable or resource, the packaging workflow must assert that the referenced payload actually exists before NSIS runs.
- The x64 package gate must smoke-install and uninstall the classic setup.
- Do not add Chocolatey, AppX/MSIX deployment, certificates, or unrelated package managers to the required path.

## Workflow-change safety

Architecture tests may inspect workflow contracts, but tests must validate durable behavior rather than arbitrary YAML wording.

Required commit-CI behavior:
- x64 configure/build;
- x64 `ctest`;
- no AppX installation;
- no full packaging on every ordinary source commit.

Required package behavior:
- manual/tag trigger;
- self-contained WinUI payload;
- classic `VelocityCopy-Setup-x64.exe`;
- payload verification before installer construction;
- smoke install/uninstall.

When ARM64 is restored, document and test its gate here in the same coherent change.

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
