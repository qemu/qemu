#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sched.h>

void checked_write(int fd, const void *buf, size_t count)
{
    ssize_t rc = write(fd, buf, count);
    assert(rc == count);
}

void *thread1_func(void *arg)
{
    int i;
    char buf[512];

    for(i=0;i<10;i++) {
        snprintf(buf, sizeof(buf), "thread1: %d %s\n", i, (char *)arg);
        checked_write(1, buf, strlen(buf));
        usleep(100 * 1000);
    }
    return NULL;
}

void *thread2_func(void *arg)
{
    int i;
    char buf[512];
    for(i=0;i<20;i++) {
        snprintf(buf, sizeof(buf), "thread2: %d %s\n", i, (char *)arg);
        checked_write(1, buf, strlen(buf));
        usleep(150 * 1000);
    }
    return NULL;
}

void test_pthread(void)
{
    pthread_t tid1, tid2;

    pthread_create(&tid1, NULL, thread1_func, "hello1");
    pthread_create(&tid2, NULL, thread2_func, "hello2");
    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);
    printf("End of pthread test.\n");
}

int main(int argc, char **argv)
{
    test_pthread();
    return 0;
}
