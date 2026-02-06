/**
 * Лабораторная работа №4
 * СЕРВЕР - Ванная комната в общежитии
 * 
 * Управляет состоянием ванной комнаты и обрабатывает запросы клиентов через сокеты.
 * Каждый клиент (студент) подключается к серверу и запрашивает вход/выход.
 */

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

// Порт сервера
#define SERVER_PORT 4567

// Команды протокола
#define CMD_CHECK_ENTRY  1   // Проверить возможность входа
#define CMD_ENTER        2   // Войти в ванную
#define CMD_EXIT         3   // Выйти из ванной
#define CMD_GET_STATUS   4   // Получить статус ванной

// Ответы сервера
#define RESP_OK          1   // Успех
#define RESP_DENIED      0   // Отказано
#define RESP_WAIT        2   // Ждать (ванная занята противоположным полом)

// Пол
#define GENDER_MALE      1
#define GENDER_FEMALE    2

/**
 * Состояние ванной комнаты
 */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    
    int total_cabins;           // Всего кабинок
    int occupied_cabins;        // Занято кабинок
    int current_gender;         // Текущий пол (0=пусто, 1=M, 2=F)
    
    // Механизм защиты от голодания
    int consecutive_entries;    // Последовательных входов одного пола
    int max_consecutive;        // Максимум последовательных входов
    bool forced_switch;         // Флаг принудительной смены
    int last_gender;            // Пол последнего вышедшего
    
    // Счётчики ожидающих
    int waiting_males;
    int waiting_females;
    
    // Статистика
    int total_entered;
    int males_entered;
    int females_entered;
    
    // Время старта
    time_t start_time;
} Bathroom;

static Bathroom bathroom;
static int server_socket;
static volatile bool server_running = true;

/**
 * Получить время от старта в секундах
 */
int getElapsedTime(void) {
    return (int)(time(NULL) - bathroom.start_time);
}

/**
 * Получить строку статуса
 */
const char* getStatusString(int gender) {
    switch (gender) {
        case 0: return "ПУСТО";
        case GENDER_MALE: return "МУЖЧИНЫ";
        case GENDER_FEMALE: return "ЖЕНЩИНЫ";
        default: return "???";
    }
}

/**
 * Проверка возможности входа
 */
bool canEnter(int gender) {
    // Все кабинки заняты
    if (bathroom.occupied_cabins >= bathroom.total_cabins) {
        return false;
    }
    
    // Ванная пуста
    if (bathroom.current_gender == 0) {
        // Принудительная смена пола
        if (bathroom.forced_switch) {
            if (gender == bathroom.last_gender) {
                return false;
            }
            return true;
        }
        
        // Оба пола ждут — приоритет большинству
        if (bathroom.waiting_males > 0 && bathroom.waiting_females > 0) {
            if (bathroom.waiting_females > bathroom.waiting_males) {
                return (gender == GENDER_FEMALE);
            } else if (bathroom.waiting_males > bathroom.waiting_females) {
                return (gender == GENDER_MALE);
            } else {
                // Равное количество — чередуем
                if (bathroom.last_gender == GENDER_MALE) {
                    return (gender == GENDER_FEMALE);
                } else if (bathroom.last_gender == GENDER_FEMALE) {
                    return (gender == GENDER_MALE);
                }
                return true;
            }
        }
        
        return true;
    }
    
    // Ванная занята тем же полом
    if (bathroom.current_gender == gender) {
        if (bathroom.forced_switch) {
            return false;
        }
        return true;
    }
    
    // Противоположный пол — запрещено
    return false;
}

/**
 * Вход студента в ванную
 */
