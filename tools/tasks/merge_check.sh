#!/usr/bin/env bash
# ==============================================================================
# merge_check.sh — Verify the project state before pushing / merging to main.
#
# Builds the Linux "develop" preset (develop_gui), the Linux release preset
# (Release_debian), builds and runs the Test preset, then aggregates the
# warnings / errors / test failures emitted by cmake, ninja, and ctest.
#
# The merge gate follows docs/verification.md:
#   * build exits cleanly
#   * all tests pass
#   * no warnings, errors, or test failures appear in the logs
#   * every tracked C++ source matches .clang-format
#
# Outputs:
#   * Cool throbber animations while each preset is built and tested
#   * Single consolidated log: build/logs/merge_check/merge_check_<TS>.log
#   * Final report with sections (Warnings / Errors / Tests) and a verdict
#
# Usage:
#   merge_check.sh                          # standard run
#   merge_check.sh --clean                  # wipe build/ first
#   merge_check.sh --skip-tests             # build only, skip ctest
#   merge_check.sh --no-tests-build         # skip Test preset build & ctest
#   merge_check.sh --presets develop_gui    # build only a subset
#   merge_check.sh --jobs N                 # override parallel build jobs
#   merge_check.sh --no-fail-on-warnings    # warn-only (errors still fail)
#   merge_check.sh --no-format-check        # skip .clang-format verification
#   merge_check.sh --help
#
# Environment overrides:
#   MAX_LINES                  # max lines per report section (default 40, 0=all)
#   CMAKE_BUILD_PARALLEL_LEVEL # forwarded to build.sh
#   PROJECT_ROOT               # absolute path to repo root
#   CLANG_FORMAT_BIN           # clang-format binary (default: clang-format)
# ==============================================================================
set -Eeuo pipefail

# Release verification must be reproducible on minimal containers where the
# host locale may be configured but not generated. Tool output is parsed below,
# so use the universally available C locale and avoid Perl locale warnings.
export LANG=C
export LC_ALL=C

SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION="0.1.0"

# Default presets: develop + release on Linux (per docs/verification.md).
# The Test preset enables unit tests and is handled separately.
DEFAULT_BUILD_PRESETS=( "develop_gui" "Release_debian" )
DEFAULT_TEST_PRESET="Test"

# ------------------------------------------------------------------------------
# Logging helpers + animation library
# ------------------------------------------------------------------------------
# shellcheck source=loading.sh
source "${SCRIPT_DIR}/loading.sh"

# ------------------------------------------------------------------------------
# Configuration
# ------------------------------------------------------------------------------
PROJECT_ROOT="${PROJECT_ROOT:-}"
if [ -z "${PROJECT_ROOT}" ]; then
  PROJECT_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || true)"
fi
if [ -z "${PROJECT_ROOT}" ] || [ ! -d "${PROJECT_ROOT}" ]; then
  PROJECT_ROOT="$(pwd)"
fi

CLANG_FORMAT_BIN="${CLANG_FORMAT_BIN:-clang-format}"
FORMAT_CHECK_EXTENSIONS=( cpp hpp )

LOG_DIR="${PROJECT_ROOT}/build/logs/merge_check"
LOG_FILE="${LOG_DIR}/merge_check_$(date +%Y%m%d_%H%M%S).log"

MAX_LINES="${MAX_LINES:-40}"

# Patterns that look like "warning" lines but aren't real build warnings.
# Extend this list as needed; matched as fixed strings (case-insensitive).
FALSE_WARNING_PATTERNS=(
  "The current warning config is"
  "CMake Warning"
  "This warning is for project developers"
  "ruby: warning"
  "has modification time"
  "warning:  Clock skew detected.  Your build may be incomplete."
  "warning: overriding recipe for target"
  "warning: ignoring old recipe for target"
)

# Resolve absolute path of a preset's build directory.
preset_build_dir() {
  printf '%s\n' "${PROJECT_ROOT}/build/${1}"
}

# ------------------------------------------------------------------------------
# CLI parsing
# ------------------------------------------------------------------------------
PRESETS=()
TEST_PRESET="${TEST_PRESET:-${DEFAULT_TEST_PRESET}}"
RUN_TESTS=1
BUILD_TEST=1
DO_CLEAN=0
CUSTOM_JOBS=""
FAIL_ON_WARNINGS=1
RUN_FORMAT_CHECK=1

