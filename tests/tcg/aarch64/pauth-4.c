#include <stdint.h>
#include <assert.h>

int main()
{
  uintptr_t x, y;

  asm("mov %0, lr\n\t"
      "pacia %0, sp\n\t"        /* sigill if pauth not supported */
      "eor %0, %0, #4\n\t"      /* corrupt single bit */
      "mov %1, %0\n\t"
      "autia %1, sp\n\t"        /* validate corrupted pointer */
      "xpaci %0\n\t"            /* strip pac from corrupted pointer */
      : "=r"(x), "=r"(y));

  /*
   * Once stripped, the corrupted pointer is of the form 0x0000...wxyz.
   * We expect the autia to indicate failure, producing a pointer of the
   * form 0x000e....wxyz.  Use xpaci and != for the test, rather than
   * extracting explicit bits from the top, because the location of the
   * error code "e" depends on the configuration of virtual memory.
   */
  assert(x != y);
  return 0;
}
