---
name: precise-coding
description: Use when writing or modifying code, implementing features, or fixing bugs. Forces grounded, precise output with minimal hallucinations by requiring verification against the existing codebase before inventing anything.
---

# Precise Coding Skill (Anti-Hallucination)

When this skill is active, follow these rules strictly before writing any code:

1. **Ground yourself first**
   - Read the relevant existing files, functions, types, and tests before proposing or writing anything.
   - Base every decision on what is actually in the repository, not on general knowledge or assumptions.
   - If you cannot find the relevant code or contract, say so explicitly instead of inventing it.

2. **Never invent**
   - Do not invent APIs, function signatures, class names, Windows APIs, WinUI controls, or behaviors that are not already present or clearly documented in the project.
   - Do not invent new semantics for the copy engine, planner, queue, IPC, or UI contracts.
   - Do not assume a header, type, or helper exists — verify it.

3. **Be explicit about uncertainty**
   - If something is unclear or missing, state the assumption and label it as such.
   - Prefer the smallest change that can be verified against existing tests and architecture contracts.
   - When in doubt, prefer matching the existing pattern over a “cleaner” alternative you are not sure exists.

4. **Verification before claiming done**
   - Cross-check the change against nearby code, existing tests, and architecture contracts.
   - Do not claim the code compiles, passes tests, or is correct unless you have reasoned from the actual project files.
   - Explicitly note any part that still needs real Windows validation.

5. **Output style**
   - Write only the precise code needed. Avoid speculative refactors, unused helpers, or extra abstractions.
   - Keep comments minimal and only for non-obvious decisions.
   - Prefer editing existing code over creating new files unless clearly necessary.

Goal: every line of code should be traceable to something that already exists in VelocityCopy or to a clearly stated, minimal assumption.
