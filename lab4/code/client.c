#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

// ============================================
// НАСТРОЙКИ ПОДКЛЮЧЕНИЯ К СЕРВЕРУ
// ============================================
#define PORT 5050                   // Порт сервера (должен совпадать с сервером)
#define BUFFER_SIZE 1024            // Размер буфера для приема данных
#define SERVER_IP "127.0.0.1"       // IP-адрес сервера (по умолчанию localhost)

// ============================================
// ЦВЕТОВЫЕ КОДЫ ДЛЯ КОНСОЛИ
// ============================================
#define COLOR_CYAN    "\033[36m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_RESET   "\033[0m"

/**
 * @brief Функция для установки TCP-соединения с сервером
 * @param ip_address - IP-адрес сервера в строковом формате
 * @param port - номер порта сервера
 * @return дескриптор сокета при успехе, -1 при ошибке
 * 
 * Алгоритм:
 * 1. Создание TCP-сокета (socket)
 * 2. Настройка структуры адреса сервера
 * 3. Подключение к серверу (connect)
 */
int connect_to_server(const char* ip_address, int port) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    // ============================================
    // ШАГ 1: Создание TCP-сокета
    // ============================================
    // socket(domain, type, protocol)
    // AF_INET - IPv4 интернет-протокол
    // SOCK_STREAM - потоковый сокет (TCP)
    // 0 - протокол по умолчанию
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Ошибка создания сокета");
        return -1;
    }
    
    // ============================================
    // ШАГ 2: Настройка адреса сервера
    // ============================================
    // Обнуление структуры адреса
    memset(&serv_addr, 0, sizeof(serv_addr));
    
    serv_addr.sin_family = AF_INET;          // Семейство адресов IPv4
    serv_addr.sin_port = htons(port);        // Порт (htons - перевод в сетевой порядок байт)
    
    // Преобразование IP-адреса из строки в бинарный формат
    // inet_pton преобразует "192.168.1.1" -> 32-битный адрес
    if (inet_pton(AF_INET, ip_address, &serv_addr.sin_addr) <= 0) {
        perror("Неверный IP-адрес");
        close(sock);
        return -1;
    }
    
    // ============================================
    // ШАГ 3: Подключение к серверу
    // ============================================
    // connect(sockfd, address, address_len)
    // Устанавливает соединение с сервером
    // Если сервер не доступен - возврат с ошибкой
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Ошибка подключения к серверу");
        close(sock);
        return -1;
    }
    
    return sock;
}

/**
 * @brief Функция для отправки данных серверу
 * @param sock - дескриптор сокета
 * @param message - сообщение для отправки
 * @return 0 при успехе, -1 при ошибке
 */
int send_message(int sock, const char* message) {
    // send(sockfd, buffer, length, flags)
    // Отправляет данные через TCP-сокет
    if (send(sock, message, strlen(message), 0) < 0) {
        perror("Ошибка отправки");
        return -1;
    }
    return 0;
}

/**
 * @brief Функция для получения данных от сервера
 * @param sock - дескриптор сокета
 * @param buffer - буфер для полученных данных
 * @param buffer_size - размер буфера
 * @return количество принятых байт
 */
int receive_message(int sock, char* buffer, int buffer_size) {
    // Обнуление буфера
    memset(buffer, 0, buffer_size);
    
    // recv(sockfd, buffer, length, flags)
    // Принимает данные из сокета
    // MSG_NOSIGNAL - игнорировать сигнал SIGPIPE при разрыве соединения
    int bytes_read = recv(sock, buffer, buffer_size - 1, MSG_NOSIGNAL);
    
    if (bytes_read < 0) {
        perror("Ошибка получения данных");
        return -1;
    } else if (bytes_read == 0) {
        printf("Сервер закрыл соединение\n");
        return 0;
    }
    
    buffer[bytes_read] = '\0';  // Добавление завершающего нуля
    return bytes_read;
}

/**
 * @brief Интерактивный цикл клиента
 * @param sock - дескриптор сокета
 * 
 * Реализует логику:
 * 1. Ожидает сигнал READY от сервера
 * 2. Получает команду от пользователя (число студентов)
 * 3. Отправляет команду серверу
 * 4. Получает и выводит ответы от сервера
 */
