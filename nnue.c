#include "defs.h"
#include "nnue.h"

#if defined(__AVX2__)
#include <immintrin.h>
#endif

// Network Weights & Biases (Aligned for SIMD)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((aligned(64))) int16_t FeatureWeights[NNUE_INPUTS * NNUE_L1];
__attribute__((aligned(64))) int16_t FeatureBiases[NNUE_L1];
__attribute__((aligned(64))) int16_t OutputWeights[2 * NNUE_L1];
#else
int16_t FeatureWeights[NNUE_INPUTS * NNUE_L1];
int16_t FeatureBiases[NNUE_L1];
int16_t OutputWeights[2 * NNUE_L1];
#endif

int32_t OutputBias;
int NNUE_Loaded = 0;

// Feature Index Mapping (HalfKP)
static inline int GetFeatureIndex(int ksq, int piece, int sq, int perspective) {
    int p_idx = -1;

    if (perspective == WHITE) {
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
            default: return -1;
        }
        return ksq * 640 + p_idx * 64 + sq;
    } else {
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
        return (ksq ^ 56) * 640 + p_idx * 64 + (sq ^ 56);
    }
}

// Full refresh of an accumulator perspective from scratch
void RefreshAccumulator(Accumulator *acc, const struct Board *pos, int side) {
    int ksq = (side == WHITE) ? pos->king_sq[WHITE] : pos->king_sq[BLACK];

    // 1. Initialize with biases
    memcpy(acc->accumulation[side], FeatureBiases, NNUE_L1 * sizeof(int16_t));

    // 2. Add active non-king pieces
    for (int sq = 0; sq < 64; ++sq) {
        int piece = pos->board[sq];
        if (piece != EMPTY && piece != K && piece != k) {
            int feat = GetFeatureIndex(ksq, piece, sq, side);
            if (feat >= 0 && feat < NNUE_INPUTS) {
                const int16_t *weights = &FeatureWeights[feat * NNUE_L1];

#if defined(__AVX2__)
                for (int i = 0; i < NNUE_L1; i += 16) {
                    __m256i acc_vec = _mm256_loadu_si256((__m256i*)&acc->accumulation[side][i]);
                    __m256i w_vec   = _mm256_loadu_si256((__m256i*)&weights[i]);
                    acc_vec = _mm256_add_epi16(acc_vec, w_vec);
                    _mm256_storeu_si256((__m256i*)&acc->accumulation[side][i], acc_vec);
                }
#else
                for (int i = 0; i < NNUE_L1; ++i) {
                    acc->accumulation[side][i] += weights[i];
                }
#endif
            }
        }
    }
    acc->computed[side] = 1;
}

// Incrementally update accumulator from parent ply
void UpdateAccumulatorMove(Accumulator *nextAcc, const Accumulator *prevAcc, const struct Board *pos,
                           int movedPiece, int from, int to, int capturedPiece, int capSq, int promPiece) {
    for (int side = 0; side <= 1; ++side) {
        // FIX 1: If parent accumulator was not computed, child CANNOT be updated incrementally!
        // FIX 2: If this side's king moved, this perspective MUST be refreshed from scratch later.
        if (!prevAcc->computed[side] || (side == WHITE && movedPiece == K) || (side == BLACK && movedPiece == k)) {
            nextAcc->computed[side] = 0;
            continue;
        }

        // FIX 3: Prevent undefined behavior when nextAcc and prevAcc point to the same memory
        if (nextAcc != prevAcc) {
            memcpy(nextAcc->accumulation[side], prevAcc->accumulation[side], NNUE_L1 * sizeof(int16_t));
        }

        int ksq = (side == WHITE) ? pos->king_sq[WHITE] : pos->king_sq[BLACK];

        int subFeat = GetFeatureIndex(ksq, movedPiece, from, side);
        int addFeat = GetFeatureIndex(ksq, (promPiece != EMPTY) ? promPiece : movedPiece, to, side);
        int capFeat = (capturedPiece != EMPTY && capturedPiece != K && capturedPiece != k) ? GetFeatureIndex(ksq, capturedPiece, capSq, side) : -1;

#if defined(__AVX2__)
        for (int i = 0; i < NNUE_L1; i += 16) {
            __m256i acc_vec = _mm256_loadu_si256((__m256i*)&nextAcc->accumulation[side][i]);

            if (subFeat >= 0) {
                __m256i w_sub = _mm256_loadu_si256((__m256i*)&FeatureWeights[subFeat * NNUE_L1 + i]);
                acc_vec = _mm256_sub_epi16(acc_vec, w_sub);
            }
            if (capFeat >= 0) {
                __m256i w_cap = _mm256_loadu_si256((__m256i*)&FeatureWeights[capFeat * NNUE_L1 + i]);
                acc_vec = _mm256_sub_epi16(acc_vec, w_cap);
            }
            if (addFeat >= 0) {
                __m256i w_add = _mm256_loadu_si256((__m256i*)&FeatureWeights[addFeat * NNUE_L1 + i]);
                acc_vec = _mm256_add_epi16(acc_vec, w_add);
            }

            _mm256_storeu_si256((__m256i*)&nextAcc->accumulation[side][i], acc_vec);
        }
#else
        for (int i = 0; i < NNUE_L1; ++i) {
            if (subFeat >= 0) nextAcc->accumulation[side][i] -= FeatureWeights[subFeat * NNUE_L1 + i];
            if (capFeat >= 0) nextAcc->accumulation[side][i] -= FeatureWeights[capFeat * NNUE_L1 + i];
            if (addFeat >= 0) nextAcc->accumulation[side][i] += FeatureWeights[addFeat * NNUE_L1 + i];
        }
#endif

        nextAcc->computed[side] = 1;
    }
}

