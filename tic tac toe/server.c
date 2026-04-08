#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>

#define PORT 8080
#define MAX_PLAYERS 3
#define BOARD_SIZE 16
#define SCORE_FILE "scores.txt"
#define LOG_FILE "game.log"

typedef struct {
    char board[BOARD_SIZE];
    int scores[MAX_PLAYERS];       
    int active_players;            
    int current_turn;              
    int game_running;              
    int winner_id;                 
    int move_count;                
    pthread_mutex_t state_mutex;   
    pthread_cond_t turn_cond;      
} SharedState;

SharedState *shm;                 
int log_pipe_fd[2];               
pid_t child_pids[MAX_PLAYERS];    

void setup_shared_memory();
void load_scores();
void save_scores();
void *logger_thread(void *arg);
void *scheduler_thread(void *arg);
void handle_client(int socket, int player_id);
void log_event(const char *msg);
void reset_game();
int check_win(char s);

// Logger helper
void log_event(const char *msg) {
    write(log_pipe_fd[1], msg, strlen(msg));
    write(log_pipe_fd[1], "\n", 1);
}

void sigchld_handler(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void cleanup(int sig) {
    log_event("Server manual shutdown triggered.");
    save_scores();
    pthread_mutex_destroy(&shm->state_mutex);
    pthread_cond_destroy(&shm->turn_cond);
    munmap(shm, sizeof(SharedState));
    exit(0);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    pthread_t log_tid, sched_tid;

    signal(SIGINT, cleanup);
    signal(SIGCHLD, sigchld_handler);

    setup_shared_memory();
    load_scores();
    reset_game();

    if (pipe(log_pipe_fd) == -1) exit(1);

    pthread_create(&log_tid, NULL, logger_thread, NULL);
    pthread_create(&sched_tid, NULL, scheduler_thread, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 5);

    printf("[MAIN] Server running. Waiting for players...\n");
    log_event("Server started.");

    int player_count = 0;
    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) continue;

        pthread_mutex_lock(&shm->state_mutex);
        if (shm->active_players >= MAX_PLAYERS) {
            close(new_socket);
            pthread_mutex_unlock(&shm->state_mutex);
            continue;
        }
        int id = player_count % MAX_PLAYERS;
        shm->active_players++;
        player_count++;
        pthread_mutex_unlock(&shm->state_mutex);

        // [REQUIRED] Log connection event
        char connect_msg[64];
        sprintf(connect_msg, "Player %d connected.", id + 1);
        log_event(connect_msg);

        pid_t pid = fork();
        if (pid == 0) {
            close(server_fd);
            handle_client(new_socket, id);
            exit(0);
        } else {
            close(new_socket);
        }
    }
    return 0;
}

void *scheduler_thread(void *arg) {
    while (1) {
        pthread_mutex_lock(&shm->state_mutex);
        
        if (shm->active_players == MAX_PLAYERS && !shm->game_running && shm->winner_id == -1 && shm->move_count == 0) {
             shm->game_running = 1;
             log_event("Game Session Started.");
             pthread_cond_broadcast(&shm->turn_cond);
        }

        if (shm->game_running == 0 && (shm->winner_id != -1 || shm->move_count >= BOARD_SIZE)) {
            pthread_mutex_unlock(&shm->state_mutex);
            sleep(5); 
            pthread_mutex_lock(&shm->state_mutex);
            reset_game();
            log_event("Board reset for new round.");
            pthread_cond_broadcast(&shm->turn_cond);
        }
        
        pthread_mutex_unlock(&shm->state_mutex);
        usleep(500000); 
    }
    return NULL;
}

