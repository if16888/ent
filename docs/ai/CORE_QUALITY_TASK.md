# Public core quality closure

User request: implement the reviewed P1/P2 backlog. Existing completed features
must be reconciled with evidence rather than implemented again.

Scope: public core only. No distributed services, hard-real-time guarantee,
private dependencies, API signature or structure-layout changes. Preserve
existing tests and user files. Work on `codex/ent-core-quality`; do not merge.
Risk R3; authorized implementation, validation, branch commits and PR follow
the user's standing instructions. Each phase is a separate rollback unit.

| Phase | Allowed changes | Acceptance |
| --- | --- | --- |
| P1 resource bounds | DB validators/materialization, shared-map validation, headers and focused tests | Boundary/over-limit rejection before backend side effects; failure cleanup; full CTest |
| P1 SDK/contracts | SDK checks, lifecycle documentation, backlog state | Actual relocated archive shared/static consumers; public scope check |
| P2 engineering gates | CMake opt-in diagnostics, CI, quality scripts and their tests | Warning baseline detects new warnings; static analysis reports; coverage artifacts; ABI comparison; bounded parser fuzz campaign |

Before implementation, inspect public headers → implementations → tests →
CMake/CI. DB parameter dispatch currently accepts unbounded arrays and payloads;
result materialization already bounds total cells/bytes but lacks a per-field
budget. Shared mapping already rejects non-regular files and protects new files
with mode 0600; native-size validation needs to precede truncation. Release
already checks archive consumers; its metadata check must reject missing files.
Quality gates must not treat unavailable tools or runners as success.

Validation uses SQLite-only and no-DB builds, full CTest, sanitizer-focused
resource tests, SDK archive consumers, and local quality commands. Windows,
ARM and live MySQL/PostgreSQL acceptance remain CI/platform checks; record
executed and unexecuted outcomes separately. Revert each phase commit to roll
back. No release publication or merge is authorized by this task.
