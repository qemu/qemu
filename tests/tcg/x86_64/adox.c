/* See if ADOX give expected results */

#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

static uint64_t adoxq(bool *c_out, uint64_t a, uint64_t b, bool c)
{
    asm ("addl $0x7fffffff, %k1\n\t"
         "adoxq %2, %0\n\t"
         "seto %b1"
         : "+r"(a), "=&r"(c) : "r"(b), "1"((int)c));
    *c_out = c;
    return a;
}

static uint64_t adoxl(bool *c_out, uint64_t a, uint64_t b, bool c)
{
    asm ("addl $0x7fffffff, %k1\n\t"
         "adoxl %k2, %k0\n\t"
         "seto %b1"
         : "+r"(a), "=&r"(c) : "r"(b), "1"((int)c));
    *c_out = c;
    return a;
}

int main()
{
    uint64_t r;
    bool c;

    r = adoxq(&c, 0, 0, 0);
    assert(r == 0);
    assert(c == 0);

    r = adoxl(&c, 0, 0, 0);
    assert(r == 0);
    assert(c == 0);

    r = adoxl(&c, 0x100000000, 0, 0);
    assert(r == 0);
    assert(c == 0);

    r = adoxq(&c, 0, 0, 1);
    assert(r == 1);
    assert(c == 0);

    r = adoxl(&c, 0, 0, 1);
    assert(r == 1);
    assert(c == 0);

    r = adoxq(&c, -1, -1, 0);
    assert(r == -2);
    assert(c == 1);

    r = adoxl(&c, -1, -1, 0);
    assert(r == 0xfffffffe);
    assert(c == 1);

    r = adoxq(&c, -1, -1, 1);
    assert(r == -1);
    assert(c == 1);

    r = adoxl(&c, -1, -1, 1);
    assert(r == 0xffffffff);
    assert(c == 1);

    return 0;
}
