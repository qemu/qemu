#include <assert.h>

static int mem;

static unsigned long test_cmpxchgb(unsigned long orig)
{
  unsigned long ret;
  mem = orig;
  asm("cmpxchgb %b[cmp],%[mem]"
      : [ mem ] "+m"(mem), [ rax ] "=a"(ret)
      : [ cmp ] "r"(0x77), "a"(orig));
  return ret;
}

static unsigned long test_cmpxchgw(unsigned long orig)
{
  unsigned long ret;
  mem = orig;
  asm("cmpxchgw %w[cmp],%[mem]"
      : [ mem ] "+m"(mem), [ rax ] "=a"(ret)
      : [ cmp ] "r"(0x7777), "a"(orig));
  return ret;
}

static unsigned long test_cmpxchgl(unsigned long orig)
{
  unsigned long ret;
  mem = orig;
  asm("cmpxchgl %[cmp],%[mem]"
      : [ mem ] "+m"(mem), [ rax ] "=a"(ret)
      : [ cmp ] "r"(0x77777777u), "a"(orig));
  return ret;
}

int main()
{
  unsigned long test = 0xdeadbeef12345678ull;
  assert(test == test_cmpxchgb(test));
  assert(test == test_cmpxchgw(test));
  assert(test == test_cmpxchgl(test));
  return 0;
}
