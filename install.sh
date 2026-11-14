#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# -------- Detect host platform --------
HOST_CPU="$(uname -m)"
HOST_OS="$(uname -s)"

case "$HOST_CPU" in arm64|aarch64) HOST_CPU="aarch64" ;; x86_64|amd64) HOST_CPU="x86_64" ;; esac
case "$HOST_OS" in Darwin|darwin|macOS|macos) HOST_OS="Darwin" ;; Linux|linux) HOST_OS="Linux" ;; esac

TRIPLE_OS="$HOST_OS"
case "$HOST_OS" in
  Darwin)  TRIPLE_OS="apple-darwin" ;;
  Linux)   TRIPLE_OS="unknown-linux-gnu" ;;
  *) echo "error: unsupported host OS: $HOST_OS"; exit 1 ;;
esac

BUILD_TRIPLE="${HOST_CPU}-${TRIPLE_OS}"
TOOLCHAIN_SRC="$SCRIPT_DIR/toolchain/$BUILD_TRIPLE"
INSTALL_DIR="$HOME/.codeone/toolchain"

if [ ! -d "$TOOLCHAIN_SRC" ]; then
  echo "error: toolchain not found for host platform: $BUILD_TRIPLE"
  echo "  Run ./build.sh first to build the toolchain."
  exit 1
fi

echo "=== Installing c1 Toolchain ==="
echo "Source:      $TOOLCHAIN_SRC"
echo "Destination: $INSTALL_DIR"
echo ""

# ---- Clean old installation ----
echo "  Cleaning old installation..."
rm -rf "$INSTALL_DIR/bin" "$INSTALL_DIR/lib" "$INSTALL_DIR/boxes"

mkdir -p "$INSTALL_DIR/bin" "$INSTALL_DIR/lib" "$INSTALL_DIR/boxes/std"

# ---- Copy c1 and available lld ----
cp "$TOOLCHAIN_SRC/bin/c1" "$INSTALL_DIR/bin/"
cp "$TOOLCHAIN_SRC/bin/ld64.lld" "$INSTALL_DIR/bin/" 2>/dev/null || true
cp "$TOOLCHAIN_SRC/bin/ld.lld" "$INSTALL_DIR/bin/" 2>/dev/null || true

# ---- Copy platform library ----
cp "$TOOLCHAIN_SRC/lib/libplatform.a" "$INSTALL_DIR/lib/"

# ---- Copy boxes (.bx standard library) ----
cp -r "$SCRIPT_DIR/ssa/boxes/std"/* "$INSTALL_DIR/boxes/std/"

# ---- Copy docs (SSA spec for AI agents) ----
mkdir -p "$INSTALL_DIR/docs"
cp "$TOOLCHAIN_SRC/docs/ssa.md" "$INSTALL_DIR/docs/"
cp "$TOOLCHAIN_SRC/docs/box.md" "$INSTALL_DIR/docs/"
cp "$TOOLCHAIN_SRC/docs/agents.md" "$INSTALL_DIR/docs/"
cp "$TOOLCHAIN_SRC/docs/cmap.md" "$INSTALL_DIR/docs/"

# ---- Set up env file (like Rust's .cargo/env) ----
ENV_FILE="$HOME/.codeone/env"
mkdir -p "$HOME/.codeone"
cat > "$ENV_FILE" << EOF
# c1 toolchain — added by install.sh
export PATH="\$PATH:$INSTALL_DIR/bin"
EOF

# ---- Add source line to shell config (like Rust's rustup) ----
# Detect which shell config file to use
case "${SHELL:-}" in
  *zsh*)  RC_FILE="$HOME/.zshrc" ;;
  *bash*) [ -f "$HOME/.bash_profile" ] && RC_FILE="$HOME/.bash_profile" || RC_FILE="$HOME/.bashrc" ;;
  *)      RC_FILE="" ;;
esac

if [ -n "$RC_FILE" ]; then
  # Only add if not already present
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
SKILL_SRC="$SCRIPT_DIR/tools/skills/codeone"
SKILL_NAME="codeone"

install_skill_for() {
    local label="$1" cfg="$2" cmd="$3" root="$4"
    # Only install for agents that are actually present locally.
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
# ---- Apply to current shell ----
echo "  Applied: source \"\$HOME/.codeone/env\""
source "$HOME/.codeone/env"

echo ""
echo "Usage:"
echo "  c1 foo.bx"
echo "  Or:    c1 --ld lld foo.bx"
echo "  Or:    c1 foo.bx boxes/std/net/socket.bx -L $INSTALL_DIR/lib -l platform"
echo ""
echo "Code agent skill:"
echo "  /codeone create <app-name> [--bin|--static]"
echo ""
