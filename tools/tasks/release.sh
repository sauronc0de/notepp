#!/usr/bin/env bash

RELEASE_PROJECT_ROOT="."
RELEASE_VERSION_SOURCE="CMakeLists.txt"
RELEASE_PROJECT_NAME="MyProject"

release_die() {
  echo "ERROR: $1"
  exit 1
}

release_project_version() {
  local version_file
  local version

  version_file="${RELEASE_PROJECT_ROOT}/${RELEASE_VERSION_SOURCE}"

  version="$(sed -nE \
    "s/^project\\(${RELEASE_PROJECT_NAME} VERSION ([0-9]+\\.[0-9]+\\.[0-9]+).*$/\\1/p" \
    "$version_file")"

  if [ -z "$version" ]; then
    release_die "Failed to detect the project version from ${version_file}"
  fi

  printf '%s\n' "$version"
}

release_project_version