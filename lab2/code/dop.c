#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
// #include <std>

int main() {
    const size_t SIZE = 10512;
    char *shared_mem0;
    char *shared_mem1;

    int fd = open("task.c", O_RDWR);

    void* addr = (void*)malloc();

    shared_mem0 = (char *)mmap(addr, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED , fd, 0);
    //shared_mem1 = (char *)mmap(addr, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED , fd, SIZE);

    if (shared_mem0 == MAP_FAILED) {
        perror("mmap0 failed");
        return 1;
    }

    if (shared_mem1 == MAP_FAILED) {
        perror("mmap1 failed");
        return 1;
    }

    if (shared_mem0 == shared_mem1) {
        printf("%s ptr1 = %s ptr2 = %s\n", "equal", shared_mem0, shared_mem1);
        return 1;
    }
    
    printf("%s\n", "not equal");
    printf("%s ptr1 = %p ptr2 = %p\n", "equal", (void*)shared_mem0, (void*)shared_mem1);
        return 1;


    return 0;
}