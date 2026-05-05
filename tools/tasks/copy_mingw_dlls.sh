#!/usr/bin/env bash
set -euo pipefail

APP_EXE="${1:-dist/Notepp/Notepp.exe}"
APP_DIR="$(dirname "$APP_EXE")"
MINGW_BIN="/ucrt64/bin"

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

echo "Copying known MinGW runtime + SDL/image dependencies..."

DLLS=(
  "libgcc_s_seh-1.dll"
  "libstdc++-6.dll"
  "libwinpthread-1.dll"

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

echo "Scanning direct dependencies with ldd..."

ldd "$APP_EXE" \
  | awk '/=>/ { print $3 }' \
  | grep -E '^/(mingw64|ucrt64)/bin/.*\.dll$' \
  | while read -r dll_path; do
      dll_name="$(basename "$dll_path")"
      copy_if_exists "$dll_name"
    done

echo "Done."
echo "DLLs in $APP_DIR:"
ls "$APP_DIR"/*.dll 2>/dev/null || true