// Forward Pass (Inference)
int EvaluateNNUE(const struct Board *pos) {
    Accumulator *acc = (Accumulator*)&pos->accumStack[pos->hisPly];

    if (!acc->computed[WHITE]) RefreshAccumulator(acc, pos, WHITE);
    if (!acc->computed[BLACK]) RefreshAccumulator(acc, pos, BLACK);

    int us = pos->side;
    int them = pos->side ^ 1;

    int32_t sum = OutputBias;

#if defined(__AVX2__)
    __m256i sum_vec = _mm256_setzero_si256();
    __m256i zero = _mm256_setzero_si256();
    __m256i max_val = _mm256_set1_epi16(127);

    // Friendly perspective
    for (int i = 0; i < NNUE_L1; i += 16) {
        __m256i val = _mm256_loadu_si256((__m256i*)&acc->accumulation[us][i]);
        __m256i clipped = _mm256_min_epi16(_mm256_max_epi16(val, zero), max_val);
        __m256i weights = _mm256_loadu_si256((__m256i*)&OutputWeights[i]);
        __m256i prod = _mm256_madd_epi16(clipped, weights);
        sum_vec = _mm256_add_epi32(sum_vec, prod);
    }

    // Opponent perspective
    for (int i = 0; i < NNUE_L1; i += 16) {
        __m256i val = _mm256_loadu_si256((__m256i*)&acc->accumulation[them][i]);
        __m256i clipped = _mm256_min_epi16(_mm256_max_epi16(val, zero), max_val);
        __m256i weights = _mm256_loadu_si256((__m256i*)&OutputWeights[NNUE_L1 + i]);
        __m256i prod = _mm256_madd_epi16(clipped, weights);
        sum_vec = _mm256_add_epi32(sum_vec, prod);
    }

    // Horizontal sum of the 8 32-bit integers in sum_vec
    __m128i lo = _mm256_castsi256_si128(sum_vec);
    __m128i hi = _mm256_extracti128_si256(sum_vec, 1);
    __m128i sum128 = _mm_add_epi32(lo, hi);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    sum128 = _mm_hadd_epi32(sum128, sum128);
    sum += _mm_cvtsi128_si32(sum128);
#else
    for (int i = 0; i < NNUE_L1; ++i) {
        int16_t v_us = acc->accumulation[us][i];
        int16_t c_us = (v_us > 127) ? 127 : ((v_us < 0) ? 0 : v_us);
        sum += c_us * OutputWeights[i];

        int16_t v_them = acc->accumulation[them][i];
        int16_t c_them = (v_them > 127) ? 127 : ((v_them < 0) ? 0 : v_them);
        sum += c_them * OutputWeights[NNUE_L1 + i];
    }
#endif

    return sum / NNUE_SCALE;
}

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

    memset(FeatureWeights, 0, sizeof(FeatureWeights));
    for (int i = 0; i < NNUE_L1; ++i) FeatureBiases[i] = 0;
    for (int i = 0; i < 2 * NNUE_L1; ++i) OutputWeights[i] = 0;
    OutputBias = 0;
    NNUE_Loaded = 0;
}

// Updates the moved Rook in the opponent's accumulator during castling
void UpdateAccumulatorCastlingRook(Accumulator *acc, const struct Board *pos, int castlingSide, int rFrom, int rTo) {
    int opp = castlingSide ^ 1;
    if (!acc->computed[opp]) return;

    int oppKsq = (opp == WHITE) ? pos->king_sq[WHITE] : pos->king_sq[BLACK];
    int rPiece = (castlingSide == WHITE) ? R : r;
    int subFeat = GetFeatureIndex(oppKsq, rPiece, rFrom, opp);
    int addFeat = GetFeatureIndex(oppKsq, rPiece, rTo, opp);

#if defined(__AVX2__)
    for (int i = 0; i < NNUE_L1; i += 16) {
        __m256i acc_vec = _mm256_loadu_si256((__m256i*)&acc->accumulation[opp][i]);
        if (subFeat >= 0) {
            __m256i w_sub = _mm256_loadu_si256((__m256i*)&FeatureWeights[subFeat * NNUE_L1 + i]);
            acc_vec = _mm256_sub_epi16(acc_vec, w_sub);
        }
        if (addFeat >= 0) {
            __m256i w_add = _mm256_loadu_si256((__m256i*)&FeatureWeights[addFeat * NNUE_L1 + i]);
            acc_vec = _mm256_add_epi16(acc_vec, w_add);
        }
        _mm256_storeu_si256((__m256i*)&acc->accumulation[opp][i], acc_vec);
    }
#else
    for (int i = 0; i < NNUE_L1; ++i) {
        if (subFeat >= 0) acc->accumulation[opp][i] -= FeatureWeights[subFeat * NNUE_L1 + i];
        if (addFeat >= 0) acc->accumulation[opp][i] += FeatureWeights[addFeat * NNUE_L1 + i];
    }
#endif
}