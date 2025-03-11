/* See if various BMI2 instructions give expected results */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define CC_C 1
#define CC_O (1 << 11)

#ifdef __x86_64__
#define REG uint64_t
#else
#define REG uint32_t
#endif

void test_adox_adcx(uint32_t in_c, uint32_t in_o, REG adcx_operand, REG adox_operand)
{
    REG flags;
    REG out_adcx, out_adox;

    asm("pushf; pop %0" : "=r"(flags));
    flags &= ~(CC_C | CC_O);
    flags |= (in_c ? CC_C : 0);
    flags |= (in_o ? CC_O : 0);

    out_adcx = adcx_operand;
    out_adox = adox_operand;
    asm("push %0; popf;"
        "adox %3, %2;"
        "adcx %3, %1;"
        "pushf; pop %0"
        : "+r" (flags), "+r" (out_adcx), "+r" (out_adox)
        : "r" ((REG) - 1), "0" (flags), "1" (out_adcx), "2" (out_adox));

    assert(out_adcx == in_c + adcx_operand - 1);
    assert(out_adox == in_o + adox_operand - 1);
    assert(!!(flags & CC_C) == (in_c || adcx_operand));
    assert(!!(flags & CC_O) == (in_o || adox_operand));
}

void test_adcx_adox(uint32_t in_c, uint32_t in_o, REG adcx_operand, REG adox_operand)
{
    REG flags;
    REG out_adcx, out_adox;

    asm("pushf; pop %0" : "=r"(flags));
    flags &= ~(CC_C | CC_O);
    flags |= (in_c ? CC_C : 0);
    flags |= (in_o ? CC_O : 0);

    out_adcx = adcx_operand;
    out_adox = adox_operand;
    asm("push %0; popf;"
        "adcx %3, %1;"
        "adox %3, %2;"
        "pushf; pop %0"
        : "+r"(flags), "+r"(out_adcx), "+r"(out_adox)
        : "r" ((REG)-1));

    assert(out_adcx == in_c + adcx_operand - 1);
    assert(out_adox == in_o + adox_operand - 1);
    assert(!!(flags & CC_C) == (in_c || adcx_operand));
    assert(!!(flags & CC_O) == (in_o || adox_operand));
}

int main(int argc, char *argv[]) {
    /* try all combinations of input CF, input OF, CF from op1+op2,  OF from op2+op1 */
    int i;
    for (i = 0; i <= 15; i++) {
        printf("%d\n", i);
        test_adcx_adox(!!(i & 1), !!(i & 2), !!(i & 4), !!(i & 8));
        test_adox_adcx(!!(i & 1), !!(i & 2), !!(i & 4), !!(i & 8));
    }
    return 0;
}

