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

  if [ -f "$deb_src" ]; then
    cp "$deb_src" "$deb_dst"
    printf '%s\n' "$deb_dst"
  fi

  if [ -f "$exe_src" ]; then
    cp "$exe_src" "$exe_dst"
    printf '%s\n' "$exe_dst"
  fi
}

release_config_notes_intro() {
  printf 'Release generated from %s at %s.\n' "$RELEASE_DEFAULT_BRANCH" "$(git rev-parse --short HEAD)"
}