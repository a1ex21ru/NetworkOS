
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

char ch = 'A';

char ch1 = 'a';

pthread_t pth;

pid_t p;

void* fn0(void* arg) {
    for (int i = 0; i < 100; i++) {
        printf("%c", (char*)arg);
    }
    printf("\n");
}

void* fn1(void* arg) {    
    for (int i = 0; i < 100; i++) {
        printf("%c", *(char*)arg);
    }
    printf("\n");
}

void* fn(void* arg) {    
    for (int i = 0; i < 20; i++) {
        usleep(50);
        ch1++;
        //printf("%c", *(char*)arg);
    }
}

pid_t p;


void main (){

    setbuf(stdout,0);

    printf("Hello world!\n");

    //fn1((void*)&ch);

    //fn(NULL);

    //fn0((char*)ch);

    p = fork();

    if (p==0) {
        usleep(5000);
    }

    pthread_create(&pth, NULL, fn, NULL);

    pthread_create(&pth, NULL, fn1, &ch1);

    fn1((void*)&ch);

    usleep(1000);
}

