/* Test fbstp instruction.  */

#include <stdio.h>
#include <string.h>

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
    return ret;
}
