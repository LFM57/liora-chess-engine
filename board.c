#include "defs.h"

// Tables
const int PieceValuesMG[13] = { 82, 337, 365, 477, 1025, 0, 82, 337, 365, 477, 1025, 0, 0 };
const int PieceValuesEG[13] = { 94, 281, 297, 512, 936, 0, 94, 281, 297, 512, 936, 0, 0 };

// Arrays for PST (White perspective)
int PstMG[6][64];
int PstEG[6][64];

// Raw data from Python (flattened)
// Pawn
const int RawPstMgP[64] = {
    0,   0,   0,   0,   0,   0,   0,   0,
    98, 134,  61,  95,  68, 126,  34, -11,
    -6,   7,  26,  31,  65,  56,  25, -20,
    -14,  13,   6,  21,  23,  12,  17, -23,
    -27,  -2,  -5,  12,  17,   6,  10, -25,
    -26,  -4,  -4, -10,   3,   3,  33, -12,
    -35,  -1, -20, -23, -15,  24,  38, -22,
    0,   0,   0,   0,   0,   0,   0,   0
};
const int RawPstEgP[64] = {
    0,   0,   0,   0,   0,   0,   0,   0,
    178, 173, 158, 134, 147, 132, 165, 187,
    94, 100,  85,  67,  56,  53,  82,  84,
    32,  24,  13,   5,  -2,   4,  17,  17,
    13,   9,  -3,  -7,  -7,  -8,   3,  -1,
    4,   7,  -6,   1,   0,  -5,  -1,  -8,
    13,   8,   8,  10,  13,   0,   2,  -7,
    0,   0,   0,   0,   0,   0,   0,   0
};
// Knight
const int RawPstMgN[64] = {
    -167, -89, -34, -49,  61, -97, -15, -107,
    -73, -41,  72,  36,  23,  62,   7, -17,
    -47,  60,  37,  65,  84, 129,  73,  44,
    -9,  17,  19,  53,  37,  69,  18,  22,
    -13,   4,  16,  13,  28,  19,  21,  -8,
    -23,  -9,  12,  10,  19,  17,  25, -16,
    -29, -53, -12,  -3, -11,  18, -14, -19,
    -105, -21, -58, -33, -17, -28, -19, -23
};
const int RawPstEgN[64] = {
    -58, -38, -13, -28, -31, -27, -63, -99,
    -25,  -8, -25,  -2,  -9, -25, -24, -52,
    -24, -20,  10,   9,  -1,  -9, -19, -41,
    -17,   3,  22,  22,  22,  11,   8, -18,
    -18,  -6,  16,  25,  16,  17,   4, -18,
    -23,  -3,  -1,  15,  10,  -3, -20, -22,
    -42, -20, -10,  -5,  -2, -20, -23, -44,
    -29, -51, -23, -15, -22, -18, -50, -64
};
// Bishop
const int RawPstMgB[64] = {
    -29,   4, -82, -37, -25, -42,   7,  -8,
    -26,  16, -18, -13,  30,  59,  18, -47,
    -16,  37,  43,  40,  35,  50,  37,  -2,
    -4,   5,  19,  50,  37,  37,   7,  -2,
    -6,  13,  13,  26,  34,  12,  10,   4,
    0,  15,  15,  15,  14,  27,  18,  10,
    4,  15,  16,   0,   7,  21,  33,   1,
    -33,  -3, -14, -21, -13, -12, -39, -21
};
const int RawPstEgB[64] = {
    -14, -21, -11,  -8, -7,  -9, -17, -24,
    -8,  -4,   7, -12, -3, -13,  -4, -14,
    -4,  -8,   8,  20, 37,  -4,  -6,  -8,
    -3,  -6,   9,  17,  9,  10,  -9, -17,
    -7,  -6,  -1, -11, 15, -10, -15, -19,
    -4,  -6,   0,  17, -2,  -6, -10, -18,
    -8, -10, -11,   4, -1,  12,  -6, -21,
    -9, -13, -13, -21, -4, -28, -18, -23
};
// Rook
const int RawPstMgR[64] = {
    32,  42,  32,  51,  63,   9,  31,  43,
    27,  32,  58,  62,  80,  67,  26,  44,
    -5,  19,  26,  36,  17,  45,  61,  16,
    -24, -11,   7,  26,  24,  35,  -8, -20,
    -36, -26, -12,  -1,   9,  -7,   6, -23,
    -45, -25, -16, -17,   3,   0,  -5, -33,
    -44, -16, -20,  -9,  -1,  11,  -6, -71,
    -19, -13,   1,  17,  16,   7, -37, -26
};
const int RawPstEgR[64] = {
    13,  10,  18,  15,  12,  12,   8,   5,
    11,  13,  13,  11,  -3,   3,   8,   3,
    7,   7,   7,   5,   4,  -3,  -5,  -3,
    4,   3,  13,   1,   2,   1,  -1,   2,
    3,   5,   8,   4,  -5,  -6,  -8, -11,
    -4,   0,  -5,  -1,  -7, -12,  -8, -16,
    -6,  -6,   0,   2,  -9,  -9, -11,  -3,
    -9,   2,   3,  -1,  -5, -13,   4, -20
};
// Queen
const int RawPstMgQ[64] = {
    -28,   0,  29,  12,  59,  44,  43,  45,
    -24, -39,  -5,   1, -16,  57,  28,  54,
    -13, -17,   7,   8,  29,  56,  47,  57,
    -27, -27, -16, -16,  -1,  17,  -2,   1,
    -9, -26,  -9, -10,  -2,  -4,   3,  -3,
    -14,   2, -11,  -2,  -5,   2,  14,   5,
    -35,  -8,  11,   2,   8,  15,  -3,   1,
    -1, -18,  -9, -19, -15, -15, -28, -15
};
const int RawPstEgQ[64] = {
    -9,  22,  22,  27,  27,  19,  10,  20,
    -17,  20,  32,  41,  58,  25,  30,   0,
    -20,   6,   9,  49,  47,  35,  19,   9,
    3,  22,  24,  45,  57,  40,  57,  36,
    -18,  28,  19,  47,  31,  34,  39,  23,
    -16, -27,  15,   6,   9,  17,  10,   5,
    -22, -23, -30, -16, -16,  23, -36, -32,
    -33, -28, -22, -43,  -5, -32, -20, -41
};
// King
const int RawPstMgK[64] = {
    -65,  23,  16, -15, -56, -34,   2,  13,
    29,  -1, -20,  -7,  -8,  -4, -38, -29,
    -9,  24,   2, -16, -20,   6,  22, -22,
    -17, -20, -12, -27, -30, -25, -14, -36,
    -49,  -1, -27, -39, -46, -44, -33, -51,
    -14, -14, -22, -46, -44, -30, -15, -27,
    1,   7,  -8, -64, -43, -16,   9,   8,
    -15,  36,  12, -54,   8, -28,  24,  14
};
const int RawPstEgK[64] = {
    -74, -35, -18, -18, -11,  15,   4, -17,
    -12,  17,  14,  17,  17,  38,  23,  11,
    10,  17,  23,  15,  20,  45,  44,  13,
    -8,  22,  24,  27,  26,  33,  26,   3,
    -18,  -4,  21,  24,  27,  23,   9, -11,
    -19,  -3,  11,  21,  23,  16,   7,  -9,
    -27, -11,   4,  13,  14,   4,  -5, -17,
    -53, -34, -21, -11, -28, -14, -24, -43
};