void client_loop(int sock) {
    char buffer[BUFFER_SIZE];
    char command[BUFFER_SIZE];
    int running = 1;
    
    printf(COLOR_GREEN"Подключено к серверу. Ожидание команды...\n"COLOR_RESET);
    
    while (running) {
        // Очистка буферов
        memset(buffer, 0, BUFFER_SIZE);
        memset(command, 0, BUFFER_SIZE);
        
        // ============================================
        // ПОЛУЧЕНИЕ ДАННЫХ ОТ СЕРВЕРА
        // ============================================
        int bytes = receive_message(sock, buffer, BUFFER_SIZE);
        
        if (bytes <= 0) {
            // Соединение закрыто или ошибка
            break;
        }
        
        // Вывод полученного сообщения от сервера
        printf("%s", buffer);
        
        // ============================================
        // ПРОВЕРКА НА СИГНАЛ ГОТОВНОСТИ
        // ============================================
        // Если сервер прислал "SERVER_READY" - запрашиваем данные у пользователя
        if (strstr(buffer, "SERVER_READY") != NULL) {
            printf(COLOR_CYAN"\nВведите количество студентов (1-100): "COLOR_RESET);
            
            // Чтение команды из консоли
            if (fgets(command, BUFFER_SIZE, stdin) != NULL) {
                // Удаление символа новой строки
                command[strcspn(command, "\n")] = 0;
                
                // Проверка на команду выхода
                if (strcmp(command, "exit") == 0 || strcmp(command, "quit") == 0) {
                    printf("Отключение от сервера...\n");
                    running = 0;
                } else {
                    // ============================================
                    // ОТПРАВКА КОМАНДЫ СЕРВЕРУ
                    // ============================================
                    if (send_message(sock, command) < 0) {
                        break;
                    }
                }
            }
        }
        
        // Проверка на завершение симуляции
        if (strstr(buffer, "Симуляция завершена") != NULL ||
            strstr(buffer, "Отключение") != NULL) {
            printf(COLOR_YELLOW"\nСеанс завершен. Можно подключиться снова.\n"COLOR_RESET);
            running = 0;
        }
        
        // Проверка на ошибку
        if (strstr(buffer, "Ошибка") != NULL) {
            printf(COLOR_YELLOW"\nПолучена ошибка. Попробуйте снова.\n"COLOR_RESET);
        }
    }
}

/**
 * @brief Вывод справки по использованию
 */
void print_help() {
    printf(COLOR_CYAN"\n=== Клиент лабораторной работы №4 ===\n"COLOR_RESET);
    printf("Использование: ./client [IP_адрес]\n");
    printf("Примеры:\n");
    printf("  ./client              - подключиться к localhost:5050\n");
    printf("  ./client 192.168.1.1 - подключиться к указанному IP\n");
    printf("\nКоманды:\n");
    printf("  Число (1-100) - количество студентов для симуляции\n");
    printf("  exit/quit     - выход\n");
    printf(COLOR_CYAN"========================================\n\n"COLOR_RESET);
}

/**
 * @brief Главная функция клиента
 */
int main(int argc, char *argv[]) {
    int sock = 0;
    const char* server_ip = SERVER_IP;
    
    // ============================================
    // ОБРАБОТКА АРГУМЕНТОВ КОМАНДНОЙ СТРОКИ
    // ============================================
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_help();
            return 0;
        }
        server_ip = argv[1];  // Использовать указанный IP
    }
    
    print_help();
    
    // ============================================
    // ПОДКЛЮЧЕНИЕ К СЕРВЕРУ
    // ============================================
    printf(COLOR_CYAN"Подключение к %s:%d...\n"COLOR_RESET, server_ip, PORT);
    
    sock = connect_to_server(server_ip, PORT);
    
    if (sock < 0) {
        fprintf(stderr, "Не удалось подключиться к серверу\n");
        return 1;
    }
    
    printf(COLOR_GREEN"Успешное подключение!\n"COLOR_RESET);
    
    // ============================================
    // ЗАПУСК ИНТЕРАКТИВНОГО ЦИКЛА
    // ============================================
    client_loop(sock);
    
    // ============================================
    // ЗАКРЫТИЕ СОЕДИНЕНИЯ
    // ============================================
    close(sock);
    printf("Соединение закрыто.\n");
    
    return 0;
}
