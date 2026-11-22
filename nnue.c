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

// Fused incremental update: loads directly from prevAcc and stores directly into nextAcc
static void ApplyAccumulatorDelta(Accumulator *nextAcc, const Accumulator *prevAcc, int side) {
    const AccumulatorDelta *d = &nextAcc->delta;
    int16_t *dst = nextAcc->accumulation[side];
    const int16_t *src = prevAcc->accumulation[side];

    if (d->numSub == 1 && d->numAdd == 1) {
        int sub0 = GetFeatureIndex(d->subPiece[0], d->subSq[0], side);
        int add0 = GetFeatureIndex(d->addPiece[0], d->addSq[0], side);
        const int16_t *w_sub0 = &FeatureWeights[sub0 * NNUE_L1];
        const int16_t *w_add0 = &FeatureWeights[add0 * NNUE_L1];

#if defined(__AVX2__)
        for (int i = 0; i < NNUE_L1; i += 16) {
            __m256i acc = _mm256_loadu_si256((const __m256i*)&src[i]);
            acc = _mm256_sub_epi16(acc, _mm256_loadu_si256((const __m256i*)&w_sub0[i]));
            acc = _mm256_add_epi16(acc, _mm256_loadu_si256((const __m256i*)&w_add0[i]));
            _mm256_storeu_si256((__m256i*)&dst[i], acc);
        }
#else
        for (int i = 0; i < NNUE_L1; ++i) {
            dst[i] = src[i] - w_sub0[i] + w_add0[i];
        }
#endif
    } else if (d->numSub == 2 && d->numAdd == 1) {
        int sub0 = GetFeatureIndex(d->subPiece[0], d->subSq[0], side);
        int sub1 = GetFeatureIndex(d->subPiece[1], d->subSq[1], side);
        int add0 = GetFeatureIndex(d->addPiece[0], d->addSq[0], side);
        const int16_t *w_sub0 = &FeatureWeights[sub0 * NNUE_L1];
        const int16_t *w_sub1 = &FeatureWeights[sub1 * NNUE_L1];
        const int16_t *w_add0 = &FeatureWeights[add0 * NNUE_L1];

#if defined(__AVX2__)
        for (int i = 0; i < NNUE_L1; i += 16) {
            __m256i acc = _mm256_loadu_si256((const __m256i*)&src[i]);
            acc = _mm256_sub_epi16(acc, _mm256_loadu_si256((const __m256i*)&w_sub0[i]));
            acc = _mm256_sub_epi16(acc, _mm256_loadu_si256((const __m256i*)&w_sub1[i]));
            acc = _mm256_add_epi16(acc, _mm256_loadu_si256((const __m256i*)&w_add0[i]));
            _mm256_storeu_si256((__m256i*)&dst[i], acc);
        }
#else
        for (int i = 0; i < NNUE_L1; ++i) {
            dst[i] = src[i] - w_sub0[i] - w_sub1[i] + w_add0[i];
        }
#endif
    } else if (d->numSub == 2 && d->numAdd == 2) {
        int sub0 = GetFeatureIndex(d->subPiece[0], d->subSq[0], side);
        int sub1 = GetFeatureIndex(d->subPiece[1], d->subSq[1], side);
        int add0 = GetFeatureIndex(d->addPiece[0], d->addSq[0], side);
        int add1 = GetFeatureIndex(d->addPiece[1], d->addSq[1], side);
        const int16_t *w_sub0 = &FeatureWeights[sub0 * NNUE_L1];
        const int16_t *w_sub1 = &FeatureWeights[sub1 * NNUE_L1];
        const int16_t *w_add0 = &FeatureWeights[add0 * NNUE_L1];
        const int16_t *w_add1 = &FeatureWeights[add1 * NNUE_L1];

#if defined(__AVX2__)
        for (int i = 0; i < NNUE_L1; i += 16) {
            __m256i acc = _mm256_loadu_si256((const __m256i*)&src[i]);
            acc = _mm256_sub_epi16(acc, _mm256_loadu_si256((const __m256i*)&w_sub0[i]));
            acc = _mm256_sub_epi16(acc, _mm256_loadu_si256((const __m256i*)&w_sub1[i]));
            acc = _mm256_add_epi16(acc, _mm256_loadu_si256((const __m256i*)&w_add0[i]));
            acc = _mm256_add_epi16(acc, _mm256_loadu_si256((const __m256i*)&w_add1[i]));
            _mm256_storeu_si256((__m256i*)&dst[i], acc);
        }
#else
        for (int i = 0; i < NNUE_L1; ++i) {
            dst[i] = src[i] - w_sub0[i] - w_sub1[i] + w_add0[i] + w_add1[i];
        }
#endif
    } else {
        // Null move (numSub == 0 && numAdd == 0)
        memcpy(dst, src, NNUE_L1 * sizeof(int16_t));
    }

    nextAcc->computed[side] = 1;
}

// Lazily materialize accumulator for `side` by walking back to nearest computed ancestor
void EnsureAccumulatorUpdated(const struct Board *pos, int side) {
    Accumulator *stack = (Accumulator *)pos->accumStack;
    int cur = pos->hisPly;
    if (stack[cur].computed[side]) return;

    int anc = cur;
    while (anc > 0 && !stack[anc].computed[side] && (cur - anc) <= 8) {
        anc--;
    }

    if (stack[anc].computed[side]) {
        for (int p = anc + 1; p <= cur; ++p) {
            ApplyAccumulatorDelta(&stack[p], &stack[p - 1], side);
        }
    } else {
        RefreshAccumulator(&stack[cur], pos, side);
    }
}

// Forward Pass (Inference with SCReLU)
int EvaluateNNUE(const struct Board *pos) {
    EnsureAccumulatorUpdated(pos, WHITE);
    EnsureAccumulatorUpdated(pos, BLACK);
    const Accumulator *acc = &pos->accumStack[pos->hisPly];

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

