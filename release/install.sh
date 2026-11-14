#!/bin/bash
set -euo pipefail

# Installs a codeone release on this machine.
#
# Run from the release directory in the repository:
#   ./install.sh
#
# It downloads the archive matching the host triple from GitHub Releases,
# installs it to ~/.codeone/toolchain, and installs the bundled codeone skill
# for any local Claude Code / Codex CLI / Gemini CLI. A locally built archive
# next to this script is used as-is instead of downloading.
#
#   CODEONE_REPO=<owner>/<name>   GitHub repository (default jakeyang689/codeone)
#   CODEONE_GH_MIRROR=<prefix>    download prefix for slow GitHub links,
#                                 e.g. https://ghfast.top/
#   CODEONE_GH_TOKEN=<token>      token with repo access, needed when the
#                                 repository is private (GH_TOKEN also works;
#                                 otherwise the git credential store is asked)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VERSION="$(tr -d '[:space:]' < "$SCRIPT_DIR/VERSION")"
REPO="${CODEONE_REPO:-jakeyang689/codeone}"
MIRROR="${CODEONE_GH_MIRROR:-}"
# The archives are ~46 MB; links to GitHub drop the connection often enough
# that a single attempt regularly ends in an HTTP/2 protocol error. Retries
# resume where the previous attempt stopped (-C -) instead of starting over.
CURL_FLAGS=(-fL --progress-bar --retry 5 --retry-delay 2 --retry-all-errors --connect-timeout 30 -C -)

HOST_CPU="$(uname -m)"
HOST_OS="$(uname -s)"

case "$HOST_CPU" in arm64|aarch64) HOST_CPU="aarch64" ;; x86_64|amd64) HOST_CPU="x86_64" ;; esac
case "$HOST_OS" in
  Darwin|darwin|macOS|macos) TRIPLE_OS="apple-darwin" ;;
  Linux|linux)               TRIPLE_OS="unknown-linux-gnu" ;;
  *) echo "error: unsupported host OS: $HOST_OS"; exit 1 ;;
esac

BUILD_TRIPLE="${HOST_CPU}-${TRIPLE_OS}"
ASSET="codeone-$VERSION-$BUILD_TRIPLE.tar.xz"
ARCHIVE="$SCRIPT_DIR/$ASSET"
INSTALL_DIR="$HOME/.codeone/toolchain"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

# Token for private repositories: explicit env, else the git credential store.
gh_token() {
  if [ -n "${CODEONE_GH_TOKEN:-}" ]; then echo "$CODEONE_GH_TOKEN"; return; fi
  if [ -n "${GH_TOKEN:-}" ]; then echo "$GH_TOKEN"; return; fi
  command -v git >/dev/null 2>&1 || return 0
  GIT_TERMINAL_PROMPT=0 git credential fill <<'EOF' 2>/dev/null | sed -n 's/^password=//p' | head -1
protocol=https
host=github.com
EOF
}

# The web download path (and the mirror prefix) — works for public repositories.
download_from_web() {
  local base
  for base in "${MIRROR}https://github.com/$REPO/releases/download/v$VERSION" \
              "${MIRROR}https://github.com/$REPO/releases/latest/download"; do
    # Not deleted between attempts: a partial download is resumed, not restarted.
    curl "${CURL_FLAGS[@]}" -o "$ARCHIVE" "$base/$ASSET" && return 0
  done
  return 1
}

# The API asset endpoint — the only path that works for private repositories.
download_from_api() {
  local json id
  [ -n "$AUTH_TOKEN" ] || return 1
  json="$(curl -fsSL --max-time 60 -H "Authorization: token $AUTH_TOKEN" \
    -H "Accept: application/vnd.github+json" \
    "https://api.github.com/repos/$REPO/releases/tags/v$VERSION")" || return 1
  id="$(printf '%s\n' "$json" | awk -v a="$ASSET" '
    /releases\/assets\/[0-9]+"/ { if (match($0, /assets\/[0-9]+/)) last = substr($0, RSTART + 7, RLENGTH - 7) }
    index($0, "\"name\": \"" a "\"") { print last; exit }')"
  [ -n "$id" ] || return 1
  curl "${CURL_FLAGS[@]}" -o "$ARCHIVE" -H "Authorization: token $AUTH_TOKEN" \
    -H "Accept: application/octet-stream" \
    "https://api.github.com/repos/$REPO/releases/assets/$id"
}

# macOS does not ship xz; Python's lzma module reads the same archive. Both
# paths refuse to run anything inside the archive, they only unpack it.
extract() {
  local archive="$1" dest="$2"
  if command -v xz >/dev/null 2>&1; then
    tar -xJf "$archive" -C "$dest"
  elif command -v python3 >/dev/null 2>&1; then
    python3 -I - "$archive" "$dest" <<'PY'
import lzma, sys, tarfile
with tarfile.open(fileobj=lzma.open(sys.argv[1]), mode="r|") as t:
    t.extractall(sys.argv[2])
PY
  else
    echo "error: extracting $archive needs xz or python3" >&2
    return 1
  fi
}

