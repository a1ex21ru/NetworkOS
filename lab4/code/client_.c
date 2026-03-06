#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>

#define COLOR_YELLOW "\033[33m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"
#define COLOR_RESET "\033[0m"

#define PORT 8989

typedef enum { MALE = 0, FEMALE = 1 } gender_t;
#define GENDER_NAME(g) ((g) == MALE ? "мужчина" : "женщина")

typedef enum { MSG_JOIN, MSG_LEAVE, MSG_UPDATE, MSG_STATS } msg_type_t;

typedef struct {
    msg_type_t type;
    int id;
    int gender;
    long wait_start;
    long bath_time;
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

char server_ip[256] = "127.0.0.1";

void read_config(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (f) {
        fscanf(f, "%s", server_ip);
        fclose(f);
    }
}

void print_status(const msg_t *m, const char *action, int id, gender_t g) {
    printf(COLOR_GREEN"%s: студент %d (%s), занято %d/3, внутри %s, серия: %s %d, осталось: М=%d, Ж=%d\n",
           action, id, GENDER_NAME(g),
           m->occupied,
           (m->streak_gender == -1 ? "пусто" : (m->streak_gender == 0 ? "мужчины" : "женщины")),
           (m->streak_gender == -1 ? "не определён" : GENDER_NAME(m->streak_gender)),
           m->streak_used, m->remaining_male, m->remaining_female);
}

void* student_thread(void *arg) {
    int id = *((int*)arg);
    free(arg);

    gender_t gender = (rand() % 100 < 50) ? MALE : FEMALE;
    int wash_time = 1 + rand() % 4;

    int sock = 0;
    struct sockaddr_in serv_addr;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Ошибка создания сокета\n");
        return NULL;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        printf("Неверный адрес/адрес недоступен\n");
        return NULL;
    }

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Подключение не удалось\n");
        return NULL;
    }

    msg_t msg;
    msg.type = MSG_JOIN;
    msg.id = id;
    msg.gender = gender;
    msg.wait_start = (long)time(NULL);

    send(sock, &msg, sizeof(msg), 0);

    recv(sock, &msg, sizeof(msg), 0);
    print_status(&msg, "ВХОД", id, gender);

    sleep(wash_time);

    msg.type = MSG_LEAVE;
    msg.bath_time = wash_time;  // отправляем время в ванной
    send(sock, &msg, sizeof(msg), 0);

    close(sock);

    long elapsed = wash_time;
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    printf(COLOR_RED"Студент %d (%s) вышел. Время в ванной: %ld сек.\n"COLOR_RESET, id, GENDER_NAME(gender), elapsed);
    pthread_mutex_unlock(&lock);

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <число_студентов>\n", argv[0]);
        return -1;
    }

    int total_students = atoi(argv[1]);
    if (total_students <= 0) {
        fprintf(stderr, "Некорректное число студентов: %s\n", argv[1]);
        return -1;
    }

    read_config("config.txt");

    pthread_t threads[total_students];
    for (int i = 0; i < total_students; i++) {
        int *id = malloc(sizeof(int));
        *id = i;
        pthread_create(&threads[i], NULL, student_thread, id);
        usleep(100000);
    }

    for (int i = 0; i < total_students; i++) {
        pthread_join(threads[i], NULL);
    }

    // Запрос статистики
    int sock = 0;
    struct sockaddr_in serv_addr;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) return -1;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, server_ip, &serv_addr.sin_addr);
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) return -1;

    msg_t msg;
    msg.type = MSG_STATS;
    send(sock, &msg, sizeof(msg), 0);
    recv(sock, &msg, sizeof(msg), 0);

    printf("\n=== СТАТИСТИКА ===\n");
    printf("Всего вошло: %d\n", msg.entered_count);
    printf("Мужчин: %d, Женщин: %d\n", msg.male_entered, msg.female_entered);
    double avg_wait = (msg.entered_count > 0) ? (double)msg.total_wait_time / msg.entered_count : 0;
    double avg_bath = (msg.entered_count > 0) ? (double)msg.total_bath_time / msg.entered_count : 0;
    printf("Среднее ожидание: %.2f сек\n", avg_wait);

    close(sock);

    return 0;
}