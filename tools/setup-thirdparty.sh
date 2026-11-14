#!/usr/bin/env bash
# Fetch, unpack and build the sources listed in thirdparty/packages.conf.
# Everything except packages.conf is gitignored.
#
#   tools/setup-thirdparty.sh            # every package
#   tools/setup-thirdparty.sh raylib     # selected packages
#
# Set CODEONE_MIRROR to a URL prefix (e.g. https://ghfast.top/) when GitHub is
# slow; the prefix is prepended to each archive URL.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONF="${CODEONE_PACKAGES:-$ROOT/thirdparty/packages.conf}"
DEST_ROOT="$ROOT/thirdparty"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
export JOBS

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d' ' -f1
  else
    shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

wanted=("$@")
is_wanted() {
  [ ${#wanted[@]} -eq 0 ] && return 0
  local w
  for w in "${wanted[@]}"; do
    [ "$w" = "$1" ] && return 0
  done
  return 1
}

found=0
while IFS='|' read -r name version url sha build; do
  name="$(printf %s "$name" | tr -d '[:space:]')"
  case "$name" in ''|\#*) continue ;; esac
  is_wanted "$name" || continue
  found=$((found + 1))

  version="$(printf %s "$version" | tr -d '[:space:]')"
  sha="$(printf %s "$sha" | tr -d '[:space:]')"
  tgz="$DEST_ROOT/$name-$version.tar.gz"
  dest="$DEST_ROOT/$name"

  if [ -n "${CODEONE_MIRROR:-}" ]; then
    url="${CODEONE_MIRROR%/}/$url"
  fi

  if [ ! -f "$tgz" ]; then
    echo "Downloading $name $version..."
    curl -fL --retry 3 -o "$tgz.part" "$url"
    mv "$tgz.part" "$tgz"
  fi

  actual="$(sha256_of "$tgz")"
  if [ "$actual" != "$sha" ]; then
    echo "error: checksum mismatch for $tgz" >&2
    echo "  expected $sha" >&2
    echo "  got      $actual" >&2
    exit 1
  fi

  if [ ! -d "$dest" ]; then
    echo "Extracting $name $version..."
    rm -rf "$dest"
    mkdir -p "$dest"
    tar -xzf "$tgz" -C "$dest" --strip-components=1
  fi

  if [ -n "$build" ]; then
    echo "Building $name..."
    ( cd "$dest" && sh -c "$build" )
  fi

  echo "$name ready: $dest"
done < "$CONF"

if [ "$found" -eq 0 ]; then
  echo "error: no matching package in $CONF" >&2
  exit 1
fi
