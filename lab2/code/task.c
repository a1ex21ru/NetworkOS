/**
 * Лабораторная работа №2
 * Многопроцессная программа "Ванная комната в общежитии"
 * 
 * Использует:
 * - fork() для создания процессов-студентов
 * - shm_open() / mmap() для разделяемой памяти
 * - pthread_mutex_t (мьютекс) с атрибутом PTHREAD_PROCESS_SHARED
 * - pthread_cond_t (условная переменная) для синхронизации ожидания
 * 
 * Мьютекс и условная переменная размещены в разделяемой памяти,
 * что позволяет использовать их для межпроцессной синхронизации.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>

// Имя разделяемой памяти
#define SHM_NAME "/bathroom_shm"

// Константы времени (в микросекундах)
#define MIN_SHOWER_TIME 1000000   // 1 секунда
#define MAX_SHOWER_TIME 4000000   // 4 секунды

/**
 * Статус ванной комнаты
 */
typedef enum {
    EMPTY,      // Ванная пуста
    MALE,       // В ванной мужчины
    FEMALE      // В ванной женщины
} BathroomStatus;

/**
 * Структура разделяемой памяти ванной комнаты
 */
typedef struct {
    // Синхронизация (с атрибутом PTHREAD_PROCESS_SHARED для межпроцессного использования)
    pthread_mutex_t mutex;          // Мьютекс для защиты данных
    pthread_cond_t  can_enter;      // Условная переменная для ожидания входа
    
    // Состояние ванной
    unsigned int total_cabins;      // Всего кабинок
    unsigned int occupied_cabins;   // Занятых кабинок
    BathroomStatus current_status;  // Текущий статус (кто в ванной)
    BathroomStatus last_gender;     // Пол последнего вышедшего
    
    // Механизм предотвращения голодания
    unsigned int consecutive_entries;      // Последовательных входов одного пола
    unsigned int max_consecutive_entries;  // Лимит входов
    bool forced_switch_pending;            // Флаг принудительной смены
    
    // Счётчики ожидающих
    unsigned int waiting_males;
    unsigned int waiting_females;
    
    // Статистика
    unsigned int entered_count;      // Сколько вошло
    unsigned int male_entered;       // Мужчин вошло
    unsigned int female_entered;     // Женщин вошло
    double total_wait_time;          // Суммарное время ожидания
    
    // Флаг завершения симуляции
    volatile bool simulation_running;
    
    // Время начала симуляции
    time_t start_time;
} SharedBathroom;

// Указатель на разделяемую память
static SharedBathroom* bathroom = NULL;

/**
 * Вывод разделителя
 */
void printSeparator(int count) {
    for (int i = 0; i < count; i++) {
        printf("=");
    }
    printf("\n");
}

/**
 * Получить текущее время в секундах от начала симуляции
 */
int getElapsedTime(void) {
    return (int)(time(NULL) - bathroom->start_time);
}

/**
 * Проверка возможности входа
 */
static bool isEntryAllowed(BathroomStatus gender) 
{
    // 1. Все кабинки заняты
    if (bathroom->occupied_cabins >= bathroom->total_cabins) {
        return false;
    }
    
    // 2. Ванная пуста — применяем механизм справедливости
    if (bathroom->current_status == EMPTY) {
        // Принудительная смена пола
        if (bathroom->forced_switch_pending) {
            if (gender == bathroom->last_gender) {
                return false; // Тот же пол — не пускаем
            }
            return true; // Противоположный пол — пускаем
        }
        
        // Оба пола ждут — приоритет большинству
        if (bathroom->waiting_males > 0 && bathroom->waiting_females > 0) {
            if (bathroom->waiting_females > bathroom->waiting_males) {
                return (gender == FEMALE);
            } else if (bathroom->waiting_males > bathroom->waiting_females) {
                return (gender == MALE);
            } else {
                // Равное количество — чередуем
                if (bathroom->last_gender == MALE) {
                    return (gender == FEMALE);
                } else if (bathroom->last_gender == FEMALE) {
                    return (gender == MALE);
                }
                return true;
            }
        }
        
        // Только один пол ждёт — даём ему приоритет
        if (gender == MALE && bathroom->waiting_females > 0) {
            return false;
        }
        if (gender == FEMALE && bathroom->waiting_males > 0) {
            return false;
        }
        
        return true;
    }
    
    // 3. Ванная занята — проверяем пол
    if (bathroom->current_status == gender) {
        // Тот же пол
        if (bathroom->forced_switch_pending) {
            return false; // Принудительная смена — блокируем
        }
        return true;
    }
    
    // 4. Противоположный пол — вход запрещён
    return false;
}

