#include "testdev_assert.h"

static int fib(int n)
{
    if (n == 1 || n == 2) {
        return 1;
    }
    return fib(n - 2) + fib(n - 1);
}

int main(int argc, char **argv)
{
    testdev_assert(fib(10) == 55);
    return 0;
}
