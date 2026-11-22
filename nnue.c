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

// Feature Index Mapping (768: 12 piece types including both kings * 64 squares)
static inline int GetFeatureIndex(int piece, int sq, int perspective) {
    if (piece < P || piece > k) return -1;

    if (perspective == WHITE) {
        return piece * 64 + sq;
    } else {
        int p_idx = (piece < 6) ? (piece + 6) : (piece - 6);
        return p_idx * 64 + (sq ^ 56);
    }
}

// Full refresh of an accumulator perspective from scratch
void RefreshAccumulator(Accumulator *acc, const struct Board *pos, int side) {
    // 1. Initialize with biases
    memcpy(acc->accumulation[side], FeatureBiases, NNUE_L1 * sizeof(int16_t));

    // 2. Add all active pieces (including both kings)
    for (int sq = 0; sq < 64; ++sq) {
        int piece = pos->board[sq];
        if (piece != EMPTY) {
            int feat = GetFeatureIndex(piece, sq, side);
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
    (void)pos;

    for (int side = 0; side <= 1; ++side) {
        if (!prevAcc->computed[side]) {
            nextAcc->computed[side] = 0;
            continue;
        }

        if (nextAcc != prevAcc) {
            memcpy(nextAcc->accumulation[side], prevAcc->accumulation[side], NNUE_L1 * sizeof(int16_t));
        }

        int subFeat = GetFeatureIndex(movedPiece, from, side);
        int addFeat = GetFeatureIndex((promPiece != EMPTY) ? promPiece : movedPiece, to, side);
        int capFeat = (capturedPiece != EMPTY) ? GetFeatureIndex(capturedPiece, capSq, side) : -1;

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

// Forward Pass (Inference with SCReLU)
int EvaluateNNUE(const struct Board *pos) {
    Accumulator *acc = (Accumulator*)&pos->accumStack[pos->hisPly];

    if (!acc->computed[WHITE]) RefreshAccumulator(acc, pos, WHITE);
    if (!acc->computed[BLACK]) RefreshAccumulator(acc, pos, BLACK);

    int us = pos->side;
    int them = pos->side ^ 1;

    int64_t sum = (int64_t)OutputBias;

#if defined(__AVX2__)
    __m256i sum_vec = _mm256_setzero_si256();
    __m256i zero = _mm256_setzero_si256();
    __m256i max_val = _mm256_set1_epi16(NNUE_QA);

    // Friendly perspective
    for (int i = 0; i < NNUE_L1; i += 16) {
        __m256i val = _mm256_loadu_si256((__m256i*)&acc->accumulation[us][i]);
        __m256i clamped = _mm256_min_epi16(_mm256_max_epi16(val, zero), max_val);
        __m256i sq = _mm256_mullo_epi16(clamped, clamped);
        __m256i weights = _mm256_loadu_si256((__m256i*)&OutputWeights[i]);
        __m256i prod = _mm256_madd_epi16(sq, weights);
        sum_vec = _mm256_add_epi32(sum_vec, prod);
    }

    // Opponent perspective
    for (int i = 0; i < NNUE_L1; i += 16) {
        __m256i val = _mm256_loadu_si256((__m256i*)&acc->accumulation[them][i]);
        __m256i clamped = _mm256_min_epi16(_mm256_max_epi16(val, zero), max_val);
        __m256i sq = _mm256_mullo_epi16(clamped, clamped);
        __m256i weights = _mm256_loadu_si256((__m256i*)&OutputWeights[NNUE_L1 + i]);
        __m256i prod = _mm256_madd_epi16(sq, weights);
        sum_vec = _mm256_add_epi32(sum_vec, prod);
    }

    // Horizontal sum of the 8 32-bit integers in sum_vec into int64_t
    int32_t lane_sums[8];
    _mm256_storeu_si256((__m256i*)lane_sums, sum_vec);
    for (int i = 0; i < 8; ++i) {
        sum += (int64_t)lane_sums[i];
    }
#else
    for (int i = 0; i < NNUE_L1; ++i) {
        int32_t v_us = acc->accumulation[us][i];
        int32_t c_us = (v_us > NNUE_QA) ? NNUE_QA : ((v_us < 0) ? 0 : v_us);
        sum += (int64_t)(c_us * c_us) * (int64_t)OutputWeights[i];

        int32_t v_them = acc->accumulation[them][i];
        int32_t c_them = (v_them > NNUE_QA) ? NNUE_QA : ((v_them < 0) ? 0 : v_them);
        sum += (int64_t)(c_them * c_them) * (int64_t)OutputWeights[NNUE_L1 + i];
    }
#endif

    return (int)((sum * NNUE_SCALE) / ((int64_t)NNUE_QA * NNUE_QA * NNUE_QB));
}

static inline int EqualsIgnoreCase(const char *s1, const char *s2) {
    if (!s1 || !s2) return 0;
    while (*s1 && *s2) {
        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : *s2;
        if (c1 != c2) return 0;
        s1++;
        s2++;
    }
    return (*s1 == '\0' && *s2 == '\0');
}

void DisableNNUE(void) {
    memset(FeatureWeights, 0, sizeof(FeatureWeights));
    for (int i = 0; i < NNUE_L1; ++i) FeatureBiases[i] = 0;
    for (int i = 0; i < 2 * NNUE_L1; ++i) OutputWeights[i] = 0;
    OutputBias = 0;
    NNUE_Loaded = 0;
}

int LoadNNUEMemory(const void *data, size_t size) {
    size_t expected = sizeof(FeatureBiases) + sizeof(FeatureWeights) + sizeof(OutputBias) + sizeof(OutputWeights);
    if (!data || size != expected) {
        DisableNNUE();
        return 0;
    }

    const uint8_t *ptr = (const uint8_t *)data;
    memcpy(FeatureBiases, ptr, sizeof(FeatureBiases));
    ptr += sizeof(FeatureBiases);
    memcpy(FeatureWeights, ptr, sizeof(FeatureWeights));
    ptr += sizeof(FeatureWeights);
    memcpy(&OutputBias, ptr, sizeof(OutputBias));
    ptr += sizeof(OutputBias);
    memcpy(OutputWeights, ptr, sizeof(OutputWeights));

    NNUE_Loaded = 1;
    return 1;
}

int LoadEmbeddedNNUE(void) {
    if (!HasEmbeddedNNUE()) {
        DisableNNUE();
        return 0;
    }
    return LoadNNUEMemory(GetEmbeddedNNUEData(), GetEmbeddedNNUESize());
}

int LoadNNUE(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;

    size_t r = 0;
    r += fread(FeatureBiases, sizeof(int16_t), NNUE_L1, f);
    r += fread(FeatureWeights, sizeof(int16_t), (size_t)NNUE_INPUTS * NNUE_L1, f);
    r += fread(&OutputBias, sizeof(int32_t), 1, f);
    r += fread(OutputWeights, sizeof(int16_t), 2 * NNUE_L1, f);

    fclose(f);

    size_t expected = (size_t)NNUE_L1 + (size_t)NNUE_INPUTS * NNUE_L1 + 1 + 2 * (size_t)NNUE_L1;
    if (r != expected) {
        DisableNNUE();
        return 0;
    }

    NNUE_Loaded = 1;
    return 1;
}

void InitNNUE(const char *filename) {
    // Check if filename requests embedded net
    if (!filename || filename[0] == '\0' ||
        EqualsIgnoreCase(filename, "<embedded>") ||
        EqualsIgnoreCase(filename, "embedded") ||
        EqualsIgnoreCase(filename, "<internal>") ||
        EqualsIgnoreCase(filename, "internal")) {
        if (LoadEmbeddedNNUE()) {
            printf("info string Embedded NNUE net loaded successfully\n");
            return;
        }
        printf("info string Warning: No embedded NNUE available, falling back to HCE\n");
        DisableNNUE();
        return;
    }

    // Check if filename explicitly disables NNUE
    if (EqualsIgnoreCase(filename, "<none>") ||
        EqualsIgnoreCase(filename, "none") ||
        EqualsIgnoreCase(filename, "off") ||
        EqualsIgnoreCase(filename, "<empty>")) {
        DisableNNUE();
        printf("info string NNUE disabled, falling back to HCE\n");
        return;
    }

    // Otherwise attempt to load custom net from file
    if (LoadNNUE(filename)) {
        printf("info string NNUE net loaded successfully from %s\n", filename);
        return;
    }

    // If loading custom net fails, fallback to embedded net if available
    if (HasEmbeddedNNUE() && LoadEmbeddedNNUE()) {
        printf("info string Warning: Failed to load NNUE net from %s, falling back to embedded NNUE\n", filename);
        return;
    }

    printf("info string Warning: Failed to load NNUE net from %s, falling back to HCE\n", filename);
    DisableNNUE();
}

// Updates the moved Rook in both perspectives during castling
void UpdateAccumulatorCastlingRook(Accumulator *acc, const struct Board *pos, int castlingSide, int rFrom, int rTo) {
    (void)pos;
    int rPiece = (castlingSide == WHITE) ? R : r;

    for (int side = 0; side <= 1; ++side) {
        if (!acc->computed[side]) continue;

        int subFeat = GetFeatureIndex(rPiece, rFrom, side);
        int addFeat = GetFeatureIndex(rPiece, rTo, side);

#if defined(__AVX2__)
        for (int i = 0; i < NNUE_L1; i += 16) {
            __m256i acc_vec = _mm256_loadu_si256((__m256i*)&acc->accumulation[side][i]);
            if (subFeat >= 0) {
                __m256i w_sub = _mm256_loadu_si256((__m256i*)&FeatureWeights[subFeat * NNUE_L1 + i]);
                acc_vec = _mm256_sub_epi16(acc_vec, w_sub);
            }
            if (addFeat >= 0) {
                __m256i w_add = _mm256_loadu_si256((__m256i*)&FeatureWeights[addFeat * NNUE_L1 + i]);
                acc_vec = _mm256_add_epi16(acc_vec, w_add);
            }
            _mm256_storeu_si256((__m256i*)&acc->accumulation[side][i], acc_vec);
        }
#else
        for (int i = 0; i < NNUE_L1; ++i) {
            if (subFeat >= 0) acc->accumulation[side][i] -= FeatureWeights[subFeat * NNUE_L1 + i];
            if (addFeat >= 0) acc->accumulation[side][i] += FeatureWeights[addFeat * NNUE_L1 + i];
        }
#endif
    }
}
