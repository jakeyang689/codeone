#!/bin/sh
# Generates build/<cfg>/*.bx from the *.tmpl.bx templates.
#
# The tensor instruction requires integer-literal shapes, so a model size is
# fixed at compile time. This script substitutes the dims and every derived
# offset into the sources, which keeps one copy of the model code for both the
# tiny validation config and the ~100M-parameter config.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)

case "${1:-}" in
  tiny) V=256 D=64  H=4  L=2  T=32 B=2;  LR=0.003;  STEPS=400 ;;
  big)  V=256 D=768 H=12 L=14 T=64 B=1;  LR=0.0004; STEPS=20000 ;;
  *) echo "usage: $0 tiny|big" >&2; exit 2 ;;
esac

DH=$((D / H))
FF=$((4 * D))
INVD=$(awk "BEGIN{printf \"%.10f\", 1/$D}")
INVFF=$(awk "BEGIN{printf \"%.10f\", 1/$FF}")
INVT=$(awk "BEGIN{printf \"%.10f\", 1/$T}")
INVBT=$(awk "BEGIN{printf \"%.10f\", 1/($B*$T)}")

# ---- parameter layout (floats) ----------------------------------------
# 0 .. N_WTE+N_WPE: embeddings, then L layer blocks, then the final LN.
cur=0
OFF_LN1G=$cur; cur=$((cur + D))
OFF_LN1B=$cur; cur=$((cur + D))
OFF_WQKV=$cur; cur=$((cur + 3 * D * D))
OFF_BQKV=$cur; cur=$((cur + 3 * D))
OFF_WO=$cur;   cur=$((cur + D * D))
OFF_BO=$cur;   cur=$((cur + D))
OFF_LN2G=$cur; cur=$((cur + D))
OFF_LN2B=$cur; cur=$((cur + D))
OFF_W1=$cur;   cur=$((cur + D * FF))
OFF_B1=$cur;   cur=$((cur + FF))
OFF_W2=$cur;   cur=$((cur + FF * D))
OFF_B2=$cur;   cur=$((cur + D))
PL=$cur

N_WTE=$((V * D))
N_WPE=$((T * D))
N_LN_BASE=$((N_WTE + N_WPE))
N_LAYERS=$((L * PL))
LN_BASE=$((N_WTE + N_WPE + L * PL))
N_PARAMS=$((LN_BASE + 2 * D))

# ---- per-layer activation layout (floats) -----------------------------
ACUR=0
A_X=$ACUR;    ACUR=$((ACUR + T * D))
A_H1=$ACUR;   ACUR=$((ACUR + T * D))
A_QKV=$ACUR;  ACUR=$((ACUR + 3 * T * D))
A_PROB=$ACUR; ACUR=$((ACUR + H * T * T))
A_ATT=$ACUR;  ACUR=$((ACUR + T * D))
A_RES1=$ACUR; ACUR=$((ACUR + T * D))
A_H2=$ACUR;   ACUR=$((ACUR + T * D))
A_FFP=$ACUR;  ACUR=$((ACUR + T * FF))
A_FF=$ACUR;   ACUR=$((ACUR + T * FF))
A_RES2=$ACUR; ACUR=$((ACUR + T * D))
AL=$ACUR
N_ACTS=$((L * AL))

