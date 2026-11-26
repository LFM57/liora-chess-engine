#include "defs.h"

// Globals
int PvArray[MAX_DEPTH];
int History[2][64][64];
Move KillerMoves[MAX_DEPTH][2];
int EvalStack[1024];
TT TranspositionTable;
_Static_assert(sizeof(TTEntry) == 16, "TTEntry must be 16 bytes");
_Static_assert(sizeof(TTCluster) == 64, "TTCluster must be 64 bytes");
uint8_t TTAge = 0;
uint64_t GameNoiseSeed = 0;
int EvalNoiseAmp = 0;
int MaxQDepth = 0;
static double GamePieceJitterSeed[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
static int ActivePieceJitter[5] = { 0, 0, 0, 0, 0 };
static int HasActivePieceJitter = 0;

static uint64_t SplitMix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static uint64_t NextRngU64(uint64_t *state) {
    if (*state == 0) {
        uint64_t r0 = (uint64_t)(unsigned int)rand();
        uint64_t r1 = (uint64_t)(unsigned int)rand();
        uint64_t r2 = (uint64_t)(unsigned int)rand();
        uint64_t r3 = (uint64_t)(unsigned int)rand();
        *state = SplitMix64((uint64_t)GetTimeMs() ^ (r0 << 48) ^ (r1 << 32) ^ (r2 << 16) ^ r3);
        if (*state == 0) *state = 0x9e3779b97f4a7c15ULL;
    }
    *state = SplitMix64(*state);
    return *state;
}

void ReseedGameNoise(void) {
    static uint64_t rngState = 0;
    GameNoiseSeed = NextRngU64(&rngState);
    for (int t = 0; t < 5; ++t) {
        uint64_t h = SplitMix64(GameNoiseSeed ^ (0x9e3779b97f4a7c15ULL * (uint64_t)(t + 1)));
        double u = (double)(h >> 11) * (1.0 / 9007199254740992.0);
        GamePieceJitterSeed[t] = 2.0 * u - 1.0;
    }
}

static int PositionNoise(U64 posKey, uint64_t seed, int amp) {
    if (amp <= 0) return 0;
    uint64_t h = SplitMix64(posKey ^ seed);
    uint64_t span = (uint64_t)(2 * amp + 1);
    return (int)((h >> 32) % span) - amp;
}

typedef struct {
    int elo;
    int maxDepth;
    uint64_t maxNodes;
    int maxQDepth;
    double tempWdl;
    int evalNoise;
    int pieceJitter;
    int maxBookPly;
} EloLevel;

typedef struct {
    int maxDepth;
    uint64_t maxNodes;
    int maxQDepth;
    double tempWdl;
    int evalNoise;
    int pieceJitter;
    int maxBookPly;
} StrengthParams;

static const EloLevel EloTable[] = {
    { 1200,  2,      45ULL, 1, 0.220, 50, 25,  0 },
    { 1400,  3,     100ULL, 2, 0.150, 30, 15,  0 },
    { 1600,  5,     250ULL, 4, 0.095, 15,  8,  4 },
    { 1800,  7,     700ULL, 0, 0.055,  0,  0,  6 },
    { 2100, 10,    2500ULL, 0, 0.030,  0,  0,  9 },
    { 2400, 13,   10000ULL, 0, 0.015,  0,  0, 14 },
    { 2750, 18,   45000ULL, 0, 0.000,  0,  0, 20 },
    { 3000, 24,  200000ULL, 0, 0.000,  0,  0, -1 },
    { 3100, 64,       0ULL, 0, 0.000,  0,  0, -1 }
};

static StrengthParams GetStrengthParams(int elo) {
    const int numLevels = (int)(sizeof(EloTable) / sizeof(EloTable[0]));
    if (elo <= EloTable[0].elo) {
        StrengthParams p = {
            EloTable[0].maxDepth, EloTable[0].maxNodes, EloTable[0].maxQDepth,
            EloTable[0].tempWdl, EloTable[0].evalNoise, EloTable[0].pieceJitter,
            EloTable[0].maxBookPly
        };
        return p;
    }
    if (elo >= EloTable[numLevels - 1].elo) {
        StrengthParams p = {
            EloTable[numLevels - 1].maxDepth, EloTable[numLevels - 1].maxNodes,
            EloTable[numLevels - 1].maxQDepth, EloTable[numLevels - 1].tempWdl,
            EloTable[numLevels - 1].evalNoise, EloTable[numLevels - 1].pieceJitter,
            EloTable[numLevels - 1].maxBookPly
        };
        return p;
    }

    for (int i = 0; i < numLevels - 1; ++i) {
        if (elo >= EloTable[i].elo && elo < EloTable[i + 1].elo) {
            const EloLevel *a = &EloTable[i];
            const EloLevel *b = &EloTable[i + 1];
            double t = (double)(elo - a->elo) / (double)(b->elo - a->elo);

            StrengthParams p;
            p.maxDepth = (int)lround(a->maxDepth * (1.0 - t) + b->maxDepth * t);
            p.tempWdl = a->tempWdl * (1.0 - t) + b->tempWdl * t;
            p.evalNoise = (int)lround(a->evalNoise * (1.0 - t) + b->evalNoise * t);
            p.pieceJitter = (int)lround(a->pieceJitter * (1.0 - t) + b->pieceJitter * t);

            // Strength scales roughly linearly with log(nodes)
            if (a->maxNodes == 0 && b->maxNodes == 0) {
                p.maxNodes = 0;
            } else {
                double aNodes = (a->maxNodes > 0) ? (double)a->maxNodes : 2000000.0;
                double bNodes = (b->maxNodes > 0) ? (double)b->maxNodes : 2000000.0;
                p.maxNodes = (uint64_t)llround(exp(log(aNodes) * (1.0 - t) + log(bNodes) * t));
                if (p.maxNodes < 1) p.maxNodes = 1;
            }

            // 0 means unlimited Q-depth (>= 1800); substitute 8 when bracketing a positive anchor
            if ((a->maxQDepth == 0 && b->maxQDepth == 0) || elo >= 1800) {
                p.maxQDepth = 0;
            } else {
                double aQ = (a->maxQDepth > 0) ? (double)a->maxQDepth : 8.0;
                double bQ = (b->maxQDepth > 0) ? (double)b->maxQDepth : 8.0;
                p.maxQDepth = (int)lround(aQ * (1.0 - t) + bQ * t);
                if (p.maxQDepth < 1) p.maxQDepth = 1;
            }

            // -1 means unlimited book ply; substitute 24 when bracketing a finite anchor
            if (a->maxBookPly < 0 && b->maxBookPly < 0) {
                p.maxBookPly = -1;
            } else {
                double aBook = (a->maxBookPly < 0) ? 24.0 : (double)a->maxBookPly;
                double bBook = (b->maxBookPly < 0) ? 24.0 : (double)b->maxBookPly;
                p.maxBookPly = (int)lround(aBook * (1.0 - t) + bBook * t);
                if (p.maxBookPly < 0) p.maxBookPly = 0;
            }

            if (p.maxDepth < 1) p.maxDepth = 1;
            if (p.tempWdl < 0.0) p.tempWdl = 0.0;
            if (p.evalNoise < 0) p.evalNoise = 0;
            if (p.pieceJitter < 0) p.pieceJitter = 0;
            return p;
        }
    }

    StrengthParams fallback = { MAX_DEPTH, 0ULL, 0, 0.0, 0, 0, -1 };
    return fallback;
}

// Eval weights
extern int PieceValuesMG[13];
extern int PieceValuesEG[13];
extern int PstMG[6][64];
extern int PstEG[6][64];

// MVV-LVA Table [attacker][victim]
int MvvLva[6][6];
void InitMvvLva() {
    int pieceVal[6] = { 100, 300, 300, 500, 900, 10000 };
    for(int attacker = P; attacker <= K; ++attacker) {
        for(int victim = P; victim <= K; ++victim) {
            // Victim value * 100 - Attacker value
            MvvLva[attacker][victim] = pieceVal[victim] * 100 - pieceVal[attacker];
        }
    }
}

int LmrTable[MAX_DEPTH][MAX_MOVES];
void InitLMR() {
    for (int d = 0; d < MAX_DEPTH; ++d) {
        for (int m = 0; m < MAX_MOVES; ++m) {
            if (d >= 1 && m >= 1) {
                LmrTable[d][m] = (int)(0.75 + log(d) * log(m) / 2.25);
            } else {
                LmrTable[d][m] = 0;
            }
        }
    }
}

static int EvaluateRaw(const Board *pos) {
    if (NNUE_Loaded) {
        return EvaluateNNUE(pos);
    }

    int mg = pos->eval_mg;
    int eg = pos->eval_eg;

    U64 wp = pos->pieces[P];
    U64 bp = pos->pieces[p];

    // 1. PAIRE DE FOUS (Bonus : 30 MG / 50 EG)
    if (__builtin_popcountll(pos->pieces[B]) >= 2) { mg += 30; eg += 50; }
    if (__builtin_popcountll(pos->pieces[b]) >= 2) { mg -= 30; eg -= 50; }

    // 2. ANALYSE DES PIONS BLANCS
    U64 bb = wp;
    while(bb) {
        int sq = __builtin_ctzll(bb);
        bb &= bb - 1;
        int f = sq % 8;
        int r = sq / 8;
        
        U64 adjFiles = 0;
        if(f > 0) adjFiles |= FileMask[f-1];
        if(f < 7) adjFiles |= FileMask[f+1];

        // Pion isolé (Aucun pion blanc sur les colonnes adjacentes)
        if ((wp & adjFiles) == 0) { mg -= 15; eg -= 15; }
        
        // Pion Doublé (Un autre pion de NOTRE couleur devant nous sur la même colonne)
        U64 aheadMask = (r < 7) ? (0xFFFFFFFFFFFFFFFFULL << (8 * (r + 1))) : 0ULL;
        if (wp & FileMask[f] & aheadMask) { mg -= 10; eg -= 20; }

        // Pion Passé (Aucun pion noir devant sur sa colonne ou adjacentes)
        if (((FileMask[f] | adjFiles) & aheadMask & bp) == 0) {
            int pBonusMg[8] = {0, 5, 10, 20, 35, 60, 100, 0};
            int pBonusEg[8] = {0, 10, 25, 45, 70, 100, 150, 0};
            mg += pBonusMg[r];
            eg += pBonusEg[r];
        }
    }

    // 3. ANALYSE DES PIONS NOIRS
    bb = bp;
    while(bb) {
        int sq = __builtin_ctzll(bb);
        bb &= bb - 1;
        int f = sq % 8;
        int r = sq / 8;
        
        U64 adjFiles = 0;
        if(f > 0) adjFiles |= FileMask[f-1];
        if(f < 7) adjFiles |= FileMask[f+1];

        // Pion isolé
        if ((bp & adjFiles) == 0) { mg += 15; eg += 15; } // favorable aux blancs
        
        U64 aheadMask = (1ULL << (8 * r)) - 1;
        
        // Pion Doublé Noir (Pions noirs devant sur la même colonne)
        if (bp & FileMask[f] & aheadMask) { mg += 10; eg += 20; }

        // Pion Passé
        if (((FileMask[f] | adjFiles) & aheadMask & wp) == 0) {
            int pBonusMg[8] = {0, 100, 60, 35, 20, 10, 5, 0};
            int pBonusEg[8] = {0, 150, 100, 70, 45, 25, 10, 0};
            mg -= pBonusMg[r];
            eg -= pBonusEg[r];
        }
    }
    
    // 4. TOURS SUR COLONNES OUVERTES / SEMI-OUVERTES
    for(int side = 0; side <= 1; ++side) {
        U64 rooks = pos->pieces[side == WHITE ? R : r];
        U64 myPawns = pos->pieces[side == WHITE ? P : p];
        U64 enemyPawns = pos->pieces[side == WHITE ? p : P];
        int sign = (side == WHITE) ? 1 : -1;
        
        while(rooks) {
            int sq = __builtin_ctzll(rooks);
            rooks &= rooks - 1;
            int f = sq % 8;
            
            // Y a-t-il un de MES pions sur cette colonne ?
            if ((myPawns & FileMask[f]) == 0) {
                // Y a-t-il un pion ENNEMI ?
                if ((enemyPawns & FileMask[f]) == 0) {
                    // Colonne totalement ouverte
                    mg += sign * 15;
                    eg += sign * 15;
                } else {
                    // Colonne semi-ouverte (seulement bloquée par l'adversaire)
                    mg += sign * 10;
                    eg += sign * 10;
                }
            }
        }
    }

    // 5. MOBILITÉ DES PIÈCES MINEURES (Le contrôle du terrain)
    int mobMg = 0, mobEg = 0;
    U64 occ = pos->pawns[BOTH];
    for(int i=0; i<12; ++i) occ |= pos->pieces[i];
    
    for(int side = 0; side <= 1; ++side) {
        int sign = (side == WHITE) ? 1 : -1;
        U64 myPieces = 0;
        for(int i=0; i<6; ++i) myPieces |= pos->pieces[side == WHITE ? i : i+6];
        
        // Cavaliers : +4 points par case contrôlée
        U64 knights = pos->pieces[side == WHITE ? N : n];
        while(knights) {
            int sq = __builtin_ctzll(knights);
            knights &= knights - 1;
            U64 attacks = KnightAttacks[sq] & ~myPieces;
            int count = __builtin_popcountll(attacks);
            mobMg += sign * count * 4; mobEg += sign * count * 4;
        }
        
        // Fous : +3 points par case contrôlée (grâce aux Magic Bitboards !)
        U64 bishops = pos->pieces[side == WHITE ? B : b];
        while(bishops) {
            int sq = __builtin_ctzll(bishops);
            bishops &= bishops - 1;
            U64 attacks = GetBishopAttacks(sq, occ) & ~myPieces;
            int count = __builtin_popcountll(attacks);
            mobMg += sign * count * 3; mobEg += sign * count * 3;
        }
    }
    mg += mobMg;
    eg += mobEg;

    // 6. SÉCURITÉ DU ROI (King Safety)
    for(int side = 0; side <= 1; ++side) {
        int ksq = pos->king_sq[side];
        int k_f = ksq % 8;
        int k_r = ksq / 8;
        int sign = (side == WHITE) ? 1 : -1;
        int penalty = 0;
        
        // Si le roi est bloqué au centre (colonnes d ou e) en milieu de jeu
        if (k_f == 3 || k_f == 4) { 
            penalty += 20;
        }

        // Analyser les 3 colonnes autour du roi
        for (int f = k_f - 1; f <= k_f + 1; ++f) {
            if (f >= 0 && f <= 7) {
                U64 fileMask = FileMask[f];
                U64 myPawns = pos->pieces[side == WHITE ? P : p];
                U64 enemyPawns = pos->pieces[side == WHITE ? p : P];
                
                // Danger : Colonne totalement ouverte près du roi
                if ((fileMask & myPawns) == 0 && (fileMask & enemyPawns) == 0) {
                    penalty += 25; 
                }
                // Danger : Colonne semi-ouverte pointée vers le roi
                else if ((fileMask & myPawns) == 0) {
                    penalty += 15;
                }
                
                // Qualité du bouclier de pions
                U64 myPawnsOnFile = fileMask & myPawns;
                if (myPawnsOnFile) {
                    int p_sq = (side == WHITE) ? __builtin_ctzll(myPawnsOnFile) : 63 - __builtin_clzll(myPawnsOnFile);
                    int p_r = p_sq / 8;
                    int dist = abs(p_r - k_r);
                    
                    if (dist == 2) penalty += 10;
                    else if (dist > 2) penalty += 20;
                } else {
                    penalty += 20;
                }
            }
        }
        mg -= sign * penalty;
    }

    // Calcul de la phase de jeu (Tapered Eval)
    int phase = pos->phase > 24 ? 24 : pos->phase;
    int score = (mg * phase + eg * (24 - phase)) / 24;
    
    // Tempo Bonus : Petit avantage (10 centipions) au trait
    score += (pos->side == WHITE) ? 10 : -10;

    return (pos->side == WHITE) ? score : -score;
}

int Evaluate(const Board *pos) {
    int eval = EvaluateRaw(pos);
    if (HasActivePieceJitter) {
        int jitter = 0;
        for (int t = 0; t < 5; ++t) {
            if (ActivePieceJitter[t] != 0) {
                int diff = __builtin_popcountll(pos->pieces[t]) - __builtin_popcountll(pos->pieces[t + 6]);
                jitter += ActivePieceJitter[t] * diff;
            }
        }
        eval += (pos->side == WHITE) ? jitter : -jitter;
    }
    if (EvalNoiseAmp > 0) {
        eval += PositionNoise(pos->posKey, GameNoiseSeed, EvalNoiseAmp);
    }
    return eval;
}

// TT
void InitTT(int mb) {
    // Free existing memory if the GUI changes the hash size during runtime
    if (TranspositionTable.clusters != NULL) {
        free(TranspositionTable.clusters);
        TranspositionTable.clusters = NULL;
    }
    
    int size = (mb * 1024 * 1024) / sizeof(TTCluster);
    if (size < 1) size = 1;
    TranspositionTable.clusters = (TTCluster*)malloc(size * sizeof(TTCluster));
    TranspositionTable.numClusters = size;
    ClearTT();
}

void ClearTT() {
    TTAge = 0;
    if (!TranspositionTable.clusters) return;
    for(int i = 0; i < TranspositionTable.numClusters; ++i) {
        for(int j = 0; j < TT_CLUSTER_SIZE; ++j) {
            TranspositionTable.clusters[i].entries[j].key = 0;
            TranspositionTable.clusters[i].entries[j].depth = 0;
            TranspositionTable.clusters[i].entries[j].flag = 0;
            TranspositionTable.clusters[i].entries[j].score = 0;
            TranspositionTable.clusters[i].entries[j].age = 0;
            TranspositionTable.clusters[i].entries[j].move_from = NO_SQ;
            TranspositionTable.clusters[i].entries[j].move_to = NO_SQ;
            TranspositionTable.clusters[i].entries[j].move_prom = EMPTY;
        }
    }
}

void StoreTT(U64 key, int depth, int score, int flag, Move move, int ply) {
    if (!TranspositionTable.clusters) return;
    int index = key % TranspositionTable.numClusters;
    TTCluster *cluster = &TranspositionTable.clusters[index];
    
    if (score > MATE_SCORE - MAX_DEPTH) score += ply;
    else if (score < -MATE_SCORE + MAX_DEPTH) score -= ply;
    
    TTEntry *target = NULL;
    
    // 1. Check if entry already exists with same key in this cluster
    for (int i = 0; i < TT_CLUSTER_SIZE; ++i) {
        if (cluster->entries[i].key == key) {
            target = &cluster->entries[i];
            break;
        }
    }
    
    if (target) {
        // If entry exists and is from the same search with greater depth, don't overwrite with shallower
        if (target->age == TTAge && target->depth > depth && (flag != 0 || depth == 0)) {
            if (move.from != NO_SQ && target->move_from == NO_SQ) {
                target->move_from = move.from;
                target->move_to = move.to;
                target->move_prom = move.promoted;
            }
            return;
        }
    } else {
        // 2. Look for an empty slot
        for (int i = 0; i < TT_CLUSTER_SIZE; ++i) {
            if (cluster->entries[i].key == 0) {
                target = &cluster->entries[i];
                break;
            }
        }
        
        // 3. Replacement policy: select slot with lowest replacement priority
        if (!target) {
            int worstPriority = 1000000;
            for (int i = 0; i < TT_CLUSTER_SIZE; ++i) {
                int ageDiff = (int)(uint8_t)(TTAge - cluster->entries[i].age);
                int priority = (int)cluster->entries[i].depth - (ageDiff * 4);
                if (priority < worstPriority) {
                    worstPriority = priority;
                    target = &cluster->entries[i];
                }
            }
        }
    }
    
    int sameKey = (target->key == key);
    target->key = key;
    target->depth = depth;
    target->flag = flag;
    target->age = TTAge;
    
    int tt_score = score;
    if (score > MATE_SCORE - MAX_DEPTH) tt_score = 30000 + (score - MATE_SCORE);
    else if (score < -MATE_SCORE + MAX_DEPTH) tt_score = -30000 + (score - (-MATE_SCORE));
    else if (score > 30000) tt_score = 30000;
    else if (score < -30000) tt_score = -30000;
    target->score = (int16_t)tt_score;
    
    if (move.from != NO_SQ && !(flag == 1 && target->move_from != NO_SQ)) {
        target->move_from = move.from;
        target->move_to = move.to;
        target->move_prom = move.promoted;
    } else if (!sameKey) {
        target->move_from = NO_SQ;
        target->move_to = NO_SQ;
        target->move_prom = EMPTY;
    }
}

TTEntry* ProbeTT(U64 key) {
    if (!TranspositionTable.clusters) return NULL;
    int index = key % TranspositionTable.numClusters;
    TTCluster *cluster = &TranspositionTable.clusters[index];
    for(int i = 0; i < TT_CLUSTER_SIZE; ++i) {
        if(cluster->entries[i].key == key) {
            return &cluster->entries[i];
        }
    }
    return NULL;
}

// Search
long long GetTimeMs() {
    #ifdef _WIN32
    return (long long)GetTickCount();
    #else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long long)tv.tv_sec * 1000) + (tv.tv_usec / 1000);
    #endif
}

long long StartTime;
long long StopTime;
int StopSearch;
uint64_t Nodes;
uint64_t MaxNodes = 0;
int CurrentSearchDepth = 0;

void CheckTime() {
    if (MaxNodes > 0 && CurrentSearchDepth > 1 && Nodes >= MaxNodes) {
        StopSearch = 1;
    }
    // Vérifie le temps plus souvent (tous les 1024 noeuds au lieu de 2048)
    if((Nodes & 1023) == 0) {
        if(GetTimeMs() >= StopTime) StopSearch = 1;
    }
}

static const int see_values[13] = {
    100, 300, 300, 500, 900, 20000, // P, N, B, R, Q, K
    100, 300, 300, 500, 900, 20000, // p, n, b, r, q, k
    0                               // EMPTY
};

int SEE(const Board *pos, Move move, int threshold) {
    int from = move.from;
    int to = move.to;

    int moving_piece = pos->board[from];
    if (moving_piece == EMPTY) {
        moving_piece = GetPieceAt(pos, from, pos->side);
    }
    int attacker_value = see_values[moving_piece];

    int gain = 0;
    int is_ep = (move.move_type == 1);
    if (is_ep) {
        gain = 100; // Pawn
    } else {
        int captured = move.capture;
        if (captured == EMPTY) captured = pos->board[to];
        gain = see_values[captured];
    }

    if (move.promoted != EMPTY) {
        gain += see_values[move.promoted] - 100;
        attacker_value = see_values[move.promoted];
    }

    // Early exits
    if (gain - threshold < 0) return 0;
    if (gain - attacker_value >= threshold) return 1;

    // Build initial occupancy
    U64 occ = 0;
    for (int i = 0; i < 12; ++i) occ |= pos->pieces[i];

    // Remove from square
    occ ^= (1ULL << from);
    if (is_ep) {
        int capSq = (pos->side == WHITE) ? to - 8 : to + 8;
        occ ^= (1ULL << capSq);
    }

    int captured_val[32];
    int num_captures = 0;
    captured_val[num_captures++] = gain;
    int piece_on_to_val = attacker_value;

    int side = pos->side ^ 1;

    while (num_captures < 32) {
        int att_val = 0;
        int att_sq = NO_SQ;

        // 1. Pawn
        U64 pawns = (side == WHITE) ? (PawnAttacks[BLACK][to] & pos->pieces[P] & occ)
                                    : (PawnAttacks[WHITE][to] & pos->pieces[p] & occ);
        if (pawns) {
            att_val = 100;
            att_sq = __builtin_ctzll(pawns);
        } else {
            // 2. Knight
            U64 knights = KnightAttacks[to] & pos->pieces[side == WHITE ? N : n] & occ;
            if (knights) {
                att_val = 300;
                att_sq = __builtin_ctzll(knights);
            } else {
                // 3. Bishop
                U64 bishops = GetBishopAttacks(to, occ) & pos->pieces[side == WHITE ? B : b] & occ;
                if (bishops) {
                    att_val = 300;
                    att_sq = __builtin_ctzll(bishops);
                } else {
                    // 4. Rook
                    U64 rooks = GetRookAttacks(to, occ) & pos->pieces[side == WHITE ? R : r] & occ;
                    if (rooks) {
                        att_val = 500;
                        att_sq = __builtin_ctzll(rooks);
                    } else {
                        // 5. Queen
                        U64 queens = (GetBishopAttacks(to, occ) | GetRookAttacks(to, occ)) & pos->pieces[side == WHITE ? Q : q] & occ;
                        if (queens) {
                            att_val = 900;
                            att_sq = __builtin_ctzll(queens);
                        } else {
                            // 6. King
                            U64 king = KingAttacks[to] & pos->pieces[side == WHITE ? K : k] & occ;
                            if (king) {
                                int enemy = side ^ 1;
                                U64 enemy_pawns = (enemy == WHITE) ? (PawnAttacks[BLACK][to] & pos->pieces[P] & occ)
                                                                   : (PawnAttacks[WHITE][to] & pos->pieces[p] & occ);
                                U64 enemy_knights = KnightAttacks[to] & pos->pieces[enemy == WHITE ? N : n] & occ;
                                U64 enemy_bishops_queens = GetBishopAttacks(to, occ) & (pos->pieces[enemy == WHITE ? B : b] | pos->pieces[enemy == WHITE ? Q : q]) & occ;
                                U64 enemy_rooks_queens = GetRookAttacks(to, occ) & (pos->pieces[enemy == WHITE ? R : r] | pos->pieces[enemy == WHITE ? Q : q]) & occ;
                                U64 enemy_king = KingAttacks[to] & pos->pieces[enemy == WHITE ? K : k] & occ;

                                if (!enemy_pawns && !enemy_knights && !enemy_bishops_queens && !enemy_rooks_queens && !enemy_king) {
                                    att_val = 20000;
                                    att_sq = __builtin_ctzll(king);
                                }
                            }
                        }
                    }
                }
            }
        }

        if (att_sq == NO_SQ) break;

        captured_val[num_captures++] = piece_on_to_val;
        piece_on_to_val = att_val;
        occ ^= (1ULL << att_sq);
        side ^= 1;
    }

    int r[33];
    r[num_captures] = 0;
    for (int k = num_captures - 1; k >= 1; k--) {
        int score = captured_val[k] - r[k + 1];
        r[k] = (score > 0) ? score : 0;
    }
    r[0] = captured_val[0] - r[1];
    return (r[0] >= threshold);
}

int Quiescence(Board *pos, int alpha, int beta, int qply) {
    Nodes++;
    CheckTime();
    if(StopSearch) return 0;

    if (MaxQDepth > 0 && qply >= MaxQDepth) {
        return Evaluate(pos);
    }
    
    // TT Probe
    TTEntry* tte = ProbeTT(pos->posKey);
    Move bestMoveTT = {NO_SQ, NO_SQ, EMPTY, EMPTY, 0};
    if (tte && tte->key == pos->posKey) {
        int tt_score = tte->score;
        if (tt_score > 30000 - MAX_DEPTH) {
            tt_score = MATE_SCORE + (tt_score - 30000) - pos->ply;
        } else if (tt_score < -30000 + MAX_DEPTH) {
            tt_score = -MATE_SCORE + (tt_score - (-30000)) + pos->ply;
        } else if (tt_score > MATE_SCORE - MAX_DEPTH) {
            tt_score -= pos->ply;
        } else if (tt_score < -MATE_SCORE + MAX_DEPTH) {
            tt_score += pos->ply;
        }

        if (tte->flag == 0) return tt_score;
        if (tte->flag == 1 && tt_score <= alpha) return tt_score;
        if (tte->flag == 2 && tt_score >= beta) return tt_score;

        bestMoveTT.from = tte->move_from;
        bestMoveTT.to = tte->move_to;
        bestMoveTT.promoted = tte->move_prom;
    }

    int inCheck = SqAttacked(pos->king_sq[pos->side], (pos->side ^ 1), pos);
    int oldAlpha = alpha;
    
    int score = -INF;
    // On ne fait le "Stand-Pat" QUE si le roi n'est pas en échec
    if (!inCheck) {
        score = Evaluate(pos);
        if(score >= beta) {
            if (!(MaxQDepth > 0 && qply > 0)) {
                StoreTT(pos->posKey, 0, beta, 2, bestMoveTT, pos->ply);
            }
            return beta;
        }
        if(score > alpha) alpha = score;
    }
    
    Move moves[MAX_MOVES];
    int count = 0;
    
    // Si on est en échec, il faut générer TOUS les coups pour trouver une parade
    if (inCheck) {
        GenerateMoves(pos, moves, &count);
    } else {
        GenerateCaptures(pos, moves, &count);
    }
    
    // MVV-LVA Move Ordering in QSearch + TT move bonus
    int moveScores[MAX_MOVES];
    for(int i=0; i<count; ++i) {
        if(moves[i].from == bestMoveTT.from && moves[i].to == bestMoveTT.to && moves[i].promoted == bestMoveTT.promoted) {
            moveScores[i] = 1000000;
        } else if(moves[i].promoted != EMPTY) {
            moveScores[i] = 200000 + moves[i].promoted;
        } else if(moves[i].capture != EMPTY) {
            int pType = pos->board[moves[i].from];
            int attacker = (pType != EMPTY) ? (pType % 6) : P;
            moveScores[i] = 100000 + MvvLva[attacker][moves[i].capture % 6];
        } else {
            moveScores[i] = History[pos->side][moves[i].from][moves[i].to];
        }
    }
    
    int bestScore = score;
    Move bestMove = bestMoveTT;
    int legalMoves = 0;
    for(int i=0; i<count; ++i) {
        int maxS = -2000000000; int maxI = i;
        for(int j=i; j<count; ++j) {
            if(moveScores[j] > maxS) { maxS = moveScores[j]; maxI = j; }
        }
        Move temp = moves[i]; moves[i] = moves[maxI]; moves[maxI] = temp;
        int tempS = moveScores[i]; moveScores[i] = moveScores[maxI]; moveScores[maxI] = tempS;
        
        // Delta Pruning : Uniquement hors échec
        if (!inCheck && moves[i].promoted == EMPTY && moves[i].move_type != 1) {
            int capVal = PieceValuesMG[moves[i].capture % 6];
            if (score + capVal + 200 < alpha) {
                continue;
            }
        }
        
        // SEE Pruning : Prune losing captures in Quiescence
        if (!inCheck && moves[i].promoted == EMPTY && !SEE(pos, moves[i], 0)) {
            continue;
        }

        Move move = moves[i];
        if(!MakeMove(pos, move)) continue;
        legalMoves++;
        int val = -Quiescence(pos, -beta, -alpha, qply + 1);
        TakeMove(pos, move);
        
        if(StopSearch) return 0;
        
        if(val > bestScore) {
            bestScore = val;
            bestMove = move;
            if(val > alpha) {
                alpha = val;
                if(val >= beta) {
                    break;
                }
            }
        }
    }
    
    // Si on était en échec et qu'aucun coup n'est légal -> Mat !
    if (inCheck && legalMoves == 0) {
        return -MATE_SCORE + pos->ply;
    }
    
    int flag = 1; // Upper bound
    if (bestScore >= beta) flag = 2; // Lower bound
    else if (bestScore > oldAlpha) flag = 0; // Exact
    
    int ttScore = (flag == 1) ? alpha : bestScore;
    if (!(MaxQDepth > 0 && qply > 0)) {
        StoreTT(pos->posKey, 0, ttScore, flag, bestMove, pos->ply);
    }
    return (bestScore > alpha) ? bestScore : alpha;
}

int Negamax(Board *pos, int depth, int alpha, int beta, int allow_null, Move* pv) {
    // Init PV
    pv[0].from = NO_SQ; // Null move terminator
    
    if(StopSearch) return 0;
    Nodes++;
    
    if(pos->ply > 0 && (IsRepetition(pos) || pos->fiftyMove >= 100)) return 0;
    
    // Check Extension (étendue à tous les nœuds pour ne pas rater les combinaisons)
    int inCheck = SqAttacked(pos->king_sq[pos->side], (pos->side^1), pos);
    int isPvNode = (beta - alpha > 1);
    
    if(inCheck && pos->ply < MAX_DEPTH - 1) {
        depth++;
    }

    if(depth <= 0) return Quiescence(pos, alpha, beta, 0);
    
    CheckTime();
    
    // TT Probe
    TTEntry* tte = ProbeTT(pos->posKey);
    Move bestMoveTT = {NO_SQ, NO_SQ, EMPTY, EMPTY, 0};
    if(tte && tte->key == pos->posKey) {
        int score = tte->score;
        if (score > 30000 - MAX_DEPTH) {
            score = MATE_SCORE + (score - 30000) - pos->ply;
        } else if (score < -30000 + MAX_DEPTH) {
            score = -MATE_SCORE + (score - (-30000)) + pos->ply;
        } else if (score > MATE_SCORE - MAX_DEPTH) {
            score -= pos->ply;
        } else if (score < -MATE_SCORE + MAX_DEPTH) {
            score += pos->ply;
        }

        if(tte->depth >= depth) {
            int isPvNode = (beta - alpha > 1);
            if(!isPvNode) {
                if(tte->flag == 0) return score;
                if(tte->flag == 1 && score <= alpha) return score;
                if(tte->flag == 2 && score >= beta) return score;
            }
        }
        bestMoveTT.from = tte->move_from;
        bestMoveTT.to = tte->move_to;
        bestMoveTT.promoted = tte->move_prom;
    }

    // Internal Iterative Reduction (IIR)
    if (isPvNode && bestMoveTT.from == NO_SQ && depth >= 5) {
        depth--;
    }

    int ttMoveIsTactical = 0;
    if (bestMoveTT.from != NO_SQ) {
        if (bestMoveTT.promoted != EMPTY || 
            pos->board[bestMoveTT.to] != EMPTY || 
            (pos->enPas != NO_SQ && bestMoveTT.to == pos->enPas && (pos->board[bestMoveTT.from] % 6 == P))) {
            ttMoveIsTactical = 1;
        }
    }
    
    // Vérification anti-Zugzwang (on s'assure qu'il reste autre chose que des Pions)
    int has_major_pieces = 0;
    for(int p = 1; p <= 4; ++p) { // Cavaliers, Fous, Tours, Dames
        if(pos->pieces[pos->side == WHITE ? p : p + 6]) {
            has_major_pieces = 1;
            break;
        }
    }

    int static_eval = -INF;
    if (!inCheck) {
        static_eval = Evaluate(pos);
    }
    EvalStack[pos->ply] = static_eval;

    int improving = 0;
    if (!inCheck && pos->ply >= 2 && EvalStack[pos->ply - 2] != -INF) {
        improving = (static_eval > EvalStack[pos->ply - 2]);
    }

    // Reverse Futility Pruning (RFP)
    if (!isPvNode && !inCheck && depth <= 5 && allow_null && abs(beta) < MATE_SCORE - MAX_DEPTH) {
        int margin = 120 * depth - (improving && depth > 1 ? 40 : 0);
        if (static_eval - margin >= beta) {
            return beta;
        }
    }

    // Null Move Pruning (uniquement si l'évaluation statique bat déjà beta)
    if(!isPvNode && allow_null && pos->ply > 0 && depth >= 3 && !inCheck && has_major_pieces && static_eval >= beta && abs(beta) < MATE_SCORE - MAX_DEPTH && pos->hisPly < 1023) {
        // Make Null
        int savedEnPas = pos->enPas;
        int savedFifty = pos->fiftyMove;
        U64 savedKey = pos->posKey;
        
        pos->historyPosKey[pos->hisPly] = pos->posKey;
        pos->fiftyMove = 0;
        pos->side ^= 1;
        EvalStack[pos->ply] = -INF;
        pos->ply++;
        pos->hisPly++;
        pos->posKey ^= SideKey;
        if(pos->enPas != NO_SQ) {
            pos->posKey ^= EnPasKeys[pos->enPas];
            pos->enPas = NO_SQ;
        }
        
        // Record lazy no-op delta for null move instead of eagerly copying accumulator
        if (NNUE_Loaded) {
            Accumulator *acc = &pos->accumStack[pos->hisPly];
            acc->delta.numSub = 0;
            acc->delta.numAdd = 0;
            acc->computed[WHITE] = 0;
            acc->computed[BLACK] = 0;
        }
        
        Move nullPv[MAX_DEPTH]; // Dummy
        nullPv[0].from = NO_SQ;
        
        // NMP Adaptatif : La réduction de profondeur est proportionnelle à la profondeur actuelle
        // Plus on cherche profond, plus on coupe fort l'arbre (Minimum 3)
        int R = 3 + depth / 6; 
        int val = -Negamax(pos, depth - 1 - R, -beta, -beta + 1, 0, nullPv);
        
        // Undo null
        pos->side ^= 1;
        pos->ply--;
        EvalStack[pos->ply] = static_eval;
        pos->hisPly--;
        pos->enPas = savedEnPas;
        pos->fiftyMove = savedFifty;
        pos->posKey = savedKey;
        
        if(StopSearch) return 0;
        if(val >= beta) {
            return beta;
        }
    }
    
    Move moves[MAX_MOVES];
    int count = 0;
    GenerateMoves(pos, moves, &count);
    
    if(count == 0) {
        if(inCheck) return -MATE_SCORE + pos->ply;
        return 0; // Stalemate
    }
    
    // Move Ordering
    // Score moves
    int moveScores[MAX_MOVES];
    for(int i=0; i<count; ++i) {
        moveScores[i] = 0;
        if(moves[i].from == bestMoveTT.from && moves[i].to == bestMoveTT.to && moves[i].promoted == bestMoveTT.promoted) moveScores[i] = 2000000;
        else if(moves[i].promoted != EMPTY) moveScores[i] = 150000 + moves[i].promoted;
        else if(moves[i].capture != EMPTY) {
            int type = GetPieceAt(pos, moves[i].from, pos->side);
            int attacker = (type != EMPTY) ? (type % 6) : P;
            if (SEE(pos, moves[i], 0)) {
                moveScores[i] = 100000 + MvvLva[attacker][moves[i].capture % 6];
            } else {
                moveScores[i] = -100000 + MvvLva[attacker][moves[i].capture % 6];
            }
        }
        else {
            if(moves[i].from == KillerMoves[pos->ply][0].from && moves[i].to == KillerMoves[pos->ply][0].to) moveScores[i] = 90000;
            else if(moves[i].from == KillerMoves[pos->ply][1].from && moves[i].to == KillerMoves[pos->ply][1].to) moveScores[i] = 80000;
            else {
                int hScore = History[pos->side][moves[i].from][moves[i].to];
                moveScores[i] = (hScore > 70000) ? 70000 : hScore;
            }
        }
    }
    
    // Search Loop
    int bestScore = -INF;
    int movesSearched = 0;
    int movesPruned = 0;
    Move bestMove = {NO_SQ, NO_SQ, EMPTY, EMPTY, 0};
    int flag = 1; // Alpha
    
    Move quietMovesSearched[MAX_MOVES];
    int quietCount = 0;
    
    Move childPv[MAX_DEPTH];
    
    for(int i=0; i<count; ++i) {
        // Selection Sort one by one
        int maxS = -2000000000; int maxI = i;
        for(int j=i; j<count; ++j) {
            if(moveScores[j] > maxS) { maxS = moveScores[j]; maxI = j; }
        }
        Move temp = moves[i]; moves[i] = moves[maxI]; moves[maxI] = temp;
        int tempS = moveScores[i]; moveScores[i] = moveScores[maxI]; moveScores[maxI] = tempS;
        
        Move move = moves[i];
        int isKiller = (move.from == KillerMoves[pos->ply][0].from && move.to == KillerMoves[pos->ply][0].to) ||
                       (move.from == KillerMoves[pos->ply][1].from && move.to == KillerMoves[pos->ply][1].to);

        int canPrune = !isPvNode && !inCheck && movesSearched > 0 &&
                       bestScore > -MATE_SCORE + MAX_DEPTH && abs(alpha) < MATE_SCORE - MAX_DEPTH;
        int lmpPrune = 0;
        int futilityPrune = 0;
        int seePrune = 0;

        if (canPrune) {
            // LMP
            if (depth <= 3 && move.capture == EMPTY && move.promoted == EMPTY) {
                int lmpThreshold = 3 + 2 * depth * depth;
                if (movesSearched >= lmpThreshold) {
                    lmpPrune = 1;
                }
            }

            // Forward Futility Pruning (depth <= 3, reasonable margin, spare killers)
            if (!lmpPrune && depth <= 3 && !isKiller && move.capture == EMPTY && move.promoted == EMPTY) {
                int futilityMargin = 100 + 100 * depth;
                if (static_eval + futilityMargin <= alpha) {
                    futilityPrune = 1;
                }
            }

            // SEE Pruning (quadratic for quiets, linear for captures, exclude promotions)
            if (!lmpPrune && !futilityPrune && depth <= 5 && !isKiller && move.promoted == EMPTY) {
                int seeThreshold = (move.capture == EMPTY)
                                 ? (-20 * depth * depth) : (-100 * depth);
                if (!SEE(pos, move, seeThreshold)) {
                    seePrune = 1;
                }
            }
        }

        if(!MakeMove(pos, move)) continue;
        
        // Détecter si le coup joué donne échec
        int givesCheck = SqAttacked(pos->king_sq[pos->side], (pos->side ^ 1), pos);
        
        if (!givesCheck && (lmpPrune || futilityPrune || seePrune)) {
            movesPruned = 1;
            TakeMove(pos, move);
            continue;
        }
        
        movesSearched++;
        if (move.capture == EMPTY && move.promoted == EMPTY) {
            quietMovesSearched[quietCount++] = move;
        }
        childPv[0].from = NO_SQ;
        
        int score;
        if(movesSearched == 1) {
            // Full window search for first move
            score = -Negamax(pos, depth - 1, -beta, -alpha, 1, childPv);
        } else {
            // Late Move Reduction (LMR) + Principal Variation Search (PVS)
            int reduction = 0;
            if(depth >= 3 && movesSearched > (isPvNode ? 4 : 2) && move.capture == EMPTY && move.promoted == EMPTY && !inCheck && !givesCheck) {
                int d = (depth < MAX_DEPTH) ? depth : MAX_DEPTH - 1;
                int m = (movesSearched < MAX_MOVES) ? movesSearched : MAX_MOVES - 1;
                reduction = LmrTable[d][m];

                if (isPvNode) reduction--;

                if (!improving) reduction++;

                if (ttMoveIsTactical) reduction++;

                if (isKiller) {
                    reduction--;
                }

                int histReduction = History[pos->side ^ 1][move.from][move.to] / 4096;
                if (histReduction > 2) histReduction = 2;
                if (histReduction < -2) histReduction = -2;
                reduction -= histReduction;

                if (reduction < 0) reduction = 0;
                if (reduction > depth - 2) reduction = depth - 2;
            }
            
            // Step 1: Zero-Window search with reduction
            score = -Negamax(pos, depth - 1 - reduction, -alpha - 1, -alpha, 1, childPv);
            
            // Step 2: Re-search without reduction if reduced search beat alpha
            if(score > alpha && reduction > 0) {
                childPv[0].from = NO_SQ;
                score = -Negamax(pos, depth - 1, -alpha - 1, -alpha, 1, childPv);
            }
            
            // Step 3: Full window search if move beat alpha and did not already fail high
            if(score > alpha && score < beta) {
                childPv[0].from = NO_SQ;
                score = -Negamax(pos, depth - 1, -beta, -alpha, 1, childPv);
            }
        }
        
        TakeMove(pos, move);
        
        if(StopSearch) return 0;
        
        if(score > bestScore) {
            bestScore = score;
            if(score > alpha) {
                bestMove = move;
                alpha = score;
                flag = 0; // Exact
                
                // Update PV
                pv[0] = move;
                // Copy child PV
                int j = 0;
                while(childPv[j].from != NO_SQ && j < MAX_DEPTH - 2) {
                    pv[j+1] = childPv[j];
                    j++;
                }
                pv[j+1].from = NO_SQ; // Terminator
                
                if(alpha >= beta) {
                    // Store Killer / History
                    if(move.capture == EMPTY && move.promoted == EMPTY) {
                        if (move.from != KillerMoves[pos->ply][0].from || move.to != KillerMoves[pos->ply][0].to) {
                            KillerMoves[pos->ply][1] = KillerMoves[pos->ply][0];
                            KillerMoves[pos->ply][0] = move;
                        }
                        
                        // Clamped History Update to prevent table overflow & order inversions
                        int bonus = depth * depth;
                        if (bonus > 400) bonus = 400;
                        int cur = History[pos->side][move.from][move.to];
                        History[pos->side][move.from][move.to] = cur + bonus - (cur * bonus / 16384);
                        
                        for (int k = 0; k < quietCount; k++) {
                            if (quietMovesSearched[k].from == move.from && quietMovesSearched[k].to == move.to) continue;
                            int c = History[pos->side][quietMovesSearched[k].from][quietMovesSearched[k].to];
                            History[pos->side][quietMovesSearched[k].from][quietMovesSearched[k].to] = c - bonus - (c * bonus / 16384);
                        }
                    }
                    flag = 2; // Beta
                    break;
                }
            }
        }
    }
    
    if(movesSearched == 0) {
        if(inCheck) return -MATE_SCORE + pos->ply;
        return 0; // Stalemate
    }
    
    int ttScore = (flag == 1 && movesPruned && bestScore < alpha) ? alpha : bestScore;
    StoreTT(pos->posKey, depth, ttScore, flag, bestMove, pos->ply);
    return ttScore;
}

static double RandomUnitDouble(void) {
    static uint64_t rngState = 0;
    uint64_t r = NextRngU64(&rngState);
    return (double)(r >> 11) * (1.0 / 9007199254740992.0);
}

static void PrintMoveUci(Move move) {
    const char *files = "abcdefgh";
    const char *ranks = "12345678";
    printf("%c%c%c%c", files[move.from % 8], ranks[move.from / 8],
           files[move.to % 8], ranks[move.to / 8]);
    if (move.promoted != EMPTY) {
        char pChar = 'q';
        if (move.promoted == N || move.promoted == n) pChar = 'n';
        else if (move.promoted == R || move.promoted == r) pChar = 'r';
        else if (move.promoted == B || move.promoted == b) pChar = 'b';
        printf("%c", pChar);
    }
}

static void PrintUciScore(int score) {
    if (score > MATE_SCORE - MAX_DEPTH) {
        int plies = MATE_SCORE - score;
        int moves = (plies + 1) / 2;
        if (moves < 1) moves = 1;
        printf("score mate %d", moves);
    } else if (score < -MATE_SCORE + MAX_DEPTH) {
        int plies = MATE_SCORE + score;
        int moves = (plies + 1) / 2;
        if (moves < 1) moves = 1;
        printf("score mate -%d", moves);
    } else {
        printf("score cp %d", score);
    }
}

static double CpToWdl(int cp) {
    if (cp > 4000) cp = 4000;
    if (cp < -4000) cp = -4000;
    return 1.0 / (1.0 + exp(-(double)cp / 400.0));
}

static int WdlFloorCp(int bestScore, double tempWdl) {
    int mateFloor = -MATE_SCORE + MAX_DEPTH + 100;
    double targetWdl = CpToWdl(bestScore) - 4.0 * tempWdl;
    int floorCp;
    if (targetWdl <= 0.01) {
        floorCp = bestScore - 600;
    } else {
        if (targetWdl >= 0.999999) targetWdl = 0.999999;
        int rawFloor = (int)floor(400.0 * log(targetWdl / (1.0 - targetWdl)));
        int drop = bestScore - rawFloor;
        if (drop < 150) drop = 150;
        if (drop > 600) drop = 600;
        floorCp = bestScore - drop;
    }
    if (floorCp < mateFloor) floorCp = mateFloor;
    if (floorCp > bestScore) floorCp = bestScore;
    return floorCp;
}

typedef struct {
    Move move;
    int score;
    Move pv[MAX_DEPTH];
} RootCandidate;

static int SameMove(Move a, Move b) {
    return a.from == b.from && a.to == b.to && a.promoted == b.promoted;
}

static int ScoreRootMove(const Board *pos, Move move) {
    if (move.promoted != EMPTY) {
        return 150000 + move.promoted;
    }
    if (move.capture != EMPTY) {
        int pType = pos->board[move.from];
        int attacker = (pType != EMPTY) ? (pType % 6) : P;
        return 100000 + MvvLva[attacker][move.capture % 6];
    }
    int hScore = History[pos->side][move.from][move.to];
    return (hScore > 70000) ? 70000 : hScore;
}

static int CollectLegalRootMoves(Board *pos, Move *legalMoves, int *moveOrderScores) {
    Move rawMoves[MAX_MOVES];
    int rawCount = 0;
    GenerateMoves(pos, rawMoves, &rawCount);

    int numLegal = 0;
    for (int i = 0; i < rawCount; ++i) {
        if (MakeMove(pos, rawMoves[i])) {
            TakeMove(pos, rawMoves[i]);
            legalMoves[numLegal] = rawMoves[i];
            moveOrderScores[numLegal] = ScoreRootMove(pos, rawMoves[i]);
            numLegal++;
        }
    }
    return numLegal;
}

static void SortMovesByScore(Move *moves, int *scores, int count) {
    for (int i = 0; i < count - 1; ++i) {
        int bestIdx = i;
        for (int j = i + 1; j < count; ++j) {
            if (scores[j] > scores[bestIdx]) bestIdx = j;
        }
        if (bestIdx != i) {
            Move tm = moves[i];
            moves[i] = moves[bestIdx];
            moves[bestIdx] = tm;
            int ts = scores[i];
            scores[i] = scores[bestIdx];
            scores[bestIdx] = ts;
        }
    }
}

static void SortCandidatesDesc(RootCandidate *cands, int count) {
    for (int i = 1; i < count; ++i) {
        RootCandidate key = cands[i];
        int j = i - 1;
        while (j >= 0 && cands[j].score < key.score) {
            cands[j + 1] = cands[j];
            j--;
        }
        cands[j + 1] = key;
    }
}

static Move PickLimitedStrengthMove(const Board *pos, const RootCandidate *cands, int numCands,
                                    int uciElo, double tempWdl) {
    Move empty = {NO_SQ, NO_SQ, EMPTY, EMPTY, 0};
    if (numCands <= 0) return empty;
    if (numCands == 1 || tempWdl <= 0.0) return cands[0].move;

    int bestScore = cands[0].score;
    if (bestScore > MATE_SCORE - MAX_DEPTH || bestScore < -MATE_SCORE + MAX_DEPTH) {
        return cands[0].move;
    }

    double effTempWdl = tempWdl;
    if (numCands >= 2 && pos->hisPly > 0) {
        int lastToSq = (pos->historyMove[pos->hisPly - 1] >> 6) & 63;
        if (cands[0].move.to == lastToSq && cands[0].move.capture != EMPTY &&
            (cands[0].score - cands[1].score) >= 150) {
            if (uciElo >= 1500) {
                return cands[0].move;
            } else {
                effTempWdl /= 4.0;
            }
        }
    }

    double bestWdl = CpToWdl(bestScore);
    double weights[MAX_MOVES];
    double sumWeights = 0.0;
    for (int i = 0; i < numCands; ++i) {
        if (cands[i].score < -MATE_SCORE + MAX_DEPTH) {
            weights[i] = 0.0;
            continue;
        }
        double wdlDiff = bestWdl - CpToWdl(cands[i].score);
        if (wdlDiff < 0.0) wdlDiff = 0.0;
        weights[i] = exp(-wdlDiff / effTempWdl);
        sumWeights += weights[i];
    }

    if (sumWeights <= 0.0) return cands[0].move;

    double r = RandomUnitDouble() * sumWeights;
    double accum = 0.0;
    for (int i = 0; i < numCands; ++i) {
        accum += weights[i];
        if (r < accum) {
            return cands[i].move;
        }
    }
    return cands[0].move;
}

static Move SearchRootWindowed(Board *pos, int maxDepth, long optTime, const StrengthParams *params) {
    Move legalMoves[MAX_MOVES];
    int moveOrderScores[MAX_MOVES];
    int numLegal = CollectLegalRootMoves(pos, legalMoves, moveOrderScores);

    Move empty = {NO_SQ, NO_SQ, EMPTY, EMPTY, 0};
    if (numLegal == 0) return empty;
    if (numLegal == 1) return legalMoves[0];

    RootCandidate completedCands[MAX_MOVES];
    int numCompleted = 0;
    Move pass1Best = {NO_SQ, NO_SQ, EMPTY, EMPTY, 0};

    int inCheckRoot = SqAttacked(pos->king_sq[pos->side], pos->side ^ 1, pos);
    int rootEval = inCheckRoot ? -INF : Evaluate(pos);

    for (int d = 1; d <= maxDepth; ++d) {
        CurrentSearchDepth = d;
        EvalStack[0] = rootEval;
        SortMovesByScore(legalMoves, moveOrderScores, numLegal);

        // Pass 1: Single-PV Root Search
        int alpha = -INF;
        int beta = INF;
        int bestIdx = -1;
        int bestScore = -INF;
        Move bestPv[MAX_DEPTH];
        bestPv[0].from = NO_SQ;

        for (int m = 0; m < numLegal; ++m) {
            if (!MakeMove(pos, legalMoves[m])) continue;

            Move childPv[MAX_DEPTH];
            childPv[0].from = NO_SQ;
            int score;
            if (bestIdx == -1) {
                score = -Negamax(pos, d - 1, -beta, -alpha, 1, childPv);
            } else {
                score = -Negamax(pos, d - 1, -alpha - 1, -alpha, 1, childPv);
                if (!StopSearch && score > alpha && score < beta) {
                    childPv[0].from = NO_SQ;
                    score = -Negamax(pos, d - 1, -beta, -alpha, 1, childPv);
                }
            }

            TakeMove(pos, legalMoves[m]);
            if (StopSearch) {
                if (d == 1 && bestIdx != -1) {
                    pass1Best = legalMoves[bestIdx];
                }
                break;
            }

            if (score > bestScore) {
                bestScore = score;
                bestIdx = m;
                alpha = score;
                bestPv[0] = legalMoves[m];
                int j = 0;
                while (childPv[j].from != NO_SQ && j < MAX_DEPTH - 2) {
                    bestPv[j + 1] = childPv[j];
                    j++;
                }
                bestPv[j + 1].from = NO_SQ;
            }
        }

        if (StopSearch || bestIdx == -1) {
            break;
        }
        if (d == 1) {
            pass1Best = legalMoves[bestIdx];
        }

        // Pass 2: Single-Pass Windowed Candidate Search
        RootCandidate currentCands[MAX_MOVES];
        int numCurrent = 0;
        currentCands[0].move = legalMoves[bestIdx];
        currentCands[0].score = bestScore;
        memcpy(currentCands[0].pv, bestPv, sizeof(bestPv));
        numCurrent = 1;

        if (bestScore <= MATE_SCORE - MAX_DEPTH && bestScore >= -MATE_SCORE + MAX_DEPTH) {
            int floorScore = WdlFloorCp(bestScore, params->tempWdl);
            for (int m = 0; m < numLegal; ++m) {
                if (m == bestIdx) continue;
                if (!MakeMove(pos, legalMoves[m])) continue;

                Move childPv[MAX_DEPTH];
                childPv[0].from = NO_SQ;
                int score = -Negamax(pos, d - 1, -(bestScore + 1), -(floorScore - 1), 1, childPv);
                if (!StopSearch && score >= bestScore + 1) {
                    childPv[0].from = NO_SQ;
                    score = -Negamax(pos, d - 1, -INF, -(floorScore - 1), 1, childPv);
                }

                TakeMove(pos, legalMoves[m]);
                if (StopSearch) break;

                if (score >= floorScore && numCurrent < MAX_MOVES) {
                    currentCands[numCurrent].move = legalMoves[m];
                    currentCands[numCurrent].score = score;
                    currentCands[numCurrent].pv[0] = legalMoves[m];
                    int j = 0;
                    while (childPv[j].from != NO_SQ && j < MAX_DEPTH - 2) {
                        currentCands[numCurrent].pv[j + 1] = childPv[j];
                        j++;
                    }
                    currentCands[numCurrent].pv[j + 1].from = NO_SQ;
                    numCurrent++;
                }
            }
        }

        // Discard partial depth results so Softmax never samples an incomplete pass
        if (StopSearch) {
            break;
        }

        SortCandidatesDesc(currentCands, numCurrent);
        memcpy(completedCands, currentCands, (size_t)numCurrent * sizeof(RootCandidate));
        numCompleted = numCurrent;

        for (int m = 0; m < numLegal; ++m) {
            moveOrderScores[m] = ScoreRootMove(pos, legalMoves[m]);
            for (int k = 0; k < numCompleted; ++k) {
                if (SameMove(legalMoves[m], completedCands[k].move)) {
                    moveOrderScores[m] = 2000000 + (numCompleted - k) * 10000 + completedCands[k].score;
                    break;
                }
            }
        }

        long t = GetTimeMs() - StartTime;
        printf("info depth %d ", d);
        PrintUciScore(completedCands[0].score);
        printf(" nodes %llu time %ld pv ", (unsigned long long)Nodes, t);
        for (int j = 0; completedCands[0].pv[j].from != NO_SQ && j < MAX_DEPTH; ++j) {
            PrintMoveUci(completedCands[0].pv[j]);
            printf(" ");
        }
        printf("\n");

        if (MaxNodes > 0 && Nodes >= MaxNodes) break;
        if (t >= optTime && maxDepth > 1) break;
    }

    if (numCompleted > 0) {
        return PickLimitedStrengthMove(pos, completedCands, numCompleted, UciElo, params->tempWdl);
    }
    if (pass1Best.from != NO_SQ) {
        return pass1Best;
    }

    return legalMoves[0];
}

void SearchPosition(Board *pos, int depth, int wtime, int btime, int winc, int binc, int movestogo, int movetime) {
    TTAge++;
    StopSearch = 0;
    Nodes = 0;
    MaxNodes = 0;
    MaxQDepth = 0;
    EvalNoiseAmp = 0;
    memset(ActivePieceJitter, 0, sizeof(ActivePieceJitter));
    HasActivePieceJitter = 0;
    CurrentSearchDepth = 0;
    StartTime = GetTimeMs();
    
    // CRITICAL: Reset search tree ply to 0 at the root of every search!
    pos->ply = 0;

    // Warm root accumulator once so child plies always have a computed anchor
    if (NNUE_Loaded) {
        EnsureAccumulatorUpdated(pos, WHITE);
        EnsureAccumulatorUpdated(pos, BLACK);
    }

    // Reset killer moves and eval stack between positions
    memset(KillerMoves, 0, sizeof(KillerMoves));
    memset(EvalStack, 0, sizeof(EvalStack));

    // Scale history scores down between searches to give priority to recent cutoffs
    for (int s = 0; s < 2; s++)
        for (int f = 0; f < 64; f++)
            for (int t = 0; t < 64; t++)
                History[s][f][t] /= 2;
    
    // Time Management
    long opt_time = 0;
    long max_time = 0;

    if (movetime != -1) {
        opt_time = (long)(movetime * 0.9);
        max_time = movetime - 20; 
    } else {
        long time_left = (pos->side == WHITE) ? wtime : btime;
        long inc = (pos->side == WHITE) ? winc : binc;

        int move_overhead = 50; 
        time_left -= move_overhead;
        if (time_left < 20) time_left = 20;

        if (movestogo == 0 || movestogo > 40) movestogo = 40; 

        opt_time = (time_left / movestogo) + (inc * 3 / 4);
        max_time = (time_left / 5); 

        if (opt_time >= time_left) opt_time = time_left - 10;
        if (max_time >= time_left) max_time = time_left - 10;
        if (opt_time < 1) opt_time = 1;
        if (max_time < 1) max_time = 1;
    }

    StopTime = StartTime + max_time;
    if(depth == -1) depth = MAX_DEPTH;

    StrengthParams params = { MAX_DEPTH, 0ULL, 0, 0.0, 0, 0, -1 };
    if (LimitStrength) {
        params = GetStrengthParams(UciElo);
    }
    
    // --- OPENING BOOK CHECK ---
    if (!LimitStrength || params.maxBookPly < 0 || pos->hisPly < params.maxBookPly) {
        double bookExp = (!LimitStrength || UciElo >= 2200) ? 1.0 : 0.5;
        Move bookMove = GetBookMoveWeighted(pos, bookExp);
        if(bookMove.from != NO_SQ) {
            char *files = "abcdefgh";
            char *ranks = "12345678";
            
            // Message propre pour l'interface
            printf("info string Playing from Book\n");
            
            printf("bestmove %c%c%c%c", files[bookMove.from%8], ranks[bookMove.from/8], files[bookMove.to%8], ranks[bookMove.to/8]);
            if(bookMove.promoted != EMPTY) {
                char pChar = 'q';
                if(bookMove.promoted == N || bookMove.promoted == n) pChar = 'n';
                if(bookMove.promoted == R || bookMove.promoted == r) pChar = 'r';
                if(bookMove.promoted == B || bookMove.promoted == b) pChar = 'b';
                printf("%c", pChar);
            }
            printf("\n");
            return; // Skip search completely!
        }
    }
    // --------------------------

    if (LimitStrength) {
        MaxNodes = params.maxNodes;
        MaxQDepth = params.maxQDepth;
        EvalNoiseAmp = params.evalNoise;
        if (params.pieceJitter > 0) {
            for (int t = 0; t < 5; ++t) {
                ActivePieceJitter[t] = (int)lround(GamePieceJitterSeed[t] * (double)params.pieceJitter);
                if (ActivePieceJitter[t] != 0) {
                    HasActivePieceJitter = 1;
                }
            }
        }
        if (depth > params.maxDepth) {
            depth = params.maxDepth;
        }
    }

    // CORRECTION : Toujours initialiser avec EMPTY (12) et pas 0 (qui est le pion P)
    Move bestMoveSoFar = {NO_SQ, NO_SQ, EMPTY, EMPTY, 0};

    if (LimitStrength && params.tempWdl > 0.0) {
        bestMoveSoFar = SearchRootWindowed(pos, depth, opt_time, &params);
    } else {
    Move pvLine[MAX_DEPTH];
    
    int previousScore = 0; 
    
    for(int d=1; d<=depth; ++d) {
        CurrentSearchDepth = d;
        int alpha = -INF;
        int beta = INF;
        int score;
        int delta = 25;

        if (d >= 4) {
            alpha = previousScore - delta;
            beta = previousScore + delta;
        }

        while (1) {
            pvLine[0].from = NO_SQ;
            score = Negamax(pos, d, alpha, beta, 0, pvLine);
            if (StopSearch) break;

            if (score <= alpha) {
                beta = (alpha + beta) / 2;
                alpha = score - delta;
                if (alpha < -INF) alpha = -INF;
                delta *= 2;
            } else if (score >= beta) {
                beta = score + delta;
                if (beta > INF) beta = INF;
                delta *= 2;
            } else {
                break;
            }

            if (delta > 500) {
                alpha = -INF;
                beta = INF;
            }
        }
        
        if(StopSearch) break;
        previousScore = score;
        
        // FIX: Verify pvLine[0] strictly exists in the legal move list before storing
        if(pvLine[0].from != NO_SQ) {
            Move legals[MAX_MOVES]; int lCount = 0;
            GenerateMoves(pos, legals, &lCount);
            for(int m = 0; m < lCount; ++m) {
                if(legals[m].from == pvLine[0].from && 
                   legals[m].to == pvLine[0].to && 
                   legals[m].promoted == pvLine[0].promoted) {
                    if (MakeMove(pos, legals[m])) {
                        TakeMove(pos, legals[m]);
                        bestMoveSoFar = legals[m];
                    }
                    break;
                }
            }
        }
        
        long t = GetTimeMs() - StartTime;
        printf("info depth %d score cp %d nodes %llu time %ld pv ", d, score, (unsigned long long)Nodes, t);
        
        int i = 0;
        while(pvLine[i].from != NO_SQ && i < MAX_DEPTH) {
             char *files = "abcdefgh";
             char *ranks = "12345678";
             printf("%c%c%c%c", files[pvLine[i].from%8], ranks[pvLine[i].from/8], files[pvLine[i].to%8], ranks[pvLine[i].to/8]);
             if(pvLine[i].promoted != EMPTY) {
                 char pChar = 'q';
                 if(pvLine[i].promoted == N || pvLine[i].promoted == n) pChar = 'n';
                 if(pvLine[i].promoted == R || pvLine[i].promoted == r) pChar = 'r';
                 if(pvLine[i].promoted == B || pvLine[i].promoted == b) pChar = 'b';
                 printf("%c", pChar);
             }
             printf(" ");
             i++;
        }
        printf("\n");
        
        if (MaxNodes > 0 && Nodes >= MaxNodes) break;

        // Full use of allocated optimal time
        if(t >= opt_time && depth > 1) break; 
    }
    }
    
    // Bestmove and Fallback Logic Fix
    if(bestMoveSoFar.from != NO_SQ) {
         char *files = "abcdefgh";
         char *ranks = "12345678";
         printf("bestmove %c%c%c%c", files[bestMoveSoFar.from%8], ranks[bestMoveSoFar.from/8], files[bestMoveSoFar.to%8], ranks[bestMoveSoFar.to/8]);
         if(bestMoveSoFar.promoted != EMPTY) {
             char pChar = 'q';
             if(bestMoveSoFar.promoted == N || bestMoveSoFar.promoted == n) pChar = 'n';
             if(bestMoveSoFar.promoted == R || bestMoveSoFar.promoted == r) pChar = 'r';
             if(bestMoveSoFar.promoted == B || bestMoveSoFar.promoted == b) pChar = 'b';
             printf("%c", pChar);
         }
         printf("\n");
    } else {
        Move mList[MAX_MOVES]; int count=0; GenerateMoves(pos, mList, &count);
        int foundLegal = 0;
        for(int i=0; i<count; ++i) {
            if(MakeMove(pos, mList[i])) {
                 TakeMove(pos, mList[i]);
                 char *files = "abcdefgh";
                 char *ranks = "12345678";
                 printf("bestmove %c%c%c%c", files[mList[i].from%8], ranks[mList[i].from/8], files[mList[i].to%8], ranks[mList[i].to/8]);
                 if(mList[i].promoted != EMPTY) {
                     char pChar = 'q';
                     if(mList[i].promoted == N || mList[i].promoted == n) pChar = 'n';
                     if(mList[i].promoted == R || mList[i].promoted == r) pChar = 'r';
                     if(mList[i].promoted == B || mList[i].promoted == b) pChar = 'b';
                     printf("%c", pChar);
                 }
                 printf("\n");
                 foundLegal = 1;
                 break;
            }
        }
        if(!foundLegal) {
            printf("bestmove 0000\n");
        }
    }
}

int IsRepetition(const Board *pos) {
    // Check history, bounded by the 50-move rule
    int limit = pos->hisPly - pos->fiftyMove;
    if(limit < 0) limit = 0;
    for(int i = pos->hisPly - 2; i >= limit; i -= 2) {
        if(pos->historyPosKey[i] == pos->posKey) return 1;
    }
    return 0;
}

// Generates a FEN string from current Board state
void GetBoardFen(const Board *pos, char *fen) {
    int rank, file, emptyCount;
    char *ptr = fen;

    for (rank = 7; rank >= 0; rank--) {
        emptyCount = 0;
        for (file = 0; file < 8; file++) {
            int sq = rank * 8 + file;
            int piece = pos->board[sq];

            if (piece == EMPTY) {
                emptyCount++;
            } else {
                if (emptyCount > 0) {
                    *ptr++ = '0' + emptyCount;
                    emptyCount = 0;
                }
                char pChars[] = "PNBRQKpnbrqk";
                *ptr++ = pChars[piece];
            }
        }
        if (emptyCount > 0) *ptr++ = '0' + emptyCount;
        if (rank > 0) *ptr++ = '/';
    }

    *ptr++ = ' ';
    *ptr++ = (pos->side == WHITE) ? 'w' : 'b';
    *ptr++ = ' ';

    if (pos->castlePerm == 0) {
        *ptr++ = '-';
    } else {
        if (pos->castlePerm & WKCA) *ptr++ = 'K';
        if (pos->castlePerm & WQCA) *ptr++ = 'Q';
        if (pos->castlePerm & BKCA) *ptr++ = 'k';
        if (pos->castlePerm & BQCA) *ptr++ = 'q';
    }

    *ptr++ = ' ';
    if (pos->enPas == NO_SQ) {
        *ptr++ = '-';
    } else {
        *ptr++ = 'a' + (pos->enPas % 8);
        *ptr++ = '1' + (pos->enPas / 8);
    }
    *ptr = '\0';
}

// Runs self-play data generation with Iterative Deepening
void RunDataGen(int numGames, int maxPlies, int searchDepth, const char *outFile) {
    FILE *f = fopen(outFile, "a");
    if (!f) {
        printf("Error: Could not open output file %s\n", outFile);
        return;
    }

    printf("Starting self-play generation: %d games at Depth %d -> File: %s\n", numGames, searchDepth, outFile);

    static Board gPos;
    StopTime = 0x7FFFFFFFFFFFFFFFLL; // Disable timer cutoff
    MaxNodes = 0;
    MaxQDepth = 0;
    EvalNoiseAmp = 0;
    memset(ActivePieceJitter, 0, sizeof(ActivePieceJitter));
    HasActivePieceJitter = 0;
    CurrentSearchDepth = 0;
    ClearTT(); // Clear TT once before data generation starts

    for (int g = 1; g <= numGames; ++g) {
        // Increment TT age between games instead of zeroing memory every game
        TTAge++;
        if (TTAge == 0) {
            ClearTT();
        }
        memset(KillerMoves, 0, sizeof(KillerMoves));
        memset(History, 0, sizeof(History));

        ParseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &gPos);

        char gameFens[512][128];
        int gameScores[512];
        int posCount = 0;

        // 1. Opening Diversity: 6 to 10 random half-moves (3 to 5 full moves)
        int randomPlies = 6 + (rand() % 5);
        for (int r = 0; r < randomPlies; ++r) {
            Move mList[MAX_MOVES]; int count = 0;
            GenerateMoves(&gPos, mList, &count);
            Move legal[MAX_MOVES]; int lCount = 0;
            for (int i = 0; i < count; ++i) {
                if (MakeMove(&gPos, mList[i])) {
                    TakeMove(&gPos, mList[i]);
                    legal[lCount++] = mList[i];
                }
            }
            if (lCount == 0) break;
            MakeMove(&gPos, legal[rand() % lCount]);
        }

        // 2. Play game using iterative deepening search
        float gameResult = 0.5f; // Draw default
        int whiteWinStreak = 0;
        int blackWinStreak = 0;
        for (int ply = 0; ply < maxPlies; ++ply) {
            Move pvLine[MAX_DEPTH];
            Move bestMove = {NO_SQ, NO_SQ, EMPTY, EMPTY, 0};
            int score = 0;

            // --- ITERATIVE DEEPENING (Depth 1 to searchDepth) ---
            // Essential for move ordering, TT population, and performance
            if (NNUE_Loaded) {
                EnsureAccumulatorUpdated(&gPos, WHITE);
                EnsureAccumulatorUpdated(&gPos, BLACK);
            }
            Nodes = 0;
            for (int d = 1; d <= searchDepth; ++d) {
                pvLine[0].from = NO_SQ;
                StopSearch = 0;
                gPos.ply = 0;
                score = Negamax(&gPos, d, -INF, INF, 1, pvLine);
                if (pvLine[0].from != NO_SQ) {
                    bestMove = pvLine[0];
                }
                if (Nodes >= 5000) break;
            }

            int inCheck = SqAttacked(gPos.king_sq[gPos.side], gPos.side ^ 1, &gPos);

            // Record quiet positions (avoid tactical noise/checks during training)
            if (!inCheck && ply >= 2 && bestMove.from != NO_SQ &&
                bestMove.capture == EMPTY && bestMove.promoted == EMPTY &&
                abs(score) < 3000 && posCount < 500) {
                GetBoardFen(&gPos, gameFens[posCount]);
                // Save score normalized from White's perspective
                gameScores[posCount] = (gPos.side == WHITE) ? score : -score;
                posCount++;
            }

            // Early adjudication
            if (score > MATE_SCORE - 100) { 
                gameResult = (gPos.side == WHITE) ? 1.0f : 0.0f; 
                break; 
            }
            if (score < -MATE_SCORE + 100) { 
                gameResult = (gPos.side == WHITE) ? 0.0f : 1.0f; 
                break; 
            }

            // Win adjudication for decisive non-mate positions
            int whiteScore = (gPos.side == WHITE) ? score : -score;
            if (whiteScore >= 1000) {
                whiteWinStreak++;
                blackWinStreak = 0;
                if (whiteWinStreak >= 4) {
                    gameResult = 1.0f;
                    break;
                }
            } else if (whiteScore <= -1000) {
                blackWinStreak++;
                whiteWinStreak = 0;
                if (blackWinStreak >= 4) {
                    gameResult = 0.0f;
                    break;
                }
            } else {
                whiteWinStreak = 0;
                blackWinStreak = 0;
            }

            // Apply best move found
            if (bestMove.from == NO_SQ || !MakeMove(&gPos, bestMove)) {
                if (inCheck) {
                    gameResult = (gPos.side == WHITE) ? 0.0f : 1.0f;
                }
                break;
            }

            // Fifty-move rule or 3-fold repetition
            if (IsRepetition(&gPos) || gPos.fiftyMove >= 100) {
                gameResult = 0.5f;
                break;
            }
        }

        // 3. Write positions from this game
        for (int i = 0; i < posCount; ++i) {
            fprintf(f, "%s | %d | %.1f\n", gameFens[i], gameScores[i], gameResult);
        }
        fflush(f);

        if (g % 10 == 0 || g == numGames) {
            printf("Generated %d / %d games (%d positions in file)...\n", g, numGames, posCount);
        }
    }

    fclose(f);
    printf("Data generation complete! Saved to %s\n", outFile);
}