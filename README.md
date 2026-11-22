# Liora Chess Engine

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Language: C](https://img.shields.io/badge/Language-C11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![UCI Protocol](https://img.shields.io/badge/Protocol-UCI-brightgreen.svg)](https://www.chessprogramming.org/UCI)

**Liora** is a high-performance, open-source chess engine written in C. It supports the standard Universal Chess Interface (UCI) protocol and can be integrated with popular chess GUIs such as Cutechess, Arena, BanksiaGUI, and Nibbler.

Flagship version, [v12.0 F](https://github.com/LFM57/liora-chess-engine/releases/tag/v12.0_F), introduces a rebuilt `(768 -> 256)x2` SCReLU NNUE evaluation (Gen 5) that scores **+432.7 ± 33.0 Elo** over [v11.13 C](https://github.com/LFM57/liora-chess-engine/releases/tag/v11.13_C) HCE (`923.5 / 1000`, `92.35%` win rate across 1,000 Bullet/Ultra-Bullet games), following [v11.13 C](https://github.com/LFM57/liora-chess-engine/releases/tag/v11.13_C)'s `+84.3 ± 17.9` Elo gain over [v11.0 F](https://github.com/LFM57/liora-chess-engine/releases/tag/v11.0_F) (`2569 ± 18` official [CCRL Blitz Rating](https://computerchess.org.uk/404/cgi/engine_details.cgi?eng=Liora%2011.0%20F%2064-bit)).

---

## Features

### Board Representation & Move Generation
- **Bitboards (64-bit)**: Fast bitwise operations for piece locations, pawn pushes, and attack masks.
- **Magic Bitboards**: Precomputed magic multiplier hashing for ultra-fast sliding piece ray attacks (Rooks and Bishops).
- **Zobrist Hashing**: Polyglot-compliant position keys for fast transposition indexing and threefold repetition detection.
- **Opening Books**: Native support for Polyglot opening books (`.bin`).

### Search Techniques
- **Principal Variation Search (PVS)** within an iterative deepening framework.
- **Aspiration Windows**: Exponential window widening to reduce node tree overhead.
- **Transposition Table (TT)**:
  - Memory-packed 16-byte `TTEntry` structures for high cache locality.
  - Generational entry aging policy (`TTAge`) to prevent hash table paralysis.
- **Pruning & Reductions**:
  - Null Move Pruning with adaptive reduction $R$.
  - Static Exchange Evaluation (SEE) pruning for quiet moves and bad captures.
  - Dynamic Late Move Reductions (LMR) based on precomputed logarithmic tables, history heuristic, and killer moves.
  - Forward Futility Pruning.
- **Quiescence Search**:
  - Tactical resolution with capture and check evasions.
  - Stand-pat delta pruning.
- **Move Ordering**:
  - Hash move (PV move)
  - MVV-LVA (Most Valuable Victim – Least Valuable Attacker)
  - Killer move heuristic (2 slots per search depth)
  - History heuristic table

### Evaluation
- **Hybrid Evaluation Architecture**:
  - **NNUE (Efficiently Updatable Neural Network)**: `(768 -> 256)x2 -> 1` SCReLU architecture (12 piece types including both kings) with lazy, fused AVX2 incremental accumulator updates.
  - **Handcrafted Evaluation (HCE)**: Automatic robust fallback when no NNUE net is loaded:
    - Tapered evaluation interpolating midgame and endgame phases.
    - Piece-square tables (PSQT).
    - Bishop pair bonuses.
    - Pawn structure evaluation (passed, isolated, doubled pawns).
    - King safety penalties and pawn shields.

### Data Generation & Training
- Built-in self-play data generation pipeline (`RunDataGen`) with quiet-position and tactical noise filtering.
- PyTorch training workflow (`train.py`) with in-check filtering, game-level validation splitting, cosine LR annealing, and quantized SCReLU binary export directly compatible with Liora.

---

## Building Liora

### Prerequisites
- GCC / Clang with C11 support
- Make

### Compilation (Linux & macOS)
```bash
make
```

By default, `EMBED_NNUE=1` embeds `../NNUE Weights/Gen 5/liora.nnue`. To build without an embedded network or with a custom `(768 -> 256)x2` SCReLU `.nnue` file:
```bash
make EMBED_NNUE=0
make EMBED_NNUE=1 NNUE_FILE=path/to/liora.nnue
```

To build both Linux and Windows distribution binaries in `../dist/`:
```bash
make dist
```

To clean build artifacts:
```bash
make clean
```

### Compilation (Windows)
Using MinGW / MSYS2:
```bash
mingw32-make
```
Or directly with GCC:
```bash
gcc -O3 -Wall -Wextra board.c main.c nnue.c nnue_embed.c polyglot.c search.c -lm -o liora.exe
```

---

## UCI Configuration Options

| Option | Type | Default | Description |
| :--- | :---: | :---: | :--- |
| `Hash` | `spin` | `64` | Transposition table size in megabytes (1 to 4096 MB). |
| `BookPath` | `string` | `<empty>` | Filepath to an external Polyglot opening book (`.bin`). |
| `UseNNUE` | `check` | `true` (if embedded) | Enable NNUE evaluation (`true` by default when built with `EMBED_NNUE=1`, `false` otherwise). |
| `EvalFile` | `string` | `<embedded>` | Path to a `(768 -> 256)x2` SCReLU `.nnue` network (`<embedded>` when built with `EMBED_NNUE=1`, `<empty>` otherwise). Falls back to HCE if missing. |

## License

This project is licensed under the MIT License.
