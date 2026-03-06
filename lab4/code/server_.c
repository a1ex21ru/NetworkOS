#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>

#define PORT 8989
#define BACKLOG 10
#define CAPACITY 3
#define STREAK_LIMIT 5

#define COLOR_YELLOW "\033[33m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"
#define COLOR_RESET "\033[0m"

typedef enum { MALE = 0, FEMALE = 1 } gender_t;
typedef enum { EMPTY, MEN_INSIDE, WOMEN_INSIDE } bath_state_t;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    int capacity;
    int occupied;
    bath_state_t bath_gender;

    int streak_gender;      // 0=MALE, 1=FEMALE, -1=unset
    int streak_used;
    int streak_limit;

    int remaining_male;
    int remaining_female;

    long total_wait_time;
    long total_bath_time;
    int entered_count;
    int male_entered, female_entered;
} bath_server_t;

bath_server_t bath_s;

void bath_server_init(bath_server_t *b) {
    pthread_mutex_init(&b->mutex, NULL);
    pthread_cond_init(&b->cond, NULL);
    b->capacity = CAPACITY;
    b->occupied = 0;
    b->bath_gender = EMPTY;
    b->streak_gender = -1;
    b->streak_used = 0;
    b->streak_limit = STREAK_LIMIT;
    b->remaining_male = 0;
    b->remaining_female = 0;
    b->total_wait_time = 0;
    b->total_bath_time = 0;
    b->entered_count = 0;
    b->male_entered = 0;
    b->female_entered = 0;
}

// Структура сообщений
typedef enum {
    MSG_JOIN,
    MSG_LEAVE,
    MSG_UPDATE,
    MSG_STATS
} msg_type_t;

typedef struct {
    msg_type_t type;
    int id;
    int gender;       // 0=MALE, 1=FEMALE
    long wait_start;  // время начала ожидания (от клиента)
    long bath_time;   // время в ванной (от клиента)
    int occupied;
    int capacity;
    int streak_gender;
    int streak_used;
    int remaining_male;
    int remaining_female;
    long total_wait_time;
    long total_bath_time;
    int entered_count;
    int male_entered;
    int female_entered;
} msg_t;

void* handle_client(void *arg) {
    int client_fd = *(int*)arg;
    free(arg);

    // Получаем IP-адрес клиента
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    getpeername(client_fd, (struct sockaddr*)&addr, &len);
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    printf(COLOR_RESET"Клиент подключился с IP: %s\n", client_ip);

    msg_t msg;
    while (recv(client_fd, &msg, sizeof(msg), 0) > 0) {
        pthread_mutex_lock(&bath_s.mutex);

        if (msg.type == MSG_JOIN) {
            if (msg.gender == 0) bath_s.remaining_male++;
            else bath_s.remaining_female++;

            while (1) {
                int can_enter = 0;

                if (bath_s.occupied >= bath_s.capacity) {
                    can_enter = 0;
                } else if (bath_s.occupied == 0) {
                    if (bath_s.streak_gender == -1) {
                        can_enter = 1;
                    } else {
                        int allowed = (bath_s.streak_used < bath_s.streak_limit) ? bath_s.streak_gender : !bath_s.streak_gender;
                        if (msg.gender == allowed) {
                            can_enter = 1;
                        } else {
                            if ((allowed == 0 && bath_s.remaining_male == 0) || (allowed == 1 && bath_s.remaining_female == 0)) {
                                bath_s.streak_gender = -1;
                                bath_s.streak_used = 0;
                                can_enter = 1;
                            }
                        }
                    }
                } else {
                    if ((bath_s.bath_gender == MEN_INSIDE && msg.gender != 0) ||
                        (bath_s.bath_gender == WOMEN_INSIDE && msg.gender != 1)) {
                        can_enter = 0;
                    } else {
                        int allowed = (bath_s.streak_used < bath_s.streak_limit) ? bath_s.streak_gender : !bath_s.streak_gender;
                        can_enter = (msg.gender == allowed);
                    }
                }

                if (can_enter) break;
                pthread_cond_wait(&bath_s.cond, &bath_s.mutex);
            }

            // Считаем время ожидания
            long wait_duration = time(NULL) - msg.wait_start;
            bath_s.total_wait_time += wait_duration;

            bath_s.occupied++;
            if (bath_s.occupied == 1) {
                bath_s.bath_gender = (msg.gender == 0) ? MEN_INSIDE : WOMEN_INSIDE;
            }
            if (msg.gender == 0) bath_s.remaining_male--; else bath_s.remaining_female--;

            if (bath_s.streak_gender == -1) {
                bath_s.streak_gender = msg.gender;
                bath_s.streak_used = 1;
            } else if (msg.gender == bath_s.streak_gender) {
                bath_s.streak_used++;
            } else {
                bath_s.streak_gender = msg.gender;
                bath_s.streak_used = 1;
            }

            msg.type = MSG_UPDATE;
            msg.occupied = bath_s.occupied;
            msg.capacity = bath_s.capacity;
            msg.streak_gender = bath_s.streak_gender;
            msg.streak_used = bath_s.streak_used;
            msg.remaining_male = bath_s.remaining_male;
            msg.remaining_female = bath_s.remaining_female;
            send(client_fd, &msg, sizeof(msg), 0);
        }
        else if (msg.type == MSG_LEAVE) {
            bath_s.occupied--;
            bath_s.total_bath_time += msg.bath_time;
            bath_s.entered_count++;
            if (msg.gender == 0) {
                bath_s.male_entered++;
            } else {
                bath_s.female_entered++;
            }

            if (bath_s.occupied == 0) {
                bath_s.bath_gender = EMPTY;
            }

            pthread_cond_broadcast(&bath_s.cond);
        }
        else if (msg.type == MSG_STATS) {
            msg.total_wait_time = bath_s.total_wait_time;
            msg.total_bath_time = bath_s.total_bath_time;
            msg.entered_count = bath_s.entered_count;
            msg.male_entered = bath_s.male_entered;
            msg.female_entered = bath_s.female_entered;
            send(client_fd, &msg, sizeof(msg), 0);
        }

        pthread_mutex_unlock(&bath_s.mutex);
    }

    //printf(COLOR_RESET"Клиент с IP %s отключился\n", client_ip);
    close(client_fd);
    return NULL;
}

int main() {
    bath_server_init(&bath_s);

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    socklen_t addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf(COLOR_GREEN"Сервер запущен на порту %d\n", PORT);

    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen)) < 0) {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        int *fd_ptr = malloc(sizeof(int));
        *fd_ptr = new_socket;
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, fd_ptr);
        pthread_detach(thread);
    }

    return 0;
}