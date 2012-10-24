#include "io.h"

int main(void)
{
  long long rs, rt, dspreg, dspresult;

  rs = 0x123456789ABCDEFF;
  rt = 0x123456789ABCDEFF;
  dspresult = 0x03;

  __asm
      ("cmp.eq.pw %1, %2\n\t"
       "rddsp %0\n\t"
       : "=r"(dspreg)
       : "r"(rs), "r"(rt)
      );

  dspreg = ((dspreg >> 24) & 0x03);

  if (dspreg != dspresult) {
    printf("1 cmp.eq.pw error\n");

    return -1;
  }

  rs = 0x123456799ABCDEFe;
  rt = 0x123456789ABCDEFF;
  dspresult = 0x00;

  __asm
      ("cmp.eq.pw %1, %2\n\t"
       "rddsp %0\n\t"
       : "=r"(dspreg)
       : "r"(rs), "r"(rt)
      );

  dspreg = ((dspreg >> 24) & 0x03);

  if (dspreg != dspresult) {
    printf("2 cmp.eq.pw error\n");

    return -1;
  }

  return 0;
}
