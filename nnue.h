#ifndef NNUE_H
#define NNUE_H

#include <stdint.h>
#include <stddef.h>

#define NNUE_INPUTS 768   // 12 piece types (including both kings) * 64 squares
#define NNUE_L1 256       // 256 neurons per perspective
#define NNUE_QA 181       // Feature quantization scale (181^2 = 32761 <= 32767, no int16 overflow)
#define NNUE_QB 64        // Output weight quantization scale
#define NNUE_SCALE 400    // Centipawn output scale

// Compact representation of board changes for lazy incremental accumulator updates
typedef struct {
    uint8_t numSub;
    uint8_t numAdd;
    uint8_t subPiece[2];
    uint8_t subSq[2];
    uint8_t addPiece[2];
    uint8_t addSq[2];
} AccumulatorDelta;

// Accumulator storing the first layer outputs for both White and Black perspectives
typedef struct {
    int16_t accumulation[2][NNUE_L1];
    AccumulatorDelta delta;
    uint8_t computed[2]; // 1 if calculated, 0 if dirty / needs lazy update or refresh
} Accumulator;

// Forward declaration of tagged struct
struct Board;

// NNUE Public API
void InitNNUE(const char *filename);
int LoadNNUE(const char *filename);
int LoadNNUEMemory(const void *data, size_t size);
int LoadEmbeddedNNUE(void);
void DisableNNUE(void);
int EvaluateNNUE(const struct Board *pos);

// Embedded NNUE query API
int HasEmbeddedNNUE(void);
const void *GetEmbeddedNNUEData(void);
size_t GetEmbeddedNNUESize(void);

void RefreshAccumulator(Accumulator *acc, const struct Board *pos, int side);
void EnsureAccumulatorUpdated(const struct Board *pos, int side);

#endif