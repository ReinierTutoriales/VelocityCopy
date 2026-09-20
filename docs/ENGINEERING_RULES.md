# VelocityCopy Engineering Rules

These rules are the default engineering protocol for VelocityCopy. They exist to keep `main` buildable, reduce regressions, and prevent assumptions from being treated as verified facts.

## 1. Verify before changing

- Inspect the current implementation, tests, build configuration, packaging, and relevant CI run before modifying code.
- Do not assume a failure's cause from the error name alone.
- When behavior depends on Windows, WinUI, Explorer, COM, Windows App SDK, NSIS, or another external contract, verify the relevant documentation when there is uncertainty.
- Distinguish explicitly between what is proven by source inspection, automated tests, CI, and real Windows validation.
- Never report a behavior as working unless the available evidence actually establishes it.

## 2. Diagnose the cause, not the symptom

When a check fails, determine which layer is wrong before editing:

1. production implementation;
2. test implementation;
3. architectural contract;
4. build/package configuration;
5. CI environment;
6. real Windows integration.

A failing test does not automatically mean production code is wrong. A passing test does not prove end-to-end Windows integration works.

Do not modify tests merely to make CI green. If a contract is obsolete, explain why it is obsolete and replace it with a behavior-oriented invariant.

## 3. Keep main coherent

- Do not intentionally push a known-broken intermediate state to `main`.
- Multi-file changes must be prepared as one coherent change whenever those files depend on each other.
- Never integrate a resource or feature partially. Required files must exist before project/build references to them are committed.
- Avoid chains of tiny corrective commits for one logical change.
- Do not rewrite published `main` history as routine cleanup.
- Remove temporary compatibility code only after verifying that no supported path depends on it.

Preferred cycle:

`inspect -> diagnose -> implement complete block -> statically verify -> commit once -> run CI -> inspect result -> continue`

## 4. Tests protect behavior

Architecture tests should enforce durable invariants, not formatting or incidental source spelling.

Prefer checks for:
- required execution routes;
- ownership boundaries;
- forbidden unsafe mechanisms;
- persistence and recovery semantics;
- packaging/install contracts;
- copy/move correctness and queue behavior.

Avoid tests whose only purpose is to require comments, whitespace, exact local variable names, exact expression spelling, decorative glyph literals, or arbitrary implementation details.

Never add comments, dead strings, or unreachable code solely to satisfy a textual test.

## 5. Stabilization order

Unless a specific defect requires otherwise, stabilize in this order:

1. core copy/move correctness and data safety;
2. queue, stop/resume/cancel/conflict behavior;
3. Explorer/clipboard/IPC integration;
4. tray and installed-application behavior;
5. x64 build and automated tests;
6. classic x64 installer and install/uninstall smoke validation;
7. ARM64 using the same recipe with architecture-specific changes only;
8. visual redesign and polish.

Do not mix a broad UI redesign into functional stabilization.

## 6. Windows integration rules

- Do not introduce global keyboard hooks to emulate Explorer integration.
- Explorer integration must use documented Windows shell/clipboard mechanisms.
- Drag/drop onto VelocityCopy only appends StorageItems to the currently active transfer destination/session. It must not invent a destination or open destination/layout prompts.
- Validate Explorer commands, tray restoration, startup behavior, installed executable resources, and installer registration on real Windows before calling those paths complete.

## 7. Build and release discipline

Supported distribution targets are x64 and ARM64 classic Windows installers. Do not introduce x86, portable, or MSIX distribution unless the project requirements explicitly change.

Normal commit CI should remain deterministic and relatively fast. Packaging belongs in the packaging workflow, not every source commit.

x64 is the stabilization gate. ARM64 follows the same source and packaging recipe after x64 is healthy; ARM64-specific failures must not destabilize the x64 path.

A release candidate requires clean build, automated tests green, expected payload present, shell DLL present when registration references it, installer/uninstaller success, installed executable launch, and real-Windows integration checks for features CI cannot prove.

## 8. CI evidence

After a commit intended to fix CI:

