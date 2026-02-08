#ifndef STUDENT_H
#define STUDENT_H

#include <time.h>
#include <bathroom.h>

enum State {
    nobody,
    man,
    woman,
};

void* studentThread(void* arg);

struct Student {
    int studentID;

    enum State sex;
    float timeForShower;

    struct timespec arrival;
};

#endif // STUDENT_H