usage() {
  sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --help|-h)        usage ;;
    --clean)          DO_CLEAN=1 ;;
    --skip-tests)     RUN_TESTS=0 ;;
    --no-tests-build) RUN_TESTS=0; BUILD_TEST=0 ;;
    --no-fail-on-warnings) FAIL_ON_WARNINGS=0 ;;
    --no-format-check) RUN_FORMAT_CHECK=0 ;;
    --presets)        shift; IFS=',' read -ra PRESETS <<< "${1:-}" ;;
    --presets=*)      IFS=',' read -ra PRESETS <<< "${1#*=}" ;;
    --test-preset)    shift; TEST_PRESET="${1:-}" ;;
    --test-preset=*)  TEST_PRESET="${1#*=}" ;;
    --jobs)           shift; CUSTOM_JOBS="${1:-}" ;;
    --jobs=*)         CUSTOM_JOBS="${1#*=}" ;;
    --)
      shift
      while [ "$#" -gt 0 ]; do PRESETS+=( "$1" ); shift; done
      ;;
    -*)
      printf 'Unknown option: %s\n' "$1" >&2
      printf 'Run "%s --help" for usage.\n' "${SCRIPT_NAME}" >&2
      exit 2
      ;;
    *)
      # Positional args extend the preset list.
      PRESETS+=( "$1" )
      ;;
  esac
  shift
done

# Forward job count to build.sh via the env it already honours.
if [ -n "${CUSTOM_JOBS}" ]; then
  export CMAKE_BUILD_PARALLEL_LEVEL="${CUSTOM_JOBS}"
fi

# ------------------------------------------------------------------------------
# Pre-flight checks
# ------------------------------------------------------------------------------
errexit() { printf '%sERROR:%s %s\n' "${C_RED}" "${C_RESET}" "$*" >&2; exit 1; }

[ -d "${PROJECT_ROOT}" ] || errexit "Project root not found: ${PROJECT_ROOT}"
[ -f "${PROJECT_ROOT}/CMakePresets.json" ] || errexit "CMakePresets.json not found in ${PROJECT_ROOT}"

cd "${PROJECT_ROOT}"

if [ ! -x "${SCRIPT_DIR}/build.sh" ]; then
  errexit "build.sh not found or not executable at ${SCRIPT_DIR}/build.sh"
fi

# Confirm every requested preset exists in CMakePresets.json.
verify_preset_exists() {
  local preset="$1"
  local label
  label="$(grep -E "\"name\":[[:space:]]*\"${preset}\"" "${PROJECT_ROOT}/CMakePresets.json" || true)"
  [ -n "${label}" ] || errexit "Preset '${preset}' not declared in CMakePresets.json"
}
# Apply env overrides when no CLI preset list was given yet.
if [ "${#PRESETS[@]}" -eq 0 ]; then
  if [ -n "${MERGE_PRESETS:-}" ]; then
    IFS=',' read -ra PRESETS <<< "${MERGE_PRESETS}"
  else
    PRESETS=( "${DEFAULT_BUILD_PRESETS[@]}" )
  fi
fi

for p in "${PRESETS[@]}";     do verify_preset_exists "${p}"; done
[ "${BUILD_TEST}" -eq 1 ] && verify_preset_exists "${TEST_PRESET}"

# ------------------------------------------------------------------------------
# Filter helpers
# ------------------------------------------------------------------------------
filter_warnings() {
  local src="${1:-${LOG_FILE}}"
  local args=()
  for pat in "${FALSE_WARNING_PATTERNS[@]}"; do
    args+=( -e "${pat}" )
  done
  if [ "${#args[@]}" -gt 0 ]; then
    grep -iE '\bwarning(s)?\b' "${src}" 2>/dev/null \
      | grep -viF "${args[@]}" 2>/dev/null \
      | grep -viE '(^|[^a-z])no[[:space:]]+warning(s)?\b' 2>/dev/null || true
  else
    grep -iE '\bwarning(s)?\b' "${src}" 2>/dev/null \
      | grep -viE '(^|[^a-z])no[[:space:]]+warning(s)?\b' 2>/dev/null || true
  fi
}

