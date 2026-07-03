# `tools/tasks/` — task scripts

Helper shell scripts that drive the project. Run them from the **repository
root** unless noted otherwise.

| Script | What it does |
| ------ | ------------ |
| `build.sh`             | Configure + build a CMake preset (`build.sh all <preset>`). Forwards into `cmake --preset` / `cmake --build --preset`. |
| `merge_check.sh`       | Pre-merge verification gate: verifies `.clang-format` compliance, builds the develop + release Linux presets, runs `ctest --preset Test`, parses the log for warnings / errors / test failures, prints a coloured pass/fail report and exits with the merge verdict. |
| `format.sh`            | Developer helper: apply / verify `.clang-format` on every `*.cpp` / `*.hpp` under `src/`. Supports `--check`, `--diff`, and explicit-file modes. |
| `release.sh`           | Create a GitHub release, optionally build the `Release_debian` / `Release_mingw` presets, and upload artifacts declared by `.config/release.config.sh`. |
| `copy_mingw_dlls.sh`   | (MSYS2 / UCRT64 only) copy the Qt-free runtime DLLs next to the bundled `Notepp.exe`. |
| `dependences.sh`       | Generate a graphviz dependency diagram from the CMake target graph. |
| `full_clean.sh`        | Wipe `build/` and any generated `mocks/` directories. |
| `cp_code.sh`           | Quick helper that copies sources to the clipboard (or another target). |
| `print_tree.sh`        | Pretty-print the project tree. |
| `loading.sh`           | Sourceable helpers (colour, banner, animated throbber, status icons). Sourced by `merge_check.sh`. |

## merge_check.sh — pre-merge verification

`merge_check.sh` is the gate that every push / merge to `main` must pass.

By default it builds, in order:

* **`develop_gui`** — the Linux development preset (Debug, ccache, unity
  build).
* **`Release_debian`** — the Linux release preset (Release, optimised,
  links a `.deb`).
* **`Test`** — the test preset (RelWithDebInfo, `ENABLE_TEST=ON`).
  This is followed by `ctest --preset Test`.

After every phase the log is filtered for *real* warnings, *real* errors,
and ctest failure summaries. The exit code mirrors the verdict:

* `0` — clean. Safe to push / merge.
* `1` — at least one build failed, at least one test failed, the log
  contains errors, the log contains warnings, **or any C++ source drifts
  from `.clang-format`**.

> The gate is **stricter than the merge_check base** in the upstream
> project: it follows `docs/verification.md`, which states **no merge to
> `main` is allowed if warnings appear or if `.clang-format` is not
> respected**. To temporarily soften the rule (e.g. for an in-progress
> branch), pass `--no-fail-on-warnings` or `--no-format-check`.

### Quick start

```bash
./tools/tasks/merge_check.sh                 # full verification
./tools/tasks/merge_check.sh --clean         # wipe build/ first
./tools/tasks/merge_check.sh --skip-tests    # build only, skip ctest
./tools/tasks/merge_check.sh --no-tests-build        # skip Test preset
./tools/tasks/merge_check.sh --jobs N        # set parallelism
./tools/tasks/merge_check.sh --presets develop_gui   # one preset only
./tools/tasks/merge_check.sh --no-fail-on-warnings   # warnings allowed
./tools/tasks/merge_check.sh --no-format-check       # skip .clang-format check
./tools/tasks/merge_check.sh --help
```

The default output is a cool throbber animation while each preset builds,
plus a final framed report with sections for warnings, errors, and test
failures. Disable colour with `NO_COLOR=1`.

### Where to find the logs

All phase output is appended to:

```
build/logs/merge_check/merge_check_<YYYYMMDD_HHMMSS>.log
```

A one-line status mirror is kept at `build/logs/merge_check/last_result.txt`
so external notifiers (WSL toast, desktop notifications, CI bots) can read
it without re-parsing the full log.

### Useful environment overrides

```bash
MAX_LINES=200 ./tools/tasks/merge_check.sh   # show up to 200 lines per section (0 = all)
PROJECT_ROOT=/path/to/notepp ./tools/tasks/merge_check.sh
CMAKE_BUILD_PARALLEL_LEVEL=8 ./tools/tasks/merge_check.sh
CLANG_FORMAT_BIN=clang-format-18 ./tools/tasks/merge_check.sh
```

### Fixing formatting drift

The format phase is read-only — it never edits your files. To apply
`.clang-format` after a failed check:

```bash
./tools/tasks/format.sh                 # rewrite every *.cpp/*.hpp under src/
./tools/tasks/format.sh --check         # exit non-zero if anything would change
./tools/tasks/format.sh --diff          # like --check, but print the diffs
./tools/tasks/format.sh src/core/foo/src/foo.cpp   # format one file
```

The helper reads `.clang-format` from the repo root and never touches
`build/`, `dist/`, or `externals/`.

### Suggested CI usage

```yaml
- name: Verify (pre-merge)
  run: ./tools/tasks/merge_check.sh --clean
  # Exits 1 on any warning, error, or test failure.
```

## loading.sh — visual helpers

`loading.sh` is **not meant to be executed directly**; it is sourced by
other scripts:

```bash
source "$(dirname "$0")/loading.sh"

print_banner "Pipeline" "Running on Linux"
start_throbber "Demolishing"
sleep 2
stop_throbber
print_status "Demolish" "ok" "2s"
```

Public API:

| Function | Behaviour |
| -------- | --------- |
| `print_banner "Title" ["Subtitle"]` | Draw a framed title in a single bar. |
| `print_section "Phase"` | Bold phase heading (`── Phase ──`). |
| `print_status "Label" "ok\|fail\|warn\|skip" ["Detail"]` | Coloured ✔ / ✖ / ⚠ / ⏭ row. |
| `start_throbber "Label"` | Animate a Braille-spinner + elapsed timer in the background. |
| `stop_throbber` | Stop the throbber and clear its line. |
| `run_with_throbber "Label" cmd arg1 …` | Spawn `cmd …` in the background, capture to a log file (set `MERGE_CHECK_LOG_FILE` or `LOG_FILE`), and wrap with the throbber. Returns the command's exit code. |

Colours are auto-enabled on a TTY; force off with `NO_COLOR=1`, force on
with `CLICOLOR_FORCE=1`.
