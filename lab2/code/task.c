#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <linux/time.h>
#include <sys/wait.h>
#include <sys/mman.h>

#define MAX_STREAK_FOR_STATE 5
#define MAX_STUDENTS 100
#define BATHROOM_CAPACITY 4

#define COLOR_YELLOW "\033[33m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RED   "\033[31m"
#define COLOR_RESET "\033[0m"

enum State {
    nobody, 
    man,  
    woman,
};

typedef enum State Sex;

typedef struct 
{
    pthread_mutex_t dataMutex;
    pthread_cond_t cond;

    pthread_mutex_t condMutex;

    unsigned cabins_total;
    unsigned cabins_used;
    Sex state;
    Sex last_state;
    bool force_change;

    unsigned waiting_men;
    unsigned waiting_women;

    // сколько последовательно зашло человек одного пола
    unsigned streak;
    unsigned max_streak;
} Br;

typedef struct
{
    int studentID; 

    Sex sex;
    float timeForShower;

    struct timespec arrival;
    struct timespec enter;
    struct timespec leave;
} St;

typedef struct timespec Time;

/// @brief Опредение глобальной душевой
Br* b;
/// @brief Глобальное определние студентов
St* students;

/// @brief Определение разницы времени между a и b
/// @param a timespec
/// @param b timespec
/// @return diff
double timespec_diff(Time a, Time b)
{
    return (b.tv_sec - a.tv_sec) +
           (b.tv_nsec - a.tv_nsec) / 1e9;
}

/// @brief Проверка доступности входа
/// @param b ванная
/// @param s студент
/// @return 
bool canEnter(Br* b, St* s)
{
    pthread_mutex_lock(&b->dataMutex);

    // все кабинки заняты
    if (b->cabins_used == b->cabins_total) {
        pthread_mutex_unlock(&b->dataMutex);
        return false;
    }

    // если пусто, можно всем
    if (b->state == nobody) {
        if (b->force_change && b->last_state == s->sex) {
            pthread_mutex_unlock(&b->dataMutex);
            return false;  // запрет тому же полу после серии
        }
        pthread_mutex_unlock(&b->dataMutex);
        return true;
    }
    
    // разный пол вместе находится не может
    if (b->state != s->sex) {
        pthread_mutex_unlock(&b->dataMutex);
        return false;
    }

    // Справедливый вход
    if (b->streak >= b->max_streak) {
        // Приоритет противоположному полу, если они ждут
        if (s->sex == man && b->waiting_women > 0)
        {
            pthread_mutex_unlock(&b->dataMutex);
            return false;
        }

        if (s->sex == woman && b->waiting_men > 0)
        {
            pthread_mutex_unlock(&b->dataMutex);
            return false;
        }
    }

    pthread_mutex_unlock(&b->dataMutex);
    return true;
}

/// @brief Вход в ванную
/// @param b ванная 
/// @param s студент
/// @return 
bool enterBathroom(Br *b, St *s)
{
    pthread_mutex_lock(&b->condMutex);

    bool must_wait = !canEnter(b, s);

    if (must_wait) {
        if (s->sex == man) b->waiting_men++;
        else b->waiting_women++;
    }

    while (!canEnter(b,s)) {
        // ожидание cond_broadcast
        pthread_cond_wait(&b->cond, &b->condMutex);
    }

    if (must_wait) {
        if (s->sex == man) b->waiting_men--;
        else b->waiting_women--;
    }

    if (b->state == nobody)
    {
        if (b->force_change && b->last_state != s->sex) {
            b->force_change = false; 
            printf(COLOR_YELLOW"\t>>> СМЕНА ПОЛА ВЫПОЛНЕНА: Теперь в ванной %s <<<\n",
                   s->sex == man ? "мужчины" : "женщины");
        }
        b->state = s->sex;
        b->streak = 0;
    }

    b->cabins_used++;
    b->streak++;
    
    printf(COLOR_GREEN"%4d. Студент (%-5s) [PID %d] in, время %5.3f. Занято: %d/%d streak: %d/%d \twait_m: %d wait_w: %d\n",
        s->studentID,
        s->sex == man ? "man":"woman",
        getpid(),
        s->timeForShower,
        b->cabins_used, b->cabins_total,
        b->streak, b->max_streak,
        b->waiting_men, b->waiting_women
    );

    if (b->streak == b->max_streak) {
        printf(COLOR_YELLOW"\t>>> СМЕНА: Достигнут максимальный streak (%d) для %6s! <<<\n", 
               b->max_streak,
               s->sex == man ? "мужчин" : "женщин");
    }
    
    pthread_mutex_unlock(&b->condMutex);

    return true;
}

/// @brief Выход из ванной
/// @param b ванная
/// @param s студент
void leaveBathroom(Br* b, St* s) 
{
    pthread_mutex_lock(&b->condMutex);

    pthread_mutex_lock(&b->dataMutex);

    b->cabins_used--;

    if (b->cabins_used == 0)
    {
        if (b->streak >= b->max_streak) {
            b->last_state = b->state;  // запомнить пол
            b->force_change = true;     // требовать смену
        } else {
            b->force_change = false;  // обычный выход, смена не требуется
        }

        b->state = nobody;
        b->streak = 0;
    }
    
    printf(COLOR_RED"%4d. Студент (%-5s) out. Занято: %d/%d\n",
        s->studentID,
        s->sex == man ? "man":"woman",
        b->cabins_used, b->cabins_total
    );

    pthread_mutex_unlock(&b->dataMutex);

    // посылаем сигнал всем: место освободилось, кто сможет тот зайдет
    pthread_cond_broadcast(&b->cond);

    pthread_mutex_unlock(&b->condMutex);
}

