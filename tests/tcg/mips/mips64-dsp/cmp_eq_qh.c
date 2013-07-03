#include "io.h"

int main(void)
{
  long long rs, rt, dspreg, dspresult;

  rs = 0x123456789ABCDEF0;
  rt = 0x123456789ABCDEFF;
  dspresult = 0x0E;

  __asm
      ("cmp.eq.qh %1, %2\n\t"
        "rddsp %0\n\t"
        : "=r"(dspreg)
        : "r"(rs), "r"(rt)
       );

  dspreg = ((dspreg >> 24) & 0x0F);

  if (dspreg != dspresult) {
    printf("cmp.eq.qh error\n");

    return -1;
  }

  rs = 0x12355a789A4CD3F0;
  rt = 0x123456789ABCDEFF;
  dspresult = 0x00;

  __asm
      ("cmp.eq.qh %1, %2\n\t"
        "rddsp %0\n\t"
        : "=r"(dspreg)
        : "r"(rs), "r"(rt)
       );

  dspreg = ((dspreg >> 24) & 0x0F);

  if (dspreg != dspresult) {
    printf("cmp.eq.qh error\n");

    return -1;
  }

  return 0;
}
