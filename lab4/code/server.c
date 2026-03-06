#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <linux/time.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>

// ============================================
// НАСТРОЙКИ СЕТЕВОГО ПОДКЛЮЧЕНИЯ
// ============================================
#define PORT 5050           // Порт сервера (как требуется в задании)
#define MAX_CLIENTS 2       // Максимальное количество клиентов (по заданию не более 2)
#define BUFFER_SIZE 1024    // Размер буфера для обмена данными

// ============================================
// ЦВЕТОВЫЕ КОДЫ ДЛЯ КОНСОЛИ
// ============================================
#define COLOR_YELLOW "\033[33m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"
#define COLOR_RESET "\033[0m"

#define MAX_STREAK_FOR_STATE 5

// ============================================
// ТИПЫ ДАННЫХ МОДЕЛИ ВАННОЙ (из ЛР1)
// ============================================
enum State {
    nobody, 
    man,  
    woman,
};
typedef enum State Sex;

typedef struct timespec Time;

// Структура ванной комнаты (разделяемый ресурс)
struct Bathroom
{
    pthread_mutex_t dataMutex;      // Мьютекс для защиты данных
    pthread_cond_t cond;            // Условная переменная для ожидания
    pthread_mutex_t condMutex;      // Мьютекс для условной переменной

    unsigned cabins_total;          // Всего кабинок
    unsigned cabins_used;           // Занято кабинок
    Sex state;                      // Текущее состояние (nobody/man/woman)
    Sex last_state;                 // Предыдущее состояние

    unsigned waiting_men;           // Ожидающие мужчины
    unsigned waiting_women;         // Ожидающие женщины

    unsigned streak;                // Текущая серия одного пола
    unsigned max_streak;            // Максимальная серия
    bool force_change;              // Флаг принудительной смены пола
};
typedef struct Bathroom Br;

// Структура студента (потока)
struct Student
{
    int studentID;
    enum State sex;
    float timeForShower;
    struct timespec arrival;
    struct timespec enter;
    struct timespec leave;
    int client_socket;
};
typedef struct Student St;

const int BATHROOM_CAPACITY = 4;

// ============================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// ============================================

// Глобальная ванная (разделяемый ресурс между потоками)
struct Bathroom b = {
    .dataMutex = PTHREAD_MUTEX_INITIALIZER,
    .condMutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .cabins_total = BATHROOM_CAPACITY,
    .cabins_used = 0,
    .state = nobody,
    .last_state = nobody,
    .force_change = false,
    .waiting_men = 0,
    .waiting_women = 0,
    .streak = 0,
    .max_streak = MAX_STREAK_FOR_STATE,
};

// Массив клиентских сокетов для отслеживания подключений
int client_sockets[MAX_CLIENTS];
int num_clients = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================

