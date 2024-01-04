#include "libcflat.h"

int __argc;
char *__argv[100];
char *__args;
char __args_copy[1000];

static bool isblank(char p)
{
    return p == ' ' || p == '\t';
}

static char *skip_blanks(char *p)
{
    while (isblank(*p))
        ++p;
    return p;
}

void __setup_args(void)
{
    char *args = __args;
    char **argv = __argv;
    char *p = __args_copy;

    while (*(args = skip_blanks(args)) != '\0') {
        *argv++ = p;
        while (*args != '\0' && !isblank(*args))
            *p++ = *args++;
        *p++ = '\0';
    }
    __argc = argv - __argv;
}

void setup_args(char *args)
{
    if (!args)
        return;

    __args = args;
    __setup_args();
}
