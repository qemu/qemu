/* PR rtl-optimization/28634.  On targets with delayed branches,
   dbr_schedule could do the next iteration's addition in the
   branch delay slot, then subtract the value again if the branch
   wasn't taken.  This can lead to rounding errors.  */
int x = -1;
int y = 1;
int
main (void)
{
  while (y > 0)
    y += x;
  if (y != x + 1)
    abort ();
  exit (0);
}
