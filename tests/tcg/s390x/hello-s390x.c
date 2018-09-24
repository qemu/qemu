#include <unistd.h>

int main(void)
{
    write(1, "hello\n", 6);
    return 0;
}
