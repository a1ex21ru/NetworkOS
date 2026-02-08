#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include "bathroom.h"
#include "student.h"

// struct Bathroom b = {
//     .mutex = PTHREAD_MUTEX_INITIALIZER,
//     .cond = PTHREAD_COND_INITIALIZER,
//     .cabins_total = BATHROOM_CAPACITY,
//     .cabins_used = 0,
//     .state = nobody,
//     .waiting_men = 0,
//     .waiting_women = 0,
//     .streak = 0,
// };

int random_number(int min_num, int max_num) {
    return (rand() % (max_num - min_num + 1)) + min_num;
}

int main(void) {
    srand((unsigned)time(NULL));
    int randLen = random_number(9, 25);
    struct Student students[30];
    pthread_t tid[randLen];

    for (int i = 0; i < randLen; i++) {
        students[i].studentID = i + 1;
        students[i].timeForShower = (float)random_number(2, 5);
        students[i].sex = (i % 2 == 0) ? man : woman;

        pthread_create(&tid[i], NULL, studentThread, &students[i]);
    }

    for (int i = 0; i < randLen; i++) {
        pthread_join(tid[i], NULL);
    }

    return 0;
}