// Bitboards
U64 SetMask[64];
U64 ClearMask[64];
U64 RankMask[8];
U64 FileMask[8];
U64 KingAttacks[64];
U64 KnightAttacks[64];
U64 PawnAttacks[2][64]; // [side][sq]

// Zobrist keys
U64 PieceKeys[13][64];
U64 SideKey;
U64 CastleKeys[16];
U64 EnPasKeys[64];

// Eval Weights
const int PhaseWeights[13] = { 0, 1, 1, 2, 4, 0, 0, 1, 1, 2, 4, 0, 0 };

// --- EVALUATION HELPERS ---
static inline int GetPieceValMG(int pType, int sq) {
    int type = pType % 6;
    int val = 0;
    if(pType < 6) { // White Piece
        val = PieceValuesMG[type] + PstMG[type][sq ^ 56];
        if(type == P) val += (sq / 8) * 10;
        return val;
    } else { // Black Piece
        val = PieceValuesMG[type] + PstMG[type][sq];
        if(type == P) val += (7 - (sq / 8)) * 10;
        return -val; // Return negative because it's from White's perspective
    }
}

static inline int GetPieceValEG(int pType, int sq) {
    int type = pType % 6;
    int val = 0;
    if(pType < 6) { 
        val = PieceValuesEG[type] + PstEG[type][sq ^ 56];
        if(type == P) val += (sq / 8) * 10;
        return val;
    } else { 
        val = PieceValuesEG[type] + PstEG[type][sq];
        if(type == P) val += (7 - (sq / 8)) * 10;
        return -val; 
    }
}

void InitEval(Board *pos) {
    pos->eval_mg = 0;
    pos->eval_eg = 0;
    pos->phase = 0;
    for(int i=0; i<12; ++i) {
        U64 bb = pos->pieces[i];
        while(bb) {
            int sq = __builtin_ctzll(bb);
            bb &= bb - 1;
            pos->eval_mg += GetPieceValMG(i, sq);
            pos->eval_eg += GetPieceValEG(i, sq);
            pos->phase += PhaseWeights[i % 6];
        }
    }
}

// RAND
U64 Rand64() {
    return (U64)rand() | ((U64)rand() << 15) | ((U64)rand() << 30) | ((U64)rand() << 45) | ((U64)rand() << 60);
}

void InitBitMasks() {
    for(int i = 0; i < 64; ++i) {
        SetMask[i] = (1ULL << i);
        ClearMask[i] = ~SetMask[i];
    }
    for(int i = 0; i < 8; ++i) {
        RankMask[i] = 0;
        FileMask[i] = 0;
        for(int j = 0; j < 8; ++j) {
            RankMask[i] |= SetMask[i * 8 + j];
            FileMask[i] |= SetMask[j * 8 + i];
        }
    }
}

int IsSafe(int sq) { return sq >= 0 && sq < 64; }

void InitAttacks() {
    // Knight
    int kn_offsets[] = { -17, -15, -10, -6, 6, 10, 15, 17 };
    for(int i = 0; i < 64; ++i) {
        KnightAttacks[i] = 0;
        for(int j = 0; j < 8; ++j) {
            int target = i + kn_offsets[j];
            if(IsSafe(target)) {
                // Check dist to avoid wrap around
                int r1 = i / 8; int c1 = i % 8;
                int r2 = target / 8; int c2 = target % 8;
                if(abs(r1-r2) <= 2 && abs(c1-c2) <= 2)
                    KnightAttacks[i] |= SetMask[target];
            }
        }
    }
    // King
    int k_offsets[] = { -9, -8, -7, -1, 1, 7, 8, 9 };
    for(int i = 0; i < 64; ++i) {
        KingAttacks[i] = 0;
        for(int j = 0; j < 8; ++j) {
            int target = i + k_offsets[j];
            if(IsSafe(target)) {
                int r1 = i / 8; int c1 = i % 8;
                int r2 = target / 8; int c2 = target % 8;
                if(abs(r1-r2) <= 1 && abs(c1-c2) <= 1)
                    KingAttacks[i] |= SetMask[target];
            }
        }
    }
    // Pawn
    for(int i = 0; i < 64; ++i) {
        PawnAttacks[WHITE][i] = 0;
        PawnAttacks[BLACK][i] = 0;
        // White attacks (up)
        if(IsSafe(i + 7) && (i % 8) != 0) PawnAttacks[WHITE][i] |= SetMask[i+7]; // Capture left
        if(IsSafe(i + 9) && (i % 8) != 7) PawnAttacks[WHITE][i] |= SetMask[i+9]; // Capture right
        // Black attacks (down)
        if(IsSafe(i - 7) && (i % 8) != 7) PawnAttacks[BLACK][i] |= SetMask[i-7]; 
        if(IsSafe(i - 9) && (i % 8) != 0) PawnAttacks[BLACK][i] |= SetMask[i-9]; 
    }
}

void InitZobrist() {
    for(int i = 0; i < 13; ++i)
        for(int j = 0; j < 64; ++j)
            PieceKeys[i][j] = Rand64();
    SideKey = Rand64();
    for(int i = 0; i < 16; ++i)
            CastleKeys[i] = Rand64();
        for(int i = 0; i < 64; ++i)
            EnPasKeys[i] = Rand64();
}

