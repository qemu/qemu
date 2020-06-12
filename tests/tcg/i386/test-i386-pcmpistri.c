/* Test pcmpistri instruction.  */

#include <nmmintrin.h>
#include <stdio.h>

union u {
    __m128i x;
    unsigned char uc[16];
};

union u s0 = { .uc = { 0 } };
union u s1 = { .uc = "abcdefghijklmnop" };
union u s2 = { .uc = "bcdefghijklmnopa" };
union u s3 = { .uc = "bcdefghijklmnab" };

int
main(void)
{
    int ret = 0;
    if (_mm_cmpistri(s0.x, s0.x, 0x4c) != 15) {
        printf("FAIL: pcmpistri test 1\n");
        ret = 1;
    }
    if (_mm_cmpistri(s1.x, s2.x, 0x4c) != 15) {
        printf("FAIL: pcmpistri test 2\n");
        ret = 1;
    }
    if (_mm_cmpistri(s1.x, s3.x, 0x4c) != 16) {
        printf("FAIL: pcmpistri test 3\n");
        ret = 1;
    }
    return ret;
}