int enterBathroom(int gender, int client_id) {
    pthread_mutex_lock(&bathroom.mutex);
    
    // Увеличиваем счётчик ожидающих
    if (gender == GENDER_MALE) bathroom.waiting_males++;
    else bathroom.waiting_females++;
    
    // Ждём возможности входа
    while (!canEnter(gender) && server_running) {
        pthread_cond_wait(&bathroom.cond, &bathroom.mutex);
    }
    
    // Уменьшаем счётчик ожидающих
    if (gender == GENDER_MALE) bathroom.waiting_males--;
    else bathroom.waiting_females--;
    
    if (!server_running) {
        pthread_cond_broadcast(&bathroom.cond);
        pthread_mutex_unlock(&bathroom.mutex);
        return RESP_DENIED;
    }
    
    // Обновляем состояние
    if (bathroom.current_gender == 0) {
        if (gender != bathroom.last_gender || bathroom.last_gender == 0) {
            bathroom.consecutive_entries = 0;
            bathroom.forced_switch = false;
        }
        bathroom.current_gender = gender;
    }
    
    bathroom.consecutive_entries++;
    bathroom.occupied_cabins++;
    bathroom.total_entered++;
    
    if (gender == GENDER_MALE) bathroom.males_entered++;
    else bathroom.females_entered++;
    
    // Проверяем лимит входов
    bool opposite_waiting = (gender == GENDER_MALE && bathroom.waiting_females > 0) ||
                            (gender == GENDER_FEMALE && bathroom.waiting_males > 0);
    
    if (bathroom.consecutive_entries >= bathroom.max_consecutive && opposite_waiting) {
        bathroom.forced_switch = true;
    }
    
    // Вывод
    const char* gender_str = (gender == GENDER_MALE) ? "M" : "F";
    printf("[ВХОД %3d] Клиент %d (%s) вошёл. Занято: %d/%d | Входы: %d/%d | Ожидают: M=%d F=%d%s\n",
           getElapsedTime(),
           client_id,
           gender_str,
           bathroom.occupied_cabins,
           bathroom.total_cabins,
           bathroom.consecutive_entries,
           bathroom.max_consecutive,
           bathroom.waiting_males,
           bathroom.waiting_females,
           bathroom.forced_switch ? " | СМЕНА!" : "");
    fflush(stdout);
    
    pthread_mutex_unlock(&bathroom.mutex);
    return RESP_OK;
}

/**
 * Выход студента из ванной
 */
void exitBathroom(int gender, int client_id) {
    pthread_mutex_lock(&bathroom.mutex);
    
    bathroom.occupied_cabins--;
    bathroom.last_gender = gender;
    
    if (bathroom.occupied_cabins == 0) {
        bathroom.current_gender = 0;
    }
    
    const char* gender_str = (gender == GENDER_MALE) ? "M" : "F";
    printf("[ВЫХОД%3d] Клиент %d (%s) вышел. Занято: %d/%d | Статус: %s\n",
           getElapsedTime(),
           client_id,
           gender_str,
           bathroom.occupied_cabins,
           bathroom.total_cabins,
           getStatusString(bathroom.current_gender));
    fflush(stdout);
    
    // Будим ожидающих
    pthread_cond_broadcast(&bathroom.cond);
    
    pthread_mutex_unlock(&bathroom.mutex);
}

/**
 * Получить статус ванной
 */
void getStatus(int* status, int* occupied, int* total) {
    pthread_mutex_lock(&bathroom.mutex);
    *status = bathroom.current_gender;
    *occupied = bathroom.occupied_cabins;
    *total = bathroom.total_cabins;
    pthread_mutex_unlock(&bathroom.mutex);
}

/**
 * Обработчик клиента (выполняется в отдельном потоке)
 */
