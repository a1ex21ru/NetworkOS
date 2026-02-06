#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

/**
 * Перечисление для определения текущего статуса ванной комнаты
 */
typedef enum {
    EMPTY,      // Ванная комната свободна
    MALE,       // В ванной находятся только мужчины
    FEMALE      // В ванной находятся только женщины
} BathroomStatus;

/**
 * Структура для сбора статистики по студенту
 */
typedef struct {
    BathroomStatus gender;
    double shower_duration;
    double wait_start_time;   // Время начала ожидания
    double wait_end_time;     // Время окончания ожидания (входа)
    bool   entered;           // Успешно ли вошёл
    int    student_id;
} Student;

/**
 * Структура ванной комнаты с механизмом предотвращения голодания
 */
typedef struct {
    pthread_mutex_t access_lock;
    pthread_cond_t  availability_signal;
    
    unsigned int total_cabins;         // Общее количество кабинок
    unsigned int occupied_cabins;      // Занятые кабинки
    
    BathroomStatus current_status;     // Текущий статус (пол в ванной)
    BathroomStatus last_exiting_gender; // Пол последнего вышедшего (для справедливости)
    
    // === МЕХАНИЗМ ПРЕДОТВРАЩЕНИЯ ГОЛОДАНИЯ ===
    unsigned int consecutive_entries;   // Количество последовательных ВХОДОВ одного пола
    unsigned int max_consecutive_entries; // Максимум последовательных входов (лимит)
    bool         forced_switch_pending;  // Флаг: следующим ДОЛЖЕН зайти противоположный пол
    
    // Счётчики ожидающих для отладки
    unsigned int waiting_males;
    unsigned int waiting_females;
    
    // Статистика использования
    double total_occupancy_time;  // Общее время занятости ванной (для расчёта загрузки)
    struct timespec last_change_time; // Время последнего изменения состояния
} Bathroom;

// Глобальная ванная комната
static Bathroom shared_bathroom;

// Флаг завершения симуляции (для корректного завершения потоков)
static volatile bool simulation_running = true;

// Барьер для одновременного старта всех потоков ("стартовый пистолет")
static pthread_barrier_t start_barrier;

/**
 * Инициализация ванной комнаты
 */
void initBathroom(Bathroom* bathroom, unsigned int capacity, unsigned int max_entries) {
    pthread_mutex_init(&bathroom->access_lock, NULL);
    pthread_cond_init(&bathroom->availability_signal, NULL);
    
    bathroom->total_cabins = capacity;
    bathroom->occupied_cabins = 0;
    bathroom->current_status = EMPTY;
    bathroom->last_exiting_gender = EMPTY;
    
    bathroom->consecutive_entries = 0;
    bathroom->max_consecutive_entries = max_entries;
    bathroom->forced_switch_pending = false;
    
    bathroom->waiting_males = 0;
    bathroom->waiting_females = 0;
    
    bathroom->total_occupancy_time = 0.0;
    
    // Запоминаем время старта для расчёта загрузки
    clock_gettime(CLOCK_MONOTONIC, &bathroom->last_change_time);
}

/**
 * Освобождение ресурсов ванной
 */
void destroyBathroom(Bathroom* bathroom) {
    pthread_mutex_destroy(&bathroom->access_lock);
    pthread_cond_destroy(&bathroom->availability_signal);
}

/**
 * Выводит разделительную линию из повторяющихся символов
 */
void printSeparator(int count) {
    for (int i = 0; i < count; i++) {
        printf("=");
    }
    printf("\n");
}

/**
 * Проверка возможности входа с учётом механизма предотвращения голодания
 */
