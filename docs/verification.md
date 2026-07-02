# Verification Gate

Every task that creates or modifies **code, CMake, build configuration, or
folder structure** is not finished until `tools/tasks/merge_check.sh` returns
exit code `0`.

The script is the single source of truth for this gate:

* Flags, env vars, defaults → `merge_check.sh --help` and
  [`tools/tasks/README.md`](../tools/tasks/README.md).
* Build / CMake policy → [`docs/cmake_guidelines.md`](cmake_guidelines.md).
* C++ policy → [`docs/code_guidelines.md`](code_guidelines.md).

This document only states the **gate criteria** and the **log location**
that the script enforces.

------------------------------------------------------------------------

# 1. When To Run

Before any of:

* pushing a branch / opening a PR
* merging into `main`
* tagging a release (after `release.sh`)
* declaring a CMake or target refactor complete

Standard developer loop:

```bash
./tools/tasks/build.sh all develop_gui          # fast feedback
./tools/tasks/merge_check.sh --clean            # full gate before push
```

`--clean` wipes `build/` so ccache cannot mask regressions. Use it at
least before merging.

------------------------------------------------------------------------

# 2. Gate Criteria

A green run satisfies **all** of the following, on the final tree:

| #   | Criterion                                                                                              |
| --- | ------------------------------------------------------------------------------------------------------ |
| 1   | `develop_gui` builds cleanly (exit `0`).                                                               |
| 2   | `Release_debian` builds cleanly (exit `0`).                                                            |
| 3   | `Test` preset builds cleanly (exit `0`).                                                               |
| 4   | `ctest --preset Test` reports `100% tests passed`.                                                    |
| 5   | The aggregated log contains **zero real warnings**.                                                    |
| 6   | The aggregated log contains **zero errors / fatal / exceptions**.                                      |
| 7   | No `undefined reference to`, `^FAILED:`, `ninja: build stopped`, `collect2: error`, `make: *** Error`. |

Criteria 5 and 6 are the strict rule. `--no-fail-on-warnings` is a developer
escape hatch for in-progress branches; **never** use it in CI or on a
release-ready branch.

------------------------------------------------------------------------

# 3. Verdict & Logs

The script exits `0` (green) or `1` (red). One-line mirror for external
notifiers:

```text
build/logs/merge_check/last_result.txt
```

Full master log (cmake / ninja / ctest output, in order):

```text
build/logs/merge_check/merge_check_<YYYYMMDD_HHMMSS>.log
```

CI must capture both paths as artifacts on failure.

------------------------------------------------------------------------

# 4. CI Usage

```yaml
- name: Verify (pre-merge gate)
  run: ./tools/tasks/merge_check.sh --clean
  # Exits 1 on any warning, error, or test failure.
```

No flags. No `--no-fail-on-warnings`. The script's exit code is the merge
button.

------------------------------------------------------------------------

# 5. Failure Triage

1. Read the verdict line: warnings / errors / test failures.
2. Read the matching report section (printed inline, capped by `MAX_LINES`).
3. Open the master log; each phase is wrapped in a `▶ Phase: ...` banner.
4. Fix the root cause. Do not silence warnings; do not add `--no-fail-on-warnings`
   to a release-ready branch.
5. Re-run until green.

For recurring toolchain noise lines (CMake notices, clock-skew messages,
stale make recipes, Ruby deprecation chatter) the script already filters a
curated allow-list (`FALSE_WARNING_PATTERNS` at the top of `merge_check.sh`).
Extend it only when the upstream cause cannot be fixed.

------------------------------------------------------------------------

# 6. Related Documents

* [`docs/cmake_guidelines.md`](cmake_guidelines.md)
* [`docs/code_guidelines.md`](code_guidelines.md)
* [`docs/architecture.md`](architecture.md)
* [`tools/tasks/README.md`](../tools/tasks/README.md) — index, flags, log paths.
* [`tools/tasks/merge_check.sh`](../tools/tasks/merge_check.sh) — the gate itself.
