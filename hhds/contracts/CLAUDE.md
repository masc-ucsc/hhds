See [AGENTS.md](AGENTS.md) in this directory.

Short version: do not modify any file under `hhds/contracts/` unless the user
explicitly asks for a change scoped to this directory. If a contract test
starts failing because of code changes elsewhere, report the breakage and let
the user decide — do not silently relax the test.
