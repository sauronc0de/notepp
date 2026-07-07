#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:-$(pwd)}"
APP_EXE="${2:-dist/Notepp/Notepp.exe}"

ROOT_DIR="$(cygpath -u "$ROOT_DIR")"
APP_EXE="$(cygpath -u "$APP_EXE")"

cd "$ROOT_DIR"

if [[ "$APP_EXE" != /* ]]; then
  APP_EXE="$ROOT_DIR/$APP_EXE"
fi

APP_DIR="$(dirname "$APP_EXE")"
MINGW_BIN="/ucrt64/bin"

echo "Root dir: $ROOT_DIR"
echo "App exe: $APP_EXE"
echo "App dir: $APP_DIR"

if [ ! -f "$APP_EXE" ]; then
  echo "ERROR: exe not found: $APP_EXE"
  exit 1
fi

copy_if_exists() {
  local dll="$1"
  local src="$MINGW_BIN/$dll"

  if [ ! -f "$src" ]; then
    echo "WARNING: missing in MSYS2: $src"
    return
  fi

  if [ -f "$APP_DIR/$dll" ]; then
    echo "Already exists: $dll"
    return
  fi

  echo "Copying $dll"
  cp "$src" "$APP_DIR/"
}

is_windows_system_dll() {
  local dll="${1,,}"

  case "$dll" in
    api-ms-win-*.dll | \
    advapi32.dll | \
    bcrypt.dll | \
    cfgmgr32.dll | \
    combase.dll | \
    crypt32.dll | \
    d3d*.dll | \
    dwmapi.dll | \
    dxgi.dll | \
    gdi32.dll | \
    gdi32full.dll | \
    imm32.dll | \
    kernel32.dll | \
    kernelbase.dll | \
    msvcp_win.dll | \
    msvcrt.dll | \
    ntdll.dll | \
    ole32.dll | \
    oleaut32.dll | \
    opengl32.dll | \
    powrprof.dll | \
    rpcrt4.dll | \
    sechost.dll | \
    setupapi.dll | \
    shell32.dll | \
    shlwapi.dll | \
    user32.dll | \
    ucrtbase.dll | \
    version.dll | \
    win32u.dll | \
    winmm.dll | \
    ws2_32.dll)
      return 0
      ;;
  esac

  return 1
}

copy_mingw_dependency_closure() {
  local copied=1

  while ((copied)); do
    copied=0

    while IFS= read -r binary; do
      while IFS= read -r dll; do
        if is_windows_system_dll "$dll"; then
          continue
        fi

        if [ -f "$APP_DIR/$dll" ]; then
          continue
        fi

        if [ -f "$MINGW_BIN/$dll" ]; then
          copy_if_exists "$dll"
          copied=1
        fi
      done < <(objdump -p "$binary" 2>/dev/null | awk '/DLL Name:/ { print $3 }')
    done < <(find "$APP_DIR" -maxdepth 1 -type f \( -iname '*.exe' -o -iname '*.dll' \))
  done
}

echo "Copying known MinGW runtime + SDL/image dependencies..."

DLLS=(
  "libgcc_s_seh-1.dll"
  "libstdc++-6.dll"
  "libwinpthread-1.dll"
  "glew32.dll"
  "SDL2.dll"
  "SDL2_image.dll"
  "libjpeg-8.dll"
  "libpng16-16.dll"
  "libtiff-6.dll"
  "libwebp-7.dll"
  "libwebpdemux-2.dll"
  "libsharpyuv-0.dll"
  "libjxl.dll"
  "libavif-16.dll"
  "libaom.dll"
  "libdav1d-7.dll"
  "librav1e.dll"
  "libSvtAv1Enc-4.dll"
  "libyuv.dll"
  "libbrotlidec.dll"
  "libbrotlicommon.dll"
  "libbrotlienc.dll"
)

for dll in "${DLLS[@]}"; do
  copy_if_exists "$dll"
done

echo "Scanning bundled executables and DLLs for transitive MinGW dependencies..."
copy_mingw_dependency_closure

echo "Done."
echo "DLLs in $APP_DIR:"
ls "$APP_DIR"/*.dll 2>/dev/null || true
