#include "macros.h"
.text
.global _start
_start:
    #                              Result                   RS1            RS2
    TEST_E_ED(dvstep,   1, 0x000001ff, 0xfffe5cff, 0x00000001, 0xfffffe5c, 0x0)
    TEST_E_ED(dvstep,   2, 0x00000000, 0x000000ff, 0x00000000, 0x00000000, 0x0)
    TEST_E_ED(dvstep,   3, 0x0000f000, 0x000000fd, 0x010000f0, 0x00000000, 0x0)
    TEST_E_ED(dvstep,   4, 0xfffff000, 0x00000000, 0x7ffffff0, 0x00000000, 0x0)
    TEST_E_ED(dvstep.u, 5, 0xffffff00, 0x100008ff, 0xffffffff, 0x00100008, 0x0)
    TEST_E_ED(dvstep.u, 6, 0x00000100, 0x00000000, 0x08000001, 0x00000000, \
                           0xffffff2d)

    TEST_PASSFAIL

