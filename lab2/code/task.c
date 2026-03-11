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

#define SHM_NAME "/bathroom_shm"

typedef enum { EMPTY, MALE, FEMALE } BathroomStatus;

typedef struct {
    pthread_mutex_t access_lock;
    pthread_cond_t  availability_signal;
    unsigned int total_cabins, occupied_cabins;
    BathroomStatus current_status;
    unsigned int consecutive_entries, max_consecutive_entries;
    bool forced_switch_pending;
    BathroomStatus last_gender;
    volatile bool bathroom_open;
    bool header_printed;
    time_t start_time;
    unsigned int entered_count, male_entered, female_entered;
    double total_wait_time;
} Bathroom;

static Bathroom* shared_bathroom;

void openBathroom(Bathroom* b, unsigned int capacity, unsigned int max_entries) {
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&b->access_lock, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);

    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&b->availability_signal, &cond_attr);
    pthread_condattr_destroy(&cond_attr);

    b->total_cabins = capacity;
    b->occupied_cabins = 0;
    b->current_status = EMPTY;
    b->consecutive_entries = 0;
    b->max_consecutive_entries = max_entries;
    b->forced_switch_pending = false;
    b->last_gender = EMPTY;
    b->bathroom_open = true;
    b->header_printed = false;
    b->start_time = time(NULL);
    b->entered_count = 0;
    b->male_entered = 0;
    b->female_entered = 0;
    b->total_wait_time = 0.0;
}

void printDoorStatus(Bathroom* b) {
    unsigned int free = b->total_cabins - b->occupied_cabins;
    const char* status = (b->current_status == EMPTY) ? "НИКОГО НЕТ" :
                         (b->current_status == MALE) ? "В ВАННОЙ МУЖЧИНЫ" : "В ВАННОЙ ЖЕНЩИНЫ";
    printf("[ДВЕРЬ] Индикатор: %s | Свободных мест: %u из %u\n", status, free, b->total_cabins);
}

static bool isEntryAllowed(Bathroom* b, BathroomStatus gender) {
    if (b->occupied_cabins >= b->total_cabins) return false;
    if (b->current_status == EMPTY)
        return !(b->forced_switch_pending && b->last_gender != EMPTY && b->last_gender == gender);
    return (b->current_status == gender) && !b->forced_switch_pending;
}

bool enterBathroom(Bathroom* b, int student_id, BathroomStatus gender, double* wait_time) {
    time_t t0 = time(NULL);
    pthread_mutex_lock(&b->access_lock);
    if (!b->bathroom_open) {
        pthread_mutex_unlock(&b->access_lock);
        return false;
    }
    while (!isEntryAllowed(b, gender) && b->bathroom_open) {
        pthread_cond_wait(&b->availability_signal, &b->access_lock);
    }
    if (!b->bathroom_open) {
        pthread_cond_broadcast(&b->availability_signal);
        pthread_mutex_unlock(&b->access_lock);
        return false;
    }
    *wait_time = difftime(time(NULL), t0);

    if (b->current_status == EMPTY) {
        b->consecutive_entries = 0;
        if (b->last_gender != EMPTY && b->last_gender != gender) b->forced_switch_pending = false;
        b->current_status = gender;
    }
    if (++b->consecutive_entries >= b->max_consecutive_entries)
        b->forced_switch_pending = true;
    b->occupied_cabins++;

    b->entered_count++;
    b->total_wait_time += *wait_time;
    if (gender == MALE) b->male_entered++; else b->female_entered++;

    printf("[ВХОД] Студент %2d (%s) вошёл. Занято: %u/%u | Входы: %u/%u%s\n",
           student_id + 1, (gender == MALE) ? "M" : "F",
           b->occupied_cabins, b->total_cabins, b->consecutive_entries,
           b->max_consecutive_entries, b->forced_switch_pending ? " | СМЕНА!" : "");
    printDoorStatus(b);
    pthread_cond_broadcast(&b->availability_signal);
    pthread_mutex_unlock(&b->access_lock);
    return true;
}