void* handleClient(void* arg) {
    int client_socket = *(int*)arg;
    free(arg);
    
    int buffer[4];  // [команда, пол, client_id, доп.данные]
    int response[4];
    
    // Статус соединения: 1 = подключено, 0 = отключено
    int connection_status = 1;
    
    while (server_running && connection_status) {
        // Получаем команду от клиента
        int bytes = recv(client_socket, buffer, sizeof(buffer), 0);
        
        // Проверка статуса соединения
        if (bytes == 0) {
            // Клиент корректно закрыл соединение
            printf("[СЕРВЕР] Клиент отключился (нормальное закрытие)\n");
            fflush(stdout);
            connection_status = 0;
            break;
        } else if (bytes < 0) {
            // Ошибка при получении данных
            perror("[СЕРВЕР] Ошибка recv");
            connection_status = 0;
            break;
        }
        
        // Проверяем, что получили полный пакет
        if (bytes < (int)sizeof(int)) {
            printf("[СЕРВЕР] Получен неполный пакет (%d байт)\n", bytes);
            fflush(stdout);
            continue;
        }
        
        int cmd = buffer[0];
        int gender = buffer[1];
        int client_id = buffer[2];
        
        switch (cmd) {
            case CMD_CHECK_ENTRY: {
                // Проверка возможности входа (без блокировки)
                pthread_mutex_lock(&bathroom.mutex);
                bool can = canEnter(gender);
                pthread_mutex_unlock(&bathroom.mutex);
                response[0] = can ? RESP_OK : RESP_DENIED;
                
                if (send(client_socket, response, sizeof(int), 0) < 0) {
                    perror("[СЕРВЕР] Ошибка send (CHECK_ENTRY)");
                    connection_status = 0;
                }
                break;
            }
            
            case CMD_ENTER: {
                // Вход (с блокировкой до освобождения места)
                int result = enterBathroom(gender, client_id);
                response[0] = result;
                
                if (send(client_socket, response, sizeof(int), 0) < 0) {
                    perror("[СЕРВЕР] Ошибка send (ENTER)");
                    connection_status = 0;
                }
                break;
            }
            
            case CMD_EXIT: {
                // Выход
                exitBathroom(gender, client_id);
                response[0] = RESP_OK;
                
                if (send(client_socket, response, sizeof(int), 0) < 0) {
                    perror("[СЕРВЕР] Ошибка send (EXIT)");
                    connection_status = 0;
                }
                break;
            }
            
            case CMD_GET_STATUS: {
                // Получить статус
                int status, occupied, total;
                getStatus(&status, &occupied, &total);
                response[0] = status;
                response[1] = occupied;
                response[2] = total;
                
                if (send(client_socket, response, 3 * sizeof(int), 0) < 0) {
                    perror("[СЕРВЕР] Ошибка send (GET_STATUS)");
                    connection_status = 0;
                }
                break;
            }
            
            default: {
                printf("[СЕРВЕР] Неизвестная команда: %d\n", cmd);
                fflush(stdout);
                response[0] = RESP_DENIED;
                send(client_socket, response, sizeof(int), 0);
                break;
            }
        }
    }
    
    // Закрываем соединение
    if (connection_status == 0) {
        printf("[СЕРВЕР] Соединение закрыто\n");
        fflush(stdout);
    }
    
    close(client_socket);
    return NULL;
}

/**
 * Поток для приёма новых подключений
 */
void* acceptConnections(void* arg) {
    (void)arg;
    
    printf("[СЕРВЕР] Поток приёма подключений запущен\n");
    fflush(stdout);
    
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        // Ожидаем новое подключение
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
        
        // Проверка статуса accept
        if (client_socket < 0) {
            if (server_running) {
                perror("[СЕРВЕР] Ошибка accept");
            }
            continue;
        }
        
        // Успешное подключение
        printf("[СЕРВЕР] [ПОДКЛЮЧЕНИЕ] Новый клиент от %s:%d (сокет: %d)\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               client_socket);
        fflush(stdout);
        
        // Создаём поток для обработки клиента
        int* socket_ptr = (int*)malloc(sizeof(int));
        if (socket_ptr == NULL) {
            perror("[СЕРВЕР] Ошибка malloc");
            close(client_socket);
            continue;
        }
        
        *socket_ptr = client_socket;
        
        pthread_t thread;
        int result = pthread_create(&thread, NULL, handleClient, socket_ptr);
        if (result != 0) {
            fprintf(stderr, "[СЕРВЕР] Ошибка создания потока: %d\n", result);
            free(socket_ptr);
            close(client_socket);
            continue;
        }
        
        pthread_detach(thread);
    }
    
    printf("[СЕРВЕР] Поток приёма подключений завершён\n");
    fflush(stdout);
    
    return NULL;
}

