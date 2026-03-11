#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define PORT       4567
#define PORT_MAX   4587
#define CMD_ENTER  2
#define CMD_EXIT   3
#define CMD_STATUS 4
#define RESP_OK    1
#define RESP_DENIED 0
#define MALE       1
#define FEMALE     2
#define SHOWER_MIN 1000000
#define SHOWER_MAX 4000000
#define SERVER_IP  "127.0.0.1"
#define DEFAULT_MULTI 8
#define N_CLIENTS  20
#define MALE_RATIO 0.5

static int sock = -1;

static int send_all(int fd, const void* buf, size_t len) {
    const char* p = (const char*)buf;
    while (len) {
        ssize_t n = send(fd, p, len, 0);
        if (n <= 0) return -1;
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, void* buf, size_t len) {
    char* p = (char*)buf;
    while (len) {
        ssize_t n = recv(fd, p, len, 0);
        if (n <= 0) return -1;
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int connect_server(const char* ip) {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return -1; }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr(ip);
    for (int p = PORT; p <= PORT_MAX; p++) {
        sa.sin_port = htons(p);
        if (connect(sock, (struct sockaddr*)&sa, sizeof(sa)) == 0) {
            printf("  [client] Подключение к %s:%d (fd=%d) успешно\n", ip, p, sock);
            fflush(stdout);
            return 0;
        }
        close(sock);
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) { perror("socket"); return -1; }
    }
    perror("connect");
    close(sock);
    return -1;
}

static int req_enter(int g, int id) {
    int buf[4] = {CMD_ENTER, g, id, 0}, r;
    if (send_all(sock, buf, sizeof(buf)) < 0 || recv_all(sock, &r, sizeof(r)) < 0)
        return RESP_DENIED;
    printf("  [client] Ответ на ENTER для %d (%s): %s\n",
           id, g == MALE ? "M" : "F", r == RESP_OK ? "OK" : "DENIED");
    fflush(stdout);
    return r;
}

static void req_exit(int g, int id) {
    int buf[4] = {CMD_EXIT, g, id, 0}, r;
    send_all(sock, buf, sizeof(buf));
    recv_all(sock, &r, sizeof(r));
    printf("  [client] EXIT отправлен для %d (%s)\n", id, g == MALE ? "M" : "F");
    fflush(stdout);
}

static void student_run(int id, int g, const char* ip) {
    char s = (g == MALE) ? 'M' : 'F';
    printf("[student %d %c] старт\n", id, s);
    fflush(stdout);
    if (connect_server(ip) < 0) {
        printf("[%d %c] Ошибка подключения\n", id, s);
        return;
    }
    int r = req_enter(g, id);
    if (r == RESP_OK) {
        useconds_t us = SHOWER_MIN + (rand() % (SHOWER_MAX - SHOWER_MIN));
        printf("  [student %d %c] вошёл, душ %.2f сек\n",
               id, s, us / 1000000.0);
        fflush(stdout);
        usleep(us);
        req_exit(g, id);
    } else {
        printf("  [student %d %c] вход отклонён сервером\n", id, s);
        fflush(stdout);
    }
    close(sock);
    printf("[student %d %c] завершён\n", id, s);
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    setbuf(stdout, 0);
    const char* ip = SERVER_IP;
    if (argc >= 2) ip = argv[1];

    /* Без аргументов или только IP: запуск по умолчанию (несколько клиентов) */
    if (argc < 3) {
        int n = DEFAULT_MULTI;
        double ratio = 0.5;
        if (argc == 2 && strcmp(argv[1], "--multi") == 0) ip = SERVER_IP;
        srand((unsigned)time(NULL) ^ getpid());
        pid_t* pids = malloc((size_t)n * sizeof(pid_t));
        printf("[main] Запуск %d студентов (по умолчанию), сервер %s:%d\n", n, ip, PORT);
        fflush(stdout);
        for (int i = 0; i < n; i++) {
            int g = (rand() / (double)RAND_MAX < ratio) ? MALE : FEMALE;
            if (fork() == 0) {
                srand((unsigned)time(NULL) ^ getpid());
                student_run(i + 1, g, ip);
                exit(0);
            }
            usleep(50000);
        }
        for (int i = 0; i < n; i++) waitpid(pids[i], NULL, 0);
        printf("[main] Все %d студентов завершили работу. Итоговую статистику см. в окне сервера.\n", n);
        fflush(stdout);
        free(pids);
        return 0;
    }

    if (strcmp(argv[2], "--multi") == 0) {
        if (argc < 4) { printf("Использование: %s [ip] --multi <N> [%%мужчин]\n", argv[0]); return 1; }
        int n = atoi(argv[3]);
        double ratio = argc >= 5 ? atoi(argv[4]) / 100.0 : 0.5;
        srand((unsigned)time(NULL) ^ getpid());
        pid_t* pids = malloc((size_t)n * sizeof(pid_t));
        printf("[main] Запуск %d студентов (--multi, мужчин ~%.0f%%), сервер %s:%d\n",
               n, ratio * 100.0, ip, PORT);
        fflush(stdout);
        for (int i = 0; i < n; i++) {
            int g = (rand() / (double)RAND_MAX < ratio) ? MALE : FEMALE;
            if (fork() == 0) {
                srand((unsigned)time(NULL) ^ getpid());
                student_run(i + 1, g, ip);
                exit(0);
            }
            usleep(50000);
        }
        for (int i = 0; i < n; i++) waitpid(pids[i], NULL, 0);
        free(pids);
    } else {
        if (argc < 4) { printf("Использование: %s [ip] <id> <M|F>\n", argv[0]); return 1; }
        int id = atoi(argv[2]);
        int g = (argv[3][0] == 'M' || argv[3][0] == 'm') ? MALE : FEMALE;
        student_run(id, g, ip);
    }
    return 0;
}