void InitPST() {
    // Copy Raw to PstMG/EG
    for(int i=0; i<64; ++i) { PstMG[P][i] = RawPstMgP[i]; PstEG[P][i] = RawPstEgP[i]; }
    for(int i=0; i<64; ++i) { PstMG[N][i] = RawPstMgN[i]; PstEG[N][i] = RawPstEgN[i]; }
    for(int i=0; i<64; ++i) { PstMG[B][i] = RawPstMgB[i]; PstEG[B][i] = RawPstEgB[i]; }
    for(int i=0; i<64; ++i) { PstMG[R][i] = RawPstMgR[i]; PstEG[R][i] = RawPstEgR[i]; }
    for(int i=0; i<64; ++i) { PstMG[Q][i] = RawPstMgQ[i]; PstEG[Q][i] = RawPstEgQ[i]; }
    for(int i=0; i<64; ++i) { PstMG[K][i] = RawPstMgK[i]; PstEG[K][i] = RawPstEgK[i]; }
}

// --- MAGIC BITBOARDS IMPLEMENTATION ---
U64 RookMasks[64];
U64 RookMagics[64];
U64 RookAttacks[64][4096];
int RookBits[64];

U64 BishopMasks[64];
U64 BishopMagics[64];
U64 BishopAttacks[64][512];
int BishopBits[64];

U64 GetRookMask(int sq) {
    U64 mask = 0;
    int r = sq / 8; int f = sq % 8;
    for(int i = r + 1; i <= 6; i++) mask |= (1ULL << (i * 8 + f));
    for(int i = r - 1; i >= 1; i--) mask |= (1ULL << (i * 8 + f));
    for(int i = f + 1; i <= 6; i++) mask |= (1ULL << (r * 8 + i));
    for(int i = f - 1; i >= 1; i--) mask |= (1ULL << (r * 8 + i));
    return mask;
}

U64 GetBishopMask(int sq) {
    U64 mask = 0;
    int r = sq / 8; int f = sq % 8;
    for(int i=r+1, j=f+1; i<=6 && j<=6; i++, j++) mask |= (1ULL << (i * 8 + j));
    for(int i=r+1, j=f-1; i<=6 && j>=1; i++, j--) mask |= (1ULL << (i * 8 + j));
    for(int i=r-1, j=f+1; i>=1 && j<=6; i--, j++) mask |= (1ULL << (i * 8 + j));
    for(int i=r-1, j=f-1; i>=1 && j>=1; i--, j--) mask |= (1ULL << (i * 8 + j));
    return mask;
}

U64 GetRookAttacksSlow(int sq, U64 block) {
    U64 attacks = 0;
    int r = sq / 8; int f = sq % 8;
    for(int i = r + 1; i <= 7; i++) { U64 bit = 1ULL << (i * 8 + f); attacks |= bit; if(block & bit) break; }
    for(int i = r - 1; i >= 0; i--) { U64 bit = 1ULL << (i * 8 + f); attacks |= bit; if(block & bit) break; }
    for(int i = f + 1; i <= 7; i++) { U64 bit = 1ULL << (r * 8 + i); attacks |= bit; if(block & bit) break; }
    for(int i = f - 1; i >= 0; i--) { U64 bit = 1ULL << (r * 8 + i); attacks |= bit; if(block & bit) break; }
    return attacks;
}

U64 GetBishopAttacksSlow(int sq, U64 block) {
    U64 attacks = 0;
    int r = sq / 8; int f = sq % 8;
    for(int i=r+1, j=f+1; i<=7 && j<=7; i++, j++) { U64 bit = 1ULL << (i * 8 + j); attacks |= bit; if(block & bit) break; }
    for(int i=r+1, j=f-1; i<=7 && j>=0; i++, j--) { U64 bit = 1ULL << (i * 8 + j); attacks |= bit; if(block & bit) break; }
    for(int i=r-1, j=f+1; i>=0 && j<=7; i--, j++) { U64 bit = 1ULL << (i * 8 + j); attacks |= bit; if(block & bit) break; }
    for(int i=r-1, j=f-1; i>=0 && j>=0; i--, j--) { U64 bit = 1ULL << (i * 8 + j); attacks |= bit; if(block & bit) break; }
    return attacks;
}

U64 SetOccupancy(int index, int bitsInMask, U64 mask) {
    U64 occ = 0;
    for(int i = 0; i < bitsInMask; i++) {
        int sq = __builtin_ctzll(mask);
        mask &= mask - 1;
        if(index & (1 << i)) occ |= (1ULL << sq);
    }
    return occ;
}

U64 MagicRandomU64() {
    U64 u1 = (U64)(rand() & 0xFFFF); U64 u2 = (U64)(rand() & 0xFFFF);
    U64 u3 = (U64)(rand() & 0xFFFF); U64 u4 = (U64)(rand() & 0xFFFF);
    return u1 | (u2 << 16) | (u3 << 32) | (u4 << 48);
}

U64 FindMagic(int sq, int bits, int isRook) {
    U64 mask = isRook ? GetRookMask(sq) : GetBishopMask(sq);
    int num_occs = 1 << bits;
    U64 occs[4096];
    U64 attacks[4096];
    U64 used[4096];

    for(int i=0; i<num_occs; i++) {
        occs[i] = SetOccupancy(i, bits, mask);
        attacks[i] = isRook ? GetRookAttacksSlow(sq, occs[i]) : GetBishopAttacksSlow(sq, occs[i]);
    }

    for(int i=0; i<100000000; i++) {
        U64 magic = MagicRandomU64() & MagicRandomU64() & MagicRandomU64();
        if(__builtin_popcountll((mask * magic) & 0xFF00000000000000ULL) < 6) continue;

        int fail = 0;
        for(int j=0; j<4096; j++) used[j] = 0;

        for(int j=0; j<num_occs; j++) {
            int index = (occs[j] * magic) >> (64 - bits);
            if(used[index] == 0) used[index] = attacks[j];
            else if(used[index] != attacks[j]) { fail = 1; break; }
        }
        if(!fail) return magic;
    }
    return 0ULL;
}

void InitMagics() {
    for(int sq=0; sq<64; sq++) {
        BishopMasks[sq] = GetBishopMask(sq);
        RookMasks[sq] = GetRookMask(sq);
        BishopBits[sq] = __builtin_popcountll(BishopMasks[sq]);
        RookBits[sq] = __builtin_popcountll(RookMasks[sq]);
        
        BishopMagics[sq] = FindMagic(sq, BishopBits[sq], 0);
        RookMagics[sq] = FindMagic(sq, RookBits[sq], 1);

        int b_occs = 1 << BishopBits[sq];
        for(int i=0; i<b_occs; i++) {
            U64 occ = SetOccupancy(i, BishopBits[sq], BishopMasks[sq]);
            int index = (occ * BishopMagics[sq]) >> (64 - BishopBits[sq]);
            BishopAttacks[sq][index] = GetBishopAttacksSlow(sq, occ);
        }

        int r_occs = 1 << RookBits[sq];
        for(int i=0; i<r_occs; i++) {
            U64 occ = SetOccupancy(i, RookBits[sq], RookMasks[sq]);
            int index = (occ * RookMagics[sq]) >> (64 - RookBits[sq]);
            RookAttacks[sq][index] = GetRookAttacksSlow(sq, occ);
        }
    }
}

