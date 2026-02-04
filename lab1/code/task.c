#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

enum SexState {
    nobody, // nobody 
    man, // man 
    woman, // women
};

struct Bathroom 
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    unsigned cabins_total;
    unsigned cabins_used;
    enum SexState state;
};

struct Student
{
    enum SexState sex;
    float timeForShower;
};

bool canEnter(struct Bathroom* b, struct Student* s) 
{
    // все кабинки заняты
    if (b->cabins_used == b->cabins_total)
    {
        return false;
    }

    // если пусто, можно всем
    if (b->state == nobody)
    {
        return true;
    }

    // если тот же пол
    if (b->state == s->sex)
    {
        return true;
    }

    return false;
}

bool enterBathroom(struct Bathroom *b, struct Student *s)
{
    pthread_mutex_lock(&b->mutex);

    while (!canEnter(b,s)) {

        // ожидание cond_broadcast
        pthread_cond_wait(&b->cond, &b->mutex);

    }

    if (b->state == nobody)
    {
        b->state = s->sex;
    }
    b->cabins_used++;

    printf("Student (%s) in. Занято: %d/%d\n",
        s->sex == man ? "m":"w",
        b->cabins_used, b->cabins_total
    );

    pthread_mutex_unlock(&b->mutex);

    return true;
}

void leaveBathroom(struct Bathroom* b, struct Student* s) 
{
    pthread_mutex_lock(&b->mutex);

    b->cabins_used--;

    if (b->cabins_used == 0)
    {
        b->state = nobody;
    }

    printf("Student (%s) out. Занято: %d/%d\n",
        s->sex == man ? "m":"w",
        b->cabins_used, b->cabins_total
    );

    // место освободилось, кто сможет тот зайдет
    pthread_cond_broadcast(&b->cond);

    pthread_mutex_unlock(&b->mutex);
}

struct Bathroom b = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,

    .cabins_total = 3,
    .cabins_used = 0,
    .state = nobody, 
};

void* threadFunc (void* arg) {

    struct Student* s = arg;

    if (enterBathroom(&b,s)) {

        struct timespec ts;
        ts.tv_sec  = (time_t)s->timeForShower;
        ts.tv_nsec = (long)((s->timeForShower - ts.tv_sec) * 1e9);

        nanosleep(&ts, NULL);

        leaveBathroom(&b, s);
    }

    return NULL;
}

int random_number(int min_num, int max_num);

int main(void) {

    srand((unsigned)time(NULL));

    struct Student students[] = {
        { man,   9.0 },
        { woman, 15.5 },
        { man,   5.5 },
        { woman, 21.5 },
        { woman, 12.5 },
        { man,   2.5 },
    };

    int countS = sizeof(students) / sizeof(struct Student);

    for (int i = 0; i < countS; i++) {
        students[i].timeForShower = random_number(0, 20);
        printf("%s - %f \n", 
            students[i].sex == man ? "m" : "w", 
            students[i].timeForShower);
    }

    pthread_t tid[countS];

    struct Student* temp;

    for (int i = 0; i < countS; i++) {

        temp = &students[i];

        pthread_create(&tid[i], NULL, threadFunc, temp);

    }

    for (int i = 0; i < countS; i++) {

        pthread_join(tid[i], NULL);
    }

    return 0;
}


int random_number(int min_num, int max_num) {
    int result = 0;
    
    int range = max_num - min_num + 1;
    
    result = (rand() % range) + min_num;
    return result;
}