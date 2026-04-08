#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080

void draw_board(char *board) {
    system("clear");
    printf("\n    === 4x4 TIC-TAC-TOE ===\n\n");
    printf("        0   1   2   3\n"); // Column headers
    printf("      +---+---+---+---+\n");
    for (int i = 0; i < 4; i++) {
        printf("   %2d |", i * 4); // Row start index
        for (int j = 0; j < 4; j++) {
            int idx = i * 4 + j;
            // Print dots for empty, symbol for taken
            printf(" %c |", board[idx]);
        }
        printf("\n      +---+---+---+---+\n");
    }
    printf("\n   (Empty cells are '.')\n\n");
}

int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[1024] = {0};
    int my_id; char my_symbol;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) return -1;

    read(sock, buffer, 1024);
    sscanf(buffer, "WELCOME %d %c", &my_id, &my_symbol);
    printf("Connected as Player %d (%c)\n", my_id + 1, my_symbol);

    while (1) { // Persistent loop for successive games [cite: 9]
        memset(buffer, 0, 1024);
        if (read(sock, buffer, 1024) <= 0) break;

        char command[10]; sscanf(buffer, "%s", command);

        if (strcmp(command, "WAIT") == 0) {
            int curr; char bstate[17]; sscanf(buffer, "WAIT %d %s", &curr, bstate);
            draw_board(bstate);
            printf(">> Player %d is moving...\n", curr + 1);
        }
        else if (strcmp(command, "TURN") == 0) {
            char bstate[17]; sscanf(buffer, "TURN %s", bstate);
            draw_board(bstate);
            printf(">> YOUR TURN (%c)!\n", my_symbol);
            int move;
            while(1) {
                printf("Pos (0-15): ");
                if(scanf("%d", &move) == 1 && move >= 0 && move <= 15 && bstate[move] == '.') break;
                while(getchar() != '\n');
            }
            char m_str[10]; sprintf(m_str, "%d", move); send(sock, m_str, strlen(m_str), 0);
        }
        else if (strcmp(command, "WIN") == 0 || strcmp(command, "LOSE") == 0 || strcmp(command, "DRAW") == 0) {
            char bstate[17]; sscanf(buffer, "%*s %s", bstate);
            draw_board(bstate);
            if (buffer[0] == 'W') printf("\n*** YOU WON! ***\n");
            else if (buffer[0] == 'L') printf("\n--- YOU LOST. ---\n");
            else printf("\n=== DRAW! ===\n");
            printf("Waiting for next game to start...\n");
            // Don't break; loop back to WAIT/TURN for the next game
        }
    }
    close(sock);
    return 0;
}