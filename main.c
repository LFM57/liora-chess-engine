#include "defs.h"

#ifndef _WIN32
#include <unistd.h>
#endif

Board pos;
char BookPath[512] = "";
char EvalFile[512] = "<embedded>";
int UseNNUE = 0;
int LimitStrength = 0;
int UciElo = 1500;

static char ToLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static const char *FindOptionTokenIgnoreCase(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle) return NULL;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < nlen && p[i] && ToLowerAscii(p[i]) == ToLowerAscii(needle[i])) {
            i++;
        }
        if (i == nlen) {
            char next = p[nlen];
            if (next == '\0' || next == ' ' || next == '\t' || next == '\n' || next == '\r') {
                return p;
            }
        }
    }
    return NULL;
}

static int StartsWithIgnoreCase(const char *str, const char *prefix) {
    if (!str || !prefix) return 0;
    while (*prefix) {
        if (!*str || ToLowerAscii(*str) != ToLowerAscii(*prefix)) return 0;
        str++;
        prefix++;
    }
    return 1;
}

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
        ptr += 5;
        while(*ptr == ' ') ptr++;
        while(*ptr && *ptr != '\n' && *ptr != '\r') {
            if(ptr[0] < 'a' || ptr[0] > 'h' || ptr[1] < '1' || ptr[1] > '8' ||
               ptr[2] < 'a' || ptr[2] > 'h' || ptr[3] < '1' || ptr[3] > '8') {
                break;
            }
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
            
            while(*ptr && *ptr != ' ' && *ptr != '\n' && *ptr != '\r') ptr++;
            while(*ptr == ' ') ptr++;
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
    // Parse: setoption name UseNNUE value <true|false>
    else if(strstr(line, "name UseNNUE") || strstr(line, "name usennue") ||
            strstr(line, "name Use NNUE") || strstr(line, "name use nnue")) {
        char *ptr = strstr(line, "value");
        if(ptr) {
            ptr += 6;
            while(*ptr == ' ') ptr++;
            if(strncmp(ptr, "false", 5) == 0 || strncmp(ptr, "0", 1) == 0) {
                UseNNUE = 0;
                DisableNNUE();
                printf("info string NNUE disabled (using HCE)\n");
            } else if(strncmp(ptr, "true", 4) == 0 || strncmp(ptr, "1", 1) == 0) {
                UseNNUE = 1;
                InitNNUE(EvalFile);
            }
            for (int i = 0; i <= pos.hisPly; ++i) {
                pos.accumStack[i].computed[WHITE] = 0;
                pos.accumStack[i].computed[BLACK] = 0;
            }
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
            strncpy(EvalFile, ptr, sizeof(EvalFile) - 1);
            EvalFile[sizeof(EvalFile) - 1] = '\0';

            if(UseNNUE) {
                InitNNUE(EvalFile);
            }
            for (int i = 0; i <= pos.hisPly; ++i) {
                pos.accumStack[i].computed[WHITE] = 0;
                pos.accumStack[i].computed[BLACK] = 0;
            }
        }
    }
    // Parse: setoption name UCI_LimitStrength value <true|false>
    else if (FindOptionTokenIgnoreCase(line, "name UCI_LimitStrength")) {
        const char *ptr = FindOptionTokenIgnoreCase(line, "value");
        if (ptr) {
            ptr += 5;
            while (*ptr == ' ' || *ptr == '\t') ptr++;
            if (StartsWithIgnoreCase(ptr, "true") || *ptr == '1') {
                LimitStrength = 1;
            } else if (StartsWithIgnoreCase(ptr, "false") || *ptr == '0') {
                LimitStrength = 0;
            }
        }
    }
    // Parse: setoption name UCI_Elo value <x>
    else if (FindOptionTokenIgnoreCase(line, "name UCI_Elo")) {
        const char *ptr = FindOptionTokenIgnoreCase(line, "value");
        if (ptr) {
            ptr += 5;
            while (*ptr == ' ' || *ptr == '\t') ptr++;
            if ((*ptr >= '0' && *ptr <= '9') || *ptr == '-' || *ptr == '+') {
                int elo = atoi(ptr);
                if (elo < 1200) elo = 1200;
                if (elo > 3100) elo = 3100;
                UciElo = elo;
            }
        }
    }
}

int main() {
    AllInit();

    // Ensure parallel instances do not generate identical random seeds
#ifdef _WIN32
    srand((unsigned int)(time(NULL) ^ GetCurrentProcessId()));
#else
    srand((unsigned int)(time(NULL) ^ (getpid() << 16)));
#endif
    ReseedGameNoise();

    if(HasEmbeddedNNUE()) {
        strncpy(EvalFile, "<embedded>", sizeof(EvalFile) - 1);
        InitNNUE(EvalFile);
        UseNNUE = NNUE_Loaded;
    } else {
        strncpy(EvalFile, "<empty>", sizeof(EvalFile) - 1);
        UseNNUE = 0;
        DisableNNUE();
    }

    InitTT(64);
    
    extern void InitMvvLva();
    InitMvvLva();
    InitLMR();
    
    static char line[65536];
    
    setbuf(stdout, NULL);
    setbuf(stdin, NULL);
    
    while(1) {
        if(!fgets(line, sizeof(line), stdin)) {
            if (feof(stdin)) break;
            continue;
        }
        
        if(line[0] == '\n' || line[0] == '\r') continue;
        if(!strncmp(line, "ucinewgame", 10)) {
            ClearTT();
            ReseedGameNoise();
            ParseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", &pos);
        } else if(!strncmp(line, "uci", 3) && (line[3] == '\n' || line[3] == '\r' || line[3] == ' ' || line[3] == '\t' || line[3] == '\0')) {
            printf("id name %s\n", NAME); // Uses the NAME macro from defs.h
            printf("id author %s\n", AUTHOR);
            printf("option name Hash type spin default 64 min 1 max 4096\n");
            printf("option name BookPath type string default <empty>\n");
            printf("option name UseNNUE type check default %s\n", UseNNUE ? "true" : "false");
            printf("option name EvalFile type string default %s\n", HasEmbeddedNNUE() ? "<embedded>" : "<empty>");
            printf("option name UCI_LimitStrength type check default false\n");
            printf("option name UCI_Elo type spin default 1500 min 1200 max 3100\n");
            printf("uciok\n");
        } else if(!strncmp(line, "setoption", 9)) {
            ParseOption(line);
        } else if(!strncmp(line, "isready", 7)) {
            printf("readyok\n");
        } else if(!strncmp(line, "position", 8)) {
            ParsePosition(line);
        } else if(!strncmp(line, "go", 2)) {
            ParseGo(line);
        } else if(!strncmp(line, "datagen", 7)) {
            int games = 1000;
            int depth = 8;
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
