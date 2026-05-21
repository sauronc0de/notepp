#!/usr/bin/env bash

RELEASE_PROJECT_NAME=${PROJECT_NAME}
RELEASE_PROJECT_ROOT="$WORKSPACE_DIR"
RELEASE_REMOTE="origin"
RELEASE_PRESET="release"
RELEASE_VERSION_SOURCE="CMakeLists.txt"

RELEASE_PACKAGE_BINARY_PATH="${WORKSPACE_DIR}/build/${RELEASE_PRESET}/${RELEASE_PROJECT_NAME}"
RELEASE_PACKAGE_BINARY_ASSET_NAME="${RELEASE_PROJECT_NAME}"

release_config_package_extra_assets() {
  local release_dir="$1"
  local artifact_dir="$2"
  local tag="$3"
  local version="${tag#v}"

  local deb_src="${WORKSPACE_DIR}/dist/${version}/${RELEASE_PROJECT_NAME}.deb"
  local exe_src="${WORKSPACE_DIR}/dist/${version}/${RELEASE_PROJECT_NAME}.exe"

  local deb_dst="${artifact_dir}/${RELEASE_PROJECT_NAME}.deb"
  local exe_dst="${artifact_dir}/${RELEASE_PROJECT_NAME}.exe"

  : "$release_dir"

  mkdir -p "$artifact_dir"

  printf '🔎 Looking for extra release assets in dist/%s\n' "$version" >&2

  if [ -f "$deb_src" ]; then
    cp "$deb_src" "$deb_dst"
    printf '✅ Added .deb asset: %s\n' "$deb_dst" >&2
    printf '%s\n' "$deb_dst"
  else
    printf '⏭️  Skipped .deb asset, not found: %s\n' "$deb_src" >&2
  fi

  if [ -f "$exe_src" ]; then
    cp "$exe_src" "$exe_dst"
    printf '✅ Added .exe asset: %s\n' "$exe_dst" >&2
    printf '%s\n' "$exe_dst"
  else
    local zip_src="${WORKSPACE_DIR}/dist/${version}/${RELEASE_PROJECT_NAME}-windows-x64.zip"
    local zip_dst="${artifact_dir}/${RELEASE_PROJECT_NAME}-windows-x64.zip"
    if [ -f "$zip_src" ]; then
      cp "$zip_src" "$zip_dst"
      printf '✅ Added .zip asset: %s\n' "$zip_dst" >&2
      printf '%s\n' "$zip_dst"
    else
      printf '⏭️  Skipped Windows asset, not found: %s\n' "$exe_src" >&2
    fi
  fi
}

release_config_notes_intro() {
  printf 'Release generated from %s at %s.\n' "$RELEASE_DEFAULT_BRANCH" "$(git rev-parse --short HEAD)"
}