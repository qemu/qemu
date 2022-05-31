#include <stdio.h>

int overflow_add_32(int x, int y)
{
    int res;
    return __builtin_add_overflow(x, y, &res);
}

int overflow_add_64(long long x, long long y)
{
    long long res;
    return __builtin_add_overflow(x, y, &res);
}

int overflow_sub_32(int x, int y)
{
    int res;
    return __builtin_sub_overflow(x, y, &res);
}

int overflow_sub_64(long long x, long long y)
{
    long long res;
    return __builtin_sub_overflow(x, y, &res);
}

int a1_add = -2147483648;
int b1_add = -2147483648;
long long a2_add = -9223372036854775808ULL;
long long b2_add = -9223372036854775808ULL;

int a1_sub;
int b1_sub = -2147483648;
long long a2_sub = 0L;
long long b2_sub = -9223372036854775808ULL;

int main()
{
    int ret = 0;

    if (!overflow_add_32(a1_add, b1_add)) {
        fprintf(stderr, "data overflow while adding 32 bits\n");
        ret = 1;
    }
    if (!overflow_add_64(a2_add, b2_add)) {
        fprintf(stderr, "data overflow while adding 64 bits\n");
        ret = 1;
    }
    if (!overflow_sub_32(a1_sub, b1_sub)) {
        fprintf(stderr, "data overflow while subtracting 32 bits\n");
        ret = 1;
    }
    if (!overflow_sub_64(a2_sub, b2_sub)) {
        fprintf(stderr, "data overflow while subtracting 64 bits\n");
        ret = 1;
    }
    return ret;
}
