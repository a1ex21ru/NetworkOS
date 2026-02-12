#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

typedef enum { EMPTY, MALE, FEMALE } BathroomStatus;

typedef struct {
    BathroomStatus gender;
    double shower_duration, wait_start_time, wait_end_time;
    bool entered;
    int student_id;
} Student;

typedef struct {
    pthread_mutex_t access_lock;
    pthread_cond_t availability_signal;
    unsigned int total_cabins, occupied_cabins;
    BathroomStatus current_status;
    unsigned int consecutive_entries, max_consecutive_entries;
    bool forced_switch_pending;
    BathroomStatus last_gender;
} Bathroom;

static Bathroom shared_bathroom;
static volatile bool bathroom_open = true;

void openBathroom(Bathroom* b, unsigned int capacity, unsigned int max_entries) {
    pthread_mutex_init(&b->access_lock, NULL);
    pthread_cond_init(&b->availability_signal, NULL);
    b->total_cabins = capacity;
    b->occupied_cabins = 0;
    b->current_status = EMPTY;
    b->consecutive_entries = 0;
    b->max_consecutive_entries = max_entries;
    b->forced_switch_pending = false;
    b->last_gender = EMPTY;
}


void updateDoorStatus(Bathroom* b) {
    unsigned int free = b->total_cabins - b->occupied_cabins;
    const char* status = (b->current_status == EMPTY) ? "НИКОГО НЕТ" :
                         (b->current_status == MALE) ? "В ВАННОЙ МУЖЧИНЫ" : "В ВАННОЙ ЖЕНЩИНЫ";
    printf("[ДВЕРЬ] Индикатор: %s | Свободных мест: %u из %u\n", status, free, b->total_cabins);
}

static bool isEntryAllowed(Bathroom* b, Student* s) {
    if (b->occupied_cabins >= b->total_cabins) return false;
    if (b->current_status == EMPTY)
        return !(b->forced_switch_pending && b->last_gender != EMPTY && b->last_gender == s->gender);
    return (b->current_status == s->gender) && !b->forced_switch_pending;
}

bool enterBathroom(Bathroom* b, Student* s) {
    pthread_mutex_lock(&b->access_lock);
    if (!bathroom_open) {
        pthread_mutex_unlock(&b->access_lock);
        return false;
    }
    s->wait_start_time = (double)clock() / CLOCKS_PER_SEC;
    while (!isEntryAllowed(b, s)) {
        pthread_cond_wait(&b->availability_signal, &b->access_lock);
    }
    s->wait_end_time = (double)clock() / CLOCKS_PER_SEC;
    s->entered = true;
    
    if (b->current_status == EMPTY) {
        b->consecutive_entries = 0;
        if (b->last_gender != EMPTY && b->last_gender != s->gender) b->forced_switch_pending = false;
        b->current_status = s->gender;
    }
    if (++b->consecutive_entries >= b->max_consecutive_entries)
        b->forced_switch_pending = true;
    
    b->occupied_cabins++;
    printf("[ВХОД] Студент %2d (%s) вошёл. Занято: %u/%u | Входы: %u/%u%s\n",
           s->student_id + 1, (s->gender == MALE) ? "M" : "F",
           b->occupied_cabins, b->total_cabins, b->consecutive_entries,
           b->max_consecutive_entries, b->forced_switch_pending ? " | СМЕНА!" : "");
    updateDoorStatus(b);
    pthread_mutex_unlock(&b->access_lock);
    return true;
}

void exitBathroom(Bathroom* b, Student* s) {
    pthread_mutex_lock(&b->access_lock);
    b->occupied_cabins--;
    if (b->occupied_cabins == 0) {
        b->last_gender = b->current_status;
        b->current_status = EMPTY;
    }
    printf("[ВЫХОД] Студент %2d (%s) вышел. Занято: %u/%u\n",
           s->student_id + 1, (s->gender == MALE) ? "M" : "F",
           b->occupied_cabins, b->total_cabins);
    updateDoorStatus(b);
    pthread_cond_broadcast(&b->availability_signal);
    pthread_mutex_unlock(&b->access_lock);
}

void* studentRoutine(void* arg) {
    Student* s = (Student*)arg;
    if (enterBathroom(&shared_bathroom, s)) {
        usleep((useconds_t)(s->shower_duration * 1000000));
        exitBathroom(&shared_bathroom, s);
    }
    return NULL;
}

void bathroomDay(int student_count, double male_ratio, unsigned int capacity,
                 unsigned int max_entries, double time_sec) {
    printf("\n======================================================================\n"
           " СИМУЛЯЦИЯ ВАННОЙ КОМНАТЫ\n"
           "======================================================================\n"
           " Студентов: %d (мужчин: ~%.0f%%, женщин: ~%.0f%%)\n"
           " Вместимость ванной: %u кабинок | Макс. входов: %u\n"
           " Время душа: случайно 1.0–4.0 сек\n"
           "======================================================================\n\n",
           student_count, male_ratio * 100, (1 - male_ratio) * 100, capacity, max_entries);
    
    openBathroom(&shared_bathroom, capacity, max_entries);
    updateDoorStatus(&shared_bathroom);
    
    Student* students = malloc(student_count * sizeof(Student));
    pthread_t* threads = malloc(student_count * sizeof(pthread_t));
    srand((unsigned int)time(NULL));
    
    bathroom_open = true;
    for (int i = 0; i < student_count; i++) {
        students[i] = (Student){.gender = (rand() / (double)RAND_MAX < male_ratio) ? MALE : FEMALE,
                                 .shower_duration = 1.0 + (rand() / (double)RAND_MAX) * 3.0,
                                 .student_id = i};
        pthread_create(&threads[i], NULL, studentRoutine, &students[i]);
    }
    
    sleep((unsigned int)time_sec);
    usleep(500000);
    bathroom_open = false;
    
    pthread_mutex_lock(&shared_bathroom.access_lock);
    pthread_cond_broadcast(&shared_bathroom.availability_signal);
    pthread_mutex_unlock(&shared_bathroom.access_lock);
    
    for (int i = 0; i < 10; i++) {
        pthread_mutex_lock(&shared_bathroom.access_lock);
        pthread_cond_broadcast(&shared_bathroom.availability_signal);
        pthread_mutex_unlock(&shared_bathroom.access_lock);
        usleep(200000);
    }
    
    for (int i = 0; i < student_count; i++)
        pthread_join(threads[i], NULL);
    
    double total_wait = 0.0;
    int entered = 0, male = 0, female = 0;
    for (int i = 0; i < student_count; i++)
        if (students[i].entered) {
            entered++;
            total_wait += (students[i].wait_end_time - students[i].wait_start_time);
            students[i].gender == MALE ? male++ : female++;
        }
    
    printf("\n======================================================================\n");
    printf(" РЕЗУЛЬТАТЫ СИМУЛЯЦИИ\n");
    printf("======================================================================\n");
    printf(" Успешно вошли: %d из %d студентов (М: %d, Ж: %d)\n",
           entered, student_count, male, female);
    if (entered > 0)
        printf(" Среднее время ожидания: %.2f сек\n", total_wait / entered);
    printf("======================================================================\n\n");
    fflush(stdout);
    
    free(students);
    free(threads);
    pthread_mutex_destroy(&shared_bathroom.access_lock);
    pthread_cond_destroy(&shared_bathroom.availability_signal);
}

int main(void) {
    bathroomDay(20, 0.3, 3, 5, 20.0);
    return 0;
}