filter_errors() {
  local src="${1:-${LOG_FILE}}"
  grep -Ei \
    '(^|[^a-z0-9_])errors?([^a-z0-9_]|$)|(^|[^a-z0-9_])fatal([^a-z0-9_]|$)|(^|[^a-z0-9_])exceptions?([^a-z0-9_]|$)|undefined reference to|^ninja: build stopped|^FAILED:|collect2:[[:space:]]*error:|make(\[[0-9]+\])?:[[:space:]]*\*\*\*[[:space:]].* Error [0-9]+' \
    "${src}" 2>/dev/null \
    | grep -Eiv \
        '(^|[^0-9])0+[[:space:]]*errors?([^a-z0-9_]|$)|(^|[^a-z0-9_])no[[:space:]]+errors?([^a-z0-9_]|$)' \
    || true
}

# Detect the ctest summary line and emit it if any tests failed.
filter_testfails() {
  local src="${1:-${LOG_FILE}}"
  local summary
  summary="$(grep -E '[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+' "${src}" 2>/dev/null | tail -n1 || true)"
  [ -z "${summary}" ] && return 0
  local failed
  failed="$(printf '%s\n' "${summary}" | sed -E 's/.* ([0-9]+) tests failed out of .*/\1/')"
  if [ -n "${failed}" ] && [ "${failed}" != "0" ]; then
    printf '%s\n' "${summary}"
  fi
}

# Count lines for a filter, fast and defensive.
count_filtered() { "$1" "$LOG_FILE" 2>/dev/null | wc -l | tr -d ' '; }

# Extract the list of files clang-format flagged during the format phase.
filter_format_drift() {
  local src="${1:-${LOG_FILE}}"
  grep -E '^DRIFT: ' "${src}" 2>/dev/null \
    | sed -E 's/^DRIFT: //' \
    | sort -u || true
}

# ------------------------------------------------------------------------------
# Output helpers (pretty report sections)
# ------------------------------------------------------------------------------
print_report_section() {
  local title="$1"; shift
  local data
  data="$("$@" 2>/dev/null || true)"
  printf '\n  %s── %s ──%s\n' "${C_BOLD}${C_BLUE}" "${title}" "${C_RESET}"
  if [ -z "${data}" ]; then
    printf '    %s(none)%s\n' "${C_DIM}" "${C_RESET}"
    return
  fi
  local total head_n extra
  total="$(printf '%s\n' "${data}" | wc -l | tr -d ' ')"
  if [ "${MAX_LINES}" -gt 0 ] && [ "${total}" -gt "${MAX_LINES}" ]; then
    head_n="${MAX_LINES}"
    extra=$(( total - MAX_LINES ))
  else
    head_n="${total}"
    extra=0
  fi
  printf '%s\n' "${data}" | head -n "${head_n}" | sed 's/^/    /'
  if [ "${extra}" -gt 0 ]; then
    printf '    %s… (%d more line%s elided)%s\n' \
      "${C_DIM}" "${extra}" "$( [ "${extra}" -eq 1 ] && printf s || printf s )" "${C_RESET}"
  fi
}

# ------------------------------------------------------------------------------
# Build + test phases
# ------------------------------------------------------------------------------
declare -A PHASE_STATUS

# Run a build phase for a single preset.
# Returns 0 iff the build command returned 0. Warnings are reported via the
# aggregate filter at the end of the run, not here.
run_build_phase() {
  local preset="$1"
  print_section "Building preset: ${preset}"

  local label="Building ${preset}"
  local start_epoch
  start_epoch="$(date +%s)"

  # Append command output to the master log while running.
  {
    printf '\n'
    printf '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
    printf '▶  Phase: %s   (%s)\n' "${label}" "$(date -Iseconds)"
    printf '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
    "${SCRIPT_DIR}/build.sh" all "${preset}"
  } >> "${LOG_FILE}" 2>&1 &
  local cmd_pid=$!

  start_throbber "${label}"

  local rc=0
  wait "${cmd_pid}" || rc=$?

  stop_throbber

  local elapsed=$(( $(date +%s) - start_epoch ))
  if [ "${rc}" -eq 0 ]; then
    print_status "${label}" "ok" "${elapsed}s"
    PHASE_STATUS["${preset}"]="OK"
    return 0
  else
    print_status "${label}" "fail" "exit ${rc}, ${elapsed}s"
    PHASE_STATUS["${preset}"]="FAIL"
    return 1
  fi
}