U64 GetBishopAttacks(int sq, U64 occ) {
    occ &= BishopMasks[sq];
    occ *= BishopMagics[sq];
    occ >>= (64 - BishopBits[sq]);
    return BishopAttacks[sq][occ];
}

U64 GetRookAttacks(int sq, U64 occ) {
    occ &= RookMasks[sq];
    occ *= RookMagics[sq];
    occ >>= (64 - RookBits[sq]);
    return RookAttacks[sq][occ];
}

U64 GetQueenAttacks(int sq, U64 occ) {
    return GetBishopAttacks(sq, occ) | GetRookAttacks(sq, occ);
}
// --- END MAGIC BITBOARDS ---

void AllInit() {
    srand((unsigned int)time(NULL)); // Initialise l'aléatoire pour le livre
    InitBitMasks();
    InitAttacks();
    InitZobrist();
    InitPST();
    InitMagics(); // On initialise les magics au démarrage
}

// Utils
int GetRank(int sq) { return sq / 8; }
int GetFile(int sq) { return sq % 8; }
int GetPieceAt(const Board *pos, int sq, int side) {
    (void)side;
    if (sq < 0 || sq >= 64) return EMPTY;
    return pos->board[sq];
}

// Board Logic
void ResetBoard(Board *pos) {
    for(int i=0; i<64; ++i) pos->board[i] = EMPTY;
    for(int i=0; i<13; ++i) pos->pieces[i] = 0ULL;
    pos->pawns[0] = pos->pawns[1] = pos->pawns[2] = 0ULL;
    pos->king_sq[0] = pos->king_sq[1] = NO_SQ;
    pos->side = WHITE;
    pos->enPas = NO_SQ;
    pos->fiftyMove = 0;
    pos->ply = 0;
    pos->hisPly = 0;
    pos->castlePerm = 0;
    pos->posKey = 0ULL;
    pos->accumStack[0].computed[WHITE] = 0;
    pos->accumStack[0].computed[BLACK] = 0;
}

U64 GeneratePosKey(const Board *pos) {
    U64 key = 0;
    for(int p=0; p<12; ++p) {
        U64 bb = pos->pieces[p];
        while(bb) {
            int sq = __builtin_ctzll(bb);
            key ^= PieceKeys[p][sq];
            bb &= bb - 1;
        }
    }
    if(pos->side == WHITE) key ^= SideKey;
    if(pos->enPas != NO_SQ && pos->enPas < 64) {
            key ^= EnPasKeys[pos->enPas];
        }
    key ^= CastleKeys[pos->castlePerm];
    return key;
}

void ParseFen(char *fen, Board *pos) {
    ResetBoard(pos);
    int rank = 7;
    int file = 0;
    
    while((rank >= 0) && *fen) {
        int count = 1;
        switch(*fen) {
            case 'p': pos->board[rank*8+file] = p; pos->pieces[p] |= SetMask[rank*8+file]; pos->pawns[BLACK] |= SetMask[rank*8+file]; file++; break;
            case 'r': pos->board[rank*8+file] = r; pos->pieces[r] |= SetMask[rank*8+file]; file++; break;
            case 'n': pos->board[rank*8+file] = n; pos->pieces[n] |= SetMask[rank*8+file]; file++; break;
            case 'b': pos->board[rank*8+file] = b; pos->pieces[b] |= SetMask[rank*8+file]; file++; break;
            case 'k': pos->board[rank*8+file] = k; pos->pieces[k] |= SetMask[rank*8+file]; pos->king_sq[BLACK] = rank*8+file; file++; break;
            case 'q': pos->board[rank*8+file] = q; pos->pieces[q] |= SetMask[rank*8+file]; file++; break;
            case 'P': pos->board[rank*8+file] = P; pos->pieces[P] |= SetMask[rank*8+file]; pos->pawns[WHITE] |= SetMask[rank*8+file]; file++; break;
            case 'R': pos->board[rank*8+file] = R; pos->pieces[R] |= SetMask[rank*8+file]; file++; break;
            case 'N': pos->board[rank*8+file] = N; pos->pieces[N] |= SetMask[rank*8+file]; file++; break;
            case 'B': pos->board[rank*8+file] = B; pos->pieces[B] |= SetMask[rank*8+file]; file++; break;
            case 'K': pos->board[rank*8+file] = K; pos->pieces[K] |= SetMask[rank*8+file]; pos->king_sq[WHITE] = rank*8+file; file++; break;
            case 'Q': pos->board[rank*8+file] = Q; pos->pieces[Q] |= SetMask[rank*8+file]; file++; break;
            case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8':
                count = *fen - '0';
                file += count;
                break;
            case '/':
            case ' ':
                rank--;
                file = 0;
                fen++;
                continue;
        }
        fen++;
    }
    
    // fen points to 'w' or 'b' here because the loop consumed the space as a rank separator
    // case ' ': rank--; file = 0; fen++; continue;
    // So fen is at 'w'.
    
    pos->side = (*fen == 'w') ? WHITE : BLACK;
    fen += 2;
    
    // Castle
    while(*fen != ' ') {
        switch(*fen) {
            case 'K': pos->castlePerm |= WKCA; break;
            case 'Q': pos->castlePerm |= WQCA; break;
            case 'k': pos->castlePerm |= BKCA; break;
            case 'q': pos->castlePerm |= BQCA; break;
        }
        fen++;
    }
    fen++;
    
    // EnPas
    if(*fen != '-') {
        file = fen[0] - 'a';
        rank = fen[1] - '0' - 1;
        pos->enPas = rank*8+file;
    }
    
    pos->pawns[BOTH] = pos->pawns[WHITE] | pos->pawns[BLACK];
    pos->posKey = GeneratePosKey(pos);
    InitEval(pos);
    RefreshAccumulator(&pos->accumStack[pos->hisPly], pos, WHITE);
    RefreshAccumulator(&pos->accumStack[pos->hisPly], pos, BLACK);
}

