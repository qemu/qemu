#include <math.h>
#include <fenv.h>
#include "exec.h"

//#define DEBUG_MMU

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
	env->fsr &= ~(FSR_FCC1 | FSR_FCC0);
	env->fsr |= T0;
	if (env->fsr & FSR_NVM) {
	    raise_exception(TT_FP_EXCP);
	} else {
	    env->fsr |= FSR_NVA;
	}
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
	env->fsr &= ~(FSR_FCC1 | FSR_FCC0);
	env->fsr |= T0;
	if (env->fsr & FSR_NVM) {
	    raise_exception(TT_FP_EXCP);
	} else {
	    env->fsr |= FSR_NVA;
	}
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
    uint32_t ret;

    switch (asi) {
    case 3: /* MMU probe */
	{
	    int mmulev;

	    mmulev = (T0 >> 8) & 15;
	    if (mmulev > 4)
		ret = 0;
	    else {
		ret = mmu_probe(T0, mmulev);
		//bswap32s(&ret);
	    }
#ifdef DEBUG_MMU
	    printf("mmu_probe: 0x%08x (lev %d) -> 0x%08x\n", T0, mmulev, ret);
#endif
	}
	break;
    case 4: /* read MMU regs */
	{
	    int reg = (T0 >> 8) & 0xf;
	    
	    ret = env->mmuregs[reg];
	    if (reg == 3 || reg == 4) /* Fault status, addr cleared on read*/
		env->mmuregs[4] = 0;
	}
	break;
    case 0x20 ... 0x2f: /* MMU passthrough */
	cpu_physical_memory_read(T0, (void *) &ret, size);
	if (size == 4)
	    tswap32s(&ret);
        else if (size == 2)
	    tswap16s((uint16_t *)&ret);
	break;
    default:
	ret = 0;
	break;
    }
    T1 = ret;
}

void helper_st_asi(int asi, int size, int sign)
{
    switch(asi) {
    case 3: /* MMU flush */
	{
	    int mmulev;

	    mmulev = (T0 >> 8) & 15;
	    switch (mmulev) {
	    case 0: // flush page
		tlb_flush_page(cpu_single_env, T0 & 0xfffff000);
		break;
	    case 1: // flush segment (256k)
	    case 2: // flush region (16M)
	    case 3: // flush context (4G)
	    case 4: // flush entire
		tlb_flush(cpu_single_env, 1);
		break;
	    default:
		break;
	    }
	    dump_mmu();
	    return;
	}
    case 4: /* write MMU regs */
	{
	    int reg = (T0 >> 8) & 0xf, oldreg;
	    
	    oldreg = env->mmuregs[reg];
	    if (reg == 0) {
		env->mmuregs[reg] &= ~(MMU_E | MMU_NF);
		env->mmuregs[reg] |= T1 & (MMU_E | MMU_NF);
	    } else
		env->mmuregs[reg] = T1;
	    if (oldreg != env->mmuregs[reg]) {
#if 0
		// XXX: Only if MMU mapping change, we may need to flush?
		tlb_flush(cpu_single_env, 1);
		cpu_loop_exit();
		FORCE_RET();
#endif
	    }
	    dump_mmu();
	    return;
	}
    case 0x17: /* Block copy, sta access */
	{
	    // value (T1) = src
	    // address (T0) = dst
	    // copy 32 bytes
	    int src = T1, dst = T0;
	    uint8_t temp[32];
	    
	    tswap32s(&src);

	    cpu_physical_memory_read(src, (void *) &temp, 32);
	    cpu_physical_memory_write(dst, (void *) &temp, 32);
	}
	return;
    case 0x1f: /* Block fill, stda access */
	{
	    // value (T1, T2)
	    // address (T0) = dst
	    // fill 32 bytes
	    int i, dst = T0;
	    uint64_t val;
	    
	    val = (((uint64_t)T1) << 32) | T2;
	    tswap64s(&val);

	    for (i = 0; i < 32; i += 8, dst += 8) {
		cpu_physical_memory_write(dst, (void *) &val, 8);
	    }
	}
	return;
    case 0x20 ... 0x2f: /* MMU passthrough */
	{
	    int temp = T1;
	    if (size == 4)
		tswap32s(&temp);
	    else if (size == 2)
		tswap16s((uint16_t *)&temp);
	    cpu_physical_memory_write(T0, (void *) &temp, size);
	}
	return;
    default:
	return;
    }
}

void helper_rett()
{
    unsigned int cwp;

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

void cpu_get_fp64(uint64_t *pmant, uint16_t *pexp, double f)
{
    int exptemp;

    *pmant = ldexp(frexp(f, &exptemp), 53);
    *pexp = exptemp;
}

double cpu_put_fp64(uint64_t mant, uint16_t exp)
{
    return ldexp((double) mant, exp - 53);
}

void helper_debug()
{
    env->exception_index = EXCP_DEBUG;
    cpu_loop_exit();
}

void do_wrpsr()
{
    PUT_PSR(env, T0);
}

void do_rdpsr()
{
    T0 = GET_PSR(env);
}