# ---- workspace scratch layout (floats) --------------------------------
# Reused across layers; one buffer, fixed offsets.
W=0
W_WT=$W;     W=$((W + 4 * D * D))               # transposed weight scratch
W_DW=$W;     W=$((W + 4 * D * D))               # weight-gradient scratch
W_TMPVD=$W;  W=$((W + V * D))                   # [D,V] head-gradient temp
W_AT=$W;     W=$((W + (FF > D ? FF : D) * T))   # transposed activation
W_QH=$W;     W=$((W + T * DH))
W_KH=$W;     W=$((W + T * DH))
W_VH=$W;     W=$((W + T * DH))
W_VT=$W;     W=$((W + T * DH))
W_DOH=$W;    W=$((W + T * DH))
W_DZ=$W;     W=$((W + T * DH))
W_DS=$W;     W=$((W + T * T))
W_SCR=$W;    W=$((W + T * T))
W_ST=$W;     W=$((W + T * T))
W_MEAN=$W;   W=$((W + T))
W_RSTD=$W;   W=$((W + T))
W_EMB=$W;    W=$((W + T * D))
W_LNF=$W;    W=$((W + T * D))
W_PROJ=$W;   W=$((W + T * D))
W_MLP=$W;    W=$((W + T * D))
W_CUR=$W;    W=$((W + T * D))
W_DX=$W;     W=$((W + T * D))
W_DH1=$W;    W=$((W + T * D))
W_DATT=$W;   W=$((W + T * D))
W_DH2=$W;    W=$((W + T * D))
W_DFF1=$W;   W=$((W + T * FF))
W_DFF2=$W;   W=$((W + T * FF))
W_DQKV=$W;   W=$((W + 3 * T * D))
W_LOGITS=$W; W=$((W + T * V))
W_DLOGITS=$W; W=$((W + T * V))
N_WS=$W

OUT="$ROOT/build/$1"
mkdir -p "$OUT"