static bool isEntryAllowed(Bathroom* bathroom, Student* student) 
{
    // 1. Проверка на полную занятость кабинок
    if (bathroom->occupied_cabins >= bathroom->total_cabins) {
        return false;
    }
    
    // 2. Если ванная пуста — применяем механизм предотвращения голодания
    if (bathroom->current_status == EMPTY) {
        // Сценарий А: принудительная смена пола (квота исчерпана) — приоритет
        if (bathroom->forced_switch_pending) {
            if (student->gender == bathroom->last_exiting_gender) {
                return false; // Тот же пол, что и в прошлом раунде — не пускаем
            }
            // Противоположный пол — разрешаем вход (флаг сбросится после входа)
            return true;
        }
        
        // Сценарий Б: есть ожидающие обоих полов — даём приоритет тому, у кого больше ожидающих
        if (bathroom->waiting_males > 0 && bathroom->waiting_females > 0) {
            // Оба пола ждут — приоритет тому, у кого больше ожидающих
            if (bathroom->waiting_females > bathroom->waiting_males) {
                // Женщин больше — только женщины могут войти
                return (student->gender == FEMALE);
            } else if (bathroom->waiting_males > bathroom->waiting_females) {
                // Мужчин больше — только мужчины могут войти
                return (student->gender == MALE);
            } else {
                // Равное количество — чередуем (приоритет противоположному полу от последнего вышедшего)
                if (bathroom->last_exiting_gender == MALE) {
                    return (student->gender == FEMALE); // Последний вышел мужчина — приоритет женщинам
                } else if (bathroom->last_exiting_gender == FEMALE) {
                    return (student->gender == MALE); // Последняя вышла женщина — приоритет мужчинам
                }
                // Если last_exiting_gender == EMPTY (первый вход) — разрешаем любому
                return true;
            }
        }
        
        // Сценарий В: есть ожидающие только одного пола — даём им приоритет
        if (student->gender == MALE && bathroom->waiting_females > 0) {
            return false; // Женщины ждут — мужчин не пускаем
        }
        if (student->gender == FEMALE && bathroom->waiting_males > 0) {
            return false; // Мужчины ждут — женщин не пускаем
        }
        
        // Нет ожидающих или только ожидающие того же пола — вход разрешен
        return true;
    }
    
    // 3. Если ванная занята — проверяем совпадение пола
    // БАЗОВОЕ ПРАВИЛО: в ванной могут находиться только люди одного пола!
    // Противоположный пол НИКОГДА не может войти в занятую ванную.
    
    if (bathroom->current_status == student->gender) {
        // Тот же пол, что и в ванной
        
        // Проверка принудительной смены пола (квота последовательных раундов исчерпана)
        // Когда принудительная смена активна, блокируем вход того же пола
        // Ванная должна полностью освободиться, чтобы противоположный пол мог войти
        if (bathroom->forced_switch_pending) {
            return false; // Блокируем вход — ждем полного освобождения ванной
        }
        
        // Вход разрешен — тот же пол может входить, пока есть свободные кабинки
        // Справедливость обеспечивается в секции 2 (когда ванная пуста)
        return true;
    }
    
    // 4. Противоположный пол — вход ЗАПРЕЩЕН (базовое правило)
    // Противоположный пол может войти только когда ванная полностью пуста (секция 2)
    return false;
}

/**
 * Вход студента в ванную комнату
 */
bool enterBathroom(Bathroom* bathroom, Student* student)
{
    pthread_mutex_lock(&bathroom->access_lock);
    
    // Проверка: симуляция ещё идёт?
    if (!simulation_running) {
        pthread_mutex_unlock(&bathroom->access_lock);
        return false;
    }
    
    // Фиксируем время начала ожидания
    student->wait_start_time = (double)clock() / CLOCKS_PER_SEC;
    
    // Увеличиваем счётчик ожидающих
    if (student->gender == MALE) bathroom->waiting_males++;
    else bathroom->waiting_females++;
    
    // Ожидаем возможности входа (защита от ложных пробуждений)
    while (!isEntryAllowed(bathroom, student) && simulation_running) {
        pthread_cond_wait(&bathroom->availability_signal, &bathroom->access_lock);
    }
    
    // Уменьшаем счётчик ожидающих
    if (student->gender == MALE) bathroom->waiting_males--;
    else bathroom->waiting_females--;
    
    // Если симуляция завершена — выходим без входа в ванную
    if (!simulation_running) {
        pthread_mutex_unlock(&bathroom->access_lock);
        return false;
    }
    
    // Фиксируем время окончания ожидания
    student->wait_end_time = (double)clock() / CLOCKS_PER_SEC;
    student->entered = true;
    
    // === КЛЮЧЕВАЯ ЛОГИКА ПРЕДОТВРАЩЕНИЯ ГОЛОДАНИЯ ===
    
    // Если ванная была пуста — устанавливаем статус и проверяем смену пола
    if (bathroom->current_status == EMPTY) {
        // Если пол сменился (или это первый вход) — сбрасываем счетчик
        if (student->gender != bathroom->last_exiting_gender || bathroom->last_exiting_gender == EMPTY) {
            bathroom->consecutive_entries = 0;
            bathroom->forced_switch_pending = false; // Сбрасываем флаг принудительной смены
        }
        bathroom->current_status = student->gender;
    }
    
    // Увеличиваем счетчик входов текущего пола
    bathroom->consecutive_entries++;
    
    // Проверяем, есть ли ожидающие противоположного пола
    bool opposite_waiting = (student->gender == MALE && bathroom->waiting_females > 0) ||
                            (student->gender == FEMALE && bathroom->waiting_males > 0);
    
    // Если достигли лимита входов и есть ожидающие противоположного пола — активируем принудительную смену
    if (bathroom->consecutive_entries >= bathroom->max_consecutive_entries && opposite_waiting) {
        bathroom->forced_switch_pending = true;
    }
    
    // Обновляем время для расчёта загрузки
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    // Если кабинки были заняты — добавляем накопленное время (кабино-секунды)
    if (bathroom->occupied_cabins > 0) {
        double elapsed = (now.tv_sec - bathroom->last_change_time.tv_sec) +
                         (now.tv_nsec - bathroom->last_change_time.tv_nsec) / 1e9;
        bathroom->total_occupancy_time += elapsed * bathroom->occupied_cabins;
    }
    bathroom->last_change_time = now;
    
    // Занимаем кабинку
    bathroom->occupied_cabins++;
    
    // Отладочный вывод
    const char* gender_str = (student->gender == MALE) ? "M" : "F";
    printf("[ВХОД %3d] Студент %2d (%s) вошёл. Занято: %u/%u | Входы: %u/%u | Ожидают: M=%u F=%u%s\n",
           (int)(student->wait_end_time * 1000),
           student->student_id + 1,
           gender_str,
           bathroom->occupied_cabins,
           bathroom->total_cabins,
           bathroom->consecutive_entries,
           bathroom->max_consecutive_entries,
           bathroom->waiting_males,
           bathroom->waiting_females,
           bathroom->forced_switch_pending ? " | СМЕНА!" : "");
    
    pthread_mutex_unlock(&bathroom->access_lock);
    return true;
}

