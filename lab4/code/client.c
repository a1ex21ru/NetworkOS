/**
 * Лабораторная работа №4
 * КЛИЕНТ - Студент, желающий воспользоваться ванной
 * 
 * Подключается к серверу и отправляет запросы на вход/выход из ванной.
 * Можно запускать множество клиентов одновременно (на разных устройствах).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

// Порт сервера
#define SERVER_PORT 4567

// Команды протокола
#define CMD_CHECK_ENTRY  1
#define CMD_ENTER        2
#define CMD_EXIT         3
#define CMD_GET_STATUS   4

// Ответы сервера
#define RESP_OK          1
#define RESP_DENIED      0

// Пол
#define GENDER_MALE      1
#define GENDER_FEMALE    2

// Время в душе (микросекунды)
#define MIN_SHOWER_TIME  1000000   // 1 сек
#define MAX_SHOWER_TIME  4000000   // 4 сек

static int client_socket = -1;

/**
 * Подключение к серверу
 */
int connectToServer(const char* ip_address) {
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("socket");
        return -1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip_address);
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        close(client_socket);
        return -1;
    }
    
    return 0;
}

/**
 * Отправить команду входа и дождаться разрешения
 */
int requestEnter(int gender, int client_id) {
    int buffer[4] = {CMD_ENTER, gender, client_id, 0};
    int response;
    
    send(client_socket, buffer, sizeof(buffer), 0);
    recv(client_socket, &response, sizeof(response), 0);
    
    return response;
}

/**
 * Отправить команду выхода
 */
int requestExit(int gender, int client_id) {
    int buffer[4] = {CMD_EXIT, gender, client_id, 0};
    int response;
    
    send(client_socket, buffer, sizeof(buffer), 0);
    recv(client_socket, &response, sizeof(response), 0);
    
    return response;
}

/**
 * Получить статус ванной
 */
void getStatus(int* status, int* occupied, int* total) {
    int buffer[4] = {CMD_GET_STATUS, 0, 0, 0};
    int response[3];
    
    send(client_socket, buffer, sizeof(buffer), 0);
    recv(client_socket, response, sizeof(response), 0);
    
    *status = response[0];
    *occupied = response[1];
    *total = response[2];
}

/**
 * Генерация случайного времени в душе
 */
useconds_t generateShowerTime(void) {
    return MIN_SHOWER_TIME + (rand() % (MAX_SHOWER_TIME - MIN_SHOWER_TIME));
}

/**
 * Строка статуса
 */
const char* getStatusString(int status) {
    switch (status) {
        case 0: return "ПУСТО";
        case GENDER_MALE: return "МУЖЧИНЫ";
        case GENDER_FEMALE: return "ЖЕНЩИНЫ";
        default: return "???";
    }
}

/**
 * Процесс студента
 */
void studentProcess(int client_id, int gender, const char* server_ip) {
    const char* gender_str = (gender == GENDER_MALE) ? "M" : "F";
    
    printf("[Студент %d (%s)] Подключаюсь к серверу %s:%d...\n", 
           client_id, gender_str, server_ip, SERVER_PORT);
    fflush(stdout);
    
    if (connectToServer(server_ip) < 0) {
        printf("[Студент %d (%s)] Ошибка подключения!\n", client_id, gender_str);
        return;
    }
    
    printf("[Студент %d (%s)] Подключён. Запрашиваю вход в ванную...\n", 
           client_id, gender_str);
    fflush(stdout);
    
    // Запрашиваем вход (сервер заблокирует, пока не будет места)
    int result = requestEnter(gender, client_id);
    
    if (result == RESP_OK) {
        printf("[Студент %d (%s)] Вошёл в ванную! Принимаю душ...\n", 
               client_id, gender_str);
        fflush(stdout);
        
        // Принимаем душ
        useconds_t shower_time = generateShowerTime();
        usleep(shower_time);
        
        printf("[Студент %d (%s)] Выхожу из ванной (был %.1f сек)\n", 
               client_id, gender_str, shower_time / 1000000.0);
        fflush(stdout);
        
        // Выходим
        requestExit(gender, client_id);
        
        printf("[Студент %d (%s)] Готово!\n", client_id, gender_str);
    } else {
        printf("[Студент %d (%s)] Вход отклонён сервером\n", client_id, gender_str);
    }
    
    close(client_socket);
}