- identify the Actions run for that exact commit SHA;
- wait for the relevant job result before declaring it fixed;
- inspect the failing job/test/log if red;
- do not treat missing commit statuses as success;
- do not start unrelated feature work while the stabilization gate remains red.

Record whether a failure is compile, test, packaging, installer, or runtime/integration.

## 9. Resource and icon changes are atomic

Binary/resource integration must be complete before project references are added.

For the application icon specifically, the executable resource, project resource compilation, installer/shortcut behavior, taskbar behavior, and tray extraction must agree on the same application identity. Do not commit an `.rc` reference to an icon that is not already available to the build.

## 10. Evidence language

Use precise status descriptions:

- **Verified in source**: source inspection establishes the implementation.
- **Verified by CI**: the exact commit SHA passed the relevant workflow.
- **Verified by installer smoke test**: automated install/uninstall succeeded.
- **Verified on Windows**: behavior was exercised on an actual Windows installation.
- **Not yet verified**: evidence is incomplete.

Do not replace one level with another. Source inspection is not runtime validation.

## 11. Stop conditions

Stop adding new changes and investigate when `main` is red, a build dependency/resource is missing, the expected installer payload is incomplete, implementation/tests disagree about intended behavior, a Windows API contract is uncertain and affects correctness, or a proposed fix only hides a failure.

## 12. VelocityCopy product invariants

Until the project requirements explicitly change:

- VelocityCopy is a Windows copy/move utility.
- Distribution is classic installer, installed under Program Files.
- No desktop shortcut is created.
- The app is tray-resident and can start silently.
- Supported architectures are x64 and ARM64.
- x64 is stabilized first.
- Drag/drop is an append-to-active-transfer shortcut, not a destination-selection flow.
- UI work must not compromise copy correctness, Explorer integration, or installation reliability.
- The window itself is the copier surface; later visual work must not reintroduce a nested decorative copier/card as the primary surface.

If a future implementation decision conflicts with these rules, resolve the conflict explicitly before changing production code.

## 13. Documentation is part of the change

Every material product, architecture, workflow, packaging, UI-contract, or engineering-process decision must update the relevant repository documentation in the same coherent change.

Before implementing a change, identify the canonical document owning the contract, check other documents for contradictory legacy requirements, and reconcile contradictions instead of guessing.

Documentation authority for active work:
1. `docs/ENGINEERING_RULES.md`
2. `docs/IMPLEMENTATION_LINE.md`
3. `docs/UI_SPEC.md` + `docs/UI_ARCHITECTURE.md`
4. `docs/SYSTEM_INTEGRATION.md` + `docs/EXPLORER_INTEGRATION.md` + `docs/IPC.md`
5. `docs/RELEASE_GATES.md`
6. `docs/PRETEST_AUDIT.md`

### Regression lesson: structural UI deletion must include the linked WinUI surface

Removing a XAML flow is not complete when the controls disappear. Audit generated-accessor callers, translation-unit helper definitions, generated `.g.cpp` integration, localization resources, build targets, tests, and documentation in the same block. A CMake/core build can be green while the self-contained WinUI executable fails at link time, so structural UI cleanup must pass the real WinUI link before it is considered complete.

### Regression lesson: obsolete flow infrastructure must leave the build graph

When a product flow is removed permanently, remove its controller/model/worker sources and dedicated tests from the build graph after confirming no supported path depends on them. Keeping destination/menu machinery compiled but unreachable increases maintenance cost and creates stale contracts that later cleanups accidentally preserve.

### Regression lesson: generated XAML accessor renames

Renaming an `x:Name` changes the generated C++ accessor. Search every WinUI translation unit for the old accessor and compile the self-contained WinUI target before committing.

### Choice-state integrity

A visual selection state must only be committed after the underlying controller accepts the choice. Toggle/check state and action enablement must derive from the same validation result; never show an option selected when the flow rejected it.

### Regression lesson: do not gate a required action behind an unproven flyout

A resolved Explorer transfer must not be blocked by a destination/layout chooser. If an entry point already has an authoritative destination and operation, dispatch directly into the transfer queue. Drag/drop onto the copier likewise never opens destination/layout UI.