// Move Gen Helpers
int SqAttacked(int sq, int side, const Board *pos) {
    U64 occ = pos->pawns[BOTH];
    for(int i=0; i<12; ++i) occ |= pos->pieces[i];

    // Pawn
    if(side == WHITE) {
        if(PawnAttacks[BLACK][sq] & pos->pieces[P]) return 1;
    } else {
        if(PawnAttacks[WHITE][sq] & pos->pieces[p]) return 1;
    }
    
    // Knight
    if(KnightAttacks[sq] & pos->pieces[side == WHITE ? N : n]) return 1;
    
    // King
    if(KingAttacks[sq] & pos->pieces[side == WHITE ? K : k]) return 1;
    
    // Sliders : Tours et Dames
    U64 rq = pos->pieces[side == WHITE ? R : r] | pos->pieces[side == WHITE ? Q : q];
    if (rq && (GetRookAttacks(sq, occ) & rq)) return 1;
    
    // Sliders : Fous et Dames
    U64 bq = pos->pieces[side == WHITE ? B : b] | pos->pieces[side == WHITE ? Q : q];
    if (bq && (GetBishopAttacks(sq, occ) & bq)) return 1;
    
    return 0;
}

// Move Gen
void AddMove(Move *list, int *count, int from, int to, int capture, int prom, int type) {
    list[*count].from = from;
    list[*count].to = to;
    list[*count].capture = capture;
    list[*count].promoted = prom;
    list[*count].move_type = type;
    (*count)++;
}

void GenerateMoves(const Board *pos, Move *moveList, int *moveCount) {
    *moveCount = 0;
    int side = pos->side;
    int enemy = (side == WHITE) ? BLACK : WHITE;
    
    U64 myPieces = 0, allPieces = 0, occ = 0;
    for(int i=0; i<6; ++i) myPieces |= pos->pieces[side == WHITE ? i : i+6];
    for(int i=0; i<12; ++i) allPieces |= pos->pieces[i];
    occ = allPieces;
    
    // Pawns
    int pIdx = (side == WHITE) ? P : p;
    U64 pawns = pos->pieces[pIdx];
    int rankDir = (side == WHITE) ? 1 : -1;
    int startRank = (side == WHITE) ? 1 : 6;
    int promRank = (side == WHITE) ? 7 : 0;
    
    while(pawns) {
        int from = __builtin_ctzll(pawns);
        pawns &= pawns - 1;
        
        int r = from / 8;
        int c = from % 8;
        int forward = from + rankDir * 8;
        
        // Single Push
        if(IsSafe(forward) && !(occ & SetMask[forward])) {
            int isProm = (forward / 8 == promRank);
            if(isProm) {
                AddMove(moveList, moveCount, from, forward, EMPTY, (side==WHITE?Q:q), 0);
                AddMove(moveList, moveCount, from, forward, EMPTY, (side==WHITE?R:r), 0);
                AddMove(moveList, moveCount, from, forward, EMPTY, (side==WHITE?B:b), 0);
                AddMove(moveList, moveCount, from, forward, EMPTY, (side==WHITE?N:n), 0);
            } else {
                AddMove(moveList, moveCount, from, forward, EMPTY, EMPTY, 0);
                // Double Push
                if(r == startRank) {
                    int doublePush = forward + rankDir * 8;
                    if(!(occ & SetMask[doublePush])) {
                        AddMove(moveList, moveCount, from, doublePush, EMPTY, EMPTY, 0);
                    }
                }
            }
        }
        
        // Captures
        int caps[2] = { from + rankDir * 8 - 1, from + rankDir * 8 + 1 };
        for(int k=0; k<2; ++k) {
            int to = caps[k];
            int toFile = to % 8;
            if(!IsSafe(to)) continue;
            // Check wrapping
            if(abs(toFile - c) > 1) continue;
            
            int captured = EMPTY;
            // Normal Capture (Ultra Rapide)
            if (occ & SetMask[to]) {
                captured = GetPieceAt(pos, to, enemy);
            }
            
            if(captured != EMPTY) {
                int isProm = (to / 8 == promRank);
                if(isProm) {
                    AddMove(moveList, moveCount, from, to, captured, (side==WHITE?Q:q), 0);
                    AddMove(moveList, moveCount, from, to, captured, (side==WHITE?R:r), 0);
                    AddMove(moveList, moveCount, from, to, captured, (side==WHITE?B:b), 0);
                    AddMove(moveList, moveCount, from, to, captured, (side==WHITE?N:n), 0);
                } else {
                    AddMove(moveList, moveCount, from, to, captured, EMPTY, 0);
                }
            }
            
            // En Passant
            if(to == pos->enPas) {
                AddMove(moveList, moveCount, from, to, (side==WHITE?p:P), EMPTY, 1); // 1 = flag for EP
            }
        }
    }
    
    // Non-sliding pieces (N, K)
    int piecesToGen[] = { N, K };
    if(side == BLACK) { piecesToGen[0] = n; piecesToGen[1] = k; }
    
    for(int i=0; i<2; ++i) {
        int pType = piecesToGen[i];
        U64 bb = pos->pieces[pType];
        while(bb) {
            int from = __builtin_ctzll(bb);
            bb &= bb - 1;
            
            U64 attacks = (pType == N || pType == n) ? KnightAttacks[from] : KingAttacks[from];
            // Remove own pieces
            attacks &= ~myPieces;
            
            while(attacks) {
                int to = __builtin_ctzll(attacks);
                attacks &= attacks - 1;
                
                int captured = EMPTY;
                // Capture Cavalier/Roi (Ultra Rapide)
                if (occ & SetMask[to]) {
                    captured = GetPieceAt(pos, to, enemy);
                }
                AddMove(moveList, moveCount, from, to, captured, EMPTY, 0);
            }
        }
    }
    
    // Sliding pieces (B, R, Q) via Magic Bitboards
    int sliders[] = { B, R, Q };
    if(side == BLACK) { sliders[0] = b; sliders[1] = r; sliders[2] = q; }
    
    for(int i=0; i<3; ++i) {
        int pType = sliders[i];
        U64 bb = pos->pieces[pType];
        while(bb) {
            int from = __builtin_ctzll(bb);
            bb &= bb - 1;
            
            // Obtenir les attaques en temps O(1)
            U64 attacks = 0;
            if(pType == B || pType == b) attacks = GetBishopAttacks(from, occ);
            else if(pType == R || pType == r) attacks = GetRookAttacks(from, occ);
            else attacks = GetQueenAttacks(from, occ);
            
            attacks &= ~myPieces; // On ne capture pas ses propres pièces
            
            while(attacks) {
                int to = __builtin_ctzll(attacks);
                attacks &= attacks - 1;
                
                int captured = EMPTY;
                // Capture Sliders (Ultra Rapide)
                if(occ & SetMask[to]) {
                    captured = GetPieceAt(pos, to, enemy);
                }
                
                AddMove(moveList, moveCount, from, to, captured, EMPTY, 0);
            }
        }
    }
    
    // Castling
    if(side == WHITE) {
        if(pos->castlePerm & WKCA) {
            if(!(occ & SetMask[F1]) && !(occ & SetMask[G1])) {
                if(!SqAttacked(E1, BLACK, pos) && !SqAttacked(F1, BLACK, pos)) {
                     AddMove(moveList, moveCount, E1, G1, EMPTY, EMPTY, 2); // 2 = Castle
                }
            }
        }
        if(pos->castlePerm & WQCA) {
            if(!(occ & SetMask[D1]) && !(occ & SetMask[C1]) && !(occ & SetMask[B1])) {
                 if(!SqAttacked(E1, BLACK, pos) && !SqAttacked(D1, BLACK, pos)) {
                      AddMove(moveList, moveCount, E1, C1, EMPTY, EMPTY, 2);
                 }
            }
        }
    } else {
        if(pos->castlePerm & BKCA) {
            if(!(occ & SetMask[F8]) && !(occ & SetMask[G8])) {
                if(!SqAttacked(E8, WHITE, pos) && !SqAttacked(F8, WHITE, pos)) {
                     AddMove(moveList, moveCount, E8, G8, EMPTY, EMPTY, 2);
                }
            }
        }
        if(pos->castlePerm & BQCA) {
             if(!(occ & SetMask[D8]) && !(occ & SetMask[C8]) && !(occ & SetMask[B8])) {
                 if(!SqAttacked(E8, WHITE, pos) && !SqAttacked(D8, WHITE, pos)) {
                      AddMove(moveList, moveCount, E8, C8, EMPTY, EMPTY, 2);
                 }
            }
        }
    }
}

