#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sched.h>

int thread1_func(void *arg)
{
    int i;
    char buf[512];

    for(i=0;i<10;i++) {
        snprintf(buf, sizeof(buf), "thread1: %d %s\n", i, (char *)arg);
        write(1, buf, strlen(buf));
        usleep(100 * 1000);
    }
    return 0;
}

int thread2_func(void *arg)
{
    int i;
    char buf[512];
    for(i=0;i<20;i++) {
        snprintf(buf, sizeof(buf), "thread2: %d %s\n", i, (char *)arg);
        write(1, buf, strlen(buf));
        usleep(120 * 1000);
    }
    return 0;
}

#define STACK_SIZE 16384

void test_clone(void)
{
    uint8_t *stack1, *stack2;
    int pid1, pid2, status1, status2;

    stack1 = malloc(STACK_SIZE);
    pid1 = clone(thread1_func, stack1 + STACK_SIZE, 
                 CLONE_VM | CLONE_FS | CLONE_FILES | SIGCHLD, "hello1");

    stack2 = malloc(STACK_SIZE);
    pid2 = clone(thread2_func, stack2 + STACK_SIZE, 
                 CLONE_VM | CLONE_FS | CLONE_FILES | SIGCHLD, "hello2");

    while (waitpid(pid1, &status1, 0) != pid1);
    while (waitpid(pid2, &status2, 0) != pid2);
    printf("status1=0x%x\n", status1);
    printf("status2=0x%x\n", status2);
    printf("End of clone test.\n");
}

int main(int argc, char **argv)
{
    test_clone();
    return 0;
}