# Run the ctest phase on top of the Test preset.
run_test_phase() {
  print_section "Running tests (preset: ${TEST_PRESET})"

  local label="Tests (${TEST_PRESET})"
  local start_epoch
  start_epoch="$(date +%s)"

  # Make sure the test binary tree exists before running ctest.
  if [ ! -f "${PROJECT_ROOT}/build/${TEST_PRESET}/CTestTestfile.cmake" ]; then
    printf '%sWARN:%s %s not built yet — running incremental build first\n' \
      "${C_YELLOW}" "${C_RESET}" "${TEST_PRESET}"
    "${SCRIPT_DIR}/build.sh" all "${TEST_PRESET}" >/dev/null 2>&1 || true
  fi

  {
    printf '\n'
    printf '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
    printf '▶  Phase: %s   (%s)\n' "${label}" "$(date -Iseconds)"
    printf '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
    ctest --preset "${TEST_PRESET}" --output-on-failure
  } >> "${LOG_FILE}" 2>&1 &
  local cmd_pid=$!

  start_throbber "${label}"

  local rc=0
  wait "${cmd_pid}" || rc=$?
  stop_throbber

  local elapsed=$(( $(date +%s) - start_epoch ))
  local summary
  summary="$(grep -E '[0-9]+% tests passed, [0-9]+ tests failed out of [0-9]+' "${LOG_FILE}" | tail -n1 || true)"
  if [ "${rc}" -eq 0 ] && [ -n "${summary}" ]; then
    print_status "${label}" "ok" "${summary} (${elapsed}s)"
    PHASE_STATUS["tests"]="OK"
    return 0
  elif [ "${rc}" -eq 0 ]; then
    print_status "${label}" "warn" "no ctest summary captured (${elapsed}s)"
    PHASE_STATUS["tests"]="WARN"
    return 0
  else
    print_status "${label}" "fail" "ctest exit ${rc}, ${elapsed}s"
    PHASE_STATUS["tests"]="FAIL"
    return 1
  fi
}

# ------------------------------------------------------------------------------
# Formatting phase
# ------------------------------------------------------------------------------

# Find every C++ source under src/, minus build/dist/externals.
discover_format_targets() {
  local root="${PROJECT_ROOT}/src"
  local name_args=( \( )
  local first=1
  for ext in "${FORMAT_CHECK_EXTENSIONS[@]}"; do
    if [ "${first}" -eq 1 ]; then
      name_args+=( -name "*.${ext}" )
      first=0
    else
      name_args+=( -o -name "*.${ext}" )
    fi
  done
  name_args+=( \) )

  local prune_args=()
  for d in "${PROJECT_ROOT}/build" "${PROJECT_ROOT}/dist" "${PROJECT_ROOT}/externals"; do
    [ -d "${d}" ] || continue
    prune_args+=( -path "${d}" -prune -o )
  done

  if [ "${#prune_args[@]}" -eq 0 ]; then
    [ -d "${root}" ] || return 0
    find "${root}" -type f "${name_args[@]}" -print
  else
    [ -d "${root}" ] || return 0
    find "${root}" "${prune_args[@]}" -type f "${name_args[@]}" -print
  fi
}