void GenerateCaptures(const Board *pos, Move *moveList, int *moveCount) {
    *moveCount = 0;
    int side = pos->side;
    int enemy = (side == WHITE) ? BLACK : WHITE;
    
    U64 myPieces = 0, enemyPieces = 0;
    for(int i=0; i<6; ++i) {
        myPieces |= pos->pieces[side == WHITE ? i : i+6];
        enemyPieces |= pos->pieces[side == WHITE ? i+6 : i];
    }
    U64 occ = myPieces | enemyPieces;
    
    // Pawns
    int pIdx = (side == WHITE) ? P : p;
    U64 pawns = pos->pieces[pIdx];
    int rankDir = (side == WHITE) ? 1 : -1;
    int promRank = (side == WHITE) ? 7 : 0;
    
    while(pawns) {
        int from = __builtin_ctzll(pawns);
        pawns &= pawns - 1;
        
        int r = from / 8;
        int c = from % 8;
        
        // Push (Uniquement pour les promotions dans la recherche de captures)
        int forward = from + rankDir * 8;
        if(IsSafe(forward) && !(occ & SetMask[forward])) {
            if(forward / 8 == promRank) {
                AddMove(moveList, moveCount, from, forward, EMPTY, (side==WHITE?Q:q), 0);
                AddMove(moveList, moveCount, from, forward, EMPTY, (side==WHITE?R:r), 0);
                AddMove(moveList, moveCount, from, forward, EMPTY, (side==WHITE?B:b), 0);
                AddMove(moveList, moveCount, from, forward, EMPTY, (side==WHITE?N:n), 0);
            }
        }
        
        // Captures
        int caps[2] = { from + rankDir * 8 - 1, from + rankDir * 8 + 1 };
        for(int k=0; k<2; ++k) {
            int to = caps[k];
            if(!IsSafe(to)) continue;
            int toFile = to % 8;
            if(abs(toFile - c) > 1) continue;
            
            int captured = EMPTY;
            if (enemyPieces & SetMask[to]) {
                captured = GetPieceAt(pos, to, enemy);
            }
            
            if(captured != EMPTY) {
                if(to / 8 == promRank) {
                    AddMove(moveList, moveCount, from, to, captured, (side==WHITE?Q:q), 0);
                    AddMove(moveList, moveCount, from, to, captured, (side==WHITE?R:r), 0);
                    AddMove(moveList, moveCount, from, to, captured, (side==WHITE?B:b), 0);
                    AddMove(moveList, moveCount, from, to, captured, (side==WHITE?N:n), 0);
                } else {
                    AddMove(moveList, moveCount, from, to, captured, EMPTY, 0);
                }
            }
            
            // En Passant
            if(to == pos->enPas) {
                AddMove(moveList, moveCount, from, to, (side==WHITE?p:P), EMPTY, 1);
            }
        }
    }
    
    // Non-sliding pieces (N, K)
    int piecesToGen[] = { N, K };
    if(side == BLACK) { piecesToGen[0] = n; piecesToGen[1] = k; }
    
    for(int i=0; i<2; ++i) {
        int pType = piecesToGen[i];
        U64 bb = pos->pieces[pType];
        while(bb) {
            int from = __builtin_ctzll(bb);
            bb &= bb - 1;
            
            U64 attacks = (pType == N || pType == n) ? KnightAttacks[from] : KingAttacks[from];
            attacks &= enemyPieces; // UNIQUEMENT LES PIÈCES ENNEMIES (Captures)
            
            while(attacks) {
                int to = __builtin_ctzll(attacks);
                attacks &= attacks - 1;
                int captured = GetPieceAt(pos, to, enemy);
                AddMove(moveList, moveCount, from, to, captured, EMPTY, 0);
            }
        }
    }
    
    // Sliding pieces (B, R, Q) via Magic Bitboards
    int sliders[] = { B, R, Q };
    if(side == BLACK) { sliders[0] = b; sliders[1] = r; sliders[2] = q; }
    
    for(int i=0; i<3; ++i) {
        int pType = sliders[i];
        U64 bb = pos->pieces[pType];
        while(bb) {
            int from = __builtin_ctzll(bb);
            bb &= bb - 1;
            
            U64 attacks = 0;
            if(pType == B || pType == b) attacks = GetBishopAttacks(from, occ);
            else if(pType == R || pType == r) attacks = GetRookAttacks(from, occ);
            else attacks = GetQueenAttacks(from, occ);
            
            attacks &= enemyPieces; // UNIQUEMENT LES PIÈCES ENNEMIES (Captures)
            
            while(attacks) {
                int to = __builtin_ctzll(attacks);
                attacks &= attacks - 1;
                int captured = GetPieceAt(pos, to, enemy);
                AddMove(moveList, moveCount, from, to, captured, EMPTY, 0);
            }
        }
    }
}

