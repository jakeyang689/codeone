#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LLVM_DIR="$SCRIPT_DIR/llvm"
LLVM_SRC="$LLVM_DIR/llvm"
LLVM_BUILD="$LLVM_DIR/build"
LLVM_MLIR_BUILD="$LLVM_DIR/build-mlir"
C1_DIR="$SCRIPT_DIR/ssa/c1"
PLATFORM_DIR="$SCRIPT_DIR/ssa/platform"
HOST_CPU="$(uname -m)"
HOST_OS="$(uname -s)"

# ---- Normalise host arch/os ----
case "$HOST_CPU" in arm64|aarch64) HOST_CPU="aarch64" ;; x86_64|amd64) HOST_CPU="x86_64" ;; esac
case "$HOST_OS" in Darwin|darwin|macOS|macos) HOST_OS="Darwin" ;; Linux|linux) HOST_OS="Linux" ;; esac

# ---- Defaults: build for host ----
TARGET_CPU="$HOST_CPU"
TARGET_OS="$HOST_OS"
REBUILD_LLVM=0

# ---- Parse args ----
while [[ $# -gt 0 ]]; do
  case "$1" in
    --cpu) TARGET_CPU="$2"; shift 2 ;;
    --os)  TARGET_OS="$2"; shift 2 ;;
    --rebuild-llvm) REBUILD_LLVM=1; shift ;;
    --clean)
      echo "=== Cleaning build artifacts ==="
      make -C "$C1_DIR" clean 2>/dev/null || true
      make -C "$PLATFORM_DIR" clean 2>/dev/null || true
      make -C "$SCRIPT_DIR/ssa/test" clean 2>/dev/null || true
      make -C "$SCRIPT_DIR/ssa/test/codec_test" clean 2>/dev/null || true
      rm -rf "$SCRIPT_DIR/build" "$SCRIPT_DIR/toolchain"
      echo "=== Done ==="
      exit 0 ;;
    --help|-h)
      echo "Build c1 toolchain for target platform"
      echo ""
      echo "Usage: $0 [options]"
      echo "  --cpu <arch>     Target CPU architecture"
      echo "                   (default: host: $HOST_CPU)"
      echo "  --os <os>        Target operating system"
      echo "                   (default: host: $HOST_OS)"
      echo "  --rebuild-llvm   Rebuild LLVM with lld + cross-target support"
      echo "  --clean          Remove all build artifacts (c1, .o files, toolchain/)"
      echo "  --help           Show this help"
      exit 0 ;;
    *) echo "error: unknown option: $1"; exit 1 ;;
  esac
done

# ---- Deps check ----
check_deps() {
  local missing=0

  check_tool() {
    local tool="$1" hint="${2:-}"
    if ! command -v "$tool" >/dev/null 2>&1; then
      echo "  ERROR: missing required dependency: $tool"
      [ -n "$hint" ] && echo "    $hint"
      missing=1
    fi
  }

  check_file() {
    local path="$1" label="$2"
    if [ ! -f "$path" ]; then
      echo "  ERROR: missing $label: $path"
      echo "    Run ./build.sh --rebuild-llvm first, or pass --rebuild-llvm to this command"
      missing=1
    fi
  }

  echo "=== Checking dependencies ==="

  if [ "$HOST_OS" = "Darwin" ]; then
    check_tool cc      "Install Xcode Command Line Tools: xcode-select --install"
    check_tool clang++ "Install Xcode Command Line Tools: xcode-select --install"
  else
    check_tool cc      "Install build-essential (e.g. sudo apt install build-essential)"
    check_tool clang++ "Install clang (e.g. sudo apt install clang)"
  fi
  check_tool make
  check_tool ar

  if [ "$REBUILD_LLVM" -eq 0 ]; then
    check_file "$LLVM_BUILD/lib/libLLVMCore.a" "LLVM static libraries"
    check_file "$LLVM_MLIR_BUILD/lib/libMLIRIR.a" "MLIR static libraries"
  fi

  if [ "$REBUILD_LLVM" -eq 1 ]; then
    check_tool cmake
    check_tool ninja  "Install ninja-build (e.g. sudo apt install ninja-build)"
  fi

  if [ "$missing" -eq 1 ]; then
    echo ""
    echo "  Install missing dependencies and re-run build.sh."
    exit 1
  fi
  echo "  All dependencies found."
  echo ""
}

check_deps

# ---- Normalise target arch/os ----
case "$TARGET_CPU" in arm64|aarch64) TARGET_CPU="aarch64" ;; x86_64|amd64) TARGET_CPU="x86_64" ;; esac
case "$TARGET_OS" in Darwin|darwin|macOS|macos) TARGET_OS="Darwin" ;; Linux|linux) TARGET_OS="Linux" ;; Windows|windows) TARGET_OS="Windows" ;; esac

# Normalise OS to triple OS part
TRIPLE_OS="$TARGET_OS"
case "$TARGET_OS" in
  Darwin)  TRIPLE_OS="apple-darwin" ;;
  Linux)   TRIPLE_OS="unknown-linux-gnu" ;;
  Windows) TRIPLE_OS="unknown-windows-msvc" ;;
