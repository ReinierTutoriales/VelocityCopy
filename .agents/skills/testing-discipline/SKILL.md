---
name: testing-discipline
description: Use when adding features, fixing bugs, or modifying core/UI logic. Enforces proper testing, architecture contracts, and no test relaxation.
---

# Testing Discipline Skill

When this skill is active:

- Run or reason about the relevant tests (`ctest` on the Release configuration).
- Do not relax, skip, or weaken existing tests to make defective code pass.
- Prefer adding focused tests when behavior changes, especially for core engine, planner, queue, or architecture contracts.
- Respect the existing architecture test suite (visual fidelity, UI threading, storage, etc.).
- A change is not complete until it is consistent with the test baseline and CI expectations.
- Explicitly call out any gaps that still need real Windows verification.