int MakeMove(Board *pos, Move move) {
    if(pos->hisPly >= 1024) return 0;
    
    // Preserve history
    pos->historyPosKey[pos->hisPly] = pos->posKey;
    pos->historyMove[pos->hisPly] = (move.from) | (move.to << 6);
    pos->historyCastle[pos->hisPly] = pos->castlePerm;
    pos->historyFifty[pos->hisPly] = pos->fiftyMove;
    pos->historyEnPas[pos->hisPly] = pos->enPas;
    pos->historyEvalMG[pos->hisPly] = pos->eval_mg;
    pos->historyEvalEG[pos->hisPly] = pos->eval_eg;
    pos->historyPhase[pos->hisPly] = pos->phase;
    
    pos->ply++;
    pos->hisPly++;
    
    int from = move.from;
    int to = move.to;
    int side = pos->side;
    int pType = pos->board[from];
    
    if(pType == EMPTY) return 0;
    
    // Remove old en-passant and castling keys from hash
    if(pos->enPas != NO_SQ) pos->posKey ^= EnPasKeys[pos->enPas];
    pos->posKey ^= CastleKeys[pos->castlePerm];
    
    // --- INCREMENTAL EVAL & HASH UPDATE ---
    pos->eval_mg -= GetPieceValMG(pType, from);
    pos->eval_eg -= GetPieceValEG(pType, from);
    pos->posKey ^= PieceKeys[pType][from];
    
    if(move.capture != EMPTY) {
        int capSq = to;
        if(move.move_type == 1) capSq = (side == WHITE) ? to - 8 : to + 8;
        pos->eval_mg -= GetPieceValMG(move.capture, capSq);
        pos->eval_eg -= GetPieceValEG(move.capture, capSq);
        pos->phase -= PhaseWeights[move.capture % 6];
        pos->posKey ^= PieceKeys[move.capture][capSq];
    }
    
    if(move.promoted != EMPTY) {
        pos->eval_mg += GetPieceValMG(move.promoted, to);
        pos->eval_eg += GetPieceValEG(move.promoted, to);
        pos->phase += PhaseWeights[move.promoted % 6];
        pos->phase -= PhaseWeights[P]; 
        pos->posKey ^= PieceKeys[move.promoted][to];
    } else {
        pos->eval_mg += GetPieceValMG(pType, to);
        pos->eval_eg += GetPieceValEG(pType, to);
        pos->posKey ^= PieceKeys[pType][to];
    }
    
    if(move.move_type == 2) {
        if(to == G1) { 
            pos->eval_mg -= GetPieceValMG(R, H1); pos->eval_eg -= GetPieceValEG(R, H1); pos->eval_mg += GetPieceValMG(R, F1); pos->eval_eg += GetPieceValEG(R, F1); 
            pos->posKey ^= PieceKeys[R][H1] ^ PieceKeys[R][F1];
        } else if(to == C1) { 
            pos->eval_mg -= GetPieceValMG(R, A1); pos->eval_eg -= GetPieceValEG(R, A1); pos->eval_mg += GetPieceValMG(R, D1); pos->eval_eg += GetPieceValEG(R, D1); 
            pos->posKey ^= PieceKeys[R][A1] ^ PieceKeys[R][D1];
        } else if(to == G8) { 
            pos->eval_mg -= GetPieceValMG(r, H8); pos->eval_eg -= GetPieceValEG(r, H8); pos->eval_mg += GetPieceValMG(r, F8); pos->eval_eg += GetPieceValEG(r, F8); 
            pos->posKey ^= PieceKeys[r][H8] ^ PieceKeys[r][F8];
        } else if(to == C8) { 
            pos->eval_mg -= GetPieceValMG(r, A8); pos->eval_eg -= GetPieceValEG(r, A8); pos->eval_mg += GetPieceValMG(r, D8); pos->eval_eg += GetPieceValEG(r, D8); 
            pos->posKey ^= PieceKeys[r][A8] ^ PieceKeys[r][D8];
        }
    }
    // --- END INCREMENTAL EVAL ---

    // Remove piece from 'from'
    pos->pieces[pType] &= ClearMask[from];
    pos->board[from] = EMPTY;
    if(pType == P || pType == p) pos->pawns[side == WHITE ? 0 : 1] &= ClearMask[from];
    if(pType == K || pType == k) pos->king_sq[side == WHITE ? 0 : 1] = to;
    
    // Handle Capture
    if(move.capture != EMPTY) {
        if(move.move_type == 1) { // EP
             int capSq = (side == WHITE) ? to - 8 : to + 8;
             pos->pieces[move.capture] &= ClearMask[capSq];
             pos->pawns[side == WHITE ? 1 : 0] &= ClearMask[capSq];
             pos->board[capSq] = EMPTY;
        } else {
             pos->pieces[move.capture] &= ClearMask[to];
             if(move.capture == P || move.capture == p) pos->pawns[side == WHITE ? 1 : 0] &= ClearMask[to];
        }
        pos->fiftyMove = 0;
    } else {
        pos->fiftyMove++;
    }
    
    if(pType == P || pType == p) pos->fiftyMove = 0;
    
    // Place piece at 'to'
    if(move.promoted != EMPTY) {
        pos->pieces[move.promoted] |= SetMask[to];
        pos->board[to] = move.promoted;
    } else {
        pos->pieces[pType] |= SetMask[to];
        pos->board[to] = pType;
        if(pType == P || pType == p) pos->pawns[side == WHITE ? 0 : 1] |= SetMask[to];
    }
    
    // Castling Move Rook Handling
    if(move.move_type == 2) {
        if(to == G1) { pos->pieces[R] &= ClearMask[H1]; pos->pieces[R] |= SetMask[F1]; pos->board[H1] = EMPTY; pos->board[F1] = R; }
        if(to == C1) { pos->pieces[R] &= ClearMask[A1]; pos->pieces[R] |= SetMask[D1]; pos->board[A1] = EMPTY; pos->board[D1] = R; }
        if(to == G8) { pos->pieces[r] &= ClearMask[H8]; pos->pieces[r] |= SetMask[F8]; pos->board[H8] = EMPTY; pos->board[F8] = r; }
        if(to == C8) { pos->pieces[r] &= ClearMask[A8]; pos->pieces[r] |= SetMask[D8]; pos->board[A8] = EMPTY; pos->board[D8] = r; }
    }
    
    // Update NNUE Accumulator for the new ply
    int capSq = to;
    if (move.move_type == 1) capSq = (side == WHITE) ? to - 8 : to + 8;
    UpdateAccumulatorMove(&pos->accumStack[pos->hisPly], &pos->accumStack[pos->hisPly - 1], pos,
                          pType, from, to, move.capture, capSq, move.promoted);

    // If castling, update the moved Rook in the opponent's accumulator
    if (move.move_type == 2) {
        int rFrom = NO_SQ, rTo = NO_SQ;
        if (to == G1)      { rFrom = H1; rTo = F1; }
        else if (to == C1) { rFrom = A1; rTo = D1; }
        else if (to == G8) { rFrom = H8; rTo = F8; }
        else if (to == C8) { rFrom = A8; rTo = D8; }
        UpdateAccumulatorCastlingRook(&pos->accumStack[pos->hisPly], pos, side, rFrom, rTo);
    }
    
    // Update Castling Perms
    if(from == E1 || to == E1) pos->castlePerm &= ~(WKCA | WQCA);
    if(from == H1 || to == H1) pos->castlePerm &= ~WKCA;
    if(from == A1 || to == A1) pos->castlePerm &= ~WQCA;
    
    if(from == E8 || to == E8) pos->castlePerm &= ~(BKCA | BQCA);
    if(from == H8 || to == H8) pos->castlePerm &= ~BKCA;
    if(from == A8 || to == A8) pos->castlePerm &= ~BQCA;
    
    // Update EnPas
    pos->enPas = NO_SQ;
    if((pType == P) && (to - from == 16)) pos->enPas = from + 8;
    if((pType == p) && (from - to == 16)) pos->enPas = from - 8;
    
    // XOR in new enPas, new castlePerm, and side
    if(pos->enPas != NO_SQ) pos->posKey ^= EnPasKeys[pos->enPas];
    pos->posKey ^= CastleKeys[pos->castlePerm];
    pos->posKey ^= SideKey;
    
    // Update Side & Pawn BB
    pos->side ^= 1;
    pos->pawns[BOTH] = pos->pawns[WHITE] | pos->pawns[BLACK];
    
    // Check legality (King attacked)
    if(SqAttacked(pos->king_sq[side == WHITE ? 0 : 1], pos->side, pos)) {
        TakeMove(pos, move);
        return 0;
    }
    
    return 1;
}