/**
 * Режим одиночного клиента
 */
void singleClientMode(int client_id, int gender, const char* server_ip) {
    studentProcess(client_id, gender, server_ip);
}

/**
 * Режим множественных клиентов (запуск нескольких процессов)
 */
void multiClientMode(int count, double male_ratio, const char* server_ip) {
    printf("\n");
    printf("======================================================================\n");
    printf(" ЗАПУСК %d КЛИЕНТОВ (%.0f%% мужчин, %.0f%% женщин)\n", 
           count, male_ratio * 100, (1 - male_ratio) * 100);
    printf(" Сервер: %s:%d\n", server_ip, SERVER_PORT);
    printf("======================================================================\n");
    printf("\n");
    fflush(stdout);
    
    srand((unsigned int)time(NULL) ^ getpid());
    
    pid_t* children = (pid_t*)malloc(count * sizeof(pid_t));
    
    for (int i = 0; i < count; i++) {
        int gender = ((rand() / (double)RAND_MAX) < male_ratio) ? GENDER_MALE : GENDER_FEMALE;
        
        pid_t pid = fork();
        
        if (pid == 0) {
            // Дочерний процесс
            srand((unsigned int)time(NULL) ^ getpid());
            studentProcess(i + 1, gender, server_ip);
            exit(0);
        }
        
        children[i] = pid;
        
        // Небольшая задержка между запусками клиентов
        usleep(100000);  // 0.1 сек
    }
    
    // Ждём завершения всех детей
    for (int i = 0; i < count; i++) {
        int status;
        waitpid(children[i], &status, 0);
    }
    
    free(children);
    
    printf("\n");
    printf("======================================================================\n");
    printf(" ВСЕ КЛИЕНТЫ ЗАВЕРШИЛИ РАБОТУ\n");
    printf("======================================================================\n");
    printf("\n");
}

void printUsage(const char* prog_name) {
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║  ЛАБОРАТОРНАЯ РАБОТА №4 - КЛИЕНТ                                 ║\n");
    printf("║  Ванная комната в общежитии (сетевая версия)                     ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Использование:\n");
    printf("\n");
    printf("  Одиночный клиент:\n");
    printf("    %s <ip_сервера> <id> <пол>\n", prog_name);
    printf("    где пол: M или F\n");
    printf("    Пример: %s 127.0.0.1 1 M\n", prog_name);
    printf("\n");
    printf("  Множественные клиенты (симуляция):\n");
    printf("    %s <ip_сервера> --multi <количество> [процент_мужчин]\n", prog_name);
    printf("    Пример: %s 127.0.0.1 --multi 20 50\n", prog_name);
    printf("\n");
}

int main(int argc, char* argv[]) {
    setbuf(stdout, 0);
    
    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }
    
    const char* server_ip = argv[1];
    
    // Режим множественных клиентов
    if (strcmp(argv[2], "--multi") == 0) {
        if (argc < 4) {
            printUsage(argv[0]);
            return 1;
        }
        
        int count = atoi(argv[3]);
        double male_ratio = 0.5;
        
        if (argc >= 5) {
            male_ratio = atoi(argv[4]) / 100.0;
        }
        
        multiClientMode(count, male_ratio, server_ip);
    }
    // Режим одиночного клиента
    else {
        if (argc < 4) {
            printUsage(argv[0]);
            return 1;
        }
        
        int client_id = atoi(argv[2]);
        int gender = (argv[3][0] == 'M' || argv[3][0] == 'm') ? GENDER_MALE : GENDER_FEMALE;
        
        singleClientMode(client_id, gender, server_ip);
    }
    
    return 0;
}

