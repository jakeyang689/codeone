#!/bin/sh
# Downloads a byte-level training corpus into ../corpus/.
#
#   ./tools/fetch-corpus.sh            # TinyShakespeare (~1.1 MB)
#   ./tools/fetch-corpus.sh --sample   # tiny built-in text, no network
#   CORPUS_URL=... ./tools/fetch-corpus.sh
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/corpus"
mkdir -p "$OUT"

if [ "${1:-}" = "--sample" ]; then
  cat > "$OUT/sample.txt" <<'EOF'
The quick brown fox jumps over the lazy dog.
A small model learns to predict the next byte of this text.
Training a language model means minimising the cross entropy
between the predicted distribution and the actual next token.
codeone compiles SSA directly, and the tensor instruction lowers
to MLIR linalg kernels. Every weight in this file is a float.
EOF
  echo "wrote $OUT/sample.txt ($(wc -c < "$OUT/sample.txt") bytes)"
  exit 0
fi

URL=${CORPUS_URL:-https://raw.githubusercontent.com/karpathy/char-rnn/master/data/tinyshakespeare/input.txt}
DEST="$OUT/tinyshakespeare.txt"

echo "downloading $URL"
if curl -fL --retry 3 -o "$DEST.part" "$URL"; then
  mv "$DEST.part" "$DEST"
  echo "wrote $DEST ($(wc -c < "$DEST") bytes)"
else
  rm -f "$DEST.part"
  echo "download failed; run ./tools/fetch-corpus.sh --sample for an offline corpus" >&2
  exit 1
fi