// Вычисление разницы времени
double timespec_diff(Time a, Time b) {
    return (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
}

// ============================================
// СЕТЕВЫЕ ФУНКЦИИ ОТПРАВКИ ДАННЫХ
// ============================================

/**
 * @brief Отправка сообщения конкретному клиенту
 * @param sock - сокет клиента
 * @param msg - сообщение для отправки
 * 
 * Использует системный вызов send() для передачи данных через TCP-сокет.
 * Флаг 0 означает стандартное поведение (блокирующая отправка).
 */
void send_to_client(int sock, const char* msg) {
    if (sock > 0) {
        // send(sockfd, buffer, length, flags)
        // sock - дескриптор сокета
        // msg - указатель на данные
        // strlen(msg) - длина данных
        // 0 - флаги (стандартное поведение)
        send(sock, msg, strlen(msg), 0);
    }
}

/**
 * @brief Рассылка сообщения всем подключенным клиентам
 * @param msg - сообщение для рассылки
 * 
 * Использует мьютекс для защиты доступа к массиву клиентских сокетов.
 * Проходит по всем слотам и отправляет сообщение активным клиентам.
 */
void broadcast_to_clients(const char* msg) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] > 0) {
            send(client_sockets[i], msg, strlen(msg), 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}


// Проверка возможности входа в ванную
bool canEnter(Br* b, St* s) {
    // pthread_mutex_lock(&b->dataMutex);

    // Все кабинки заняты
    if (b->cabins_used == b->cabins_total) {
        // pthread_mutex_unlock(&b->dataMutex);
        return false;
    }

    // Если пусто, проверяем флаг смены пола
    if (b->state == nobody) {
        if (b->force_change && b->last_state == s->sex) {
            // pthread_mutex_unlock(&b->dataMutex);
            return false;
        }
        // pthread_mutex_unlock(&b->dataMutex);
        return true;
    }
    
    // Разный пол - вход запрещен
    if (b->state != s->sex) {
        // pthread_mutex_unlock(&b->dataMutex);
        return false;
    }

    // Справедливый вход: после max_streak отдаем приоритет другому полу
    if (b->streak >= b->max_streak) {
        if (s->sex == man && b->waiting_women > 0) {
            // pthread_mutex_unlock(&b->dataMutex);
            return false;
        }
        if (s->sex == woman && b->waiting_men > 0) {
            // pthread_mutex_unlock(&b->dataMutex);
            return false;
        }
    }

    // pthread_mutex_unlock(&b->dataMutex);
    return true;
}

// Вход в ванную
bool enterBathroom(Br *b, St *s) {
    // pthread_mutex_lock(&b->condMutex);
    pthread_mutex_lock(&b->dataMutex);
    // Увеличиваем счетчик ожидающих
    if (s->sex == man) b->waiting_men++;
    else b->waiting_women++;

    // Ожидание условия входа (pthread_cond_wait автоматически разблокирует мьютекс)
    while (!canEnter(b,s)) {
        pthread_cond_wait(&b->cond, &b->dataMutex);
    }

    // Уменьшаем счетчик ожидающих
    if (s->sex == man) b->waiting_men--;
    else b->waiting_women--;

    // Устанавливаем состояние ванной
    if (b->state == nobody) {
        if (b->force_change && b->last_state != s->sex) {
            b->force_change = false;
            char msg[256];
            snprintf(msg, sizeof(msg), COLOR_YELLOW"\t>>> СМЕНА ПОЛА ВЫПОЛНЕНА: Теперь в ванной %s <<<\n"COLOR_RESET,
                   s->sex == man ? "мужчины" : "женщины");
            // printf("%s", msg);
            broadcast_to_clients(msg);
        }
        b->state = s->sex;
        b->streak = 0;
    }

    b->cabins_used++;
    b->streak++;
    
    // Формируем сообщение о входе
    char msg[512];
    snprintf(msg, sizeof(msg), 
        COLOR_GREEN"%4d. Студент (%-5s) in, время (%5.3f). Занято: %d/%d Серия: %d/%d \twait_m: %d wait_w: %d\n"COLOR_RESET,
        s->studentID,
        s->sex == man ? "man":"woman",
        s->timeForShower,
        b->cabins_used, b->cabins_total,
        b->streak, b->max_streak,
        b->waiting_men, b->waiting_women);
    
    // printf("%s", msg);
    send_to_client(s->client_socket, msg);

    // Уведомление о достижении максимальной серии
    if (b->streak == b->max_streak) {
        snprintf(msg, sizeof(msg), 
            COLOR_YELLOW"\t>>> СМЕНА: Достигнута максимальная серия (%d) для %6s! <<<\n"COLOR_RESET,
            b->max_streak,
            s->sex == man ? "мужчин" : "женщин");
        // printf("%s", msg);
        broadcast_to_clients(msg);
    }
    
    pthread_mutex_unlock(&b->dataMutex);
    return true;
}

// Выход из ванной
void leaveBathroom(Br* b, St* s) {
    // pthread_mutex_lock(&b->condMutex);
    pthread_mutex_lock(&b->dataMutex);

    b->cabins_used--;

    // Если все вышли - сбрасываем состояние
    if (b->cabins_used == 0) {
        if (b->streak >= b->max_streak) {
            b->last_state = b->state;
            b->force_change = true;
        } else {
            b->force_change = false;
        }
        b->state = nobody;
        b->streak = 0;
    }
    
    // Формируем сообщение о выходе
    char msg[256];
    snprintf(msg, sizeof(msg), 
        COLOR_RED"%4d. Student (%-5s) out. Занято: %d/%d\n"COLOR_RESET,
        s->studentID,
        s->sex == man ? "man":"woman",
        b->cabins_used, b->cabins_total);
    
    //printf("%s", msg);
    send_to_client(s->client_socket, msg);

    // pthread_mutex_unlock(&b->dataMutex);
    // Сигнализируем всем ожидающим, что место освободилось
    pthread_cond_broadcast(&b->cond);
    pthread_mutex_unlock(&b->dataMutex);
}

// Функция потока студента
void* studentThread(void* arg) {
    St* s = (St*)arg;
    clock_gettime(CLOCK_MONOTONIC, &s->arrival);
    
    if (enterBathroom(&b, s)) {
        clock_gettime(CLOCK_MONOTONIC, &s->enter);
        // Имитация времени в душе (nanosleep)
        struct timespec ts;
        ts.tv_sec = (time_t)s->timeForShower;
        ts.tv_nsec = (long)((s->timeForShower - ts.tv_sec) * 1e9);
        nanosleep(&ts, NULL);
        leaveBathroom(&b, s);
    }
    clock_gettime(CLOCK_MONOTONIC, &s->leave);
    return 0;
}

// Генерация случайного числа
int random_number(int min_num, int max_num) {
    return (rand() % (max_num - min_num + 1)) + min_num;
}

// ============================================
// ЗАПУСК СИМУЛЯЦИИ ДЛЯ КЛИЕНТА
// ============================================

/**
 * @brief Запуск симуляции студентов для конкретного клиента
 * @param studLen - количество студентов
 * @param client_sock - сокет клиента для отправки результатов
 * 
 * Создает потоки студентов, запускает симуляцию и отправляет
 * результаты клиенту через сокет.
 */
void run_simulation(int studLen, int client_sock) {
    St* students = (St*)malloc(studLen * sizeof(St));
    
    // Инициализация студентов
    for (int i = 0; i < studLen; i++) {
        int randTime = random_number(2, 5);
        students[i].sex = (i % 2 == 0) ? man : woman;
        students[i].timeForShower = (students[i].sex == woman) ? 2 * randTime : randTime;
        students[i].studentID = i + 1;
        students[i].client_socket = client_sock;
    }

    // Отправка информации о начале
    char msg[256];
    snprintf(msg, sizeof(msg), 
        COLOR_RESET"\tНачало: студентов - %d, кабинок - %d, максимальная серия - %d\n"COLOR_RESET,
        studLen, b.cabins_total, b.max_streak);
    // printf("%s", msg);
    send_to_client(client_sock, msg);

    // Создание и запуск потоков
    pthread_t* tid = (pthread_t*)malloc(studLen * sizeof(pthread_t));
    Time total_begin, total_end;

    clock_gettime(CLOCK_MONOTONIC, &total_begin);
    for (int i = 0; i < studLen; i++) {
        pthread_create(&tid[i], NULL, studentThread, &students[i]);
    }

    // Ожидание завершения всех потоков
    for (int i = 0; i < studLen; i++) {
        pthread_join(tid[i], NULL);
    }
    clock_gettime(CLOCK_MONOTONIC, &total_end);

    // Отправка сообщения о завершении
    snprintf(msg, sizeof(msg), 
        COLOR_RESET"\t=======================Завершение======================\n"COLOR_RESET);
    // printf("%s", msg);
    send_to_client(client_sock, msg);

    // Расчет статистики
    double total_wait = 0.0, total_shower = 0.0;
    for (int i = 0; i < studLen; i++) {
        total_wait += timespec_diff(students[i].arrival, students[i].enter);
        total_shower += timespec_diff(students[i].enter, students[i].leave);
    }

    double total_time = timespec_diff(total_begin, total_end);
    double utilization = total_shower / (total_time * b.cabins_total) * 100;
    double avg_wait = total_wait / studLen;

    // Отправка статистики
    snprintf(msg, sizeof(msg), 
        COLOR_RESET"Среднее время ожидания: %f\n"COLOR_RESET, avg_wait);
    // printf("%s", msg);
    send_to_client(client_sock, msg);

    snprintf(msg, sizeof(msg), 
        COLOR_RESET"Утилизация: %.2f%%\n"COLOR_RESET, utilization);
    // printf("%s", msg);
    send_to_client(client_sock, msg);

    free(students);
    free(tid);
}

// ============================================
// ОБРАБОТКА КЛИЕНТА (В ОТДЕЛЬНОМ ПОТОКЕ)
// ============================================

/**
 * @brief Функция обработки клиента в отдельном потоке
 * @param arg - указатель на сокет клиента
 * 
 * Реализует протокол обмена:
 * 1. Отправляет сигнал готовности SERVER_READY
 * 2. Получает количество студентов через recv()
 * 3. Запускает симуляцию
 * 4. Закрывает соединение
 */
void* handle_client(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];

    while (1) {

        // 1. Сигнал готовности
        snprintf(response, sizeof(response),
                 "SERVER_READY: Отправьте количество студентов или quit\n");
        send(client_sock, response, strlen(response), 0);

        // 2. Получение команды
        memset(buffer, 0, BUFFER_SIZE);
        int recv_len = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);

        if (recv_len <= 0)
            break;

        buffer[recv_len] = '\0';

        // 3. Проверка на quit
        if (strncmp(buffer, "quit", 4) == 0 ||
            strncmp(buffer, "exit", 4) == 0) {
            send(client_sock, "Отключение...\n", 15, 0);
            break;
        }

        int studLen = atoi(buffer);

        if (studLen > 0 && studLen <= 100) {
            snprintf(response, sizeof(response),
                     "Принято: %d студентов. Запуск...\n", studLen);
            send(client_sock, response, strlen(response), 0);

            run_simulation(studLen, client_sock);

            send(client_sock,
                 "Симуляция завершена.\n",
                 22, 0);
        } else {
            send(client_sock,
                 "Ошибка: 1-100\n",
                 14, 0);
        }
    }

    close(client_sock);
    return NULL;
}

