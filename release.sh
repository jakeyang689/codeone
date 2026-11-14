#!/bin/bash
set -euo pipefail

# Build release archives for the current CPU architecture on macOS and Linux.
#
#   release/codeone-<version>-<triple>.tar.xz   uploaded to GitHub Releases
#   release/install.sh    installer, committed and shipped in the repository
#   release/skills/codeone
#
# Archives are not committed: they are uploaded as assets of the GitHub release
# v<version>, and install.sh downloads the asset matching the host. Only the
# current version is kept locally; older archives are removed on each run.
# Each archive holds bin/, lib/, docs/, boxes/, VERSION. The Linux toolchain
# cannot be cross-built from macOS (c1 and libplatform.a are native binaries,
# the LLVM static libraries are Mach-O), so it is built in a Linux environment
# with a fresh LLVM build: a running lima instance that mounts this project
# (used automatically when one exists), otherwise a Linux container.
#
#   CODEONE_LINUX_BUILD=auto|lima|docker   (default auto)
#   CODEONE_LINUX_VM=<lima instance>       (default: first one mounting the repo)
#   CODEONE_LINUX_IMAGE=<docker image>     (default codeone-linux-builder:24.04)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/VERSION")"
REPO="${CODEONE_REPO:-$(git -C "$SCRIPT_DIR" remote get-url origin 2>/dev/null | sed -e 's#^.*github\.com[:/]##' -e 's#\.git$##')}"
HOST_CPU="$(uname -m)"
HOST_OS_RAW="$(uname -s)"

case "$HOST_CPU" in arm64|aarch64) HOST_CPU="aarch64" ;; x86_64|amd64) HOST_CPU="x86_64" ;; esac
case "$HOST_OS_RAW" in Darwin|darwin|macOS|macos) HOST_OS="Darwin" ;; Linux|linux) HOST_OS="Linux" ;; *) HOST_OS="$HOST_OS_RAW" ;; esac

RELEASE_DIR="$SCRIPT_DIR/release"
# Not version-scoped: the LLVM build cache and the (freshly copied) sources it
# refers to have to keep the same paths for the cache to be reusable.
WORK_DIR="$SCRIPT_DIR/build/release"
LLVM_CACHE="$WORK_DIR/llvm-build"
LINUX_IMAGE="${CODEONE_LINUX_IMAGE:-codeone-linux-builder:24.04}"
# Set when the Linux toolchain is built in a lima VM, so packaging can strip
# ELF binaries with the VM's strip.
LINUX_VM_USED=""

# Archives must not record the build user's uid/gid or user name.
if [ "$HOST_OS" = "Darwin" ]; then
  TAR_OWNER=(--uid 0 --gid 0 --uname root --gname wheel)
else
  TAR_OWNER=(--owner root --group root)
fi

ONLY="all"
PUBLISH=0

usage() {
  cat <<EOF
Build codeone release archives

Usage: $0 [--only darwin|linux] [--publish]

  --only <os>   Build only one platform (default: both available ones)
  --publish     Upload the archives to the GitHub release v<version>
  --help        Show this help

Archives are written to release/. Without --publish they stay local, so
running this script never overwrites published binaries by accident.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --only) ONLY="$2"; shift 2 ;;
    --publish) PUBLISH=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "error: unknown option: $1"; usage; exit 1 ;;
  esac
done

case "$ONLY" in
  all|darwin|linux) ;;
  *) echo "error: --only expects darwin or linux"; exit 1 ;;
esac

triple_of() {
  case "$1" in
    Darwin) echo "${HOST_CPU}-apple-darwin" ;;
    Linux)  echo "${HOST_CPU}-unknown-linux-gnu" ;;
  esac
}

