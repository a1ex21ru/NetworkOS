#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "bathroom.h"
#include "student.h"

double timespec_diff(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) +
           (b.tv_nsec - a.tv_nsec) / 1e9;
}

bool canEnter(struct Bathroom* b, struct Student* s) {
    if (b->cabins_used == b->cabins_total) {
        return false;
    }
    if (b->state == nobody) {
        return true;
    }
    if (b->state != s->sex) {
        return false;
    }
    if (b->streak >= MAX_STREAK_FOR_STATE) {
        if (s->sex == man && b->waiting_women > 0)
            return false;
        if (s->sex == woman && b->waiting_men > 0)
            return false;
    }
    return true;
}

bool enterBathroom(struct Bathroom *b, struct Student *s) {
    pthread_mutex_lock(&b->mutex);
    if (s->sex == man) {
        b->waiting_men++;
    } else {
        b->waiting_women++;
    }

    while (!canEnter(b, s)) {
        pthread_cond_wait(&b->cond, &b->mutex);
    }

    if (s->sex == man) {
        b->waiting_men--;
    } else {
        b->waiting_women--;
    }

    if (b->state == nobody) {
        b->state = s->sex;
        b->streak = 0;
    }
    b->cabins_used++;
    b->streak++;

    pthread_mutex_unlock(&b->mutex);
    
    printf("%d.Student (%s) in, время (%f). Занято: %d/%d streak: %d/%d\twait_m: %d wait_w: %d\n",
        s->studentID,
        s->sex == man ? "man" : "woman",
        s->timeForShower,
        b->cabins_used, b->cabins_total,
        b->streak, MAX_STREAK_FOR_STATE,
        b->waiting_men, b->waiting_women
    );
    if (b->streak == MAX_STREAK_FOR_STATE)
        printf("Смена!\n");

    return true;
}

void leaveBathroom(struct Bathroom* b, struct Student* s) {
    pthread_mutex_lock(&b->mutex);
    b->cabins_used--;

    if (b->cabins_used == 0) {
        b->state = nobody;
        b->streak = 0;
    }

    pthread_cond_broadcast(&b->cond);
    pthread_mutex_unlock(&b->mutex);

    printf("%d.Student (%s) out. Занято: %d/%d\n",
        s->studentID,
        s->sex == man ? "man" : "woman",
        b->cabins_used, b->cabins_total
    );
}

// init bathroom
struct Bathroom b = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,

    .cabins_total = BATHROOM_CAPACITY,
    .cabins_used = 0,
    .state = nobody,

    .waiting_men = 0,
    .waiting_women = 0,
    .streak = 5,
};