void exitBathroom(Bathroom* b, int student_id, BathroomStatus gender) {
    pthread_mutex_lock(&b->access_lock);
    b->occupied_cabins--;
    if (b->occupied_cabins == 0) {
        b->last_gender = b->current_status;
        b->current_status = EMPTY;
        b->forced_switch_pending = false;  /* когда ванная пуста — сбрасываем, иначе возможен дедлок */
    }
    printf("[ВЫХОД] Студент %2d (%s) вышел. Занято: %u/%u\n",
           student_id + 1, (gender == MALE) ? "M" : "F",
           b->occupied_cabins, b->total_cabins);
    printDoorStatus(b);
    pthread_cond_broadcast(&b->availability_signal);
    pthread_mutex_unlock(&b->access_lock);
}

void studentRoutine(int student_id, BathroomStatus gender, double shower_duration) {
    double wait_time = 0.0;
    if (enterBathroom(shared_bathroom, student_id, gender, &wait_time)) {
        usleep((useconds_t)(shower_duration * 1000000));
        exitBathroom(shared_bathroom, student_id, gender);
    }
    exit(0);
}

void bathroomDay(int student_count, double male_ratio, unsigned int capacity,
                 unsigned int max_entries) {
    shm_unlink(SHM_NAME);
    int fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0666);
    if (fd == -1) {
        perror("shm_open");
        exit(1);
    }
    if (ftruncate(fd, sizeof(Bathroom)) == -1) {
        perror("ftruncate");
        exit(1);
    }
    shared_bathroom = (Bathroom*)mmap(NULL, sizeof(Bathroom), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shared_bathroom == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    close(fd);
    memset(shared_bathroom, 0, sizeof(Bathroom));

    openBathroom(shared_bathroom, capacity, max_entries);

    pthread_mutex_lock(&shared_bathroom->access_lock);
    if (!shared_bathroom->header_printed) {
        printf("\n======================================================================\n"
               " СИМУЛЯЦИЯ ВАННОЙ КОМНАТЫ\n"
               "======================================================================\n"
               " Студентов: %d (мужчин: ~%.0f%%, женщин: ~%.0f%%)\n"
               " Вместимость ванной: %u кабинок | Макс. входов: %u\n"
               " Время душа: случайно 1.0–4.0 сек\n"
               "======================================================================\n\n",
               student_count, male_ratio * 100, (1 - male_ratio) * 100, capacity, max_entries);
        printDoorStatus(shared_bathroom);
        shared_bathroom->header_printed = true;
        fflush(stdout);
    }
    pthread_mutex_unlock(&shared_bathroom->access_lock);

    srand((unsigned int)time(NULL));
    pid_t* pids = malloc((size_t)student_count * sizeof(pid_t));
    if (!pids) {
        perror("malloc");
        exit(1);
    }

    for (int i = 0; i < student_count; i++) {
        BathroomStatus gender = (rand() / (double)RAND_MAX < male_ratio) ? MALE : FEMALE;
        double shower_duration = 1.0 + (rand() / (double)RAND_MAX) * 3.0;
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            exit(1);
        }
        if (pid == 0) {
            srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
            studentRoutine(i, gender, shower_duration);
        }
        pids[i] = pid;
    }

    for (int i = 0; i < student_count; i++)
        waitpid(pids[i], NULL, 0);

    pthread_mutex_lock(&shared_bathroom->access_lock);
    shared_bathroom->bathroom_open = false;
    pthread_cond_broadcast(&shared_bathroom->availability_signal);
    pthread_mutex_unlock(&shared_bathroom->access_lock);

    int entered = (int)shared_bathroom->entered_count;
    int male = (int)shared_bathroom->male_entered;
    int female = (int)shared_bathroom->female_entered;
    double total_wait = shared_bathroom->total_wait_time;

    printf("\n======================================================================\n");
    printf(" РЕЗУЛЬТАТЫ СИМУЛЯЦИИ\n");
    printf("======================================================================\n");
    printf(" Успешно вошли: %d из %d студентов (М: %d, Ж: %d)\n",
           entered, student_count, male, female);
    if (entered > 0)
        printf(" Среднее время ожидания: %.2f сек\n", total_wait / entered);
    printf("======================================================================\n\n");
    fflush(stdout);

    free(pids);
    pthread_mutex_destroy(&shared_bathroom->access_lock);
    pthread_cond_destroy(&shared_bathroom->availability_signal);
    munmap(shared_bathroom, sizeof(Bathroom));
    shm_unlink(SHM_NAME);
}

int main(void) {
    setbuf(stdout, NULL);
    bathroomDay(20, 0.3, 3, 5);
    return 0;
}