# Verify that every tracked C++ source matches .clang-format. Failures are
# recorded as PHASE_STATUS["format"]="FAIL" and listed in the report section.
# This phase NEVER modifies files — use tools/tasks/format.sh to fix drift.
run_format_phase() {
  print_section "Checking formatting (.clang-format)"

  local label="Format check"
  local start_epoch
  start_epoch="$(date +%s)"

  if ! command -v "${CLANG_FORMAT_BIN}" >/dev/null 2>&1; then
    {
      printf '\n'
      printf '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
      printf '▶  Phase: %s   (%s)\n' "${label}" "$(date -Iseconds)"
      printf '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
      printf 'SKIPPED: %s not found in PATH (set CLANG_FORMAT_BIN to override)\n' \
        "${CLANG_FORMAT_BIN}"
    } >> "${LOG_FILE}" 2>&1
    print_status "${label}" "warn" "clang-format not found"
    PHASE_STATUS["format"]="SKIP"
    return 0
  fi

  if [ ! -f "${PROJECT_ROOT}/.clang-format" ]; then
    {
      printf '\n'
      printf '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
      printf '▶  Phase: %s   (%s)\n' "${label}" "$(date -Iseconds)"
      printf '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
      printf 'SKIPPED: %s not found\n' "${PROJECT_ROOT}/.clang-format"
    } >> "${LOG_FILE}" 2>&1
    print_status "${label}" "warn" ".clang-format not found"
    PHASE_STATUS["format"]="SKIP"
    return 0
  fi

  # Build the list of files once and reuse it.
  local file_list
  file_list="$(mktemp)"
  discover_format_targets > "${file_list}"
  local file_count
  file_count="$(wc -l < "${file_list}" | tr -d ' ')"

  {
    printf '\n'
    printf '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
    printf '▶  Phase: %s   (%s)\n' "${label}" "$(date -Iseconds)"
    printf '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
    printf 'Scanning %d C++ source(s) under %s/src/ (style: %s/.clang-format)\n' \
      "${file_count}" "${PROJECT_ROOT}" "${PROJECT_ROOT}"
  } >> "${LOG_FILE}" 2>&1

  local failing=()
  while IFS= read -r f; do
    [ -n "${f}" ] || continue
    if ! "${CLANG_FORMAT_BIN}" --style=file --fallback-style=none \
           --dry-run --Werror "${f}" >/dev/null 2>&1; then
      failing+=( "${f}" )
      printf 'DRIFT: %s\n' "${f}" >> "${LOG_FILE}"
    fi
  done < "${file_list}"
  rm -f "${file_list}"

  local elapsed=$(( $(date +%s) - start_epoch ))
  if [ "${#failing[@]}" -eq 0 ]; then
    print_status "${label}" "ok" "${file_count} files, ${elapsed}s"
    PHASE_STATUS["format"]="OK"
    return 0
  fi

  printf 'FAILED: %d file(s) need reformatting\n' "${#failing[@]}" >> "${LOG_FILE}"
  print_status "${label}" "fail" "${#failing[@]} of ${file_count} files drift"
  PHASE_STATUS["format"]="FAIL"
  return 1
}

# ------------------------------------------------------------------------------
# Main flow
# ------------------------------------------------------------------------------
print_banner "Notepp merge check" "Develop + Release + Tests — Linux gate"

if [ "${DO_CLEAN}" -eq 1 ]; then
  printf '%s⚠  --clean requested — wiping build/ before verification.%s\n' \
    "${C_YELLOW}" "${C_RESET}"
  printf '   This will discard ccache state and any prior compile_commands.json.\n'
  rm -rf "${PROJECT_ROOT}/build"
fi

# --clean removes the log directory too. Create and open the gate log only
# after the optional wipe so every subsequent phase has a valid sink.
mkdir -p "${LOG_DIR}"
: > "${LOG_FILE}"

{
  printf 'Merge check — log opened at %s\n' "$(date -Iseconds)"
  printf 'Project root: %s\n' "${PROJECT_ROOT}"
  printf 'Presets     : %s\n' "${PRESETS[*]} ${TEST_PRESET}"
  printf 'Format check: %s\n' "$( [ "${RUN_FORMAT_CHECK}" -eq 1 ] && printf on || printf off )"
} >> "${LOG_FILE}"

rc_total=0
overall_start="$(date +%s)"

# ---- Format check (fail fast before burning build time) -------------------
if [ "${RUN_FORMAT_CHECK}" -eq 1 ]; then
  if ! run_format_phase; then
    rc_total=1
  fi
fi

# ---- Build presets ---------------------------------------------------------
for preset in "${PRESETS[@]}"; do
  if ! run_build_phase "${preset}"; then
    rc_total=1
  fi