subst() {
  sed \
    -e "s/__V__/${V}/g" \
    -e "s/__D__/${D}/g" \
    -e "s/__H__/${H}/g" \
    -e "s/__DH__/${DH}/g" \
    -e "s/__T__/${T}/g" \
    -e "s/__B__/${B}/g" \
    -e "s/__FF__/${FF}/g" \
    -e "s/__L__/${L}/g" \
    -e "s/__DF__/${D}.0/g" \
    -e "s/__DHF__/${DH}.0/g" \
    -e "s/__TFF__/${T}.0/g" \
    -e "s/__INVD__/${INVD}/g" \
    -e "s/__INVFF__/${INVFF}/g" \
    -e "s/__INVT__/${INVT}/g" \
    -e "s/__INVBT__/${INVBT}/g" \
    -e "s/__LR__/${LR}/g" \
    -e "s/__STEPS__/${STEPS}/g" \
    -e "s/__OFF_LN1G__/${OFF_LN1G}/g" \
    -e "s/__OFF_LN1B__/${OFF_LN1B}/g" \
    -e "s/__OFF_WQKV__/${OFF_WQKV}/g" \
    -e "s/__OFF_BQKV__/${OFF_BQKV}/g" \
    -e "s/__OFF_WO__/${OFF_WO}/g" \
    -e "s/__OFF_BO__/${OFF_BO}/g" \
    -e "s/__OFF_LN2G__/${OFF_LN2G}/g" \
    -e "s/__OFF_LN2B__/${OFF_LN2B}/g" \
    -e "s/__OFF_W1__/${OFF_W1}/g" \
    -e "s/__OFF_B1__/${OFF_B1}/g" \
    -e "s/__OFF_W2__/${OFF_W2}/g" \
    -e "s/__OFF_B2__/${OFF_B2}/g" \
    -e "s/__PL__/${PL}/g" \
    -e "s/__LM1__/$((L - 1))/g" \
    -e "s/__N_3D__/$((3 * D))/g" \
    -e "s/__N_2D__/$((2 * D))/g" \
    -e "s/__N_D2__/$((D * D))/g" \
    -e "s/__N_WQKV__/$((3 * D * D))/g" \
    -e "s/__N_W1__/$((D * FF))/g" \
    -e "s/__N_FF__/${FF}/g" \
    -e "s/__N_QKV__/$((3 * T * D))/g" \
    -e "s/__N_TD__/$((T * D))/g" \
    -e "s/__N_TFF__/$((T * FF))/g" \
    -e "s/__N_TT__/$((T * T))/g" \
    -e "s/__N_TDH__/$((T * DH))/g" \
    -e "s/__N_HTT__/$((H * T * T))/g" \
    -e "s/__N_TV__/$((T * V))/g" \
    -e "s/__N_VD__/$((V * D))/g" \
    -e "s/__N_BT__/$((B * T))/g" \
    -e "s/__N_PARAMS_BYTES__/$((N_PARAMS * 4))/g" \
    -e "s/__N_ACTS_BYTES__/$((N_ACTS * 4))/g" \
    -e "s/__N_WS_BYTES__/$((N_WS * 4))/g" \
    -e "s/__N_WTE__/${N_WTE}/g" \
    -e "s/__N_WPE__/${N_WPE}/g" \
    -e "s/__N_EMB__/$((N_WTE + N_WPE))/g" \
    -e "s/__LN_BASE__/${LN_BASE}/g" \
    -e "s/__N_LAYERS__/${N_LAYERS}/g" \
    -e "s/__N_PARAMS__/${N_PARAMS}/g" \
    -e "s/__A_X__/${A_X}/g" \
    -e "s/__A_H1__/${A_H1}/g" \
    -e "s/__A_QKV__/${A_QKV}/g" \
    -e "s/__A_PROB__/${A_PROB}/g" \
    -e "s/__A_ATT__/${A_ATT}/g" \
    -e "s/__A_RES1__/${A_RES1}/g" \
    -e "s/__A_H2__/${A_H2}/g" \
    -e "s/__A_FFP__/${A_FFP}/g" \
    -e "s/__A_FF__/${A_FF}/g" \
    -e "s/__A_RES2__/${A_RES2}/g" \
    -e "s/__AL__/${AL}/g" \
    -e "s/__N_ACTS__/${N_ACTS}/g" \
    -e "s/__W_WT__/${W_WT}/g" \
    -e "s/__W_DW__/${W_DW}/g" \
    -e "s/__W_TMPVD__/${W_TMPVD}/g" \
    -e "s/__W_AT__/${W_AT}/g" \
    -e "s/__W_QH__/${W_QH}/g" \
    -e "s/__W_KH__/${W_KH}/g" \
    -e "s/__W_VH__/${W_VH}/g" \
    -e "s/__W_VT__/${W_VT}/g" \
    -e "s/__W_DOH__/${W_DOH}/g" \
    -e "s/__W_DZ__/${W_DZ}/g" \
    -e "s/__W_DS__/${W_DS}/g" \
    -e "s/__W_SCR__/${W_SCR}/g" \
    -e "s/__W_ST__/${W_ST}/g" \
    -e "s/__W_MEAN__/${W_MEAN}/g" \
    -e "s/__W_RSTD__/${W_RSTD}/g" \
    -e "s/__W_EMB__/${W_EMB}/g" \
    -e "s/__W_LNF__/${W_LNF}/g" \
    -e "s/__W_PROJ__/${W_PROJ}/g" \
    -e "s/__W_MLP__/${W_MLP}/g" \
    -e "s/__W_CUR__/${W_CUR}/g" \
    -e "s/__W_DX__/${W_DX}/g" \
    -e "s/__W_DH1__/${W_DH1}/g" \
    -e "s/__W_DATT__/${W_DATT}/g" \
    -e "s/__W_DH2__/${W_DH2}/g" \
    -e "s/__W_DFF1__/${W_DFF1}/g" \
    -e "s/__W_DFF2__/${W_DFF2}/g" \
    -e "s/__W_DQKV__/${W_DQKV}/g" \
    -e "s/__W_LOGITS__/${W_LOGITS}/g" \
    -e "s/__W_DLOGITS__/${W_DLOGITS}/g" \
    -e "s/__N_WS__/${N_WS}/g" \
    "$1"
}

for f in "$ROOT"/*.bx "$ROOT"/*.tmpl.bx; do
  [ -e "$f" ] || continue
  base=$(basename "$f")
  base=${base%.tmpl.bx}
  base=${base%.bx}
  subst "$f" > "$OUT/$base.bx"
done

echo "generated $OUT: V=$V D=$D H=$H L=$L T=$T B=$B params=$N_PARAMS acts=$N_ACTS ws=$N_WS"
