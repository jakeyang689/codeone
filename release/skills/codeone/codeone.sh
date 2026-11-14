#!/bin/bash
# codeone skill command dispatcher.
#
# Usage: codeone.sh [help|create] ...

set -euo pipefail

SKILL_DIR="$(cd "$(dirname "$0")" && pwd -P)"
TOOLCHAIN="$HOME/.codeone/toolchain"

usage() {
    cat <<EOF
codeone — create and develop applications in codeone SSA (.bx)

Usage:
  codeone help      Show this help.
  codeone create <app-name> [--bin|--static]
                    Scaffold a new project in the current directory. Run it
                    inside an empty directory; the project name is the
                    directory name and <app-name> names the built artifact.
                    --bin (default) builds the executable <app-name>;
                    --static builds the static library lib<app-name>.a.

create writes:
  agents.md    project instructions with the absolute toolchain path
  main.bx      Hello World entry point (lib.bx with --static)
  Makefile     make / make run / make clean (no make run with --static)
  .gitignore

Requirements:
  The codeone toolchain must be installed at:
    $TOOLCHAIN
  Install it from the codeone repository first:
    ./build.sh && ./install.sh

After creating a project (you are already inside it):
  make run          (--bin, the default)
  make              (--static; produces lib<app-name>.a)

Developing:
  All application code is .bx (SSA). Keep main.bx (or lib.bx for --static) as
  the entry file and put logic in separate <module>.bx files (the Makefile
  compiles every *.bx).
  Cross-module calls need an extern prototype in the calling file.
  Compile with:
    $TOOLCHAIN/bin/c1 main.bx <boxes...> -O2 -o <app-name>
  Link the platform library for sockets, TLS, randomness, or hardware crypto:
    $TOOLCHAIN/bin/c1 main.bx <boxes...> \\
      -L $TOOLCHAIN/lib -l platform -O2 -o <app-name>
  Language documentation:
    $TOOLCHAIN/docs/ssa.md    SSA language specification
    $TOOLCHAIN/docs/box.md    standard library modules
    $TOOLCHAIN/docs/agents.md SSA coding rules
    $TOOLCHAIN/docs/cmap.md   pseudo-C view of SSA
EOF
}

cmd="${1:-help}"
case "$cmd" in
    help|-h|--help)
        usage
        ;;
    create)
        shift
        exec bash "$SKILL_DIR/create.sh" "$@"
        ;;
    *)
        echo "error: unknown command: $cmd" >&2
        echo "" >&2
        usage >&2
        exit 2
        ;;
esac