done

# ---- Build + run Test preset ----------------------------------------------
if [ "${BUILD_TEST}" -eq 1 ]; then
  if ! run_build_phase "${TEST_PRESET}"; then
    rc_total=1
  fi
  if [ "${RUN_TESTS}" -eq 1 ]; then
    if ! run_test_phase; then
      rc_total=1
    fi
  else
    print_status "Tests" "skip" "ctest disabled via --skip-tests"
  fi
fi

# ---- Aggregate findings ----------------------------------------------------
total_elapsed=$(( $(date +%s) - overall_start ))

{
  printf '\n'
  printf 'Merge check — log closed at %s (elapsed %ds)\n' \
    "$(date -Iseconds)" "${total_elapsed}"
} >> "${LOG_FILE}"

warn_count="$(count_filtered filter_warnings)"
err_count="$(count_filtered filter_errors)"
fail_count="$(count_filtered filter_testfails)"
format_status="${PHASE_STATUS[format]:-SKIP}"

# ---- Final report ---------------------------------------------------------
printf '\n'
print_banner "Merge check report" "Total elapsed ${total_elapsed}s — log: ${LOG_FILE}"

print_report_section "Warnings"      filter_warnings
print_report_section "Errors"        filter_errors
print_report_section "Test failures" filter_testfails
# Formatting gets its own section only when the check actually ran.
[ "${RUN_FORMAT_CHECK}" -eq 1 ] && print_report_section "Formatting drift" filter_format_drift

# Plain status line for log readers / CI systems.
printf '\n  %sPhase summary:%s\n' "${C_BOLD}" "${C_RESET}"
[ "${RUN_FORMAT_CHECK}" -eq 1 ] && printf '    * %-20s %s\n' "format-check" "${format_status}"
for preset in "${PRESETS[@]}"; do
  printf '    * %-20s %s\n' "${preset}" "${PHASE_STATUS[${preset}]:-UNKNOWN}"
done
[ "${BUILD_TEST}" -eq 1 ] && printf '    * %-20s %s\n' "${TEST_PRESET}" "${PHASE_STATUS[${TEST_PRESET}]:-UNKNOWN}"
[ "${RUN_TESTS}" -eq 1 ] && [ "${BUILD_TEST}" -eq 1 ] && \
  printf '    * %-20s %s\n' "tests (ctest)" "${PHASE_STATUS[tests]:-UNKNOWN}"

printf '\n  %sCounts:%s  warnings=%s  errors=%s  test-failures=%s\n\n' \
  "${C_BOLD}" "${C_RESET}" "${warn_count}" "${err_count}" "${fail_count}"

# ---- Verdict --------------------------------------------------------------
verdict_ok=1
[ "${err_count}"  -gt 0 ] && verdict_ok=0
[ "${fail_count}" -gt 0 ] && verdict_ok=0
[ "${FAIL_ON_WARNINGS}" -eq 1 ] && [ "${warn_count}" -gt 0 ] && verdict_ok=0
# Format check is part of the gate: failing it must block the merge.
[ "${RUN_FORMAT_CHECK}" -eq 1 ] && [ "${format_status}" = "FAIL" ] && verdict_ok=0
[ "${rc_total}"  -ne 0 ] && verdict_ok=0

if [ "${verdict_ok}" -eq 1 ]; then
  printf '  %s✅ Perfect build: no warnings, errors, or test failures. Safe to merge to main.%s\n' \
    "${C_GREEN}" "${C_RESET}"
else
  printf '  %s❌ Issues detected — please review the report above before merging!%s\n' \
    "${C_RED}" "${C_RESET}"
fi

# Optional one-line status for external notifiers.
printf 'Build %s | format=%s | %s | W:%s E:%s F:%s\n' \
  "$(date -Iseconds)" \
  "${format_status}" \
  "$(for p in "${PRESETS[@]}"; do printf '%s=%s ' "${p}" "${PHASE_STATUS[${p}]:-N/A}"; done)" \
  "${warn_count}" "${err_count}" "${fail_count}" \
  > "${LOG_DIR}/last_result.txt"

exit $(( 1 - verdict_ok ))
