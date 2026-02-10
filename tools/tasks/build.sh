#!/usr/bin/env bash
set -euo pipefail

# -------------------- usage guard (must be before $1/$2 with -u) --------------------
if [ "$#" -lt 2 ]; then
  echo "Usage: $0 [TARGET] [PRESET]"
  echo "Examples:"
  echo "  $0 all Develop"
  echo "  $0 Develop Release"
  exit 1
fi

target="$1"        # e.g., all | XXX | <any cmake target>
preset="$2"        # e.g., Develop | Release | Test

# Optional parallelism override; defaults to all cores
JOBS="${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc)}"

# Build dir + log file (keep these stable for the whole run)
build_dir="$(pwd)/build/${preset}"
mkdir -p "${build_dir}"
log_file="${build_dir}/build.log"


{
echo "🧱 Build project 🧱"
echo "📅 Date: $(date)"
echo "🔧 CMake preset: ${preset}"
echo "🔧 Target: ${target}"
echo "🧰 Parallel jobs: ${JOBS}"

# -------------------- configure --------------------
build_failed=0
echo "👷‍♂️ Configuring (cmake --preset ${preset}) ..."
cmake --preset "${preset}" || build_failed=1

# -------------------- build --------------------
echo "🏗️  Building target '${target}' ..."
build_cmd=(cmake --build --preset "${preset}" -j"${JOBS}")
if [ "${target}" != "all" ]; then
  build_cmd+=(--target "${target}")
fi

if command -v /usr/bin/time >/dev/null 2>&1; then
  /usr/bin/time -f "⏱  elapsed: %E | user: %U | sys: %S | maxrss: %M KB" "${build_cmd[@]}" || build_failed=1
else
  SECONDS=0
  "${build_cmd[@]}" || build_failed=1
  echo "⏱  elapsed: ${SECONDS}s"
fi

if [ "${build_failed}" -ne 0 ]; then
  echo "❌ Build failed (status: ${build_failed})"
else
  echo "✅ Build succeeded"
fi

# -------------------- locate actual build dir if CMake used a different one --------------------
# (We keep log_file unchanged; it's already collecting output in ${build_dir}/build.log)
if [ ! -f "${build_dir}/CMakeCache.txt" ]; then
  guessed="$(cmake --preset "${preset}" --log-level=NOTICE 2>/dev/null | grep -Eo 'Build files have been written to: .*' | sed 's/.*to: //')"
  actual_build_dir="${guessed:-${build_dir}}"
else
  actual_build_dir="${build_dir}"
fi
cache="${actual_build_dir}/CMakeCache.txt"

# -------------------- optional cache helpers (if present) --------------------
if [ -f "${cache}" ]; then
  get_cache_bool () {
    local key="$1"
    awk -F= -v k="$key" '$1 ~ k":" {print $2}' "${cache}" | tr -d '\r'
  }
  # (add any cache-based logic here if you need it)
fi

} > >(tee "${log_file}") 2>&1

# Log both stdout and stderr to build.log (overwrite each run) while keeping console output

# -------------------- focus: open log at first error --------------------
first_error_line="$(grep -inm1 'error' "${log_file}" | cut -d: -f1 || true)"

if [ -n "${first_error_line}" ]; then
  echo "⚠️  First error at ${log_file}:${first_error_line}"
fi

exit "${build_failed}"
