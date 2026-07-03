#!/usr/bin/env bash
# ==============================================================================
# format.sh — Apply / verify the project's .clang-format policy on C++ sources.
#
# Default scope:
#   * src/**/*.cpp
#   * src/**/*.hpp
#
# Anything under build/, dist/, or externals/ is excluded — those are never
# formatted by this script (third-party / generated / out-of-source).
#
# Modes:
#   format.sh                # rewrite C++ sources in-place
#   format.sh --check        # exit non-zero if any file would change
#   format.sh --diff         # like --check, but also print the unified diff
#   format.sh <file>...      # format the given files explicitly
#
# clang-format reads .clang-format from the repo root automatically. We pass
# --style=file explicitly so the script behaves the same regardless of CWD.
#
# Exit codes:
#   0  — clean (no changes needed, or --check passed)
#   1  — formatting drift detected (--check / --diff)
#   2  — usage error or missing tool
#   3  — clang-format invocation failed for a non-formatting reason
# ==============================================================================
set -Eeuo pipefail

SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

PROJECT_ROOT="${PROJECT_ROOT:-}"
if [ -z "${PROJECT_ROOT}" ]; then
  PROJECT_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || true)"
fi
if [ -z "${PROJECT_ROOT}" ] || [ ! -d "${PROJECT_ROOT}" ]; then
  PROJECT_ROOT="$(pwd)"
fi

CLANG_FORMAT_BIN="${CLANG_FORMAT_BIN:-clang-format}"
CLANG_FORMAT_STYLE="file"   # use the nearest .clang-format (repo root)

# Directories excluded from the default scan.
EXCLUDE_DIRS=(
  "${PROJECT_ROOT}/build"
  "${PROJECT_ROOT}/dist"
  "${PROJECT_ROOT}/externals"
)

EXTENSIONS=( cpp hpp )

usage() {
  sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
  exit 0
}

# ------------------------------------------------------------------------------
# Discovery
# ------------------------------------------------------------------------------

# Print one matching source path per line, absolute. Uses `find` so the
# scan is independent of git state — survives refactors and partial commits.
discover_sources() {
  local root="$1"
  shift
  local exts=( "$@" )

  # Build the name predicate: \( -name '*.cpp' -o -name '*.hpp' \).
  local name_args=( \( )
  local first=1
  for ext in "${exts[@]}"; do
    if [ "${first}" -eq 1 ]; then
      name_args+=( -name "*.${ext}" )
      first=0
    else
      name_args+=( -o -name "*.${ext}" )
    fi
  done
  name_args+=( \) )

  # Build the prune predicate for excluded directories.
  local prune_args=()
  for d in "${EXCLUDE_DIRS[@]}"; do
    [ -d "${d}" ] || continue
    prune_args+=( -path "${d}" -prune -o )
  done

  if [ "${#prune_args[@]}" -eq 0 ]; then
    find "${root}" -type f "${name_args[@]}" -print
  else
    find "${root}" "${prune_args[@]}" -type f "${name_args[@]}" -print
  fi
}

# ------------------------------------------------------------------------------
# CLI parsing
# ------------------------------------------------------------------------------
MODE="write"   # write | check | diff
EXPLICIT_FILES=()

while [ "$#" -gt 0 ]; do
  case "$1" in
    --help|-h)     usage ;;
    --check)       MODE="check" ;;
    --diff)        MODE="diff"  ;;
    --)            shift; while [ "$#" -gt 0 ]; do EXPLICIT_FILES+=( "$1" ); shift; done ;;
    -*)
      printf 'Unknown option: %s\n' "$1" >&2
      printf 'Run "%s --help" for usage.\n' "${SCRIPT_NAME}" >&2
      exit 2
      ;;
    *)             EXPLICIT_FILES+=( "$1" ) ;;
  esac
  shift
done

# ------------------------------------------------------------------------------
# Pre-flight
# ------------------------------------------------------------------------------
errexit() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

if ! command -v "${CLANG_FORMAT_BIN}" >/dev/null 2>&1; then
  errexit "clang-format not found in PATH (set CLANG_FORMAT_BIN to override)"
fi

if [ ! -f "${PROJECT_ROOT}/.clang-format" ]; then
  errexit ".clang-format not found at ${PROJECT_ROOT}/.clang-format"
fi

# Build the file list.
TMP_LIST="$(mktemp)"
trap 'rm -f "${TMP_LIST}"' EXIT

if [ "${#EXPLICIT_FILES[@]}" -gt 0 ]; then
  for f in "${EXPLICIT_FILES[@]}"; do
    if [ ! -f "${f}" ]; then
      errexit "File not found: ${f}"
    fi
    printf '%s\n' "${f}" >> "${TMP_LIST}"
  done
else
  discover_sources "${PROJECT_ROOT}" "${EXTENSIONS[@]}" > "${TMP_LIST}"
fi

file_count="$(wc -l < "${TMP_LIST}" | tr -d ' ')"
if [ "${file_count}" -eq 0 ]; then
  printf 'No C++ sources found under %s (extensions: %s)\n' \
    "${PROJECT_ROOT}" "${EXTENSIONS[*]}" >&2
  exit 2
fi

# ------------------------------------------------------------------------------
# Run clang-format
# ------------------------------------------------------------------------------
printf 'clang-format %s — %d file(s) under %s\n' \
  "${MODE}" "${file_count}" "${PROJECT_ROOT}"

case "${MODE}" in
  write)
    if ! xargs -d '\n' -a "${TMP_LIST}" "${CLANG_FORMAT_BIN}" \
         --style="${CLANG_FORMAT_STYLE}" --fallback-style=none -i; then
      exit 3
    fi
    printf '✓ Reformatted %d file(s)\n' "${file_count}"
    exit 0
    ;;
  check)
    # Inspect every file individually so we can list all failures, not just
    # the first one (--Werror stops at the first failure).
    failing=()
    while IFS= read -r f; do
      [ -n "${f}" ] || continue
      if ! "${CLANG_FORMAT_BIN}" --style="${CLANG_FORMAT_STYLE}" \
             --fallback-style=none --dry-run --Werror "${f}" >/dev/null 2>&1; then
        failing+=( "${f}" )
      fi
    done < "${TMP_LIST}"

    if [ "${#failing[@]}" -eq 0 ]; then
      printf '✓ All %d file(s) match .clang-format\n' "${file_count}"
      exit 0
    fi

    printf '✗ %d file(s) would be reformatted:\n' "${#failing[@]}"
    printf '  %s\n' "${failing[@]}"
    printf '\nRun "%s" to fix them in-place.\n' "${SCRIPT_NAME}"
    exit 1
    ;;
  diff)
    changed=0
    while IFS= read -r f; do
      [ -n "${f}" ] || continue
      diff_out="$("${CLANG_FORMAT_BIN}" --style="${CLANG_FORMAT_STYLE}" \
                  --fallback-style=none "${f}" | diff -u --label "${f}" --label "${f}" "${f}" - || true)"
      if [ -n "${diff_out}" ]; then
        printf '\n--- %s\n' "${f}"
        printf '%s\n' "${diff_out}"
        changed=$(( changed + 1 ))
      fi
    done < "${TMP_LIST}"

    if [ "${changed}" -eq 0 ]; then
      printf '✓ All %d file(s) match .clang-format\n' "${file_count}"
      exit 0
    fi
    printf '\n%d file(s) would be reformatted.\n' "${changed}"
    exit 1
    ;;
esac