/**
 * Инициализация ванной
 */
void initBathroom(int cabins, int max_consecutive) {
    pthread_mutex_init(&bathroom.mutex, NULL);
    pthread_cond_init(&bathroom.cond, NULL);
    
    bathroom.total_cabins = cabins;
    bathroom.occupied_cabins = 0;
    bathroom.current_gender = 0;
    bathroom.consecutive_entries = 0;
    bathroom.max_consecutive = max_consecutive;
    bathroom.forced_switch = false;
    bathroom.last_gender = 0;
    bathroom.waiting_males = 0;
    bathroom.waiting_females = 0;
    bathroom.total_entered = 0;
    bathroom.males_entered = 0;
    bathroom.females_entered = 0;
    bathroom.start_time = time(NULL);
}

/**
 * Вывод статистики
 */
void printStatistics(void) {
    printf("\n");
    printf("======================================================================\n");
    printf(" СТАТИСТИКА СЕРВЕРА\n");
    printf("======================================================================\n");
    printf(" Всего вошло: %d студентов\n", bathroom.total_entered);
    printf("   - Мужчины: %d\n", bathroom.males_entered);
    printf("   - Женщины: %d\n", bathroom.females_entered);
    printf(" Вместимость ванной: %d кабинок\n", bathroom.total_cabins);
    printf(" Макс. последовательных входов: %d\n", bathroom.max_consecutive);
    printf("======================================================================\n");
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    setbuf(stdout, 0);
    
    // Параметры по умолчанию
    int cabins = 3;
    int max_consecutive = 5;
    char* ip_address = "0.0.0.0";  // Слушать на всех интерфейсах
    
    if (argc >= 2) {
        cabins = atoi(argv[1]);
    }
    if (argc >= 3) {
        max_consecutive = atoi(argv[2]);
    }
    if (argc >= 4) {
        ip_address = argv[3];
    }
    
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║  ЛАБОРАТОРНАЯ РАБОТА №4 - СЕРВЕР                                 ║\n");
    printf("║  Ванная комната в общежитии (сетевая версия)                     ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf(" Вместимость ванной: %d кабинок\n", cabins);
    printf(" Макс. последовательных входов: %d\n", max_consecutive);
    printf(" IP адрес: %s\n", ip_address);
    printf(" Порт: %d\n", SERVER_PORT);
    printf("\n");
    printf(" Использование: %s [кабинок] [макс_входов] [ip_адрес]\n", argv[0]);
    printf("\n");
    
    // Инициализация ванной
    initBathroom(cabins, max_consecutive);
    
    // Создание сокета
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("socket");
        return 1;
    }
    
    // Разрешаем повторное использование адреса
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Настройка адреса
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip_address);
    server_addr.sin_port = htons(SERVER_PORT);
    
    // Привязка
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_socket);
        return 1;
    }
    
    // Начинаем слушать
    if (listen(server_socket, 50) < 0) {
        perror("listen");
        close(server_socket);
        return 1;
    }
    
    printf("[СЕРВЕР] Запущен и ожидает подключений...\n");
    printf("[СЕРВЕР] Нажмите Ctrl+C для остановки\n");
    printf("\n");
    fflush(stdout);
    
    // Запускаем поток для приёма подключений
    pthread_t accept_thread;
    pthread_create(&accept_thread, NULL, acceptConnections, NULL);
    
    // Главный поток ждёт (можно добавить обработку команд)
    pthread_join(accept_thread, NULL);
    
    // Завершение
    server_running = false;
    close(server_socket);
    printStatistics();
    
    pthread_mutex_destroy(&bathroom.mutex);
    pthread_cond_destroy(&bathroom.cond);
    
    return 0;
}

