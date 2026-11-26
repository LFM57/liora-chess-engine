# Liora Chess Engine

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Language: C](https://img.shields.io/badge/Language-C11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard_revision))
[![UCI Protocol](https://img.shields.io/badge/Protocol-UCI-brightgreen.svg)](https://www.chessprogramming.org/UCI)

**Liora** is a fast, open-source UCI chess engine written in C (~3100 Elo unofficial, NNUE-powered). It works with popular chess GUIs such as Cutechess, Arena, BanksiaGUI, and Nibbler, and supports calibrated Elo strength limiting (`1200`–`3100`) alongside 6 distinct playing personalities.

Flagship [v12.0 F](https://github.com/LFM57/liora-chess-engine/releases/tag/v12.0_F) introduced a rebuilt `(768 -> 256)x2 -> 1` SCReLU NNUE scoring $`+432.7 \pm 33.0`$ Elo over [v11.13 C](https://github.com/LFM57/liora-chess-engine/releases/tag/v11.13_C) HCE, following [v11.13 C](https://github.com/LFM57/liora-chess-engine/releases/tag/v11.13_C)'s $`+84.3 \pm 17.9`$ Elo gain over [v11.0 F](https://github.com/LFM57/liora-chess-engine/releases/tag/v11.0_F) ($`2569 \pm 18`$ official [CCRL Blitz Rating](https://computerchess.org.uk/404/cgi/engine_details.cgi?eng=Liora%2011.0%20F%2064-bit)).

---

## Quick Start

### 1. Download or Build
Download a prebuilt binary from the [GitHub Releases](https://github.com/LFM57/liora-chess-engine/releases) page, or compile from source (requires GCC/Clang and `make`):

```bash
# Linux & macOS (embeds the default Gen 5 NNUE network)
make

# Build without an embedded network or with a custom .nnue file
make EMBED_NNUE=0
make EMBED_NNUE=1 NNUE_FILE=path/to/liora.nnue

# Build both Linux and Windows release binaries in ../dist/
make dist

# Clean build artifacts
make clean
```

On Windows (MinGW / MSYS2):
```bash
mingw32-make
# Or directly:
gcc -O3 -Wall -Wextra board.c main.c nnue.c nnue_embed.c polyglot.c search.c -lm -o liora.exe
```

### 2. Load in Any UCI GUI
Add the `liora` / `liora.exe` binary as a UCI engine in your chess GUI and configure any options below.

### 3. UCI Configuration Options

| Option | Type | Default | Description |
| :--- | :---: | :---: | :--- |
| `Hash` | `spin` | `64` | Transposition table size in megabytes (`1` to `4096` MB). |
| `BookPath` | `string` | `<empty>` | Filepath to an external Polyglot opening book (`.bin`). |
| `UseNNUE` | `check` | `true` (if embedded) | Enable NNUE evaluation (`true` by default when built with `EMBED_NNUE=1`, `false` otherwise). |
| `EvalFile` | `string` | `<embedded>` | Path to a `(768 -> 256)x2` SCReLU `.nnue` network (`<embedded>` when built with `EMBED_NNUE=1`, `<empty>` otherwise). Falls back to HCE if missing. |
| `UCI_LimitStrength` | `check` | `false` | Limit playing strength to `UCI_Elo` using calibrated node/depth caps, WDL Softmax move sampling, capped QSearch, and low-Elo evaluation noise. |
| `UCI_Elo` | `spin` | `1500` | Target playing strength in Elo (`1200` to `3100`) when `UCI_LimitStrength` is enabled (`3100` corresponds to full strength). |
| `MultiPV` | `spin` | `1` | Number of principal variations (top lines) to compute and display during analysis (`1` to `256`). Ignored when `UCI_LimitStrength` is enabled. |
| `Personality` | `combo` | `Standard` | Playing style preset (`Standard`, `Aggressive`, `Defensive`, `Positional`, `Endgame`, `Savage`). Composes orthogonally with `UCI_LimitStrength` / `UCI_Elo`. |

---

## Personalities

Liora's 3-layer hybrid personality system adjusts material-vs-positional balance, bitboard style features, and draw contempt on top of its NNUE. Every personality works both at full strength and combined with `UCI_LimitStrength` / `UCI_Elo` (for example, `UCI_Elo = 1500` + `Personality = Savage`).

| Personality | Playstyle Summary |
| :--- | :--- |
| **`Standard`** | Default objective NNUE play with zero overhead and full playing strength. |
| **`Aggressive`** | Keeps queens and pieces on the board, pushes pieces into enemy territory, builds strong attacks against the opposing king, avoids draws, and willingly plays sharp gambits. |
| **`Defensive`** | Prioritizes material and king shelter, keeps its pawn shield intact, trades down early to neutralize threats, and gladly accepts solid draws. |
| **`Positional`** | Emphasizes long-term positional structure over raw material — prizes the bishop pair, clean pawn structure, and passed pawns, and is willing to invest material for lasting positional compensation. |
| **`Endgame`** | Actively liquidates pieces and queens to steer into technical endgames, where it strongly values passed pawns, king centralization, and clean structure. |
| **`Savage`** | All-out tactical chaos: heavily prioritizes king-ring pressure, forward piece storms, and aggressive pawn pushes (even opening its own king shield), shuns draws, and frequently sacrifices material for the initiative. |

---

## Technical Details

### Board Representation & Move Generation
- **Bitboards (64-bit)**: Fast bitwise operations for piece locations, pawn pushes, and attack masks.
- **Magic Bitboards**: Precomputed magic multiplier hashing for ultra-fast sliding piece ray attacks (Rooks and Bishops).
- **Zobrist Hashing**: Polyglot-compliant position keys for transposition indexing and threefold repetition detection.
- **Opening Books**: Native support for Polyglot opening books (`.bin`) with weight-proportional move sampling.

### Search Techniques
- **Principal Variation Search (PVS)** within an iterative deepening framework.
- **Aspiration Windows**: Exponential window widening to reduce tree overhead.
- **Transposition Table (TT)**:
  - Memory-packed 16-byte `TTEntry` structures in 4-way clusters for high cache locality.
  - Generational entry aging policy (`TTAge`) and automatic TT clearing on eval/strength/personality changes.
- **Pruning & Reductions**:
  - Reverse Futility Pruning (RFP) & Forward Futility Pruning.
  - Adaptive Null Move Pruning.
  - Static Exchange Evaluation (SEE) pruning for quiet moves and losing captures.
  - Dynamic Late Move Reductions (LMR) with history heuristic and killer moves.
- **Quiescence Search**: Tactical resolution with capture/check evasions, delta pruning, and SEE pruning.
- **Move Ordering**: TT move, promotions, SEE-classified MVV-LVA captures, killer moves (2 slots/ply), and clamped history heuristic.
- **Strength & Analysis Modes**: Single-pass windowed root search with WDL-space Softmax and recapture protection for `UCI_LimitStrength`, plus dedicated `SearchRootMultiPV` for multi-line analysis.

### Evaluation
- **NNUE (Efficiently Updatable Neural Network)**: `(768 -> 256)x2 -> 1` SCReLU architecture (12 piece types including both kings) with lazy, fused AVX2 incremental accumulator updates.
- **3-Layer Hybrid Personality Layer**: Zero-cost bypass for `Standard`; NNUE material/positional decomposition, clamped bitboard style modifiers, and phase-scaled repetition contempt for non-standard presets.
- **Handcrafted Evaluation (HCE)**: Automatic fallback when no NNUE net is loaded (tapered MG/EG PSQT, bishop pair, pawn structure, open files, mobility, and king safety).

### Data Generation & Training
- Built-in self-play data generation pipeline (`RunDataGen`) with quiet-position and tactical noise filtering (automatically isolated from `UCI_LimitStrength`, `MultiPV`, and non-standard `Personality` settings).
- PyTorch training workflow (`train.py`) with in-check filtering, game-level validation splitting, cosine LR annealing, and quantized SCReLU binary export directly compatible with Liora.

## License

This project is licensed under the MIT License.