void handle_client(int sock, int id) {
    char buffer[1024];
    char symbol = (id == 0) ? 'X' : (id == 1) ? 'O' : 'A';
    sprintf(buffer, "WELCOME %d %c", id, symbol);
    send(sock, buffer, strlen(buffer), 0);

    while (1) {
        pthread_mutex_lock(&shm->state_mutex);
        while (!shm->game_running && shm->winner_id == -1 && shm->move_count == 0) {
            pthread_cond_wait(&shm->turn_cond, &shm->state_mutex);
        }
        
        if (shm->winner_id != -1 || shm->move_count >= BOARD_SIZE) {
            if (shm->winner_id == id) sprintf(buffer, "WIN");
            else if (shm->winner_id == -1) sprintf(buffer, "DRAW");
            else sprintf(buffer, "LOSE");
            
            char b_str[BOARD_SIZE + 1];
            memcpy(b_str, shm->board, BOARD_SIZE); b_str[BOARD_SIZE] = '\0';
            strcat(buffer, " "); strcat(buffer, b_str);
            send(sock, buffer, strlen(buffer), 0);
            
            pthread_cond_wait(&shm->turn_cond, &shm->state_mutex);
            pthread_mutex_unlock(&shm->state_mutex);
            continue;
        }

        if (shm->current_turn != id) {
            sprintf(buffer, "WAIT %d", shm->current_turn);
            char b_str[BOARD_SIZE + 1];
            memcpy(b_str, shm->board, BOARD_SIZE); b_str[BOARD_SIZE] = '\0';
            strcat(buffer, " "); strcat(buffer, b_str);
            send(sock, buffer, strlen(buffer), 0);
            pthread_cond_wait(&shm->turn_cond, &shm->state_mutex);
            pthread_mutex_unlock(&shm->state_mutex);
            continue;
        }

        sprintf(buffer, "TURN %s", shm->board);
        send(sock, buffer, strlen(buffer), 0);
        pthread_mutex_unlock(&shm->state_mutex);

        memset(buffer, 0, 1024);
        if (read(sock, buffer, 1024) <= 0) break; 
        int move = atoi(buffer);

        pthread_mutex_lock(&shm->state_mutex);
        if (move >= 0 && move < BOARD_SIZE && shm->board[move] == '.') {
            shm->board[move] = symbol;
            shm->move_count++;
            
            // [FIXED] Log move event correctly
            char move_log[64];
            sprintf(move_log, "Player %d (%c) moved to pos %d", id + 1, symbol, move);
            log_event(move_log);

            if (check_win(symbol)) {
                shm->winner_id = id;
                shm->game_running = 0;
                shm->scores[id]++;
                save_scores();
                char w_log[64];
                sprintf(w_log, "Game Over: Player %d wins!", id + 1);
                log_event(w_log);
            } else if (shm->move_count >= BOARD_SIZE) {
                shm->winner_id = -1;
                shm->game_running = 0;
                log_event("Game Over: Draw.");
            } else {
                shm->current_turn = (shm->current_turn + 1) % MAX_PLAYERS;
                // [REQUIRED] Log turn change
                char turn_log[64];
                sprintf(turn_log, "Turn changed to Player %d", shm->current_turn + 1);
                log_event(turn_log);
            }
            pthread_cond_broadcast(&shm->turn_cond);
        }
        pthread_mutex_unlock(&shm->state_mutex);
    }
    pthread_mutex_lock(&shm->state_mutex);
    shm->active_players--;
    pthread_mutex_unlock(&shm->state_mutex);
    close(sock);
}

void setup_shared_memory() {
    shm = mmap(NULL, sizeof(SharedState), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    pthread_mutexattr_t mattr;
    pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED); // Required [cite: 70, 130]
    pthread_mutex_init(&shm->state_mutex, &mattr);
    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&shm->turn_cond, &cattr);
}

void reset_game() {
    memset(shm->board, '.', BOARD_SIZE);
    shm->current_turn = 0; 
    shm->move_count = 0;
    shm->winner_id = -1; 
    shm->game_running = 0;
}

void load_scores() {
    FILE *fp = fopen(SCORE_FILE, "r");
    if (fp) {
        for(int i=0; i<MAX_PLAYERS; i++) fscanf(fp, "Player %*d: %d\n", &shm->scores[i]);
        fclose(fp);
    }
}

void save_scores() {
    FILE *fp = fopen(SCORE_FILE, "w");
    if (fp) {
        for(int i=0; i<MAX_PLAYERS; i++) fprintf(fp, "Player %d: %d\n", i + 1, shm->scores[i]);
        fclose(fp);
    }
}

int check_win(char s) {
    char *b = shm->board;
    for(int i=0; i<16; i+=4) if((b[i]==s && b[i+1]==s && b[i+2]==s) || (b[i+1]==s && b[i+2]==s && b[i+3]==s)) return 1;
    for(int i=0; i<4; i++) if((b[i]==s && b[i+4]==s && b[i+8]==s) || (b[i+4]==s && b[i+8]==s && b[i+12]==s)) return 1;
    if((b[0]==s && b[5]==s && b[10]==s) || (b[5]==s && b[10]==s && b[15]==s)) return 1;
    if((b[3]==s && b[6]==s && b[9]==s) || (b[6]==s && b[9]==s && b[12]==s)) return 1;
    return 0;
}

void *logger_thread(void *arg) {
    char buffer[256];
    while (1) {
        memset(buffer, 0, sizeof(buffer));
        if (read(log_pipe_fd[0], buffer, sizeof(buffer) - 1) > 0) {
            FILE *fp = fopen(LOG_FILE, "a");
            if (!fp) continue;
            time_t now = time(NULL);
            char *ts = ctime(&now); ts[strlen(ts)-1] = '\0'; 
            fprintf(fp, "[%s] %s\n", ts, buffer);
            fclose(fp);
            printf("[LOG] %s\n", buffer);
        }
    }
    return NULL;
}