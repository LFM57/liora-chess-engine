#include "defs.h"

#ifndef _WIN32
#include <unistd.h>
#endif

Board pos;

void ParseGo(char *line) {
    int depth = -1, wtime = 300000, btime = 300000, winc = 0, binc = 0, movestogo = 30, movetime = -1;
    
    char *ptr = NULL;
    
    if((ptr = strstr(line, "wtime"))) wtime = atoi(ptr + 6);
    if((ptr = strstr(line, "btime"))) btime = atoi(ptr + 6);
    if((ptr = strstr(line, "winc"))) winc = atoi(ptr + 5);
    if((ptr = strstr(line, "binc"))) binc = atoi(ptr + 5);
    if((ptr = strstr(line, "depth"))) depth = atoi(ptr + 6);
    if((ptr = strstr(line, "movestogo"))) movestogo = atoi(ptr + 10);
    if((ptr = strstr(line, "movetime"))) movetime = atoi(ptr + 9);
    
    SearchPosition(&pos, depth, wtime, btime, winc, binc, movestogo, movetime);
}

void ParsePosition(char *line) {
    line += 9;
    char *ptr = line;
    
    if(strncmp(line, "startpos", 8) == 0) {
        ParseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &pos);
        ptr += 8;
    } else if(strncmp(line, "fen", 3) == 0) {
        ptr += 4;
        ParseFen(ptr, &pos);
    }
    
    ptr = strstr(line, "moves");
    if(ptr) {
        ptr += 6;
        while(*ptr) {
            // Parse UCI move
            int from = (ptr[0] - 'a') + (ptr[1] - '1') * 8;
            int to = (ptr[2] - 'a') + (ptr[3] - '1') * 8;
            int prom = EMPTY;
            if(ptr[4] && ptr[4] != ' ' && ptr[4] != '\n' && ptr[4] != '\r') {
                switch(ptr[4]) {
                    case 'q': prom = (pos.side == WHITE ? Q : q); break;
                    case 'r': prom = (pos.side == WHITE ? R : r); break;
                    case 'b': prom = (pos.side == WHITE ? B : b); break;
                    case 'n': prom = (pos.side == WHITE ? N : n); break;
                }
                ptr++;
            }
            
            // We need to find the move in legal moves to get capture info
            Move moves[MAX_MOVES];
            int count = 0;
            GenerateMoves(&pos, moves, &count);
            for(int i=0; i<count; ++i) {
                if(moves[i].from == from && moves[i].to == to && moves[i].promoted == prom) {
                    // FIX: Only break if MakeMove successfully applies the legal move
                    if (MakeMove(&pos, moves[i])) {
                        break;
                    }
                }
            }
            
            while(*ptr && *ptr != ' ') ptr++;
            if(*ptr == ' ') ptr++;
        }
    }
    pos.ply = 0; // Reset search depth counter after applying game moves
}

void ParseOption(char *line) {
    // Parse: setoption name Hash value <x>
    if(strstr(line, "name Hash") || strstr(line, "name hash")) {
        char *ptr = strstr(line, "value");
        if(ptr) {
            int mb = atoi(ptr + 6);
            if(mb < 1) mb = 1; // Minimum 1 MB
            InitTT(mb);
        }
    }
    // Parse: setoption name BookPath value <path>
    else if(strstr(line, "name BookPath") || strstr(line, "name bookpath")) {
        char *ptr = strstr(line, "value");
        if(ptr) {
            ptr += 6; // Move past the word "value "
            while(*ptr == ' ') ptr++; // Skip any extra spaces
            char *end = ptr + strlen(ptr) - 1;
            // Remove trailing newlines and spaces
            while(end > ptr && (*end == '\n' || *end == '\r' || *end == ' ')) *end-- = '\0';
            strncpy(BookPath, ptr, 511);
        }
    }
    // Parse: setoption name EvalFile value <path>
    else if(strstr(line, "name EvalFile") || strstr(line, "name evalfile")) {
        char *ptr = strstr(line, "value");
        if(ptr) {
            ptr += 6;
            while(*ptr == ' ') ptr++;
            char *end = ptr + strlen(ptr) - 1;
            while(end > ptr && (*end == '\n' || *end == '\r' || *end == ' ')) *end-- = '\0';
            InitNNUE(ptr);
            pos.accumStack[pos.hisPly].computed[WHITE] = 0;
            pos.accumStack[pos.hisPly].computed[BLACK] = 0;
            pos.accumStack[0].computed[WHITE] = 0;
            pos.accumStack[0].computed[BLACK] = 0;
        }
    }
}

char BookPath[512] = "";

int main() {
    AllInit();

    // Ensure parallel instances do not generate identical random seeds
#ifdef _WIN32
    srand((unsigned int)(time(NULL) ^ GetCurrentProcessId()));
#else
    srand((unsigned int)(time(NULL) ^ (getpid() << 16)));
#endif

    InitNNUE("liora.nnue"); // Attempts to load liora.nnue from working directory
    InitTT(64);
    
    extern void InitMvvLva();
    InitMvvLva();
    
    char line[2048];
    
    setbuf(stdout, NULL);
    setbuf(stdin, NULL);
    
    while(1) {
        if(!fgets(line, 2048, stdin)) continue;
        
        if(line[0] == '\n') continue;
        if(!strncmp(line, "uci", 3)) {
            printf("id name %s\n", NAME); // Uses the NAME macro from defs.h
            printf("id author %s\n", AUTHOR);
            printf("option name Hash type spin default 64 min 1 max 4096\n");
            printf("option name BookPath type string default <empty>\n");
            printf("option name EvalFile type string default liora.nnue\n");
            printf("uciok\n");
        } else if(!strncmp(line, "setoption", 9)) {
            ParseOption(line);
        } else if(!strncmp(line, "isready", 7)) {
            printf("readyok\n");
        } else if(!strncmp(line, "ucinewgame", 10)) {
            ClearTT();
            ParseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &pos);
        } else if(!strncmp(line, "position", 8)) {
            ParsePosition(line);
        } else if(!strncmp(line, "go", 2)) {
            ParseGo(line);
        } else if(!strncmp(line, "datagen", 7)) {
            int games = 1000;
            int depth = 12;
            char outFile[256] = "dataset.txt";
            char *p = line + 7;
            while (*p == ' ') p++;
            if (*p) {
                // Syntax: datagen [games] [depth] [filename]
                sscanf(p, "%d %d %255s", &games, &depth, outFile);
            }
            RunDataGen(games, 200, depth, outFile);
        } else if(!strncmp(line, "quit", 4)) {
            break;
        }
    }
    
    return 0;
}
