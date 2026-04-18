# Contract tests — read-only for coding agents

Files in `hhds/contracts/` freeze the public API surface that downstream
projects depend on. They double as short, runnable usage samples for new users.

**Coding agents (Codex, Claude, Cursor, etc.) must not modify any file in this
directory** — including adding, renaming, removing, or rewriting tests — unless
the user explicitly asks for a change to `hhds/contracts/`.

Why this matters:
- A test passing after an agent weakens it is worse than the test failing.
- The API shape captured here is the project's promise to its consumers;
  loosening it silently breaks that promise.

If a change elsewhere in the codebase causes a contract test to fail, do not
"fix" the test. Instead, report the breakage to the user and let them decide
whether the code change or the contract needs to move.

The only edits permitted without explicit user instruction are:
- Mechanical compile-fixes when a public type/method is *renamed* (not removed
  or reshaped) — and only when the rename was the user's stated intent.

Everything else requires an explicit user request naming `hhds/contracts/`.
