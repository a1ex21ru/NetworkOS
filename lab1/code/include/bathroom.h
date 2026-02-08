#ifndef BATHROOM_H
#define BATHROOM_H

#include <pthread.h>
#include <stdbool.h>
#include <time.h>
#include "student.h"

#define MAX_STREAK_FOR_STATE 5
#define BATHROOM_CAPACITY 4



struct Bathroom {
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    unsigned cabins_total;
    unsigned cabins_used;

    enum State state;

    unsigned waiting_men;
    unsigned waiting_women;
    unsigned streak;
}b;

bool canEnter(struct Bathroom* b, struct Student* s);
bool enterBathroom(struct Bathroom *b, struct Student *s);
void leaveBathroom(struct Bathroom* b, struct Student* s);
double timespec_diff(struct timespec a, struct timespec b);

#endif // BATHROOM_H
