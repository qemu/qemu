#include <stdio.h>
#include <time.h>

#define VSYSCALL_PAGE 0xffffffffff600000
#define TIME_OFFSET 0x400
typedef time_t (*time_func)(time_t *);

int main(void)
{
    printf("%ld\n", ((time_func)(VSYSCALL_PAGE + TIME_OFFSET))(NULL));
    return 0;
}
