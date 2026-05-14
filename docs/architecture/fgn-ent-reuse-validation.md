# fgn ent Reuse Validation

## 1. Goal

Validate whether `ent` can serve as a lightweight common runtime/library for `fgn`.
The first-pass focus is the smallest reusable surface: `ent_msg`, `ent_log`, and `ent_shm`.

This document defines a validation plan, not an implementation plan.

## 2. Non-goals

- Do not migrate all of `fgn`.
- Do not introduce an `ent` runtime registry.
- Do not change `fgn` core overlay, CBFS, or metadata resolver logic.
- Do not force `fgn` to depend on every `ent` module.
- Do not design or implement runtime-owned `fgn`/`ent` integration here.

## 3. Candidate Reuse Modules

### Primary candidates

- `ent_msg`
- `ent_log`
- `ent_shm`

### Optional candidate

- `utl_tpool`

### Not now

- `ent_db`
- `UTL_Timer*`
- full `ENT_HANDLE` runtime-owner migration

## 4. Minimal Validation Path

### Phase A: Read-only analysis

- Locate `fgn` logging, shared metadata, error code, and thread pool code paths.
- Map each candidate `ent` module to the corresponding `fgn` integration point.
- Identify low-risk replacement seams.
- Output impact notes only; do not change code.

### Phase B: Smallest possible prototype

- Pick only one module for the first prototype.
- Prefer `ent_log` or `ent_shm`.
- Keep the prototype easy to roll back.
- Avoid changing `fgn` main business behavior.

### Phase C: Build validation

- Validate on Windows native build.
- Validate on Linux if the environment is available.
- Confirm that `fgn` smoke and benchmark paths do not regress.

## 5. Acceptance Criteria

- `fgn` can link against at least one `ent` module.
- The integration does not materially increase build complexity.
- Existing `fgn` smoke paths continue to pass.
- `ent` APIs are clear enough for `fgn` users.
- If integration is awkward, the issue should be recorded as an `ent` API design problem.

## 6. Stop Conditions

- Stop if adopting `ent` requires a major `fgn` architecture rewrite.
- Stop if `ent` dependencies significantly complicate `fgn` builds.
- Stop if runtime registry work is required before reuse is viable.
- Stop if even the `ent_log` or `ent_shm` single-module path is awkward.

## 7. Next Recommended Task

- `FGN-ENT-001`: read-only scan of `fgn` log, shm, msg, and tpool reuse points.

## 8. Rollout Notes

- Keep the validation order small and reversible.
- Prefer one module, one prototype, one rollback path.
- Record any API friction directly so future `ent` changes can stay compatible.