/// @brief Функция процесса студента
/// @param arg 
/// @return 
void studentProcess (St* s) {

    clock_gettime(CLOCK_MONOTONIC, &s->arrival);

    enterBathroom(b,s);

    clock_gettime(CLOCK_MONOTONIC, &s->enter);

    struct timespec ts;
    ts.tv_sec  = (time_t)s->timeForShower;
    ts.tv_nsec = (long)((s->timeForShower - ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);

    leaveBathroom(b, s);
    
    clock_gettime(CLOCK_MONOTONIC, &s->leave);

    exit(0);
}

void init();

int random_number(int min_num, int max_num);

int initVarsFromCMD(int argc, char *argv[]);

int main(int argc, char* argv[]){
    srand((unsigned)time(NULL));
    
    init();

    int studLen = 0;
    studLen = initVarsFromCMD(argc, argv);

    if (studLen == 0) {
        studLen = random_number(10, 40);
    }

    // Инициализация студентов
    for (int i = 0; i < studLen; i++) {
        int rtime = random_number(2, 5);

        // Случайный пол: 50/50
        students[i].sex = (rand() % 2 == 0) ? man : woman;

        students[i].timeForShower = 
            (students[i].sex == woman) ? 2 * rtime : rtime;
        students[i].studentID = i + 1;
    }

    printf(COLOR_RESET
        "\tНачало: студентов - %d, кабинок - %d, " \
        "максимальная серия - %d\n",
        studLen, b->cabins_total, b->max_streak);

    pid_t pids[MAX_STUDENTS];
    Time total_begin, total_end;

    clock_gettime(CLOCK_MONOTONIC, &total_begin);

    // Создание дочерних процессов
    for (int i = 0; i < studLen; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            // дочерный процесс
            studentProcess(&students[i]);
            exit(0);
        } else if(pids[i] < 0) {
            perror("fork");
            exit(1);
        }
    }

    // ожидание завершения процессов
    for (int i = 0; i < studLen; i++) {
        waitpid(pids[i], NULL, 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &total_end);
    
    printf(COLOR_RESET
        "\t==============Завершение==============\n");

    double total_wait = 0.0, total_shower = 0.0;
    for (int i = 0; i < studLen; i++) {
        double wait = timespec_diff(
            students[i].arrival, students[i].enter);
        printf("Время ожидания студента %d = %f\n",
               students[i].studentID, wait);
        total_wait += wait;
        total_shower += timespec_diff(
            students[i].enter, students[i].leave);
    }

    double total_time = timespec_diff(total_begin, total_end);
    double utilization =
        total_shower / (total_time * b->cabins_total) * 100;

    printf(COLOR_RESET"Среднее время ожидания: %f\n",
           total_wait / studLen);
    printf(COLOR_RESET"Утилизация: %.2f%%\n", utilization);

    // Очистка
    pthread_mutex_destroy(&b->dataMutex);
    pthread_mutex_destroy(&b->condMutex);
    pthread_cond_destroy(&b->cond);
    munmap(b, sizeof(Br));
    munmap(students, sizeof(St) * MAX_STUDENTS);

    return 0;

}

void init() {
    // Ванная в разделяемой памяти
    b = mmap(NULL, sizeof(Br), 
        PROT_READ | PROT_WRITE, 
        MAP_SHARED | MAP_ANON, 
        -1, 0);

    // Студенты в разделяемой памяти
    students = mmap(NULL, sizeof(St) * MAX_STUDENTS,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, 
                    -1, 0);

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    // установка атрибута для межпроцессного взаимодействия
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    
    pthread_mutex_init(&b->dataMutex, &attr);
    pthread_mutex_init(&b->condMutex, &attr);
    pthread_cond_init(&b->cond, &cattr);
    
    b->cabins_total = BATHROOM_CAPACITY;
    
    b->cabins_used = 0;
    b->state = nobody;
    b->last_state = nobody;
    b->last_state = false;
    
    b->waiting_men = 0;
    b->waiting_women = 0;
    b->streak = 0;
    b->max_streak = MAX_STREAK_FOR_STATE;
    
    pthread_mutexattr_destroy(&attr);
    pthread_condattr_destroy(&cattr);
}

int random_number(int min_num, int max_num) {
    int result = 0;
    
    int range = max_num - min_num + 1;
    
    result = (rand() % range) + min_num;
    return result;
}

int initVarsFromCMD(int argc, char *argv[]) {
    int studCounts = 0;
    if (argc >= 2) {
        studCounts = atoi(argv[1]);
    }

    if (argc >= 3) {
        b->cabins_total = atoi(argv[2]);
    }

    if (argc >= 4) {
        b->max_streak = atoi(argv[3]);
    }

    return studCounts;
}
