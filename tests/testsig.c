#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

void alarm_handler(int sig)
{
    printf("alarm signal=%d\n", sig);
    alarm(1);
}

int main(int argc, char **argv)
{
    struct sigaction act;
    act.sa_handler = alarm_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGALRM, &act, NULL);
    alarm(1);
    for(;;) {
        sleep(1);
    }
    return 0;
}
