#include <math.h>
#include <fenv.h>
#include "exec.h"

#ifdef USE_INT_TO_FLOAT_HELPERS
void do_fitos(void)
{
    FT0 = (float) *((int32_t *)&FT1);
}

void do_fitod(void)
{
    DT0 = (double) *((int32_t *)&FT1);
}
#endif

void do_fabss(void)
{
    FT0 = fabsf(FT1);
}

void do_fsqrts(void)
{
    FT0 = sqrtf(FT1);
}

void do_fsqrtd(void)
{
    DT0 = sqrt(DT1);
}

void do_fcmps (void)
{
    if (isnan(FT0) || isnan(FT1)) {
        T0 = FSR_FCC1 | FSR_FCC0;
    } else if (FT0 < FT1) {
        T0 = FSR_FCC0;
    } else if (FT0 > FT1) {
        T0 = FSR_FCC1;
    } else {
        T0 = 0;
    }
    env->fsr = T0;
}

void do_fcmpd (void)
{
    if (isnan(DT0) || isnan(DT1)) {
        T0 = FSR_FCC1 | FSR_FCC0;
    } else if (DT0 < DT1) {
        T0 = FSR_FCC0;
    } else if (DT0 > DT1) {
        T0 = FSR_FCC1;
    } else {
        T0 = 0;
    }
    env->fsr = T0;
}

void helper_ld_asi(int asi, int size, int sign)
{
    switch(asi) {
    case 3: /* MMU probe */
	T1 = 0;
	return;
    case 4: /* read MMU regs */
	{
	    int temp, reg = (T0 >> 8) & 0xf;
	    
	    temp = env->mmuregs[reg];
	    if (reg == 3 || reg == 4) /* Fault status, addr cleared on read*/
		env->mmuregs[reg] = 0;
	    T1 = temp;
	}
	return;
    case 0x20 ... 0x2f: /* MMU passthrough */
	{
	    int temp;
	    
	    cpu_physical_memory_read(T0, (void *) &temp, size);
	    bswap32s(&temp);
	    T1 = temp;
	}
	return;
    default:
	T1 = 0;
	return;
    }
}

void helper_st_asi(int asi, int size, int sign)
{
    switch(asi) {
    case 3: /* MMU flush */
	return;
    case 4: /* write MMU regs */
	{
	    int reg = (T0 >> 8) & 0xf;
	    if (reg == 0) {
		env->mmuregs[reg] &= ~(MMU_E | MMU_NF);
		env->mmuregs[reg] |= T1 & (MMU_E | MMU_NF);
	    } else
		env->mmuregs[reg] = T1;
	    return;
	}
    case 0x20 ... 0x2f: /* MMU passthrough */
	{
	    int temp = T1;
	    
	    bswap32s(&temp);
	    cpu_physical_memory_write(T0, (void *) &temp, size);
	}
	return;
    default:
	return;
    }
}

#if 0
void do_ldd_raw(uint32_t addr)
{
    T1 = ldl_raw((void *) addr);
    T0 = ldl_raw((void *) (addr + 4));
}

#if !defined(CONFIG_USER_ONLY)
void do_ldd_user(uint32_t addr)
{
    T1 = ldl_user((void *) addr);
    T0 = ldl_user((void *) (addr + 4));
}
void do_ldd_kernel(uint32_t addr)
{
    T1 = ldl_kernel((void *) addr);
    T0 = ldl_kernel((void *) (addr + 4));
}
#endif
#endif

void helper_rett()
{
    int cwp;
    env->psret = 1;
    cwp = (env->cwp + 1) & (NWINDOWS - 1); 
    if (env->wim & (1 << cwp)) {
        raise_exception(TT_WIN_UNF);
    }
    set_cwp(cwp);
    env->psrs = env->psrps;
}

void helper_ldfsr(void)
{
    switch (env->fsr & FSR_RD_MASK) {
    case FSR_RD_NEAREST:
	fesetround(FE_TONEAREST);
	break;
    case FSR_RD_ZERO:
	fesetround(FE_TOWARDZERO);
	break;
    case FSR_RD_POS:
	fesetround(FE_UPWARD);
	break;
    case FSR_RD_NEG:
	fesetround(FE_DOWNWARD);
	break;
    }
}