// ============================================
// ГЛАВНАЯ ФУНКЦИЯ СЕРВЕРА
// ============================================

/**
 * @brief Главная функция сервера
 * 
 * Алгоритм работы:
 * 1. Создание TCP-сокета (socket)
 * 2. Настройка опций (setsockopt)
 * 3. Привязка к адресу и порту (bind)
 * 4. Перевод в режим прослушивания (listen)
 * 5. Установка неблокирующего режима (fcntl)
 * 6. Цикл обработки подключений с использованием select()
 * 7. Создание потока для каждого клиента
 */
int main() {
    srand((unsigned)time(NULL));
    
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    fd_set readfds;  // Набор дескрипторов для select()
    
    // Инициализация массива клиентских сокетов
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = 0;
    }
    
    // ============================================
    // ШАГ 1: Создание TCP-сокета
    // ============================================
    // socket(domain, type, protocol)
    // AF_INET - IPv4 интернет-протокол
    // SOCK_STREAM - потоковый сокет (TCP)
    // 0 - протокол по умолчанию для данного типа
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // ============================================
    // ШАГ 2: Настройка опций сокета
    // ============================================
    // SO_REUSEADDR - разрешает повторное использование адреса
    // Это нужно для быстрого перезапуска сервера без ожидания таймаута
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }
    
    // ============================================
    // ШАГ 3: Настройка адреса сервера
    // ============================================
    address.sin_family = AF_INET;           // Семейство адресов IPv4
    address.sin_addr.s_addr = /*inet_addr("10.111.255.122") */INADDR_ANY; // Принимать соединения на всех интерфейсах
    address.sin_port = htons(PORT);       // Порт (htons - перевод в сетевой порядок байт)
    
    // ============================================
    // ШАГ 4: Привязка сокета к адресу
    // ============================================
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    
    // ============================================
    // ШАГ 5: Перевод в режим прослушивания
    // ============================================
    // listen(sockfd, backlog)
    // backlog - максимальная длина очереди ожидающих соединений
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }
    
    // ============================================
    // ШАГ 6: Установка неблокирующего режима
    // ============================================
    // F_GETFL - получить текущие флаги
    // F_SETFL - установить новые флаги
    // O_NONBLOCK - неблокирующий режим
    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    
    printf("Сервер запущен на порту %d\n", PORT);
    printf("Ожидание подключений (макс. %d клиентов)...\n", MAX_CLIENTS);
    
    // ============================================
    // ГЛАВНЫЙ ЦИКЛ ОБРАБОТКИ ПОДКЛЮЧЕНИЙ
    // ============================================
    while (1) {
        // Очистка набора дескрипторов
        FD_ZERO(&readfds);
        // Добавление серверного сокета в набор
        FD_SET(server_fd, &readfds);
        int max_sd = server_fd;
        
        // Добавление клиентских сокетов в набор для select()
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] > 0) {
                FD_SET(client_sockets[i], &readfds);
                if (client_sockets[i] > max_sd) {
                    max_sd = client_sockets[i];
                }
            }
        }
        pthread_mutex_unlock(&clients_mutex);
        
        // ============================================
        // ВЫЗОВ SELECT() - МУЛЬТИПЛЕКСИРОВАНИЕ ВВОДА/ВЫВОДА
        // ============================================
        // select(nfds, readfds, writefds, exceptfds, timeout)
        // nfds - максимальный дескриптор + 1
        // readfds - набор дескрипторов для чтения
        // timeout - таймаут (NULL - блокирующий, {0,0} - неблокирующий)
        struct timeval tv;
        tv.tv_sec = 1;   // Таймаут 1 секунда (неблокирующий режим)
        tv.tv_usec = 0;
        
        int activity = select(max_sd + 1, &readfds, NULL, NULL, &tv);
        
        if (activity < 0 && errno != EINTR) {
            perror("select error");
        }
        
        // ============================================
        // ПРОВЕРКА НОВЫХ ПОДКЛЮЧЕНИЙ
        // ============================================
        if (FD_ISSET(server_fd, &readfds)) {
            // accept() принимает входящее соединение
            // Возвращает новый сокет для общения с клиентом
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                if (errno != EWOULDBLOCK && errno != EAGAIN) {
                    perror("accept failed");
                }
            } else {
                printf("Новое подключение: сокет %d, IP %s, порт %d\n",
                       new_socket, inet_ntoa(address.sin_addr), ntohs(address.sin_port));
                
                pthread_mutex_lock(&clients_mutex);
                // Проверка лимита клиентов
                if (num_clients >= MAX_CLIENTS) {
                    char msg[] = "Сервер перегружен. Максимум 3 клиента.\n";
                    send(new_socket, msg, strlen(msg), 0);
                    close(new_socket);
                    printf("Отклонено подключение: достигнут лимит клиентов\n");
                } else {
                    // Добавление в массив клиентов
                    for (int i = 0; i < MAX_CLIENTS; i++) {
                        if (client_sockets[i] == 0) {
                            client_sockets[i] = new_socket;
                            num_clients++;
                            break;
                        }
                    }
                    
                    // Создание потока для обработки клиента
                    int* sock_ptr = malloc(sizeof(int));
                    *sock_ptr = new_socket;
                    pthread_t thread_id;
                    pthread_create(&thread_id, NULL, handle_client, sock_ptr);
                    pthread_detach(thread_id);  // Отсоединяем поток (автоочистка)
                }
                pthread_mutex_unlock(&clients_mutex);
            }
        }
        
        // ============================================
        // ПРОВЕРКА ОТКЛЮЧЕНИЙ КЛИЕНТОВ
        // ============================================
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] > 0 && FD_ISSET(client_sockets[i], &readfds)) {
                char buffer[BUFFER_SIZE];
                // MSG_PEEK - просмотр данных без удаления из буфера
                int valread = recv(client_sockets[i], buffer, BUFFER_SIZE, MSG_PEEK);
                if (valread == 0) {
                    // Клиент закрыл соединение
                    close(client_sockets[i]);
                    client_sockets[i] = 0;
                    num_clients--;
                    printf("Клиент отключился. Активных: %d\n", num_clients);
                }
            }
        }
        pthread_mutex_unlock(&clients_mutex);
        
        // ============================================
        // ВЫВОД СТАТУСА (неблокирующий режим)
        // ============================================
        static time_t last_status = 0;
        time_t now = time(NULL);
        if (now - last_status >= 5) {
            printf("Статус: ожидание подключений... (активных клиентов: %d/%d)\n", 
                   num_clients, MAX_CLIENTS);
            last_status = now;
        }
    }
    
    // Очистка ресурсов (теоретически недостижимый код в данном цикле)
    close(server_fd);
    pthread_mutex_destroy(&b.dataMutex);
    pthread_mutex_destroy(&b.condMutex);
    pthread_cond_destroy(&b.cond);
    pthread_mutex_destroy(&clients_mutex);
    
    return 0;
}
