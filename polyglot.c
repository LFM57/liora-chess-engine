#include "defs.h"
#include "polykeys.h"

// Computes the specific Polyglot Hash
U64 ComputePolyglotHash(const Board *pos) {
    U64 hash = 0;
    
    // Ton code d'ORIGINE était parfait ! L'ordre Letouzey officiel est bien celui-ci :
    // Blancs = impairs (1, 3, 5, 7, 9, 11)
    // Noirs = pairs (0, 2, 4, 6, 8, 10)
    int polyglot_piece[12] = {1, 3, 5, 7, 9, 11, 0, 2, 4, 6, 8, 10}; 
    
    for(int p = 0; p < 12; ++p) {
        U64 bb = pos->pieces[p];
        while(bb) {
            int sq = __builtin_ctzll(bb);
            bb &= bb - 1;
            int offset = 64 * polyglot_piece[p] + sq;
            
            // On utilise directement le tableau PolyglotKeys de notre fichier .h
            hash ^= PolyglotKeys[offset]; 
        }
    }

    // Castling flags
    if(pos->castlePerm & WKCA) hash ^= PolyglotKeys[768];
    if(pos->castlePerm & WQCA) hash ^= PolyglotKeys[769];
    if(pos->castlePerm & BKCA) hash ^= PolyglotKeys[770];
    if(pos->castlePerm & BQCA) hash ^= PolyglotKeys[771];
    
    // En Passant (Ensuring NO_SQ is strictly respected)
    if(pos->enPas != NO_SQ && pos->enPas < 64) {
        int ep_file = pos->enPas % 8;
        int has_pawn = 0;
        if(pos->side == WHITE) {
            if(ep_file > 0 && GetPieceAt(pos, pos->enPas - 9, WHITE) == P) has_pawn = 1;
            if(ep_file < 7 && GetPieceAt(pos, pos->enPas - 7, WHITE) == P) has_pawn = 1;
        } else {
            if(ep_file > 0 && GetPieceAt(pos, pos->enPas + 7, BLACK) == p) has_pawn = 1;
            if(ep_file < 7 && GetPieceAt(pos, pos->enPas + 9, BLACK) == p) has_pawn = 1;
        }
        if(has_pawn) hash ^= PolyglotKeys[772 + ep_file];
    }
    
    // Turn
    if(pos->side == WHITE) hash ^= PolyglotKeys[780];
    
    return hash;
}

// Converts a 16-bit Polyglot move into your engine's Move struct
Move ConvertPolyglotMove(uint16_t polyMove, const Board *pos) {
    int to_file   = (polyMove >> 0) & 7;
    int to_row    = (polyMove >> 3) & 7;
    int from_file = (polyMove >> 6) & 7;
    int from_row  = (polyMove >> 9) & 7;
    int prom_type = (polyMove >> 12) & 7;
    
    int from = from_row * 8 + from_file;
    int to   = to_row * 8 + to_file;
    int prom = EMPTY;
    
    // Fix Castling representations (King-takes-Rook -> King destination)
    if (from == E1 && to == H1) to = G1;
    else if (from == E1 && to == A1) to = C1;
    else if (from == E8 && to == H8) to = G8;
    else if (from == E8 && to == A8) to = C8;
    
    if(prom_type != 0) {
        int pListW[] = {EMPTY, N, B, R, Q};
        int pListB[] = {EMPTY, n, b, r, q};
        prom = (pos->side == WHITE) ? pListW[prom_type] : pListB[prom_type];
    }
    
    Move list[MAX_MOVES];
    int count = 0;
    GenerateMoves(pos, list, &count);
    
    for(int i = 0; i < count; ++i) {
        if(list[i].from == from && list[i].to == to && list[i].promoted == prom) {
            if(MakeMove((Board*)pos, list[i])) {
                TakeMove((Board*)pos, list[i]);
                return list[i];
            }
        }
    }
    Move empty = {NO_SQ, NO_SQ, EMPTY, EMPTY, 0};
    return empty;
}

// Binary Search inside the .bin file
Move GetBookMove(const Board *pos) {
    Move empty = {NO_SQ, NO_SQ, EMPTY, EMPTY, 0};
    
    if(BookPath[0] == '\0') return empty;
    
    FILE *f = fopen(BookPath, "rb");
    if(!f) {
        printf("info string ERROR: Could not open book file at '%s'\n", BookPath);
        return empty;
    }
    
    U64 polyHash = ComputePolyglotHash(pos);
    
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    long low = 0;
    long high = (fileSize / 16) - 1; 
    
    uint16_t foundMoves[16];
    int numFound = 0;
    
    while(low <= high) {
        long mid = low + (high - low) / 2;
        fseek(f, mid * 16, SEEK_SET);
        
        U64 key;
        fread(&key, sizeof(U64), 1, f);
        key = __builtin_bswap64(key); 
        
        if(key < polyHash) {
            low = mid + 1;
        } else if(key > polyHash) {
            high = mid - 1;
        } else {
            long firstMatch = mid;
            while(firstMatch > 0) {
                fseek(f, (firstMatch - 1) * 16, SEEK_SET);
                U64 prevKey; fread(&prevKey, sizeof(U64), 1, f);
                if(__builtin_bswap64(prevKey) != polyHash) break;
                firstMatch--;
            }
            
            fseek(f, firstMatch * 16, SEEK_SET);
            for(int i = 0; i < 16; i++) {
                U64 currentKey;
                uint16_t move, weight;
                uint32_t learn;
                
                if(fread(&currentKey, sizeof(U64), 1, f) != 1) break;
                if(__builtin_bswap64(currentKey) != polyHash) break;
                
                fread(&move, sizeof(uint16_t), 1, f);
                fread(&weight, sizeof(uint16_t), 1, f);
                fread(&learn, sizeof(uint32_t), 1, f);
                
                foundMoves[numFound++] = __builtin_bswap16(move);
            }
            break;
        }
    }
    
    fclose(f);
    
    if(numFound > 0) {
        int randomIndex = rand() % numFound;
        return ConvertPolyglotMove(foundMoves[randomIndex], pos);
    }
    
    return empty;
}