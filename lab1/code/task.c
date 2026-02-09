#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <linux/time.h>

#define MAX_STREAK_FOR_STATE 5

enum State {
    nobody, 
    man,  
    woman,
};

struct Bathroom 
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    unsigned cabins_total;
    unsigned cabins_used;
    enum State state;

    unsigned waiting_men;
    unsigned waiting_women;

    // сколько последовательно зашло человек одного пола
    unsigned streak;
    unsigned max_streak;
};
typedef struct Bathroom Br;

struct Student
{
    int studentID; 

    enum State sex;
    float timeForShower;

    struct timespec arrival;
    struct timespec enter;
    struct timespec leave;
};

typedef struct Student St;

typedef struct timespec Time;

const int BATHROOM_CAPACITY = 4;

/// @brief Определение разницы времени между a и b
/// @param a timespec
/// @param b timespec
/// @return diff
double timespec_diff(struct timespec a, struct timespec b)
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
    // все кабинки заняты
    if (b->cabins_used == b->cabins_total) {
        return false;
    }

    // если пусто, можно всем
    if (b->state == nobody) {
        return true;
    }
    
    // разный пол вместе находится не может
    if (b->state != s->sex) {
        return false;
    }

    // Справедливый вход
    if (b->streak >= b->max_streak) {
        // Приоритет противоположному полу, если они ждут
        if (s->sex == man && b->waiting_women > 0)
            return false;

        if (s->sex == woman && b->waiting_men > 0)
            return false;
    }

    return true;
}

/// @brief Вход в ванную
/// @param b - ванная 
/// @param s - студент
/// @return 
bool enterBathroom(Br *b, St *s)
{
    pthread_mutex_lock(&b->mutex);

    bool must_wait = !canEnter(b, s);

    if (must_wait) {
        if (s->sex == man) b->waiting_men++;
        else b->waiting_women++;
    }

    while (!canEnter(b,s)) {
        // ожидание cond_broadcast
        pthread_cond_wait(&b->cond, &b->mutex);
    }

    if (must_wait) {
        if (s->sex == man) b->waiting_men--;
        else b->waiting_women--;
    }

    if (b->state == nobody)
    {
        b->state = s->sex;
        b->streak = 0;
    }

    b->cabins_used++;
    b->streak++;
    
    printf("%4d. Student (%-5s) in, время (%5.3f). Занято: %d/%d streak: %d/%d \twait_m: %d wait_w: %d\n",
        s->studentID,
        s->sex == man ? "man":"woman",
        s->timeForShower,
        b->cabins_used, b->cabins_total,
        b->streak, b->max_streak,
        b->waiting_men, b->waiting_women
    );

    if (b->streak == b->max_streak) {
        printf("\t>>> СМЕНА: Достигнут максимальный streak (%d) для %6s! <<<\n", 
               b->max_streak,
               s->sex == man ? "мужчин" : "женщин");
    }
    
    pthread_mutex_unlock(&b->mutex);

    return true;
}


/// @brief Выход из ванной
/// @param b - ванная
/// @param s - студент
void leaveBathroom(Br* b, St* s) 
{
    pthread_mutex_lock(&b->mutex);

    b->cabins_used--;

    if (b->cabins_used == 0)
    {
        b->state = nobody;
        b->streak = 0;
    }

    // посылаем сигнал всем: место освободилось, кто сможет тот зайдет
    pthread_cond_broadcast(&b->cond);

    
    printf("%4d. Student (%-5s) out. Занято: %d/%d\n",
        s->studentID,
        s->sex == man ? "man":"woman",
        b->cabins_used, b->cabins_total
    );

    pthread_mutex_unlock(&b->mutex);
}

struct Bathroom b = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,

    .cabins_total = BATHROOM_CAPACITY,
    .cabins_used = 0,
    .state = nobody,

    .waiting_men = 0,
    .waiting_women = 0,
    .streak = 0,
    .max_streak = MAX_STREAK_FOR_STATE,

};

/// @brief Функция потока студента
/// @param arg 
/// @return 
void* studentThread (void* arg) {

    St* s = (St*)arg;

    clock_gettime(CLOCK_MONOTONIC, &s->arrival);
    if (enterBathroom(&b,s)) {

        clock_gettime(CLOCK_MONOTONIC, &s->enter);

        struct timespec ts;
        ts.tv_sec  = (time_t)s->timeForShower;
        ts.tv_nsec = (long)((s->timeForShower - ts.tv_sec) * 1e9);

        nanosleep(&ts, NULL);

        leaveBathroom(&b, s);
    }
    clock_gettime(CLOCK_MONOTONIC, &s->leave);

    return 0;
}

int initVarsFromCMD(int argc, char *argv[]);

int random_number(int min_num, int max_num);

//       8             4          5
// students_count cabins_total streak
int main(int argc, char *argv[]) {

    srand((unsigned)time(NULL));

    int studLen = initVarsFromCMD(argc, argv);
    if (studLen == 0) {
        studLen = random_number(9, 25);
    }

    St students[100];

    int randTimeForShower;
    for (int i = 0; i < studLen; i++) {
        randTimeForShower = random_number(2, 5);
        if (i % 2 == 0) {
            students[i].sex = man;
        } else {
            students[i].sex = woman;
        }

        if (students[i].sex == woman) {
            students[i].timeForShower = 2 * randTimeForShower;    
        } else
            students[i].timeForShower = randTimeForShower;

        students[i].studentID = i + 1;
    }

    printf("Начало: студентов - %d, кабинок - %d, max_streak - %d\n", studLen, b.cabins_total, b.max_streak);

    pthread_t tid[studLen];

    Time total_begin, total_end;

    clock_gettime(CLOCK_MONOTONIC, &total_begin);
    for (int i = 0; i < studLen; i++) {
        // создание потока
        pthread_create(&tid[i], NULL, studentThread, &students[i]);
    }

    for (int i = 0; i < studLen; i++) {
        // ожидание потока
        pthread_join(tid[i], NULL);
    }
    printf("\t=======================Завершение======================\n");

    double total_wait = 0.0, utilization = 0.0, total_time = 0.0, total_shower = 0.0;

    for (int i = 0; i < studLen; i++) {
        double wait = timespec_diff(students[i].arrival, students[i].enter);
        printf("Время ожидания для студента %d = %f\n", students[i].studentID, wait);
        total_wait += wait;
        total_shower += timespec_diff(students[i].enter, students[i].leave);
    }
    clock_gettime(CLOCK_MONOTONIC, &total_end);

    total_time = timespec_diff(total_begin, total_end);

    utilization = total_shower / (total_time * b.cabins_total) * 100;

    double avg_wait = total_wait / studLen;
    printf("Среднее время ожидания: %f\n", avg_wait);

    printf("Утилизация: %.2f%%\n", utilization);

    return 0;
}

int random_number(int min_num, int max_num) {
    int result = 0;
    
    int range = max_num - min_num + 1;
    
    result = (rand() % range) + min_num;
    return result;
}

int initVarsFromCMD(int argc, char *argv[])
{
    int studCounts = 0;
    if (argc >= 2) {
        studCounts = atoi(argv[1]);
    }

    if (argc >= 3) {
        b.cabins_total = atoi(argv[2]);
    }

    if (argc >= 4) {
        (&b)->max_streak = atoi(argv[3]);
    }

    return studCounts;
}