# A locally built archive (from ./release.sh) wins over the network.
if [ ! -f "$ARCHIVE" ]; then
  ARCHIVE="$TMP_DIR/$ASSET"
  AUTH_TOKEN="$(gh_token)"
  echo "  Downloading $ASSET"
  if ! download_from_web && ! download_from_api; then
    rm -f "$ARCHIVE"
    echo "error: could not download $ASSET from the releases of $REPO" >&2
    echo "  Check that release v$VERSION publishes this asset." >&2
    echo "  Private repository? Set CODEONE_GH_TOKEN to a token with repo access." >&2
    echo "  Slow GitHub links? Retry with:" >&2
    echo "    CODEONE_GH_MIRROR=https://ghfast.top/ ./install.sh" >&2
    exit 1
  fi
fi

echo "=== Installing c1 Toolchain $VERSION ==="
echo "Archive:     $ARCHIVE"
echo "Destination: $INSTALL_DIR"
echo ""

echo "  Extracting..."
extract "$ARCHIVE" "$TMP_DIR"
STAGE="$TMP_DIR/codeone-$VERSION-$BUILD_TRIPLE"

# ---- Clean old installation ----
echo "  Cleaning old installation..."
rm -rf "$INSTALL_DIR/bin" "$INSTALL_DIR/lib" "$INSTALL_DIR/boxes" "$INSTALL_DIR/docs"

mkdir -p "$INSTALL_DIR/bin" "$INSTALL_DIR/lib" "$INSTALL_DIR/boxes/std" "$INSTALL_DIR/docs"

cp "$STAGE/bin/"* "$INSTALL_DIR/bin/"
cp "$STAGE/lib/"* "$INSTALL_DIR/lib/"
cp -R "$STAGE/boxes/"* "$INSTALL_DIR/boxes/std/"
cp "$STAGE/docs/"* "$INSTALL_DIR/docs/"
cp "$STAGE/VERSION" "$INSTALL_DIR/VERSION"

# ---- Set up env file (like Rust's .cargo/env) ----
ENV_FILE="$HOME/.codeone/env"
mkdir -p "$HOME/.codeone"
cat > "$ENV_FILE" << EOF
# c1 toolchain — added by install.sh
export PATH="\$PATH:$INSTALL_DIR/bin"
EOF

# ---- Add source line to shell config (like Rust's rustup) ----
case "${SHELL:-}" in
  *zsh*)  RC_FILE="$HOME/.zshrc" ;;
  *bash*) [ -f "$HOME/.bash_profile" ] && RC_FILE="$HOME/.bash_profile" || RC_FILE="$HOME/.bashrc" ;;
  *)      RC_FILE="" ;;
esac

if [ -n "$RC_FILE" ]; then
  if ! grep -q 'source.*\.codeone/env' "$RC_FILE" 2>/dev/null; then
    echo "" >> "$RC_FILE"
    echo "# Added by .codeone/install.sh" >> "$RC_FILE"
    echo "source \"\$HOME/.codeone/env\"" >> "$RC_FILE"
    echo "  Added: source \"\$HOME/.codeone/env\" → $RC_FILE"
  else
    echo "  Already present: $RC_FILE (skipped)"
  fi
else
  echo "  Warning: could not detect shell config file. Add manually:"
  echo "    source \"\$HOME/.codeone/env\""
fi

# ---- Install the codeone skill for local code agents that support skills ----
SKILL_SRC="$SCRIPT_DIR/skills/codeone"
SKILL_NAME="codeone"

install_skill_for() {
    local label="$1" cfg="$2" cmd="$3" root="$4"
    if [ ! -d "$cfg" ] && ! command -v "$cmd" >/dev/null 2>&1; then
        return 1
    fi
    mkdir -p "$root"
    rm -rf "$root/$SKILL_NAME"
    cp -R "$SKILL_SRC" "$root/$SKILL_NAME"
    echo "  $label: $root/$SKILL_NAME"
    return 0
}

echo ""
echo "=== Installing codeone skill ==="
if [ -d "$SKILL_SRC" ]; then
    skill_found=0
    if install_skill_for "Claude Code" "$HOME/.claude" claude "$HOME/.claude/skills"; then
        skill_found=1
    fi
    if install_skill_for "Codex CLI" "$HOME/.codex" codex "$HOME/.codex/skills"; then
        skill_found=1
    fi
    if install_skill_for "Gemini CLI" "$HOME/.gemini" gemini "$HOME/.gemini/skills"; then
        skill_found=1
    fi
    if [ "$skill_found" -eq 0 ]; then
        echo "  No supported code agent found (Claude Code, Codex CLI, Gemini CLI)."
        echo "  Skill source: $SKILL_SRC"
    fi
else
    echo "  Warning: skill source not found at $SKILL_SRC (skipped)"
fi

echo ""
echo "=== Done ==="
echo ""
echo "Installed at: $INSTALL_DIR"
ls -lh "$INSTALL_DIR/bin/"
echo ""
echo "  Applied: source \"\$HOME/.codeone/env\""
echo "  Restart your shell (or run that command) to use c1."
echo ""
echo "Usage:"
echo "  c1 foo.bx"
echo "  Or:    c1 --ld lld foo.bx"
echo "  Or:    c1 foo.bx boxes/std/net/socket.bx -L $INSTALL_DIR/lib -l platform"
echo ""
echo "Code agent skill:"
echo "  /codeone create <app-name> [--bin|--static]"
echo ""
