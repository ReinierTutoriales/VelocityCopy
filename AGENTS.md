# VelocityCopy — Agent Instructions

## Project
Native Windows 11 file copy/move utility (WinUI 3 + C++23). Focus: performance, reliability, clean native UX, and production-ready quality.

## Stack
- Language: C++23 (MSVC)
- UI: unpackaged WinUI 3
- Build: CMake 3.28+
- Platforms: Windows 11 x64 and ARM64
- Key areas: `src/core` (engine), `src/ui` (WinUI shell), `src/shell` (Explorer COM)

## Commands
```bat
cmake -S . -B build/x64 -A x64
cmake --build build/x64 --config Release --parallel
ctest --test-dir build/x64 -C Release --output-on-failure
```
Installers are produced only by GitHub Actions (Windows Package workflow). Do not invent local packaging steps.

## Priorities (always)
- Prefer native Windows / WinUI solutions and stable, documented APIs.
- Keep architecture simple, maintainable, and measurable against real product behavior.
- One active implementation line only. Do not invent semantics the engine does not support.
- Do not introduce arbitrary heuristics when a system metric or existing contract exists.
- Do not relax tests to accommodate defective code.
- Do not declare something stable without CI/Package validation and, when relevant, physical Windows testing.
- Prefer latest *stable* versions. Do not update dependencies or APIs blindly if there is regression risk; evaluate compatibility, real benefit, and migration cost. In a release candidate, stability beats novelty without proven benefit.

## Working style
1. Brief analysis of the problem and risks.
2. Propose the optimal solution (compare alternatives if they exist and pick the most production-solid one).
3. Implement cleanly.
4. Verify against documentation, existing code, and tests.
5. Close by stating what was measured vs. what still requires real Windows validation.

## Rules
- Match existing style and patterns unless the task explicitly redesigns something.
- Changes must pass the existing test suite and architecture contracts.
- Keep changes focused. Do not touch unrelated files.
- Before modifying CI or Package workflows, read `docs/RELEASE_GATES.md`.
- `main` is the only long-lived branch. Do not create temporary long-lived branches.
- Prefer the simplest correct change that solves the requested task.

## Skills to prefer
When the task matches, use the relevant skill from `.agents/skills/` (e.g. precise-coding, code-quality, native-ui, testing-discipline).
Especially use `precise-coding` whenever writing or modifying code to reduce hallucinations.
