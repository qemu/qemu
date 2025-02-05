#include <stdio.h>
#include <assert.h>

void test_set_prio();

void inst_test()
{
    asm volatile("dczeroa(r0)\n\t"
                 "dccleanidx(r0)\n\t"
                 "dcinvidx(r0)\n\t"
                 "r1 = dctagr(r0)\n\t"
                 "dctagw(r0, r1)\n\t"
                 "dcfetch(r0)\n\t"
                 "dccleaninvidx(r0)\n\t"
                 "l2gclean\n\t"
                 "l2gclean(r1:0)\n\t"
                 "l2gcleaninv\n\t"
                 "l2gcleaninv(r1:0)\n\t"
                 "l2gunlock\n\t"
                 "l2kill\n\t"
                 "trace(r0)\n\t"
                 "pause(#1)\n\t"
                );

    asm volatile("r0 = #0\n\t"
                 "r1 = iassignr(r0)\n\t"
                 /* Set interrupt 0 to disabled on all threads */
                 "r0 = #0\n\t"
                 "iassignw(r0)\n\t");

    test_set_prio();
    printf("Executed monitor mode instructions\n");
}

int main(int argc, const char *argv[])
{
    inst_test();
    printf("Hello, World: (argc: %d)\n", argc);
    assert(argc >= 1);
    for (int i = 0; i < argc; i++) {
        printf("\t> '%s'\n", argv[i]);
    }
}
