#!/bin/bash
# Scaffold a codeone project in the current directory.
#
# Usage: create.sh <app-name> [--bin|--static]
#
# Run this inside an empty directory the user has created. The project name is
# the directory name; <app-name> names the built artifact. The default mode is
# --bin (an executable named <app-name>); --static builds lib<app-name>.a
# instead. Only the installed toolchain at $HOME/.codeone/toolchain is used.

set -euo pipefail

SKILL_DIR="$(cd "$(dirname "$0")" && pwd -P)"
TEMPLATES="$SKILL_DIR/templates"
TOOLCHAIN="$HOME/.codeone/toolchain"

MODE=""
APP=""

for arg in "$@"; do
    case "$arg" in
        --bin)
            if [ "$MODE" = "static" ]; then
                echo "error: --bin and --static are mutually exclusive" >&2
                exit 2
            fi
            MODE=bin
            ;;
        --static)
            if [ "$MODE" = "bin" ]; then
                echo "error: --bin and --static are mutually exclusive" >&2
                exit 2
            fi
            MODE=static
            ;;
        -*)
            echo "error: unknown option: $arg" >&2
            echo "usage: codeone create <app-name> [--bin|--static]" >&2
            exit 2
            ;;
        *)
            if [ -n "$APP" ]; then
                echo "error: unexpected extra argument: $arg" >&2
                echo "usage: codeone create <app-name> [--bin|--static]" >&2
                exit 2
            fi
            APP="$arg"
            ;;
    esac
done

MODE="${MODE:-bin}"

if [ -z "$APP" ]; then
    echo "usage: codeone create <app-name> [--bin|--static]   (run inside an empty project directory)" >&2
    exit 2
fi

case "$APP" in
    ""|.*|-*|*[!A-Za-z0-9._-]*)
        echo "error: invalid application name: $APP" >&2
        echo "       use letters, digits, '.', '_' or '-', not starting with '.' or '-'" >&2
        exit 2
        ;;
esac

if [ "$MODE" = "static" ]; then
    KIND="static library"
    ARTIFACT="lib$APP.a"
    BUILD_FLAGS="--static -O2"
    ENTRY_FILE="lib.bx"
    ENTRY_FN="hello"
    SRC_TEMPLATE="$TEMPLATES/lib.bx"
    ENTRY_RULE="- Library entry: expose public functions such as \`fun @hello() -> i32\`; there is no \`@main\`."
    ENTRY_DOC="- \`lib.bx\` — public API: expose the library's functions here; consumers declare them with \`extern\`."
    RUN_DOC="link it from a consumer with \`-L . -l $APP\`"
    VERIFY_DOC="Compile after every change and confirm the archive is rebuilt. Report the actual compiler output when something fails; do not assume success without building it."
else
    KIND="application"
    ARTIFACT="$APP"
    BUILD_FLAGS="-O2"
    ENTRY_FILE="main.bx"
    ENTRY_FN="main"
    SRC_TEMPLATE="$TEMPLATES/main.bx"
    ENTRY_RULE="- Entry point: \`fun @main() -> i32\` (the libc \`main\` is generated)."
    ENTRY_DOC="- \`main.bx\` — entry point only: parse arguments, call module functions, and return an exit code. Keep it small."
    RUN_DOC="run it with \`make run\`"
    VERIFY_DOC="Compile and run after every change. Report the actual compiler or runtime output when something fails; do not assume success without running it."
fi

PROJ="$(pwd -P)"
NAME="$(basename "$PROJ")"

if [ ! -x "$TOOLCHAIN/bin/c1" ]; then
    echo "error: codeone toolchain not found at $TOOLCHAIN" >&2
    echo "" >&2
    echo "Install the toolchain from the codeone repository first:" >&2
    echo "  cd <codeone-repo> && ./build.sh && ./install.sh" >&2
    exit 1
fi

TC="$(cd "$TOOLCHAIN" && pwd -P)"

if [ "$MODE" = "static" ]; then
    PLATFORM_EXTRA="A static archive leaves these calls unresolved; the program linking the archive must also pass \`-L $TC/lib -l platform\`."
else
    PLATFORM_EXTRA="The platform library provides sockets, DNS, the CSPRNG, and hardware crypto."
fi

if [ -n "$(ls -A "$PROJ" 2>/dev/null)" ]; then
    echo "error: $PROJ is not empty; run create in an empty directory" >&2
    exit 1
fi

# Escape characters that are special in sed replacements.
esc() { printf '%s' "$1" | sed -e 's/[&|\\]/\\&/g'; }

render() {
    local src="$1" dst="$2"
    sed -e "s|{{CODEONE_HOME}}|$(esc "$TC")|g" \
        -e "s|{{PROJECT_NAME}}|$(esc "$NAME")|g" \
        -e "s|{{APP_NAME}}|$(esc "$APP")|g" \
        -e "s|{{KIND}}|$(esc "$KIND")|g" \
        -e "s|{{ARTIFACT}}|$(esc "$ARTIFACT")|g" \
        -e "s|{{BUILD_FLAGS}}|$(esc "$BUILD_FLAGS")|g" \
        -e "s|{{ENTRY_FILE}}|$(esc "$ENTRY_FILE")|g" \
        -e "s|{{ENTRY_FN}}|$(esc "$ENTRY_FN")|g" \
        -e "s|{{ENTRY_RULE}}|$(esc "$ENTRY_RULE")|g" \
        -e "s|{{ENTRY_DOC}}|$(esc "$ENTRY_DOC")|g" \
        -e "s|{{RUN_DOC}}|$(esc "$RUN_DOC")|g" \
        -e "s|{{VERIFY_DOC}}|$(esc "$VERIFY_DOC")|g" \
        -e "s|{{PLATFORM_EXTRA}}|$(esc "$PLATFORM_EXTRA")|g" \
        "$src" > "$dst"
}

render "$TEMPLATES/agents.md"      "$PROJ/agents.md"
render "$SRC_TEMPLATE"             "$PROJ/$ENTRY_FILE"
render "$TEMPLATES/Makefile-$MODE" "$PROJ/Makefile"
render "$TEMPLATES/gitignore"      "$PROJ/.gitignore"

# Build the template to verify the toolchain actually works.
if command -v make >/dev/null 2>&1; then
    make -C "$PROJ" >/dev/null
else
    "$TC/bin/c1" "$PROJ/$ENTRY_FILE" $BUILD_FLAGS -o "$PROJ/$ARTIFACT" >/dev/null
fi

if [ "$MODE" = "static" ]; then
    if [ ! -s "$PROJ/$ARTIFACT" ]; then
        echo "error: build did not produce $PROJ/$ARTIFACT" >&2
        exit 1
    fi
else
    OUT="$("$PROJ/$APP")"
    case "$OUT" in
        *"Hello, World!"*) ;;
        *)
            echo "warning: unexpected program output: $OUT" >&2
            ;;
    esac
fi

echo "Created codeone $KIND: $PROJ"
echo "Artifact:                   $PROJ/$ARTIFACT"
echo "Toolchain:                  $TC"
echo ""
echo "Next:"
if [ "$MODE" = "static" ]; then
    echo "  make"
    echo "  # link it from another program:"
    echo "  $TC/bin/c1 consumer.bx -L $PROJ -l $APP -O2 -o consumer"
else
    echo "  make run"
fi
