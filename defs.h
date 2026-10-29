#ifndef DEFS_H
#define DEFS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include "nnue.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

// Types
typedef unsigned long long U64;

// Constants
#define NAME "Liora v10.4 C"
#define AUTHOR "LFM"
#define MAX_MOVES 256
#define MAX_DEPTH 64
#define INF 100000
#define MATE_SCORE 90000

enum { WHITE, BLACK, BOTH };
enum { P, N, B, R, Q, K, p, n, b, r, q, k, EMPTY };
enum { A1, B1, C1, D1, E1, F1, G1, H1,
       A2, B2, C2, D2, E2, F2, G2, H2,
       A3, B3, C3, D3, E3, F3, G3, H3,
       A4, B4, C4, D4, E4, F4, G4, H4,
       A5, B5, C5, D5, E5, F5, G5, H5,
       A6, B6, C6, D6, E6, F6, G6, H6,
       A7, B7, C7, D7, E7, F7, G7, H7,
       A8, B8, C8, D8, E8, F8, G8, H8, NO_SQ, OFFBOARD };

// Castle Permissions
// 1 = WK, 2 = WQ, 4 = BK, 8 = BQ
enum { WKCA = 1, WQCA = 2, BKCA = 4, BQCA = 8 };

typedef struct {
    int from;
    int to;
    int promoted;
    int capture; // Piece captured (for undo)
    int move_type; // Flag for special moves (EnPas, Castle, etc)
} Move;

// Encoded Move Integer for TT and History
// 0-5: to, 6-11: from, 12-15: prom, 16-19: cap type, 20: ep, 21: castling, 22: check
// Simplified: We'll use a struct in logic but pack it for TT if needed. 
// For this engine, we'll keep it simple.

typedef struct Board {
    int board[64];  // Fast Mailbox representation (O(1) piece lookup)
    U64 pieces[13]; // 0-11 for pieces, 12 reserved
    U64 pawns[3];   // 0=White, 1=Black, 2=Both
    U64 king_sq[2]; 
    int side;
    int enPas;
    int castlePerm;
    int fiftyMove;
    int ply;
    int hisPly; // Total half-moves
    U64 posKey; // Zobrist
    
    // Incremental Eval Variables
    int eval_mg;
    int eval_eg;
    int phase;
    
    // History arrays for unmake
    U64 historyPosKey[1024];
    int historyMove[1024];
    int historyCastle[1024];
    int historyFifty[1024];
    int historyEnPas[1024];
    int historyEvalMG[1024];
    int historyEvalEG[1024];
    int historyPhase[1024];
    Accumulator accumStack[1024];
} Board;

typedef struct {
    U64 key;
    int16_t score;
    uint8_t depth;
    uint8_t flag;
    uint8_t age;
    uint8_t move_from;
    uint8_t move_to;
    uint8_t move_prom;
} TTEntry;

typedef struct {
    TTEntry *entries;
    int numEntries;
} TT;

// Globals
extern uint8_t TTAge;
extern int PvArray[MAX_DEPTH];
extern int History[2][64][64];
extern Move KillerMoves[MAX_DEPTH][2];
extern int LmrTable[MAX_DEPTH][MAX_MOVES];
extern TT TranspositionTable;
extern int NNUE_Loaded;
extern U64 SetMask[64];
extern U64 ClearMask[64];
extern U64 RankMask[8];
extern U64 FileMask[8];
extern U64 SideKey;
extern U64 EnPasKeys[64];

// Magic Bitboards
extern U64 RookMasks[64];
extern U64 RookMagics[64];
extern U64 RookAttacks[64][4096];
extern int RookBits[64];
extern U64 KnightAttacks[64];
extern U64 KingAttacks[64];
extern U64 PawnAttacks[2][64];
extern U64 BishopMasks[64];
extern U64 BishopMagics[64];
extern U64 BishopAttacks[64][512];
extern int BishopBits[64];

U64 GetBishopAttacks(int sq, U64 occ);
U64 GetRookAttacks(int sq, U64 occ);
U64 GetQueenAttacks(int sq, U64 occ);
void InitMagics();

// Prototypes
extern const int PhaseWeights[13];

// init.c / board.c
void AllInit();
void ParseFen(char *fen, Board *pos);
void InitEval(Board *pos);
void PrintBoard(const Board *pos);
U64 GeneratePosKey(const Board *pos);
int MakeMove(Board *pos, Move move);
void TakeMove(Board *pos, Move move);
void GenerateMoves(const Board *pos, Move *moveList, int *moveCount);
void GenerateCaptures(const Board *pos, Move *moveList, int *moveCount);
int IsRepetition(const Board *pos);
int SqAttacked(int sq, int side, const Board *pos);

// search.c
void InitLMR();
int SEE(const Board *pos, Move move, int threshold);
void SearchPosition(Board *pos, int depth, int wtime, int btime, int winc, int binc, int movestogo, int movetime);
int Evaluate(const Board *pos);
void InitTT(int mb);
void ClearTT();
long long GetTimeMs();

// polyglot
extern char BookPath[512];
Move GetBookMove(const Board *pos);

// utils
int GetRank(int sq);
int GetFile(int sq);
int GetPieceAt(const Board *pos, int sq, int side);

// Datagen
void RunDataGen(int numGames, int maxPlies, int searchDepth, const char *outFile);

#endif