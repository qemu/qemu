#include <assert.h>

int main(void)
{
    unsigned long var;

    var = 0xFEDCBA9876543210;
    asm("brh %0, %0" : "+r"(var));
    assert(var == 0xDCFE98BA54761032);

    var = 0xFEDCBA9876543210;
    asm("brw %0, %0" : "+r"(var));
    assert(var == 0x98BADCFE10325476);

    var = 0xFEDCBA9876543210;
    asm("brd %0, %0" : "+r"(var));
    assert(var == 0x1032547698BADCFE);

    return 0;
}

