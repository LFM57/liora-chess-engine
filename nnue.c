#include "defs.h"
#include "nnue.h"

// Network Weights & Biases
int16_t FeatureWeights[NNUE_INPUTS * NNUE_L1];
int16_t FeatureBiases[NNUE_L1];
int16_t OutputWeights[2 * NNUE_L1];
int32_t OutputBias;

int NNUE_Loaded = 0;

// Feature Index Mapping (HalfKP)
// Maps (King square, piece type, piece square) to an index in [0, 40959]
static inline int GetFeatureIndex(int ksq, int piece, int sq, int perspective) {
    int p_idx = -1;

    if (perspective == WHITE) {
        // White perspective: White pieces (0..4), Black pieces (5..9)
        switch (piece) {
            case P: p_idx = 0; break;
            case N: p_idx = 1; break;
            case B: p_idx = 2; break;
            case R: p_idx = 3; break;
            case Q: p_idx = 4; break;
            case p: p_idx = 5; break;
            case n: p_idx = 6; break;
            case b: p_idx = 7; break;
            case r: p_idx = 8; break;
            case q: p_idx = 9; break;
            default: return -1; // Kings are not indexed as pieces in HalfKP
        }
        return ksq * 640 + p_idx * 64 + sq;
    } else {
        // Black perspective: Black pieces (0..4), White pieces (5..9) (Rank-Flipped)
        switch (piece) {
            case p: p_idx = 0; break;
            case n: p_idx = 1; break;
            case b: p_idx = 2; break;
            case r: p_idx = 3; break;
            case q: p_idx = 4; break;
            case P: p_idx = 5; break;
            case N: p_idx = 6; break;
            case B: p_idx = 7; break;
            case R: p_idx = 8; break;
            case Q: p_idx = 9; break;
            default: return -1;
        }
        int ksqFlipped = ksq ^ 56;
        int sqFlipped = sq ^ 56;
        return ksqFlipped * 640 + p_idx * 64 + sqFlipped;
    }
}

// Full refresh of an accumulator perspective from scratch
void RefreshAccumulator(Accumulator *acc, const Board *pos, int side) {
    int ksq = (side == WHITE) ? pos->king_sq[WHITE] : pos->king_sq[BLACK];

    // 1. Initialize with biases
    for (int i = 0; i < NNUE_L1; ++i) {
        acc->accumulation[side][i] = FeatureBiases[i];
    }

    // 2. Add active non-king pieces
    for (int sq = 0; sq < 64; ++sq) {
        int piece = pos->board[sq];
        if (piece != EMPTY && piece != K && piece != k) {
            int feat = GetFeatureIndex(ksq, piece, sq, side);
            if (feat >= 0 && feat < NNUE_INPUTS) {
                const int16_t *weights = &FeatureWeights[feat * NNUE_L1];
                for (int i = 0; i < NNUE_L1; ++i) {
                    acc->accumulation[side][i] += weights[i];
                }
            }
        }
    }
    acc->computed[side] = 1;
}

// Incrementally update accumulator from parent ply
void UpdateAccumulatorMove(Accumulator *nextAcc, const Accumulator *prevAcc, const Board *pos,
                           int movedPiece, int from, int to, int capturedPiece, int capSq, int promPiece) {
    for (int side = 0; side <= 1; ++side) {
        int ksq = (side == WHITE) ? pos->king_sq[WHITE] : pos->king_sq[BLACK];

        // If the King of this side moved, incremental update is not possible: full refresh needed
        if ((side == WHITE && movedPiece == K) || (side == BLACK && movedPiece == k)) {
            nextAcc->computed[side] = 0;
            continue;
        }

        // Copy parent accumulator
        for (int i = 0; i < NNUE_L1; ++i) {
            nextAcc->accumulation[side][i] = prevAcc->accumulation[side][i];
        }

        // Remove piece from 'from'
        int subFeat = GetFeatureIndex(ksq, movedPiece, from, side);
        if (subFeat >= 0) {
            const int16_t *wSub = &FeatureWeights[subFeat * NNUE_L1];
            for (int i = 0; i < NNUE_L1; ++i) nextAcc->accumulation[side][i] -= wSub[i];
        }

        // Remove captured piece if any
        if (capturedPiece != EMPTY && capturedPiece != K && capturedPiece != k) {
            int capFeat = GetFeatureIndex(ksq, capturedPiece, capSq, side);
            if (capFeat >= 0) {
                const int16_t *wCap = &FeatureWeights[capFeat * NNUE_L1];
                for (int i = 0; i < NNUE_L1; ++i) nextAcc->accumulation[side][i] -= wCap[i];
            }
        }

        // Add piece at 'to' (or promoted piece)
        int pieceToPlace = (promPiece != EMPTY) ? promPiece : movedPiece;
        int addFeat = GetFeatureIndex(ksq, pieceToPlace, to, side);
        if (addFeat >= 0) {
            const int16_t *wAdd = &FeatureWeights[addFeat * NNUE_L1];
            for (int i = 0; i < NNUE_L1; ++i) nextAcc->accumulation[side][i] += wAdd[i];
        }

        nextAcc->computed[side] = 1;
    }
}

// Forward Pass (Inference)
int EvaluateNNUE(const Board *pos) {
    Accumulator *acc = (Accumulator*)&pos->accumStack[pos->hisPly];

    // Ensure accumulators are up-to-date
    if (!acc->computed[WHITE]) RefreshAccumulator(acc, pos, WHITE);
    if (!acc->computed[BLACK]) RefreshAccumulator(acc, pos, BLACK);

    int us = pos->side;
    int them = pos->side ^ 1;

    int32_t sum = OutputBias;

    // ClippedReLU activation & matrix multiplication for 'us'
    for (int i = 0; i < NNUE_L1; ++i) {
        int16_t val = acc->accumulation[us][i];
        int16_t clipped = (val > 127) ? 127 : ((val < 0) ? 0 : val);
        sum += clipped * OutputWeights[i];
    }

    // ClippedReLU activation & matrix multiplication for 'them'
    for (int i = 0; i < NNUE_L1; ++i) {
        int16_t val = acc->accumulation[them][i];
        int16_t clipped = (val > 127) ? 127 : ((val < 0) ? 0 : val);
        sum += clipped * OutputWeights[NNUE_L1 + i];
    }

    return sum / NNUE_SCALE;
}

// Weight Loading and Initialization
int LoadNNUE(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;

    size_t r = 0;
    r += fread(FeatureBiases, sizeof(int16_t), NNUE_L1, f);
    r += fread(FeatureWeights, sizeof(int16_t), NNUE_INPUTS * NNUE_L1, f);
    r += fread(&OutputBias, sizeof(int32_t), 1, f);
    r += fread(OutputWeights, sizeof(int16_t), 2 * NNUE_L1, f);

    fclose(f);
    NNUE_Loaded = 1;
    return 1;
}

void InitNNUE(const char *filename) {
    if (filename && filename[0] != '\0' && LoadNNUE(filename)) {
        printf("info string NNUE net loaded successfully from %s\n", filename);
        return;
    }

    // Fallback: Initialize simple default weights to allow immediate compilation and execution
    memset(FeatureWeights, 0, sizeof(FeatureWeights));
    for (int i = 0; i < NNUE_L1; ++i) FeatureBiases[i] = 0;
    for (int i = 0; i < 2 * NNUE_L1; ++i) OutputWeights[i] = 0;
    OutputBias = 0;
    NNUE_Loaded = 0;
}