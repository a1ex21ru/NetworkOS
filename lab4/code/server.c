#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#define PORT        4567
#define CMD_CHECK   1
#define CMD_ENTER   2
#define CMD_EXIT    3
#define CMD_STATUS  4
#define RESP_OK     1
#define RESP_DENIED 0
#define MALE        1
#define FEMALE      2

typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t  cond;
    int total, occupied, gender;  /* gender: 0=empty, 1=M, 2=F */
    int consecutive, max_consec;
    bool forced_switch;
    int last_gender;
    int wait_m, wait_f;
    int entered, entered_m, entered_f;
    time_t start;
} Bathroom;

static Bathroom B;
static int listen_fd;
static volatile bool running = true;

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

static bool canEnter(int g) {
    if (B.occupied >= B.total) return false;
    if (B.gender == 0) {
        if (B.forced_switch && g == B.last_gender) return false;
        if (B.wait_m && B.wait_f) {
            if (B.wait_f > B.wait_m) return g == FEMALE;
            if (B.wait_m > B.wait_f) return g == MALE;
            return g != B.last_gender;
        }
        return true;
    }
    if (B.gender == g) return !B.forced_switch;
    return false;
}

static int enterBathroom(int g, int cid) {
    pthread_mutex_lock(&B.mtx);
    if (g == MALE) B.wait_m++; else B.wait_f++;
    while (!canEnter(g) && running)
        pthread_cond_wait(&B.cond, &B.mtx);
    if (g == MALE) B.wait_m--; else B.wait_f--;
    if (!running) {
        pthread_cond_broadcast(&B.cond);
        pthread_mutex_unlock(&B.mtx);
        return RESP_DENIED;
    }
    if (B.gender == 0) {
        if (g != B.last_gender || !B.last_gender) B.consecutive = 0, B.forced_switch = false;
        B.gender = g;
    }
    B.consecutive++;
    B.occupied++;
    B.entered++;
    if (g == MALE) B.entered_m++; else B.entered_f++;
    if (B.consecutive >= B.max_consec && ((g == MALE && B.wait_f) || (g == FEMALE && B.wait_m)))
        B.forced_switch = true;
    int t = (int)(time(NULL) - B.start);
    printf("[ВХОД %3d] Клиент %d (%s) вошёл. %d/%d | Входы %d/%d | Ожидают M=%d F=%d%s\n",
           t, cid, g == MALE ? "M" : "F", B.occupied, B.total, B.consecutive, B.max_consec,
           B.wait_m, B.wait_f, B.forced_switch ? " СМЕНА!" : "");
    fflush(stdout);
    pthread_mutex_unlock(&B.mtx);
    return RESP_OK;
}

static void exitBathroom(int g, int cid) {
    pthread_mutex_lock(&B.mtx);
    B.occupied--;
    B.last_gender = g;
    if (B.occupied == 0) B.gender = 0, B.forced_switch = false;
    int t = (int)(time(NULL) - B.start);
    printf("[ВЫХОД%3d] Клиент %d (%s) вышел. %d/%d\n", t, cid, g == MALE ? "M" : "F", B.occupied, B.total);
    fflush(stdout);
    pthread_cond_broadcast(&B.cond);
    pthread_mutex_unlock(&B.mtx);
}

static void* handle_client(void* arg) {
    int fd = *(int*)arg;
    free(arg);
    int buf[4], resp[4];
    while (running && recv_all(fd, buf, sizeof(buf)) == 0) {
        int cmd = buf[0], g = buf[1], cid = buf[2];
        int rlen = sizeof(int);
        switch (cmd) {
            case CMD_CHECK:
                pthread_mutex_lock(&B.mtx);
                resp[0] = canEnter(g) ? RESP_OK : RESP_DENIED;
                pthread_mutex_unlock(&B.mtx);
                break;
            case CMD_ENTER:
                resp[0] = enterBathroom(g, cid);
                break;
            case CMD_EXIT:
                exitBathroom(g, cid);
                resp[0] = RESP_OK;
                break;
            case CMD_STATUS:
                pthread_mutex_lock(&B.mtx);
                resp[0] = B.gender;
                resp[1] = B.occupied;
                resp[2] = B.total;
                pthread_mutex_unlock(&B.mtx);
                rlen = 3 * sizeof(int);
                break;
            default:
                resp[0] = RESP_DENIED;
        }
        if (send_all(fd, resp, rlen) < 0) break;
    }
    close(fd);
    return NULL;
}

static void* acceptor(void* arg) {
    (void)arg;
    while (running) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        int fd = accept(listen_fd, (struct sockaddr*)&addr, &len);
        if (fd < 0) continue;
        int* p = malloc(sizeof(int));
        if (!p) { close(fd); continue; }
        *p = fd;
        pthread_t t;
        if (pthread_create(&t, NULL, handle_client, p) == 0)
            pthread_detach(t);
        else
            free(p), close(fd);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    setbuf(stdout, 0);
    int cabins = argc >= 2 ? atoi(argv[1]) : 3;
    int maxc = argc >= 3 ? atoi(argv[2]) : 5;
    const char* ip = argc >= 4 ? argv[3] : "0.0.0.0";

    pthread_mutex_init(&B.mtx, NULL);
    pthread_cond_init(&B.cond, NULL);
    B.total = cabins;
    B.occupied = B.gender = B.consecutive = B.last_gender = 0;
    B.forced_switch = false;
    B.max_consec = maxc;
    B.wait_m = B.wait_f = B.entered = B.entered_m = B.entered_f = 0;
    B.start = time(NULL);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = inet_addr(ip);
    sa.sin_port = htons(PORT);
    if (bind(listen_fd, (struct sockaddr*)&sa, sizeof(sa)) < 0 ||
        listen(listen_fd, 50) < 0) {
        perror("bind/listen");
        close(listen_fd);
        return 1;
    }
    printf("Сервер: %s:%d, кабинок %d, макс входов %d\n", ip, PORT, cabins, maxc);
    pthread_t t;
    pthread_create(&t, NULL, acceptor, NULL);
    pthread_join(t, NULL);
    running = false;
    close(listen_fd);
    printf("\nИтого вошло: %d (M:%d F:%d)\n", B.entered, B.entered_m, B.entered_f);
    pthread_mutex_destroy(&B.mtx);
    pthread_cond_destroy(&B.cond);
    return 0;
}
