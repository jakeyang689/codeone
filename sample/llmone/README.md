# llmone — a language model trained and sampled entirely in SSA

`llmone` is a decoder-only (GPT-style) transformer written directly in
codeone SSA: forward pass, backpropagation, Adam and the training loop are all
`.bx` code. The matrix multiplies go through the `tensor` instruction, so they
are lowered to MLIR `linalg` kernels by the compiler.

Two model sizes are generated from the same sources:

| config | layers | d_model | heads | context | params |
|--------|--------|---------|-------|---------|--------|
| `tiny` | 2      | 64      | 4     | 32      | 118,528 |
| `big`  | 14     | 768     | 12    | 64      | 99,477,504 |

The matmuls use the `tensor` instruction that [`sample/numone`](../numone/)
demonstrates; `make test` verifies the MLIR kernel against numone's scalar
matmul loop.

`tiny` exists so the whole pipeline (training, checkpointing, sampling,
gradient check) runs in seconds on a laptop. `big` is the ~100M-parameter
model. A config is baked in at compile time because `tensor` shapes must be
integer literals; `tools/gen.sh` substitutes every dimension and buffer offset
into copies of the `*.tmpl.bx` sources under `build/<cfg>/`.

## Quick start

```sh
./tools/fetch-corpus.sh          # TinyShakespeare, ~1.1 MB
make test                        # kernels + data layer checks
make CFG=tiny gradcheck          # finite-difference check of the backward pass
make CFG=tiny train STEPS=600    # trains, writes llmone-ckpt.bin
make CFG=tiny gen-text PROMPT="ROMEO:" TOKENS=400
```

For the 100M model:

```sh
make CFG=big build               # compile ~99.5M parameters
make CFG=big gradcheck           # a few forward/backward passes
make CFG=big train STEPS=200
make CFG=big gen-text PROMPT="ROMEO:" TOKENS=400
```

Checkpoints and the binary live in `build/<cfg>/`, so the two configs do not
overwrite each other. `make clean` removes `build/`; the corpus is kept.

## Layout

| file | role |
|------|------|
| `math.bx` (no template) | `exp`, `log`, `sqrt`, `tanh`, `gelu` from f32 arithmetic only — there is no libm dependency |
| `nn.bx` (no template) | elementwise, layernorm, softmax, cross-entropy, attention helper kernels, Adam, embedding lookup |
| `data.bx` (no template) | file read/write, xorshift32 PRNG, batch sampling, checkpoint save/load |
| `model.tmpl.bx` | parameter init, `block_fwd` / `forward`, `block_bwd` / `backward` |
| `main.tmpl.bx` | CLI: `train`, `gen`, `gradcheck` |
| `test/kernel_test.bx` | 22 numeric checks of the math and NN kernels |
| `test/data_test.bx` | file I/O, PRNG, batch and checkpoint checks |
| `test/matmul_ref_test.bx` | cross-checks the `tensor` matmul against `sample/numone`'s scalar reference |
| `tools/gen.sh` | config generation with `sed` |
| `tools/fetch-corpus.sh` | corpus download (`--sample` for an offline text) |

## Model

Pre-norm transformer, tied input/output embeddings, learned absolute position
embeddings, no dropout:

```
x  = WTE[token] + WPE[pos]
for each layer:
    h  = LN1(x);  q,k,v = h·Wqkv + bqkv;  causal per-head attention
    x  = x + attn·Wo + bo
    h2 = LN2(x);  x = x + gelu(h2·W1 + b1)·W2 + b2
logits = LN_f(x)·WTE^T
```

Weights are stored `[in, out]`, so the forward matmuls need no transpose.
Activations for every layer are kept so the backward pass can reuse them.
The cross-entropy sums over the `B·T` tokens of a step and the accumulated
gradient is divided by `B·T`, so the effective loss is the mean per token.

There is no integer↔float conversion in SSA, so the kernels receive
precomputed reciprocals as `f32` (`inv_n`), and float constants are built from
i32 bit patterns. Token ids are plain bytes, i.e. vocabulary size 256: no
tokenizer file is needed, and the same code trains on any byte stream.

## CLI

```
llmone train [corpus] [steps]   # default corpus/tinyshakespeare.txt, STEPS from config
llmone gen [prompt] [tokens]    # default prompt "The ", 200 tokens
llmone gradcheck [corpus]       # numerical vs analytic gradients
```

`train` resumes from `llmone-ckpt.bin` when it exists, writes a checkpoint
every 250 steps and once at the end. `gradcheck` perturbs eight parameters
(the largest analytic gradient, the final layernorm gains and biases, and the
first MLP bias) by ±0.02 and compares the central difference with the
backpropagated value; it returns non-zero if any relative error exceeds 20%.

## Measured

TinyShakespeare, Apple Silicon, `-O2`:

| config | step time | loss, step 1 | later | checkpoint |
|--------|-----------|--------------|-------|------------|
| tiny (B=2, T=32) | ~120 ms | 5.54 / token | 2.79 at step 600, ~2.5–2.6 at 3000 | 474 KB |
| big (B=1, T=64)  | ~101 s  | 5.52 / token | 3.90 at step 5 | 398 MB |

At 3000 steps the `tiny` model has only learned the shape of the text — it
emits mostly spaces, newlines and "the" — which is what a 118K-parameter model
sees in an hour of CPU training:

```
$ make CFG=tiny gen-text PROMPT="ROMEO:" TOKENS=120
ROMEO:

I the I I I I I I I I I I I I I thathathathe the the the the ...
```

The big model runs one token sequence per step in plain f32 loops, so a step is
CPU-bound; a full convergence run is a multi-day job. `gradcheck` on `big`
performs 16 forward and one backward pass, roughly 15 minutes; use `tiny` for
iterating on the code.

## Limits

This is a readable sample, not a training framework. It is single-threaded,
uses f32 everywhere, has no dropout, weight decay or LR schedule, samples one
sequence per training step (further samples would be looped with gradient
accumulation), and keeps the optimizer state in memory. The learning rate and
step count are compile-time tokens in `tools/gen.sh`.
