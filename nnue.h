#ifndef NNUE_H
#define NNUE_H

#include <stdint.h>

#define NNUE_INPUTS 40960 // 64 king squares * 10 piece types * 64 piece squares
#define NNUE_L1 256       // 256 neurons per perspective
#define NNUE_SCALE 16     // Output divisor to convert raw net output to centipawns

// Accumulator storing the first layer outputs for both White and Black perspectives
typedef struct {
    int16_t accumulation[2][NNUE_L1];
    int computed[2]; // 1 if calculated, 0 if needs refresh
} Accumulator;

// Forward declaration of Board to prevent circular dependencies
typedef struct Board Board;

// NNUE Public API
void InitNNUE(const char *filename);
int LoadNNUE(const char *filename);
int EvaluateNNUE(const Board *pos);

void RefreshAccumulator(Accumulator *acc, const Board *pos, int side);
void UpdateAccumulatorMove(Accumulator *nextAcc, const Accumulator *prevAcc, const Board *pos, int movedPiece, int from, int to, int capturedPiece, int capSq, int promPiece);

#endif