# Only the version being built is kept. Also clears version subdirectories
# left behind by the older release/<version>/ layout.
prune_old_versions() {
  local entry name
  for entry in "$RELEASE_DIR"/codeone-*.tar.*; do
    [ -e "$entry" ] || continue
    name="$(basename "$entry")"
    case "$name" in
      "codeone-$VERSION-"*) ;;
      *) echo "  Removing old archive: $name"; rm -f "$entry" ;;
    esac
  done
  for entry in "$RELEASE_DIR"/*; do
    [ -d "$entry" ] || continue
    name="$(basename "$entry")"
    case "$name" in
      [0-9]*.[0-9]*)
        echo "  Removing old release directory: $name"
        rm -rf "$entry" ;;
    esac
  done
}

# Token for the release API: explicit env, else the git credential store.
gh_token() {
  if [ -n "${CODEONE_GH_TOKEN:-}" ]; then echo "$CODEONE_GH_TOKEN"; return; fi
  if [ -n "${GH_TOKEN:-}" ]; then echo "$GH_TOKEN"; return; fi
  GIT_TERMINAL_PROMPT=0 git credential fill <<'EOF' 2>/dev/null | sed -n 's/^password=//p' | head -1
protocol=https
host=github.com
EOF
}

# Upload the archives through the REST API, replacing assets with the same
# name. Used when the GitHub CLI is not installed.
publish_api() {
  local tag="$1" token json rid asset name id existing
  local api="https://api.github.com/repos/$REPO"

  token="$(gh_token)"
  if [ -z "$REPO" ] || [ -z "$token" ]; then
    echo ""
    echo "note: no GitHub repository or token; upload the archives above as"
    echo "      assets of release $tag by hand, or set CODEONE_GH_TOKEN."
    return
  fi

  json="$(curl -fsS -H "Authorization: token $token" -H "Accept: application/vnd.github+json" \
    "$api/releases/tags/$tag" 2>/dev/null || true)"
  if [ -z "$json" ]; then
    echo ""
    echo "=== Creating GitHub release $tag ==="
    json="$(curl -fsS -X POST -H "Authorization: token $token" -H "Accept: application/vnd.github+json" \
      -d "{\"tag_name\":\"$tag\",\"name\":\"codeone $VERSION\"}" "$api/releases")"
  fi
  rid="$(printf '%s\n' "$json" | sed -n 's/^  "id": \([0-9]*\),$/\1/p' | head -1)"
  if [ -z "$rid" ]; then
    echo "error: could not resolve release $tag" >&2
    return 1
  fi

  echo "=== Uploading assets to $tag ==="
  existing="$(curl -fsS -H "Authorization: token $token" -H "Accept: application/vnd.github+json" \
    "$api/releases/$rid/assets")"
  for asset in "$RELEASE_DIR"/codeone-"$VERSION"-*.tar.xz; do
    name="$(basename "$asset")"
    id="$(printf '%s\n' "$existing" | awk -v a="$name" '
      /releases\/assets\/[0-9]+"/ { if (match($0, /assets\/[0-9]+/)) last = substr($0, RSTART + 7, RLENGTH - 7) }
      index($0, "\"name\": \"" a "\"") { print last; exit }')"
    if [ -n "$id" ]; then
      echo "  Replacing $name"
      curl -fsS -o /dev/null -X DELETE -H "Authorization: token $token" "$api/releases/assets/$id"
    else
      echo "  Adding $name"
    fi
    curl -fsS --retry 5 --retry-delay 2 --retry-all-errors -X POST \
      -H "Authorization: token $token" -H "Content-Type: application/gzip" \
      --data-binary @"$asset" \
      "https://uploads.github.com/repos/$REPO/releases/$rid/assets?name=$name" >/dev/null
  done
  echo "  Done: https://github.com/$REPO/releases/tag/$tag"
}

# Upload the archives as assets of the GitHub release v<version>. The assets
# are what install.sh downloads, so this replaces tracking them in git.
publish() {
  local tag="v$VERSION"
  if command -v gh >/dev/null 2>&1; then
    if ! gh release view "$tag" >/dev/null 2>&1; then
      echo ""
      echo "=== Creating GitHub release $tag ==="
      gh release create "$tag" --title "codeone $VERSION" --generate-notes
    fi
    echo "=== Uploading assets to $tag ==="
    gh release upload "$tag" "$RELEASE_DIR"/codeone-"$VERSION"-*.tar.xz --clobber
    return
  fi
  publish_api "$tag"
}

# Release-wide files next to the archives: version marker, installer, skill.
publish_extras() {
  mkdir -p "$RELEASE_DIR"
  cp "$SCRIPT_DIR/VERSION" "$RELEASE_DIR/VERSION"
  cp "$SCRIPT_DIR/tools/release-install.sh" "$RELEASE_DIR/install.sh"
  chmod +x "$RELEASE_DIR/install.sh"
  mkdir -p "$RELEASE_DIR/skills"
  rm -rf "$RELEASE_DIR/skills/codeone"
  cp -R "$SCRIPT_DIR/tools/skills/codeone" "$RELEASE_DIR/skills/codeone"
}

# Strip debug info from the staged c1 with a tool that understands the target
# format: the host strip for Darwin and native Linux builds, otherwise the VM
# or container that built the Linux toolchain.
strip_c1() {
  local stage="$1" triple="$2" file="$1/bin/c1"
  case "$triple" in
    *apple-darwin) strip -S "$file" ;;
    *)
      if [ "$HOST_OS" = "Linux" ]; then
        strip -S "$file"
      elif [ -n "$LINUX_VM_USED" ]; then
        limactl shell "$LINUX_VM_USED" -- strip -S "$file"
      else
        docker run --rm -v "$stage:/stage" "$LINUX_IMAGE" strip -S /stage/bin/c1
      fi
      ;;
  esac
}

# Stage the toolchain directory plus boxes/docs into a tarball.
package() {
  local toolchain_dir="$1" triple="$2"
  local name="codeone-$VERSION-$triple"
  local stage="$WORK_DIR/stage/$name"

  rm -rf "$stage"
  mkdir -p "$stage"
  cp -R "$toolchain_dir/bin" "$toolchain_dir/lib" "$stage/"
  # install.sh only uses c1 plus the lld driver for the host; the other lld
  # front-ends are ~90 MB each and would dominate the archive.
  if [[ "$triple" == *apple-darwin ]]; then
    find "$stage/bin" -type f ! -name c1 ! -name ld.lld ! -name ld64.lld -delete
  else
    find "$stage/bin" -type f ! -name c1 ! -name ld.lld -delete
  fi
  # The linker's debug map records the absolute paths of the object files
  # (~/Workspace/... on the build machine), so strip the debug info before
  # the binary is published.
  strip_c1 "$stage" "$triple"
  if [ -d "$toolchain_dir/docs" ]; then
    cp -R "$toolchain_dir/docs" "$stage/"
  fi
  cp -R "$SCRIPT_DIR/ssa/boxes/std" "$stage/boxes"
  cp "$SCRIPT_DIR/VERSION" "$stage/"

  mkdir -p "$RELEASE_DIR"
  tar -cJf "$RELEASE_DIR/$name.tar.xz" "${TAR_OWNER[@]}" -C "$WORK_DIR/stage" "$name"
  echo "  $RELEASE_DIR/$name.tar.xz"
}

build_native() {
  local os="$1" triple
  triple="$(triple_of "$os")"
  echo "=== Building native $triple ==="
  "$SCRIPT_DIR/build.sh" --cpu "$HOST_CPU" --os "$os"
  package "$SCRIPT_DIR/toolchain/$triple" "$triple"
}

ensure_docker() {
  if ! command -v docker >/dev/null 2>&1; then
    echo "error: docker not found; install Docker Desktop or colima to build the Linux release" >&2
    exit 1
  fi
  if ! docker info >/dev/null 2>&1; then
    echo "error: docker daemon is not running" >&2
    echo "  start it (colima start, or open Docker Desktop) and re-run $0" >&2
    exit 1
  fi
}

build_linux_image() {
  if docker image inspect "$LINUX_IMAGE" >/dev/null 2>&1; then
    return
  fi
  echo "=== Building Linux builder image ($LINUX_IMAGE) ==="
  docker build -t "$LINUX_IMAGE" - <<'DOCKERFILE'
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential clang cmake ninja-build python3 zlib1g-dev libtinfo-dev ca-certificates \
 && rm -rf /var/lib/apt/lists/*
DOCKERFILE
}

# Copy the sources into an isolated workspace so the Linux build never touches
# the host's Mach-O LLVM build or the host toolchain directory.
prepare_linux_workdir() {
  local l="$WORK_DIR/linux"
  rm -rf "$l"
  mkdir -p "$l/ssa" "$l/build" "$l/toolchain"
  cp -R "$SCRIPT_DIR/ssa/." "$l/ssa/"
  rm -f "$l/ssa/c1/c1" "$l/ssa/platform/libplatform.a"
  cp "$SCRIPT_DIR/build.sh" "$SCRIPT_DIR/VERSION" "$l/"
  cp "$SCRIPT_DIR/ssa.md" "$SCRIPT_DIR/box.md" "$SCRIPT_DIR/agents.md" "$SCRIPT_DIR/cmap.md" "$l/"
  echo "$l"
}

# First running lima instance that has this project mounted. Either backend
# produces the same archive; lima is used when it is already available because
# it avoids pulling a container image.
detect_lima_vm() {
  command -v limactl >/dev/null 2>&1 || return 1
  local name
  for name in $(limactl list 2>/dev/null | awk '$2 == "Running" { print $1 }'); do
    if limactl shell "$name" -- test -d "$SCRIPT_DIR/ssa" 2>/dev/null; then
      echo "$name"
      return 0
    fi
  done
  return 1
}

build_linux_via_lima() {
  local vm="$1" l triple vm_build
  triple="$(triple_of Linux)"

  echo "=== Building $triple in lima VM '$vm' (LLVM rebuild, 20-40 min) ==="
  l="$(prepare_linux_workdir)"
  LINUX_VM_USED="$vm"

  # Out-of-tree LLVM needs the monorepo sources next to the build directory;
  # they are read in place, while objects go to the VM's own disk so the
  # virtiofs mount does not carry the build I/O.
  mkdir -p "$l/llvm"
  local entry name
  for entry in "$SCRIPT_DIR"/llvm/*; do
    name="$(basename "$entry")"
    if [ "$name" != "build" ]; then
      ln -sfn "$entry" "$l/llvm/$name"
    fi
  done
  # Kept between runs (and versions) so ninja only rebuilds what changed.
  # The VM's home is not the host home, so resolve it on the guest side.
  vm_build="$(limactl shell "$vm" -- bash -lc 'printf "%s\n" "$HOME/.cache/codeone/llvm-build"' | tail -1)"
  # A cache left by an older generator cannot be reused or reconfigured.
  if limactl shell "$vm" -- test -f "$vm_build/CMakeCache.txt" 2>/dev/null &&
     ! limactl shell "$vm" -- grep -q 'CMAKE_GENERATOR:INTERNAL=Ninja' "$vm_build/CMakeCache.txt" 2>/dev/null; then
    limactl shell "$vm" -- rm -rf "$vm_build"
  fi
  limactl shell "$vm" -- mkdir -p "$vm_build"
  ln -sfn "$vm_build" "$l/llvm/build"

  limactl shell "$vm" -- bash -lc "cd '$l' && ./build.sh --rebuild-llvm --cpu $HOST_CPU --os Linux"

  package "$l/toolchain/$triple" "$triple"
}

build_linux() {
  local l="$WORK_DIR/linux" triple
  triple="$(triple_of Linux)"

  case "${CODEONE_LINUX_BUILD:-auto}" in
    docker) ;;
    lima)
      local vm="${CODEONE_LINUX_VM:-}"
      if [ -z "$vm" ]; then
        if ! vm="$(detect_lima_vm)"; then
          echo "error: no running lima VM with this project mounted" >&2
          echo "  start one, or set CODEONE_LINUX_VM=<instance>" >&2
          exit 1
        fi
      fi
      build_linux_via_lima "$vm"
      return
      ;;
    auto)
      local vm
      if vm="$(detect_lima_vm)"; then
        build_linux_via_lima "$vm"
        return
      fi
      ;;
    *)
      echo "error: CODEONE_LINUX_BUILD expects auto, docker or lima" >&2
      exit 1
      ;;
  esac

  echo "=== Building $triple in docker (LLVM rebuild, 10-30 min) ==="
  ensure_docker
  build_linux_image
  l="$(prepare_linux_workdir)"

  # Reused across runs and versions; only a cache from another generator has
  # to be thrown away.
  mkdir -p "$LLVM_CACHE"
  if [ -f "$LLVM_CACHE/CMakeCache.txt" ] &&
     ! grep -q 'CMAKE_GENERATOR:INTERNAL=Ninja' "$LLVM_CACHE/CMakeCache.txt"; then
    rm -rf "$LLVM_CACHE"
    mkdir -p "$LLVM_CACHE"
  fi

  docker run --rm \
    -v "$l:/work" \
    -v "$SCRIPT_DIR/llvm:/work/llvm:ro" \
    -v "$LLVM_CACHE:/work/llvm/build" \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -w /work \
    "$LINUX_IMAGE" \
    bash -lc "./build.sh --rebuild-llvm --cpu $HOST_CPU --os Linux"

  package "$l/toolchain/$triple" "$triple"
}

echo "=== codeone $VERSION release ==="
echo "Host:   ${HOST_CPU}-${HOST_OS}"
echo "Output: $RELEASE_DIR"
echo ""

command -v xz >/dev/null 2>&1 || {
  echo "error: xz not found; the archives are .tar.xz (brew install xz, apt install xz-utils)" >&2
  exit 1
}

prune_old_versions
publish_extras

case "$HOST_OS" in
  Darwin)
    if [[ "$ONLY" == "all" || "$ONLY" == "darwin" ]]; then build_native Darwin; fi
    if [[ "$ONLY" == "all" || "$ONLY" == "linux" ]]; then build_linux; fi
    ;;
  Linux)
    if [[ "$ONLY" == "darwin" ]]; then
      echo "error: the Darwin toolchain must be built on macOS" >&2
      exit 1
    fi
    if [[ "$ONLY" == "all" ]]; then
      echo "note: skipping Darwin; the macOS toolchain must be built on macOS" >&2
    fi
    build_native Linux
    ;;
  *)
    echo "error: unsupported host OS: $HOST_OS" >&2
    exit 1
    ;;
esac

if [ "$PUBLISH" -eq 1 ]; then
  publish
else
  echo ""
  echo "Archives built locally. Re-run with --publish to upload them to"
  echo "the GitHub release v$VERSION."
fi

echo ""
echo "=== Done ==="
ls -lh "$RELEASE_DIR"