esac

BUILD_TRIPLE="${TARGET_CPU}-${TRIPLE_OS}"
OUT_DIR="$SCRIPT_DIR/toolchain/$BUILD_TRIPLE"

echo "=== Building c1 Toolchain ==="
echo "Host:         ${HOST_CPU}-${HOST_OS}"
echo "Target:       ${TARGET_CPU}-${TARGET_OS}"
echo "Output:       $OUT_DIR"
echo "Rebuild LLVM: ${REBUILD_LLVM}"
echo ""

# ---- Step 1: Rebuild LLVM (if requested) ----
if [ "$REBUILD_LLVM" -eq 1 ]; then
  echo "=== [1/3] Rebuilding LLVM with lld + multi-target ==="
  echo "This will take a while (10-30 minutes depending on machine)..."
  echo ""

  # Determine required LLVM targets: always build AArch64 + X86
  LLVM_TARGETS="AArch64;X86"

  cd "$LLVM_BUILD"

  cmake "$LLVM_SRC" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_PROJECTS="lld" \
    -DLLVM_TARGETS_TO_BUILD="$LLVM_TARGETS" \
    -DLLVM_INCLUDE_TESTS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_INCLUDE_DOCS=OFF \
    -DLLVM_ENABLE_BINDINGS=OFF

  CPU_COUNT="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
  ninja -j"$CPU_COUNT" lld

  # MLIR lives in its own build tree, and c1 links against that tree rather
  # than llvm/build: the two differ in assertion settings and are not
  # link-compatible, so every LLVM archive c1 uses comes from build-mlir.
  echo ""
  echo "=== Rebuilding MLIR for the tensor backend ==="
  cmake "$LLVM_SRC" \
    -B "$LLVM_DIR/build-mlir" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_PROJECTS="lld;mlir" \
    -DLLVM_TARGETS_TO_BUILD="$LLVM_TARGETS" \
    -DLLVM_INCLUDE_TESTS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_INCLUDE_DOCS=OFF \
    -DLLVM_ENABLE_BINDINGS=OFF \
    -DBUILD_SHARED_LIBS=OFF
  ninja -C "$LLVM_DIR/build-mlir" -j"$CPU_COUNT"

  cd "$SCRIPT_DIR"
  echo ""
  echo "LLVM + MLIR rebuild complete."
  echo ""
fi

# ---- Step 2: Build c1 compiler ----
echo "=== [2/3] Building c1 compiler ==="
cd "$C1_DIR"
make -s 2>&1 || make 2>&1
cd "$SCRIPT_DIR"
echo ""

# ---- Step 3: Build platform library ----
echo "=== [3/3] Building libplatform.a ==="
cd "$PLATFORM_DIR"
make -s 2>&1 || make 2>&1
cd "$SCRIPT_DIR"
echo ""

# ---- Create output directory ----
mkdir -p "$OUT_DIR/bin" "$OUT_DIR/lib"

# ---- Copy c1 ----
cp "$C1_DIR/c1" "$OUT_DIR/bin/"

# ---- Copy platform library ----
cp "$PLATFORM_DIR/libplatform.a" "$OUT_DIR/lib/"

# ---- Copy lld (if available) ----
if [ -f "$LLVM_BUILD/bin/ld64.lld" ]; then
  cp "$LLVM_BUILD/bin/ld64.lld" "$OUT_DIR/bin/" 2>/dev/null || true
  cp "$LLVM_BUILD/bin/ld.lld" "$OUT_DIR/bin/" 2>/dev/null || true
fi

# ---- Copy lld extras (if rebuilt) ----
if [ "$REBUILD_LLVM" -eq 1 ]; then
  cp "$LLVM_BUILD/bin/lld" "$OUT_DIR/bin/" 2>/dev/null || true
  cp "$LLVM_BUILD/bin/lld-link" "$OUT_DIR/bin/" 2>/dev/null || true
  cp "$LLVM_BUILD/bin/ld.lld" "$OUT_DIR/bin/" 2>/dev/null || true
  cp "$LLVM_BUILD/bin/wasm-ld" "$OUT_DIR/bin/" 2>/dev/null || true
fi

# ---- Copy docs (SSA spec for AI agents) ----
mkdir -p "$OUT_DIR/docs"
cp "$SCRIPT_DIR/ssa.md" "$OUT_DIR/docs/"
cp "$SCRIPT_DIR/box.md" "$OUT_DIR/docs/"
cp "$SCRIPT_DIR/agents.md" "$OUT_DIR/docs/"
cp "$SCRIPT_DIR/cmap.md" "$OUT_DIR/docs/"

echo "=== Done ==="
echo ""
echo "Toolchain output:"
ls -lh "$OUT_DIR/bin/"
ls -lh "$OUT_DIR/docs/"
echo ""
echo "Usage:"
echo "  $OUT_DIR/bin/c1 foo.bx"
echo "  $OUT_DIR/bin/c1 --cpu $TARGET_CPU --os $TARGET_OS foo.bx -o foo"
echo ""
