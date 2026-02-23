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
#define CMD_ENTER  2
#define CMD_EXIT   3
#define CMD_STATUS 4
#define RESP_OK    1
#define RESP_DENIED 0
#define MALE       1
#define FEMALE     2
#define SHOWER_MIN 1000000
#define SHOWER_MAX 4000000

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
    sa.sin_port = htons(PORT);
    if (connect(sock, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }
    return 0;
}

static int req_enter(int g, int id) {
    int buf[4] = {CMD_ENTER, g, id, 0}, r;
    if (send_all(sock, buf, sizeof(buf)) < 0 || recv_all(sock, &r, sizeof(r)) < 0)
        return RESP_DENIED;
    return r;
}

static void req_exit(int g, int id) {
    int buf[4] = {CMD_EXIT, g, id, 0}, r;
    send_all(sock, buf, sizeof(buf));
    recv_all(sock, &r, sizeof(r));
}

static void student_run(int id, int g, const char* ip) {
    char s = (g == MALE) ? 'M' : 'F';
    if (connect_server(ip) < 0) {
        printf("[%d %c] Ошибка подключения\n", id, s);
        return;
    }
    int r = req_enter(g, id);
    if (r == RESP_OK) {
        useconds_t us = SHOWER_MIN + (rand() % (SHOWER_MAX - SHOWER_MIN));
        usleep(us);
        req_exit(g, id);
    }
    close(sock);
}

int main(int argc, char* argv[]) {
    setbuf(stdout, 0);
    if (argc < 3) {
        printf("Использование: %s <ip> <id> <M|F>   или   %s <ip> --multi <N> [%%мужчин]\n", argv[0], argv[0]);
        return 1;
    }
    const char* ip = argv[1];
    if (strcmp(argv[2], "--multi") == 0) {
        if (argc < 4) return 1;
        int n = atoi(argv[3]);
        double ratio = argc >= 5 ? atoi(argv[4]) / 100.0 : 0.5;
        srand((unsigned)time(NULL) ^ getpid());
        pid_t* pids = malloc((size_t)n * sizeof(pid_t));
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
        if (argc < 4) return 1;
        int id = atoi(argv[2]);
        int g = (argv[3][0] == 'M' || argv[3][0] == 'm') ? MALE : FEMALE;
        student_run(id, g, ip);
    }
    return 0;
}
