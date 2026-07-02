#!/usr/bin/env bash
# ==============================================================================
# loading.sh — Cool-looking progress + status helpers for shell scripts.
#
# Source this file from another script:
#
#     source "$(dirname "${BASH_SOURCE[0]}")/loading.sh"
#
# Public API:
#     print_banner    "Title" ["Subtitle"]
#     print_section   "Phase name"
#     print_status    "Label" "ok|fail|warn|skip" ["Detail"]
#     start_throbber  "Label"
#     stop_throbber
#     run_with_throbber "Label" cmd arg1 arg2 ...
#
# Behavior:
#   * Colors are auto-enabled only when stdout/stderr look like a color-capable
#     terminal. Disable with NO_COLOR=1, force with CLICOLOR_FORCE=1.
#   * On direct execution (`bash loading.sh`) only the banner is shown — useful
#     for a quick sanity check.
# ==============================================================================

# Guard against re-sourcing
[[ -n "${LOADING_SH_LOADED:-}" ]] && return 0
LOADING_SH_LOADED=1

# ------------------------------------------------------------------------------
# Color detection
# ------------------------------------------------------------------------------
supports_color() {
  # If user disabled colors
  [ "${NO_COLOR:-0}" != "0" ] && [ -n "${NO_COLOR:-}" ] && return 1
  # If user forced colors
  [ "${CLICOLOR_FORCE:-0}" != "0" ] && return 0
  # Need a TTY
  [ -t 1 ] || return 1
  [ -z "${TERM:-}" ] && return 1
  case "${TERM}" in
    ''|dumb) return 1 ;;
  esac
  command -v tput >/dev/null 2>&1 || return 0
  local colors
  colors="$(tput colors 2>/dev/null || printf '0')"
  [ "${colors}" -ge 8 ]
}

disable_colors() {
  C_RESET=""; C_DIM=""; C_BOLD=""; C_RED=""; C_GREEN=""; C_YELLOW=""
  C_BLUE=""; C_MAGENTA=""; C_CYAN=""
}

enable_colors() {
  C_RESET=$'\033[0m'
  C_DIM=$'\033[2m'
  C_BOLD=$'\033[1m'
  C_RED=$'\033[31m'
  C_GREEN=$'\033[32m'
  C_YELLOW=$'\033[33m'
  C_BLUE=$'\033[34m'
  C_MAGENTA=$'\033[35m'
  C_CYAN=$'\033[36m'
}

if supports_color; then
  enable_colors
else
  disable_colors
fi

# ------------------------------------------------------------------------------
# Box drawing helpers
# ------------------------------------------------------------------------------

# Compute the display width of a string (rough — strips ANSI).
# Falls back to byte count if locale is missing.
_display_width() {
  local s="${1//$'\033'/*}"
  # Use wc -m for character count in a UTF-8 locale when available.
  if locale -a 2>/dev/null | grep -qi 'utf-8'; then
    printf '%s' "$s" | wc -m | tr -d ' '
  else
    printf '%s' "$s" | wc -c | tr -d ' '
  fi
}

