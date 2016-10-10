#ifndef CRISUTILS_H
#define CRISUTILS_H 1

static char *tst_cc_loc = NULL;

#define cris_tst_cc_init() \
do { tst_cc_loc = "test_cc failed at " CURRENT_LOCATION; } while(0)

/* We need a real symbol to signal error.  */
void _err(void) {
	if (!tst_cc_loc)
		tst_cc_loc = "tst_cc_failed\n";
	_fail(tst_cc_loc);
}

static always_inline void cris_tst_cc_n1(void)
{
	asm volatile ("bpl _err\n"
		      "nop\n");
}
static always_inline void cris_tst_cc_n0(void)
{
	asm volatile ("bmi _err\n"
		      "nop\n");
}

static always_inline void cris_tst_cc_z1(void)
{
	asm volatile ("bne _err\n"
		      "nop\n");
}
static always_inline void cris_tst_cc_z0(void)
{
	asm volatile ("beq _err\n"
		      "nop\n");
}
static always_inline void cris_tst_cc_v1(void)
{
	asm volatile ("bvc _err\n"
		      "nop\n");
}
static always_inline void cris_tst_cc_v0(void)
{
	asm volatile ("bvs _err\n"
		      "nop\n");
}

static always_inline void cris_tst_cc_c1(void)
{
	asm volatile ("bcc _err\n"
		      "nop\n");
}
static always_inline void cris_tst_cc_c0(void)
{
	asm volatile ("bcs _err\n"
		      "nop\n");
}

static always_inline void cris_tst_mov_cc(int n, int z)
{
	if (n) cris_tst_cc_n1(); else cris_tst_cc_n0();
	if (z) cris_tst_cc_z1(); else cris_tst_cc_z0();
	asm volatile ("" : : "g" (_err));
}

static always_inline void cris_tst_cc(const int n, const int z,
			       const int v, const int c)
{
	if (n) cris_tst_cc_n1(); else cris_tst_cc_n0();
	if (z) cris_tst_cc_z1(); else cris_tst_cc_z0();
	if (v) cris_tst_cc_v1(); else cris_tst_cc_v0();
	if (c) cris_tst_cc_c1(); else cris_tst_cc_c0();
	asm volatile ("" : : "g" (_err));
}

#endif