void TakeMove(Board *pos, Move move) {
    pos->side ^= 1;
    int side = pos->side;
    
    int from = move.from;
    int to = move.to;
    int pType = EMPTY;
    
    if(move.promoted != EMPTY) {
        pType = move.promoted;
        pos->pieces[pType] &= ClearMask[to];
        pos->board[to] = EMPTY;
        
        pType = (side == WHITE ? P : p);
        pos->pieces[pType] |= SetMask[from];
        pos->pawns[side == WHITE ? 0 : 1] |= SetMask[from];
        pos->board[from] = pType;
    } else {
        pType = pos->board[to];

        pos->pieces[pType] &= ClearMask[to];
        pos->pieces[pType] |= SetMask[from];
        pos->board[to] = EMPTY;
        pos->board[from] = pType;
        
        if(pType == P || pType == p) {
            pos->pawns[side == WHITE ? 0 : 1] &= ClearMask[to];
            pos->pawns[side == WHITE ? 0 : 1] |= SetMask[from];
        }
        if(pType == K || pType == k) pos->king_sq[side == WHITE ? 0 : 1] = from;
    }
    
    // Restore captured
    if(move.capture != EMPTY) {
        int capSq = to;
        if(move.move_type == 1) { // EP
             capSq = (side == WHITE) ? to - 8 : to + 8;
             pos->pieces[move.capture] |= SetMask[capSq];
             pos->pawns[side == WHITE ? 1 : 0] |= SetMask[capSq];
             pos->board[capSq] = move.capture;
        } else {
             pos->pieces[move.capture] |= SetMask[to];
             if(move.capture == P || move.capture == p) pos->pawns[side == WHITE ? 1 : 0] |= SetMask[to];
             pos->board[to] = move.capture;
        }
    }
    
    // Castling Restore
    if(move.move_type == 2) {
        if(to == G1) { pos->pieces[R] &= ClearMask[F1]; pos->pieces[R] |= SetMask[H1]; pos->board[F1] = EMPTY; pos->board[H1] = R; }
        if(to == C1) { pos->pieces[R] &= ClearMask[D1]; pos->pieces[R] |= SetMask[A1]; pos->board[D1] = EMPTY; pos->board[A1] = R; }
        if(to == G8) { pos->pieces[r] &= ClearMask[F8]; pos->pieces[r] |= SetMask[H8]; pos->board[F8] = EMPTY; pos->board[H8] = r; }
        if(to == C8) { pos->pieces[r] &= ClearMask[D8]; pos->pieces[r] |= SetMask[A8]; pos->board[D8] = EMPTY; pos->board[A8] = r; }
    }
    
    // Restore State
    pos->castlePerm = pos->historyCastle[pos->hisPly - 1];
    pos->fiftyMove = pos->historyFifty[pos->hisPly - 1];
    pos->enPas = pos->historyEnPas[pos->hisPly - 1];
    pos->posKey = pos->historyPosKey[pos->hisPly - 1];
    
    pos->eval_mg = pos->historyEvalMG[pos->hisPly - 1];
    pos->eval_eg = pos->historyEvalEG[pos->hisPly - 1];
    pos->phase = pos->historyPhase[pos->hisPly - 1];
    
    pos->pawns[BOTH] = pos->pawns[WHITE] | pos->pawns[BLACK];
    
    pos->ply--;
    pos->hisPly--;
}

void PrintBoard(const Board *pos) {
    printf("\nBoard:\n");
    for(int r=7; r>=0; r--) {
        printf("%d  ", r+1);
        for(int f=0; f<8; f++) {
            int sq = r*8+f;
            char c = '.';
            for(int i=0; i<12; ++i) {
                if(pos->pieces[i] & SetMask[sq]) {
                    char pChars[] = "PNBRQKpnbrqk";
                    c = pChars[i];
                    break;
                }
            }
            printf("%c ", c);
        }
        printf("\n");
    }
    printf("\n   a b c d e f g h\n\n");
    printf("Side: %d\n", pos->side);
    printf("KingSq: W=%llu B=%llu\n", pos->king_sq[0], pos->king_sq[1]);
}