/**
 * Выход студента из ванной комнаты
 */
void exitBathroom(Bathroom* bathroom, Student* student) 
{
    pthread_mutex_lock(&bathroom->access_lock);
    
    // Обновляем время для расчёта загрузки
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    
    // Добавляем время занятости к общей статистике (учитываем количество занятых кабинок)
    // total_occupancy_time хранит "кабино-секунды" — сумма (время * количество занятых кабинок)
    double elapsed = (now.tv_sec - bathroom->last_change_time.tv_sec) +
                     (now.tv_nsec - bathroom->last_change_time.tv_nsec) / 1e9;
    bathroom->total_occupancy_time += elapsed * bathroom->occupied_cabins;
    bathroom->last_change_time = now;
    
    // Освобождаем кабинку
    bathroom->occupied_cabins--;
    
    // Запоминаем пол выходящего студента
    bathroom->last_exiting_gender = student->gender;
    
    // Если все кабинки освободились — ванная становится пустой
    if (bathroom->occupied_cabins == 0) {
        bathroom->current_status = EMPTY;
    }
    
    // Отладочный вывод
    const char* gender_str = (student->gender == MALE) ? "M" : "F";
    printf("[ВЫХОД%3d] Студент %2d (%s) вышел. Занято: %u/%u | Принуд.смена: %s\n",
           (int)(clock() / (CLOCKS_PER_SEC / 1000.0)),
           student->student_id + 1,
           gender_str,
           bathroom->occupied_cabins,
           bathroom->total_cabins,
           bathroom->forced_switch_pending ? "ДА" : "нет");
    
    // Пробуждаем все ожидающие потоки
    pthread_cond_broadcast(&bathroom->availability_signal);
    
    pthread_mutex_unlock(&bathroom->access_lock);
}

/**
 * Функция потока студента
 */
void* studentThreadFunction(void* arg) 
{
    Student* student = (Student*)arg;
    
    // Ждём на барьере, пока все потоки не будут готовы
    // (все стартуют одновременно по "стартовому пистолету")
    pthread_barrier_wait(&start_barrier);
    
    // Пытаемся войти в ванную
    if (enterBathroom(&shared_bathroom, student)) {
        // Имитация принятия душа
        usleep((useconds_t)(student->shower_duration * 1000000));
        
        // Выходим из ванной
        exitBathroom(&shared_bathroom, student);
    }
    
    return NULL;
}

/**
 * Генерация случайного времени в диапазоне [min, max] секунд
 */
double generateRandomShowerTime(double min, double max) 
{
    return min + (rand() / (double)RAND_MAX) * (max - min);
}

/**
 * Запуск симуляции с заданными параметрами
 */