/**
 * Вход студента в ванную
 */
bool enterBathroom(int student_id, BathroomStatus gender, double* wait_time)
{
    time_t wait_start = time(NULL);
    
    pthread_mutex_lock(&bathroom->mutex);
    
    if (!bathroom->simulation_running) {
        pthread_mutex_unlock(&bathroom->mutex);
        return false;
    }
    
    // Увеличиваем счётчик ожидающих
    if (gender == MALE) bathroom->waiting_males++;
    else bathroom->waiting_females++;
    
    // Ожидаем возможности входа (pthread_cond_wait автоматически освобождает и захватывает mutex)
    while (!isEntryAllowed(gender) && bathroom->simulation_running) {
        pthread_cond_wait(&bathroom->can_enter, &bathroom->mutex);
    }
    
    // Уменьшаем счётчик ожидающих
    if (gender == MALE) bathroom->waiting_males--;
    else bathroom->waiting_females--;
    
    if (!bathroom->simulation_running) {
        // Будим всех ожидающих
        pthread_cond_broadcast(&bathroom->can_enter);
        pthread_mutex_unlock(&bathroom->mutex);
        return false;
    }
    
    // Вычисляем время ожидания
    *wait_time = difftime(time(NULL), wait_start);
    
    // Обновляем состояние ванной
    if (bathroom->current_status == EMPTY) {
        if (gender != bathroom->last_gender || bathroom->last_gender == EMPTY) {
            bathroom->consecutive_entries = 0;
            bathroom->forced_switch_pending = false;
        }
        bathroom->current_status = gender;
    }
    
    bathroom->consecutive_entries++;
    bathroom->occupied_cabins++;
    
    // Проверяем лимит входов
    bool opposite_waiting = (gender == MALE && bathroom->waiting_females > 0) ||
                            (gender == FEMALE && bathroom->waiting_males > 0);
    
    if (bathroom->consecutive_entries >= bathroom->max_consecutive_entries && opposite_waiting) {
        bathroom->forced_switch_pending = true;
    }
    
    // Обновляем статистику
    bathroom->entered_count++;
    bathroom->total_wait_time += *wait_time;
    if (gender == MALE) bathroom->male_entered++;
    else bathroom->female_entered++;
    
    // Вывод
    const char* gender_str = (gender == MALE) ? "M" : "F";
    printf("[ВХОД %3d] Студент %2d (%s) [PID:%d] вошёл. Занято: %u/%u | Входы: %u/%u | Ожидают: M=%u F=%u%s\n",
           getElapsedTime(),
           student_id,
           gender_str,
           getpid(),
           bathroom->occupied_cabins,
           bathroom->total_cabins,
           bathroom->consecutive_entries,
           bathroom->max_consecutive_entries,
           bathroom->waiting_males,
           bathroom->waiting_females,
           bathroom->forced_switch_pending ? " | СМЕНА!" : "");
    fflush(stdout);
    
    pthread_mutex_unlock(&bathroom->mutex);
    return true;
}

/**
 * Выход студента из ванной
 */
void exitBathroom(int student_id, BathroomStatus gender)
{
    pthread_mutex_lock(&bathroom->mutex);
    
    bathroom->occupied_cabins--;
    bathroom->last_gender = gender;
    
    if (bathroom->occupied_cabins == 0) {
        bathroom->current_status = EMPTY;
    }
    
    const char* gender_str = (gender == MALE) ? "M" : "F";
    printf("[ВЫХОД%3d] Студент %2d (%s) [PID:%d] вышел. Занято: %u/%u | Принуд.смена: %s\n",
           getElapsedTime(),
           student_id,
           gender_str,
           getpid(),
           bathroom->occupied_cabins,
           bathroom->total_cabins,
           bathroom->forced_switch_pending ? "ДА" : "нет");
    fflush(stdout);
    
    // Будим ВСЕХ ожидающих (broadcast гарантирует, что никто не пропустит сигнал)
    pthread_cond_broadcast(&bathroom->can_enter);
    
    pthread_mutex_unlock(&bathroom->mutex);
}

