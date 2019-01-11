#include <unistd.h>

#ifdef TEST_CMOV

#define TEST_COND(N) 				\
int test_##N (long a)				\
{						\
  int res = 1;					\
                                                \
  asm ("cmov"#N" %1,$31,%0"			\
       : "+r" (res) : "r" (a));			\
  return !res;					\
}

#else

#define TEST_COND(N) 				\
int test_##N (long a)				\
{						\
  int res = 1;					\
                                                \
  asm ("b"#N" %1,1f\n\t"			\
       "addq $31,$31,%0\n\t"			\
       "1: unop\n"				\
       : "+r" (res) : "r" (a));			\
  return res;					\
}

#endif

TEST_COND(eq)
TEST_COND(ne)
TEST_COND(ge)
TEST_COND(gt)
TEST_COND(lbc)
TEST_COND(lbs)
TEST_COND(le)
TEST_COND(lt)

static struct {
  int (*func)(long);
  long v;
  int r;
} vectors[] =
  {
    {test_eq, 0, 1},
    {test_eq, 1, 0},

    {test_ne, 0, 0},
    {test_ne, 1, 1},

    {test_ge, 0, 1},
    {test_ge, 1, 1},
    {test_ge, -1, 0},

    {test_gt, 0, 0},
    {test_gt, 1, 1},
    {test_gt, -1, 0},

    {test_lbc, 0, 1},
    {test_lbc, 1, 0},
    {test_lbc, -1, 0},

    {test_lbs, 0, 0},
    {test_lbs, 1, 1},
    {test_lbs, -1, 1},

    {test_le, 0, 1},
    {test_le, 1, 0},
    {test_le, -1, 1},

    {test_lt, 0, 0},
    {test_lt, 1, 0},
    {test_lt, -1, 1},
  };

int main (void)
{
  int i;

  for (i = 0; i < sizeof (vectors)/sizeof(vectors[0]); i++)
    if ((*vectors[i].func)(vectors[i].v) != vectors[i].r) {
      write(1, "Failed\n", 7);
      return 1;
    }
  write(1, "OK\n", 3);
  return 0;
}
