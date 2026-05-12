#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# release.sh — Create a GitHub release and optionally attach build artifacts
#
# Usage:
#   release.sh            Create the git tag + GitHub release (no artifacts)
#   release.sh --deb      Build Release_debian preset, attach .deb package
#   release.sh --windows  Build Release_mingw preset, build installer, attach .exe
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
# Helpers
# ---------------------------------------------------------------------------

die() { echo "ERROR: $1" >&2; exit 1; }

get_version() {
  local version
  version="$(sed -nE \
    "s/^project\(${CMAKE_PROJECT_NAME} VERSION ([0-9]+\.[0-9]+\.[0-9]+).*\$/\1/p" \
    "${WORKSPACE_DIR}/CMakeLists.txt")"
  [ -n "$version" ] || die "Could not extract version from CMakeLists.txt"
  printf '%s\n' "$version"
}

ensure_gh() {
  command -v gh >/dev/null 2>&1 || die "gh CLI not found — install it from https://cli.github.com"
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

if gh release view "${TAG}" --repo "$(gh repo view --json nameWithOwner -q .nameWithOwner)" >/dev/null 2>&1; then
  echo "🚀 Release ${TAG} already exists — will upload artifacts to it"
else
  gh release create "${TAG}" \
    --title "${CMAKE_PROJECT_NAME} ${VERSION}" \
    --generate-notes
  echo "🚀 Created GitHub release ${TAG}"
fi

REPO="$(gh repo view --json nameWithOwner -q .nameWithOwner)"

# ---------------------------------------------------------------------------
# --deb: build Release_debian + CPack → upload .deb
# ---------------------------------------------------------------------------

if $BUILD_DEB; then
  echo ""
  echo "🐧 Building Release_debian preset..."
  cmake --preset Release_debian -S "${WORKSPACE_DIR}"
  cmake --build --preset Release_debian --target package

  DEB_FILE="${WORKSPACE_DIR}/dist/${PROJECT_NAME}_${VERSION}_amd64.deb"
  [ -f "${DEB_FILE}" ] || die ".deb not found at ${DEB_FILE}"

  echo "⬆️  Uploading ${DEB_FILE} to release ${TAG}..."
  gh release upload "${TAG}" "${DEB_FILE}" --repo "${REPO}" --clobber
  echo "✅ Debian package uploaded"
fi

# ---------------------------------------------------------------------------
# --windows: build Release_mingw + InnoSetup → upload installer
# ---------------------------------------------------------------------------

if $BUILD_WINDOWS; then
  echo ""
  echo "🪟 Building Release_mingw preset..."
  cmake --preset Release_mingw -S "${WORKSPACE_DIR}"
  cmake --build --preset Release_mingw

  # Stage DLLs alongside the exe before packaging
  DIST_WIN="${WORKSPACE_DIR}/dist/Notepp"
  if command -v cygpath >/dev/null 2>&1; then
    "${WORKSPACE_DIR}/tools/tasks/copy_mingw_dlls.sh" \
      "${WORKSPACE_DIR}" "${DIST_WIN}/Notepp.exe"
  else
    echo "⚠️  cygpath not found — skipping DLL copy (MSYS2 environment required)"
  fi

  # Build installer with InnoSetup
  ISS_FILE="${WORKSPACE_DIR}/build/Release_mingw/notepp.iss"
  INSTALLER="${WORKSPACE_DIR}/dist/NoteppSetup-${VERSION}.exe"

  if command -v iscc >/dev/null 2>&1; then
    iscc "${ISS_FILE}"
    [ -f "${INSTALLER}" ] || die "Installer not found at ${INSTALLER}"
    echo "⬆️  Uploading ${INSTALLER} to release ${TAG}..."
    gh release upload "${TAG}" "${INSTALLER}" --repo "${REPO}" --clobber
    echo "✅ Windows installer uploaded"
  else
    echo "⚠️  iscc (InnoSetup) not found — packaging as zip instead"
    ZIP_FILE="${WORKSPACE_DIR}/dist/${PROJECT_NAME}-${VERSION}-windows-x64.zip"
    (cd "${DIST_WIN}" && zip -r "${ZIP_FILE}" .)
    [ -f "${ZIP_FILE}" ] || die "Zip not found at ${ZIP_FILE}"
    echo "⬆️  Uploading ${ZIP_FILE} to release ${TAG}..."
    gh release upload "${TAG}" "${ZIP_FILE}" --repo "${REPO}" --clobber
    echo "✅ Windows zip uploaded"
  fi
fi

echo ""
echo "🎉 Release ${TAG} done — $(gh release view "${TAG}" --repo "${REPO}" --json url -q .url)"
