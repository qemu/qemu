#include <stdio.h>
#include <string.h>

/* Cause a segfault for testing purposes. */

int main(int argc, char *argv[])
{
    int *ptr = (void *)0xdeadbeef;

    if (argc == 2 && strcmp(argv[1], "-s") == 0) {
        /* Cause segfault. */
        printf("%d\n", *ptr);
    }
}
