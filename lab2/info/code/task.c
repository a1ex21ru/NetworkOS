#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <linux/time.h>
#include <sys/wait.h>

#define MAX_STREAK_FOR_STATE 5

enum State {
    nobody, 
    man,  
    woman,
};

typedef enum State Sex;

struct Bathroom 
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    unsigned cabins_total;
    unsigned cabins_used;
    Sex state;

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

    Sex sex;
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
/// @param b ванная 
/// @param s студент
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

Br* b;

/// @brief Функция процесса студента
/// @param arg 
/// @return 
void studentProcess (void* arg) {

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

    exit(0);
}

void initBathroom();

int random_number(int min_num, int max_num);

int initVarsFromCMD(int argc, char *argv[]);

int main(int argc, char* argv[]){


}

void initBathroom()
{

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    // установка атрибута для межпроцессного взаимодействия
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_condattr_t cattr;
    pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);

    Br initB = {
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

    b = &initB;

    pthread_mutex_t* mu = &b->mutex;
    pthread_cond_init(mu, &attr);

    pthread_cond_t* cond = &b->cond;
    pthread_cond_init(cond, &cattr);
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