# Print a centered text inside a horizontal rule.
print_rule() {
  local width=${COLUMNS:-80}
  local ch="${1:-═}"
  local line=""
  while (( ${#line} < width )); do line+="${ch}"; done
  printf '%s\n' "${line}"
}

# Print a cool banner with a framed title.
#   print_banner "Title" ["Subtitle"]
print_banner() {
  local title="${1:-}"
  local subtitle="${2:-}"
  local width=${COLUMNS:-80}
  (( width < 50 )) && width=50
  local inner=$(( width - 2 ))          # space between the two │ walls
  (( inner < 10 )) && inner=10
  local pad=$(( (inner - ${#title}) / 2 ))
  (( pad < 1 )) && pad=1
  local right=$(( inner - pad - ${#title} ))
  (( right < 1 )) && right=1

  # Top / bottom borders
  local border=""
  for ((i = 0; i < inner; i++)); do border+="─"; done

  printf '\n'
  printf '%s┌%s┐%s\n' "${C_CYAN}" "${border}" "${C_RESET}"
  # Body line: cyan │ + bold pad + title + pad + cyan │ + reset.
  printf '%s│%s%*s%s%*s%s│%s\n' \
    "${C_CYAN}" "${C_BOLD}" "$pad" "" "${title}" "$right" "" \
    "${C_RESET}${C_CYAN}" "${C_RESET}"
  printf '%s└%s┘%s\n' "${C_CYAN}" "${border}" "${C_RESET}"

  if [ -n "${subtitle}" ]; then
    printf '%s%s%s\n' "${C_DIM}" "${subtitle}" "${C_RESET}"
  fi
  printf '\n'
}

# Print a phase heading.
#   print_section "Configure"  -> "── Configure ──"
print_section() {
  local name="$1"
  printf '%s── %s ──%s\n' "${C_BOLD}${C_BLUE}" "${name}" "${C_RESET}"
}

# Print a status row.
#   print_status "All tests" "ok"  "42 passed"
#   print_status "Build"     "fail" "1 error"
print_status() {
  local label="$1"
  local state="$2"
  local detail="${3:-}"

  local icon color
  case "${state}" in
    ok)   icon="✔"; color="${C_GREEN}"   ;;
    fail) icon="✖"; color="${C_RED}"     ;;
    warn) icon="⚠";  color="${C_YELLOW}"  ;;
    skip) icon="⏭";  color="${C_DIM}"     ;;
    *)    icon="•"; color="${C_DIM}"     ;;
  esac

  if [ -n "${detail}" ]; then
    printf '  %s%s%s %s  %s%s%s\n' "${color}" "${icon}" "${C_RESET}" "${label}" "${C_DIM}" "${detail}" "${C_RESET}"
  else
    printf '  %s%s%s %s\n' "${color}" "${icon}" "${C_RESET}" "${label}"
  fi
}

# ------------------------------------------------------------------------------
# Background throbber (spinner + elapsed time)
# ------------------------------------------------------------------------------

# Frames (Unicode Braille) — same shape the canonical spinners use.
_THROBBER_FRAMES=( "⠁" "⠃" "⠇" "⠧" "⠷" "⠿" "⠷" "⠯" "⠮" "⠟" "⠻" "⠽" )
_THROBBER_PID=""
_THROBBER_LABEL=""
_THROBBER_START=0
_THROBBER_ACTIVE=0

_show_cursor() { printf '\033[?25h' >/dev/null 2>&1 || true; }
_hide_cursor() { printf '\033[?25l' >/dev/null 2>&1 || true; }

# Ensure the cursor is restored on script exit.
trap '_show_cursor' EXIT INT TERM

# Escape the label for use in a printf format string.
_throbber_render() {
  local frame="$1"
  local elapsed="$2"
  local label="$3"
  printf '\r  %s%s%s %s%-28s%s %s%5ds%s' \
    "${C_CYAN}" "${frame}" "${C_RESET}" \
    "${C_BOLD}" "${label}" "${C_RESET}" \
    "${C_DIM}" "${elapsed}" "${C_RESET}"
}

# Start an animated throbber on stderr. Background-only — must be paired with
# stop_throbber (or run_with_throbber handles both ends).
start_throbber() {
  local label="${1:-working}"
  _THROBBER_LABEL="${label}"
  _THROBBER_START="$(date +%s)"
  _THROBBER_ACTIVE=1

  # Don't animate if stderr isn't a TTY — print a plain waiting line instead.
  if [ ! -t 2 ]; then
    printf '  %s⠿%s %s%s%s ...\n' "${C_CYAN}" "${C_RESET}" "${C_BOLD}" "${label}" "${C_RESET}" >&2
    return 0
  fi

  _hide_cursor
  (
    i=0
    # Trap so the child exits cleanly when killed
    trap 'exit 0' TERM INT
    while :; do
      local now elapsed frame
      now="$(date +%s)"
      elapsed=$(( now - _THROBBER_START ))
      frame="${_THROBBER_FRAMES[i % ${#_THROBBER_FRAMES[@]}]}"
      _throbber_render "${frame}" "${elapsed}" "${_THROBBER_LABEL}"
      i=$(( i + 1 ))
      sleep 0.1
    done
  ) &
  _THROBBER_PID=$!
}

# Stop the throbber and clear its line.
stop_throbber() {
  if (( _THROBBER_ACTIVE == 0 )); then
    return 0
  fi
  _THROBBER_ACTIVE=0

  if [ -n "${_THROBBER_PID}" ] && kill -0 "${_THROBBER_PID}" 2>/dev/null; then
    kill "${_THROBBER_PID}" 2>/dev/null || true
    # Give it up to 0.5s to die, then SIGKILL.
    for _ in 1 2 3 4 5; do
      kill -0 "${_THROBBER_PID}" 2>/dev/null || break
      sleep 0.1
    done
    kill -9 "${_THROBBER_PID}" 2>/dev/null || true
    wait "${_THROBBER_PID}" 2>/dev/null || true
  fi
  _THROBBER_PID=""

  # Clear the spinner line (move to col 1, then erase-to-EOL twice for safety).
  printf '\r\033[2K' >&2
  _show_cursor
}

# ------------------------------------------------------------------------------
# High-level: run a command with a throbber, capture output, return exit code.
# ------------------------------------------------------------------------------
#
#   run_with_throbber "Building foo" cmake --build --preset foo
#
# If MERGE_CHECK_LOG_FILE (or LOG_FILE) is set, stdout+stderr of the command is
# appended to it; otherwise the command inherits the parent's stdio.
#
# The return code is the command's exit code.
run_with_throbber() {
  local label="$1"; shift
  local log_file="${MERGE_CHECK_LOG_FILE:-${LOG_FILE:-}}"

  if [ -n "${log_file}" ]; then
    {
      printf '\n'
      printf '── %s ── (started %s)\n' "${label}" "$(date -Iseconds)"
      "$@"
      local rc=$?
      printf '── %s ── (finished with exit %d at %s)\n' "${label}" "${rc}" "$(date -Iseconds)"
    } >> "${log_file}" 2>&1 &
    local cmd_pid=$!
  else
    "$@" &
    local cmd_pid=$!
  fi

  start_throbber "${label}"

  local rc=0
  wait "${cmd_pid}" || rc=$?

  stop_throbber

  printf '\n' >&2
  return $(( rc ))
}

# ------------------------------------------------------------------------------
# Stand-alone demo (only when this file is executed, not sourced).
# ------------------------------------------------------------------------------
if [ "${BASH_SOURCE[0]}" = "$0" ]; then
  print_banner "loading.sh" "Color + throbber helpers — demo"
  print_section "Demo"
  start_throbber "Demoing throbber"
  sleep 2
  stop_throbber
  print_status "Demo" "ok" "throbber rendered for 2s"
  exit 0
fi
