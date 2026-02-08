#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include "student.h"
#include "bathroom.h"

void* studentThread(void* arg) {
    struct Student* s = (struct Student*)arg;
    //struct Bathroom b;  // Инициализируйте здесь

    if (enterBathroom(&b, s)) {
        clock_gettime(CLOCK_MONOTONIC, &s->arrival);

        struct timespec ts;
        ts.tv_sec = (time_t)s->timeForShower;
        ts.tv_nsec = (long)((s->timeForShower - ts.tv_sec) * 1e9);

        nanosleep(&ts, NULL);
        
        leaveBathroom(&b, s);
    }

    pthread_exit(0);
}
