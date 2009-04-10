static long test_subqv (long a, long b)
{
  long res;

  asm ("subq/v %1,%2,%0"
       : "=r" (res) : "r" (a), "r" (b));
  return res;
}
static struct {
  long (*func)(long, long);
  long a;
  long b;
  long r;
} vectors[] =
  {
    {test_subqv, 0, 0x7d54000, 0xfffffffff82ac000L}
  };

int main (void)
{
  int i;

  for (i = 0; i < sizeof (vectors)/sizeof(vectors[0]); i++)
    if ((*vectors[i].func)(vectors[i].a, vectors[i].b) != vectors[i].r) {
      write(1, "Failed\n", 7);
    }
  write(1, "OK\n", 3);
  return 0;
}