/**
 * Процесс студента
 */
void studentProcess(int student_id, BathroomStatus gender, useconds_t shower_time)
{
    double wait_time = 0;
    
    // Пытаемся войти в ванную
    if (enterBathroom(student_id, gender, &wait_time)) {
        // Принимаем душ
        usleep(shower_time);
        
        // Выходим из ванной
        exitBathroom(student_id, gender);
    }
    
    // Завершаем дочерний процесс
    exit(0);
}

/**
 * Генерация случайного времени душа
 */
useconds_t generateShowerTime(void) {
    return MIN_SHOWER_TIME + (rand() % (MAX_SHOWER_TIME - MIN_SHOWER_TIME));
}

/**
 * Запуск симуляции
 */
void runSimulation(
    int student_count,
    double male_ratio,
    unsigned int bathroom_capacity,
    unsigned int max_consecutive_entries,
    int simulation_time_sec
) {
    printf("\n");
    printSeparator(70);
    printf(" СИМУЛЯЦИЯ ВАННОЙ КОМНАТЫ (многопроцессная версия)\n");
    printSeparator(70);
    printf(" Студентов: %d (мужчин: ~%.0f%%, женщин: ~%.0f%%)\n", 
           student_count, male_ratio * 100, (1 - male_ratio) * 100);
    printf(" Вместимость ванной: %u кабинок\n", bathroom_capacity);
    printf(" Макс. последовательных входов: %u\n", max_consecutive_entries);
    printf(" Время душа: случайно 1.0–4.0 сек\n");
    printf(" Главный процесс PID: %d\n", getpid());
    printSeparator(70);
    printf("\n");
    fflush(stdout);
    
    // Создаём/открываем разделяемую память
    shm_unlink(SHM_NAME); // Удаляем старую, если есть
    
    int fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0666);
    if (fd == -1) {
        perror("shm_open");
        exit(1);
    }
    
    // Устанавливаем размер
    if (ftruncate(fd, sizeof(SharedBathroom)) == -1) {
        perror("ftruncate");
        exit(1);
    }
    
    // Отображаем в память
    bathroom = (SharedBathroom*)mmap(NULL, sizeof(SharedBathroom),
                                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (bathroom == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    
    close(fd);
    
    // Инициализируем разделяемую память
    memset(bathroom, 0, sizeof(SharedBathroom));
    
    // Инициализируем mutex с атрибутом PTHREAD_PROCESS_SHARED
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    if (pthread_mutex_init(&bathroom->mutex, &mutex_attr) != 0) {
        perror("pthread_mutex_init");
        exit(1);
    }
    pthread_mutexattr_destroy(&mutex_attr);
    
    // Инициализируем condition variable с атрибутом PTHREAD_PROCESS_SHARED
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    if (pthread_cond_init(&bathroom->can_enter, &cond_attr) != 0) {
        perror("pthread_cond_init");
        exit(1);
    }
    pthread_condattr_destroy(&cond_attr);
    
    // Инициализируем состояние ванной
    bathroom->total_cabins = bathroom_capacity;
    bathroom->occupied_cabins = 0;
    bathroom->current_status = EMPTY;
    bathroom->last_gender = EMPTY;
    bathroom->consecutive_entries = 0;
    bathroom->max_consecutive_entries = max_consecutive_entries;
    bathroom->forced_switch_pending = false;
    bathroom->waiting_males = 0;
    bathroom->waiting_females = 0;
    bathroom->entered_count = 0;
    bathroom->male_entered = 0;
    bathroom->female_entered = 0;
    bathroom->total_wait_time = 0;
    bathroom->simulation_running = true;
    bathroom->start_time = time(NULL);
    
    // Массив PID дочерних процессов
    pid_t* child_pids = (pid_t*)malloc(student_count * sizeof(pid_t));
    
    // Инициализируем генератор случайных чисел
    srand((unsigned int)time(NULL) ^ getpid());
    
    // Создаём дочерние процессы для каждого студента
    for (int i = 0; i < student_count; i++) {
        // Определяем пол студента
        BathroomStatus gender = ((rand() / (double)RAND_MAX) < male_ratio) ? MALE : FEMALE;
        useconds_t shower_time = generateShowerTime();
        
        pid_t pid = fork();
        
        if (pid == -1) {
            perror("fork");
            exit(1);
        }
        
        if (pid == 0) {
            // Дочерний процесс
            srand((unsigned int)time(NULL) ^ getpid()); // Пересеиваем генератор
            studentProcess(i + 1, gender, shower_time);
            // Сюда не дойдёт — studentProcess вызывает exit()
        }
        
        // Родительский процесс
        child_pids[i] = pid;
    }
    
    // Ждём завершения симуляции или таймаута
    sleep(simulation_time_sec);
    
    // Сигнализируем о завершении
    pthread_mutex_lock(&bathroom->mutex);
    bathroom->simulation_running = false;
    // Будим ВСЕ ожидающие процессы
    pthread_cond_broadcast(&bathroom->can_enter);
    pthread_mutex_unlock(&bathroom->mutex);
    
    // Дополнительно будим несколько раз для надёжности
    for (int i = 0; i < 3; i++) {
        usleep(100000);
        pthread_mutex_lock(&bathroom->mutex);
        pthread_cond_broadcast(&bathroom->can_enter);
        pthread_mutex_unlock(&bathroom->mutex);
    }
    
    // Ждём завершения всех дочерних процессов
    for (int i = 0; i < student_count; i++) {
        int status;
        waitpid(child_pids[i], &status, 0);
    }
    
    // Вывод результатов
    printf("\n");
    printSeparator(70);
    printf(" РЕЗУЛЬТАТЫ СИМУЛЯЦИИ\n");
    printSeparator(70);
    printf(" Успешно вошли: %u из %d студентов (%.1f%%)\n",
           bathroom->entered_count, student_count,
           (double)bathroom->entered_count / student_count * 100);
    printf("   - Мужчины: %u\n", bathroom->male_entered);
    printf("   - Женщины: %u\n", bathroom->female_entered);
    
    if (bathroom->entered_count > 0) {
        printf(" Среднее время ожидания: %.2f сек\n",
               bathroom->total_wait_time / bathroom->entered_count);
    }
    
    printf(" Последовательных входов (макс.): %u\n", bathroom->max_consecutive_entries);
    printSeparator(70);
    printf("\n");
    fflush(stdout);
    
    // Очистка
    free(child_pids);
    pthread_mutex_destroy(&bathroom->mutex);
    pthread_cond_destroy(&bathroom->can_enter);
    munmap(bathroom, sizeof(SharedBathroom));
    shm_unlink(SHM_NAME);
}

int main(int argc, char* argv[])
{
    // Отключаем буферизацию вывода
    setbuf(stdout, 0);
    
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║  ЛАБОРАТОРНАЯ РАБОТА №2                                          ║\n");
    printf("║  Многопроцессная программа с разделяемой памятью                 ║\n");
    printf("║  Задача: Ванная комната в общежитии                              ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    
    // Параметры по умолчанию или из командной строки
    int student_count = 20;
    int bathroom_capacity = 3;
    
    if (argc >= 3) {
        student_count = atoi(argv[1]);
        bathroom_capacity = atoi(argv[2]);
        printf("\nПараметры из командной строки:\n");
        printf("  Студентов: %d\n", student_count);
        printf("  Вместимость: %d\n", bathroom_capacity);
    }
    
    // Сценарий 1: Равное количество полов
    runSimulation(student_count, 0.5, bathroom_capacity, 5, 25);
    
    // Сценарий 2: Преимущественно мужчины
    runSimulation(25, 0.8, 4, 6, 40);
    
    // Сценарий 3: Преимущественно женщины
    runSimulation(25, 0.2, 4, 6, 40);
    
    // Сценарий 4: Большая вместимость
    runSimulation(30, 0.5, 6, 8, 45);
    
    printf("Все симуляции завершены.\n");
    printf("Использованы: fork(), shm_open(), mmap(), pthread_mutex_t, pthread_cond_t\n");
    printf("Синхронизация: мьютекс + условная переменная в разделяемой памяти (PTHREAD_PROCESS_SHARED)\n");
    
    return 0;
}

