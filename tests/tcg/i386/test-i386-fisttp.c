/* Test fisttpl and fisttpll instructions.  */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

union u {
    struct { uint64_t sig; uint16_t sign_exp; } s;
    long double ld;
};

volatile union u ld_invalid_1 = { .s = { 1, 1234 } };

int main(void)
{
    int ret = 0;
    int32_t res_32;
    int64_t res_64;
    __asm__ volatile ("fisttpl %0" : "=m" (res_32) : "t" (0x1p100L) : "st");
    if (res_32 != INT32_MIN) {
        printf("FAIL: fisttpl 0x1p100\n");
        ret = 1;
    }
    __asm__ volatile ("fisttpl %0" : "=m" (res_32) : "t" (-0x1p100L) : "st");
    if (res_32 != INT32_MIN) {
        printf("FAIL: fisttpl -0x1p100\n");
        ret = 1;
    }
    __asm__ volatile ("fisttpl %0" : "=m" (res_32) : "t" (__builtin_infl()) :
                      "st");
    if (res_32 != INT32_MIN) {
        printf("FAIL: fisttpl inf\n");
        ret = 1;
    }
    __asm__ volatile ("fisttpl %0" : "=m" (res_32) : "t" (-__builtin_infl()) :
                      "st");
    if (res_32 != INT32_MIN) {
        printf("FAIL: fisttpl -inf\n");
        ret = 1;
    }
    __asm__ volatile ("fisttpl %0" : "=m" (res_32) : "t" (__builtin_nanl("")) :
                      "st");
    if (res_32 != INT32_MIN) {
        printf("FAIL: fisttpl nan\n");
        ret = 1;
    }
    __asm__ volatile ("fisttpl %0" : "=m" (res_32) :
                      "t" (-__builtin_nanl("")) : "st");
    if (res_32 != INT32_MIN) {
        printf("FAIL: fisttpl -nan\n");
        ret = 1;
    }
    __asm__ volatile ("fisttpl %0" : "=m" (res_32) : "t" (ld_invalid_1.ld) :
                      "st");
    if (res_32 != INT32_MIN) {
        printf("FAIL: fisttpl invalid\n");
        ret = 1;
    }
    __asm__ volatile ("fisttpll %0" : "=m" (res_64) : "t" (0x1p100L) : "st");
    if (res_64 != INT64_MIN) {
        printf("FAIL: fisttpll 0x1p100\n");
        ret = 1;
    }
    __asm__ volatile ("fisttpll %0" : "=m" (res_64) : "t" (-0x1p100L) : "st");
    if (res_64 != INT64_MIN) {
        printf("FAIL: fisttpll -0x1p100\n");
        ret = 1;
    }
    __asm__ volatile ("fisttpll %0" : "=m" (res_64) : "t" (__builtin_infl()) :
                      "st");
    if (res_64 != INT64_MIN) {
        printf("FAIL: fisttpll inf\n");
        ret = 1;
    }
    __asm__ volatile ("fisttpll %0" : "=m" (res_64) : "t" (-__builtin_infl()) :
                      "st");
    if (res_64 != INT64_MIN) {
        printf("FAIL: fisttpll -inf\n");
        ret = 1;
    }
    __asm__ volatile ("fisttpll %0" : "=m" (res_64) :
                      "t" (__builtin_nanl("")) : "st");
    if (res_64 != INT64_MIN) {
        printf("FAIL: fisttpll nan\n");
        ret = 1;
    }
    __asm__ volatile ("fisttpll %0" : "=m" (res_64) :
                      "t" (-__builtin_nanl("")) : "st");
    if (res_64 != INT64_MIN) {
        printf("FAIL: fisttpll -nan\n");
        ret = 1;
    }
    __asm__ volatile ("fisttpll %0" : "=m" (res_64) : "t" (ld_invalid_1.ld) :
                      "st");
    if (res_64 != INT64_MIN) {
        printf("FAIL: fisttpll invalid\n");
        ret = 1;
    }
    return ret;
}
