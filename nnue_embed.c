#include "nnue.h"
#include <stddef.h>

#if defined(EMBED_NNUE) && EMBED_NNUE

#define INCBIN_PREFIX embedded_
#include "incbin.h"

#ifndef NNUE_EMBEDDED_FILE
#define NNUE_EMBEDDED_FILE "../NNUE Weights/Gen 4/liora_20.nnue"
#endif

INCBIN(nnue, NNUE_EMBEDDED_FILE);

int HasEmbeddedNNUE(void) {
    return 1;
}

const void *GetEmbeddedNNUEData(void) {
    return (const void *)embedded_nnueData;
}

size_t GetEmbeddedNNUESize(void) {
    return (size_t)embedded_nnueSize;
}

#else

int HasEmbeddedNNUE(void) {
    return 0;
}

const void *GetEmbeddedNNUEData(void) {
    return NULL;
}

size_t GetEmbeddedNNUESize(void) {
    return 0;
}

#endif
