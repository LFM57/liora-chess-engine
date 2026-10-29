# Liora Chess Engine

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Language: C](https://img.shields.io/badge/Language-C11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![UCI Protocol](https://img.shields.io/badge/Protocol-UCI-brightgreen.svg)](https://www.chessprogramming.org/UCI)

**Liora** is a high-performance, open-source chess engine written in C. It supports the standard Universal Chess Interface (UCI) protocol and can be integrated with popular chess GUIs such as Cutechess, Arena, BanksiaGUI, and Nibbler.

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
  - **NNUE (Efficiently Updatable Neural Network)**: HalfKP-style feature accumulator with incremental updates during move making/unmaking.
  - **Handcrafted Evaluation (HCE)**: Automatic robust fallback when no NNUE net is loaded:
    - Tapered evaluation interpolating midgame and endgame phases.
    - Piece-square tables (PSQT).
    - Bishop pair bonuses.
    - Pawn structure evaluation (passed, isolated, doubled pawns).
    - King safety penalties and pawn shields.

### Data Generation & Training
- Built-in self-play data generation pipeline (`RunDataGen`) with tactical noise filtering.
- PyTorch training workflow (`train.py`) exporting quantized binary weights directly compatible with Liora.

---

## Building Liora

### Prerequisites
- GCC / Clang with C11 support
- Make

### Compilation (Linux & macOS)
```bash
make
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
gcc -O3 -Wall -Wextra board.c main.c nnue.c polyglot.c search.c -lm -o liora.exe
```

---

## UCI Configuration Options

| Option | Type | Default | Description |
| :--- | :---: | :---: | :--- |
| `Hash` | `spin` | `64` | Transposition table size in megabytes (1 to 4096 MB). |
| `BookPath` | `string` | `<empty>` | Filepath to an external Polyglot opening book (`.bin`). |
| `EvalFile` | `string` | `liora.nnue` | Path to the neural network file. Falls back to HCE if missing. |

---

## Benchmark & Tournament Results

### Liora v11.0 F vs. Liora v10.4 C (Pure HCE, 8moves_v3 book, paired openings)

| Time Control | Games | Score (+ / = / -) | Win Rate | Elo Gain | LOS |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Ultra-Bullet (15s + 0.1s)** | 622 | 431 - 116 - 75 | **78.6%** | **+226.2 ± 28.4 Elo** | **100.0%** |
| **Bullet (60s + 0s)** | 515 | 360 - 104 - 51 | **80.0%** | **+240.8 ± 31.2 Elo** | **100.0%** |

---

## Author

* **LFM** (Louis Simonet)

## License

This project is licensed under the MIT License.
