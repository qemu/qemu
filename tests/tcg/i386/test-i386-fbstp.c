/* Test fbstp instruction.  */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

union u {
    struct { uint64_t sig; uint16_t sign_exp; } s;
    long double ld;
};

volatile union u ld_invalid_1 = { .s = { 1, 1234 } };
volatile union u ld_invalid_2 = { .s = { 0, 1234 } };
volatile union u ld_invalid_3 = { .s = { 0, 0x7fff } };
volatile union u ld_invalid_4 = { .s = { (UINT64_C(1) << 63) - 1, 0x7fff } };

int main(void)
{
    int ret = 0;
    unsigned char out[10];
    memset(out, 0xfe, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (-0.0L) : "st");
    out[9] &= 0x80;
    if (memcmp(out, "\0\0\0\0\0\0\0\0\0\x80", sizeof out) != 0) {
        printf("FAIL: fbstp -0\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (-0.1L) : "st");
    out[9] &= 0x80;
    if (memcmp(out, "\0\0\0\0\0\0\0\0\0\x80", sizeof out) != 0) {
        printf("FAIL: fbstp -0.1\n");
        ret = 1;
    }
    memset(out, 0x1f, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (-987654321987654321.0L) :
                      "st");
    out[9] &= 0x80;
    if (memcmp(out, "\x21\x43\x65\x87\x19\x32\x54\x76\x98\x80",
               sizeof out) != 0) {
        printf("FAIL: fbstp -987654321987654321\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (999999999999999999.5L) :
                      "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp 999999999999999999.5\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (1000000000000000000.0L) :
                      "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp 1000000000000000000\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (1e30L) : "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp 1e30\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (-999999999999999999.5L) :
                      "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp -999999999999999999.5\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (-1000000000000000000.0L) :
                      "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp -1000000000000000000\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (-1e30L) : "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp -1e30\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (__builtin_infl()) : "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp inf\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (-__builtin_infl()) :
                      "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp -inf\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (__builtin_nanl("")) :
                      "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp nan\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (-__builtin_nanl("")) :
                      "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp -nan\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (ld_invalid_1.ld) :
                      "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp invalid 1\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (ld_invalid_2.ld) :
                      "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp invalid 2\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (ld_invalid_3.ld) :
                      "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp invalid 3\n");
        ret = 1;
    }
    memset(out, 0x12, sizeof out);
    __asm__ volatile ("fbstp %0" : "=m" (out) : "t" (ld_invalid_4.ld) :
                      "st");
    if (memcmp(out, "\0\0\0\0\0\0\0\xc0\xff\xff", sizeof out) != 0) {
        printf("FAIL: fbstp invalid 4\n");
        ret = 1;
    }
    return ret;
}