void runSimulation(
    int student_count,
    double male_ratio,
    unsigned int bathroom_capacity,
    unsigned int max_consecutive_entries,
    double simulation_time_sec
) {
    printf("\n");
    printSeparator(70);
    printf(" СИМУЛЯЦИЯ ВАННОЙ КОМНАТЫ\n");
    printSeparator(70);
    printf(" Студентов: %d (мужчин: ~%.0f%%, женщин: ~%.0f%%)\n", 
           student_count, male_ratio * 100, (1 - male_ratio) * 100);
    printf(" Вместимость ванной: %u кабинок\n", bathroom_capacity);
    printf(" Макс. последовательных входов: %u\n", max_consecutive_entries);
    printf(" Время душа: случайно 1.0–4.0 сек\n");
    printSeparator(70);
    printf("\n");
    
    // Инициализация ванной
    initBathroom(&shared_bathroom, bathroom_capacity, max_consecutive_entries);
    
    // Создание студентов
    Student* students = (Student*)malloc(student_count * sizeof(Student));
    pthread_t* threads = (pthread_t*)malloc(student_count * sizeof(pthread_t));
    
    srand((unsigned int)time(NULL));
    
    for (int i = 0; i < student_count; i++) {
        students[i].gender = (rand() / (double)RAND_MAX < male_ratio) ? MALE : FEMALE;
        students[i].shower_duration = generateRandomShowerTime(1.0, 4.0);
        students[i].student_id = i;
        students[i].entered = false;
    }
    
    // Устанавливаем флаг: симуляция запущена
    simulation_running = true;
    
    // Инициализация барьера: все потоки + main должны достичь барьера
    // Это гарантирует, что все потоки стартуют ОДНОВРЕМЕННО
    pthread_barrier_init(&start_barrier, NULL, student_count + 1);
    
    // Создание потоков студентов (каждый ждёт на барьере)
    for (int i = 0; i < student_count; i++) {
        pthread_create(&threads[i], NULL, studentThreadFunction, &students[i]);
    }
    
    // Все потоки созданы и ждут на барьере — даём "стартовый пистолет"!
    pthread_barrier_wait(&start_barrier);
    
    // Уничтожаем барьер (он больше не нужен)
    pthread_barrier_destroy(&start_barrier);
    
    // Ожидание завершения симуляции
    sleep((unsigned int)simulation_time_sec);
    
    // Даём потокам время завершить текущие операции
    usleep(500000); // 0.5 секунды
    
    // Сигнализируем о завершении симуляции
    simulation_running = false;
    
    // Пробуждаем все ожидающие потоки несколько раз для надёжности
    for (int i = 0; i < 3; i++) {
        pthread_mutex_lock(&shared_bathroom.access_lock);
        pthread_cond_broadcast(&shared_bathroom.availability_signal);
        pthread_mutex_unlock(&shared_bathroom.access_lock);
        usleep(100000); // 0.1 секунды между попытками
    }
    
    // Ожидание завершения потоков
    for (int i = 0; i < student_count; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Сбор статистики
    double total_wait_time = 0.0;
    int entered_count = 0;
    int male_entered = 0, female_entered = 0;
    
    for (int i = 0; i < student_count; i++) {
        if (students[i].entered) {
            entered_count++;
            total_wait_time += (students[i].wait_end_time - students[i].wait_start_time);
            
            if (students[i].gender == MALE) male_entered++;
            else female_entered++;
        }
    }
    
    // Расчёт загрузки ванной (средняя загрузка всех кабинок)
    // Делим на количество кабинок, чтобы получить среднюю загрузку (0-100%)
    double utilization = (shared_bathroom.total_occupancy_time / 
                         (simulation_time_sec * shared_bathroom.total_cabins)) * 100.0;
    
    // Ограничиваем значение 100% (на случай погрешностей измерения времени)
    if (utilization > 100.0) utilization = 100.0;
    
    // Вывод результатов
    printf("\n");
    printSeparator(70);
    printf(" РЕЗУЛЬТАТЫ СИМУЛЯЦИИ\n");
    printSeparator(70);
    printf(" Успешно вошли: %d из %d студентов (%.1f%%)\n", 
           entered_count, student_count, (double)entered_count / student_count * 100);
    printf("   - Мужчины: %d\n", male_entered);
    printf("   - Женщины: %d\n", female_entered);
    
    if (entered_count > 0) {
        printf(" Среднее время ожидания: %.2f сек\n", total_wait_time / entered_count);
    }
    
    printf(" Загрузка ванной: %.1f%%\n", utilization);
    printf(" Последовательных входов (макс.): %u\n", shared_bathroom.max_consecutive_entries);
    printf(" Принудительных смен пола: %s\n", 
           shared_bathroom.forced_switch_pending ? "да (в процессе)" : "нет");
    printSeparator(70);
    printf("\n");
    
    // Очистка
    free(students);
    free(threads);
    destroyBathroom(&shared_bathroom);
}

int main(void) 
{   
    // Отключаем буферизацию вывода для немедленного отображения
    setbuf(stdout, 0);
    
    // Сценарий 1: Равное количество полов, малая вместимость
    // max_consecutive_entries = 5 (после 5 входов одного пола — принудительная смена)
    runSimulation(20, 0.5, 3, 5, 20.0);
    
    // Сценарий 2: Преимущественно мужчины (риск голодания женщин без защиты)
    runSimulation(30, 0.8, 4, 6, 60.0);
    
    // Сценарий 3: Преимущественно женщины
    runSimulation(30, 0.2, 4, 6, 60.0);
    
    // Сценарий 4: Большая вместимость, равное соотношение
    runSimulation(40, 0.5, 8, 8, 60.0);
    
    printf("Все симуляции завершены. Механизм защиты от голодания активен.\n");
    return 0;
}