#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# release.sh — Create a GitHub release and attach build artifacts
#
# Usage:
#   release.sh            Create tag + release, upload any pre-built artifacts
#   release.sh --deb      Also build Release_debian preset before uploading
#   release.sh --windows  Also build Release_mingw preset before uploading
#
# Artifact discovery is delegated to .config/release.config.sh via
# release_config_package_extra_assets(). Pre-built files in dist/<version>/
# are uploaded automatically even without --deb / --windows flags.
#
# Environment:
#   PROJECT_NAME   lowercase project name (e.g. notepp)
#   WORKSPACE_DIR  absolute path to repo root  (e.g. /workspaces/notepp)
# ---------------------------------------------------------------------------

WORKSPACE_DIR="${WORKSPACE_DIR:-$(git rev-parse --show-toplevel)}"
PROJECT_NAME="${PROJECT_NAME:-notepp}"
CMAKE_PROJECT_NAME="Notepp"

BUILD_DEB=false
BUILD_WINDOWS=false

for arg in "$@"; do
  case "$arg" in
    --deb)     BUILD_DEB=true ;;
    --windows) BUILD_WINDOWS=true ;;
    *)
      echo "Unknown argument: $arg"
      echo "Usage: $0 [--deb] [--windows]"
      exit 1
      ;;
  esac
done

# ---------------------------------------------------------------------------
# Source project release config (provides release_config_package_extra_assets)
# ---------------------------------------------------------------------------

RELEASE_CONFIG="${WORKSPACE_DIR}/.config/release.config.sh"
# shellcheck source=../../.config/release.config.sh
[ -f "${RELEASE_CONFIG}" ] && source "${RELEASE_CONFIG}"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

die() { echo "ERROR: $1" >&2; exit 1; }

get_version() {
  local version
  version="$(sed -nE \
    "s/^project\(.*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*\$/\1/p" \
    "${WORKSPACE_DIR}/CMakeLists.txt")"
  [ -n "$version" ] || die "Could not extract version from CMakeLists.txt"
  printf '%s\n' "$version"
}

ensure_gh() {
  command -v gh >/dev/null 2>&1 || die "gh CLI not found — install it from https://cli.github.com"
}

upload_assets() {
  local tag="$1"
  local repo="$2"

  if ! declare -f release_config_package_extra_assets > /dev/null; then
    echo "⏭️  No release_config_package_extra_assets defined — skipping extra assets"
    return
  fi

  local artifact_dir
  artifact_dir="$(mktemp -d)"

  echo ""
  local assets
  # stderr (progress messages) passes through; stdout captured as asset paths
  mapfile -t assets < <(release_config_package_extra_assets "" "${artifact_dir}" "${tag}")

  if [ "${#assets[@]}" -eq 0 ]; then
    echo "⏭️  No artifacts found in dist/${tag#v}/ — skipping upload"
    rm -rf "${artifact_dir}"
    return
  fi

  for asset in "${assets[@]}"; do
    [ -n "$asset" ] || continue
    echo "⬆️  Uploading $(basename "${asset}") to release ${tag}..."
    gh release upload "${tag}" "${asset}" --repo "${repo}" --clobber
    echo "✅ Uploaded $(basename "${asset}")"
  done

  rm -rf "${artifact_dir}"
}

# ---------------------------------------------------------------------------
# Version + tag
# ---------------------------------------------------------------------------

VERSION="$(get_version)"
TAG="v${VERSION}"

echo "📦 ${CMAKE_PROJECT_NAME} ${VERSION}  (tag: ${TAG})"

# ---------------------------------------------------------------------------
# Create git tag (idempotent)
# ---------------------------------------------------------------------------

if git -C "${WORKSPACE_DIR}" tag --list | grep -qxF "${TAG}"; then
  echo "🏷️  Tag ${TAG} already exists — skipping tag creation"
else
  git -C "${WORKSPACE_DIR}" tag -a "${TAG}" -m "Release ${TAG}"
  echo "🏷️  Created tag ${TAG}"
fi

# ---------------------------------------------------------------------------
# Create GitHub release (idempotent)
# ---------------------------------------------------------------------------

ensure_gh

REPO="$(gh repo view --json nameWithOwner -q .nameWithOwner)"

if gh release view "${TAG}" --repo "${REPO}" >/dev/null 2>&1; then
  echo "🚀 Release ${TAG} already exists — will upload artifacts to it"
else
  gh release create "${TAG}" \
    --title "${CMAKE_PROJECT_NAME} ${VERSION}" \
    --generate-notes
  echo "🚀 Created GitHub release ${TAG}"
fi

# ---------------------------------------------------------------------------
# Optional builds
# ---------------------------------------------------------------------------

if $BUILD_DEB; then
  echo ""
  echo "🐧 Building Release_debian preset..."
  cmake --preset Release_debian -S "${WORKSPACE_DIR}"
  cmake --build --preset Release_debian --target package
fi

if $BUILD_WINDOWS; then
  echo ""
  echo "🪟 Building Release_mingw preset..."
  cmake --preset Release_mingw -S "${WORKSPACE_DIR}"
  cmake --build --preset Release_mingw

  DIST_WIN="${WORKSPACE_DIR}/dist/Notepp"
  if command -v cygpath >/dev/null 2>&1; then
    "${WORKSPACE_DIR}/tools/tasks/copy_mingw_dlls.sh" \
      "${WORKSPACE_DIR}" "${DIST_WIN}/Notepp.exe"
  else
    echo "⚠️  cygpath not found — skipping DLL copy (MSYS2 environment required)"
  fi

  ISS_FILE="${WORKSPACE_DIR}/build/Release_mingw/notepp.iss"
  if command -v iscc >/dev/null 2>&1; then
    iscc "${ISS_FILE}"
  else
    echo "⚠️  iscc (InnoSetup) not found — packaging as zip instead"
    ZIP_FILE="${WORKSPACE_DIR}/dist/${VERSION}/${PROJECT_NAME}-windows-x64.zip"
    mkdir -p "${WORKSPACE_DIR}/dist/${VERSION}"
    (cd "${DIST_WIN}" && zip -r "${ZIP_FILE}" .)
  fi
fi

# ---------------------------------------------------------------------------
# Upload artifacts via release config
# ---------------------------------------------------------------------------

upload_assets "${TAG}" "${REPO}"

echo ""
echo "🎉 Release ${TAG} done — $(gh release view "${TAG}" --repo "${REPO}" --json url -q .url)"
