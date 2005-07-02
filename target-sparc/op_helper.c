#include "exec.h"

//#define DEBUG_MMU

void raise_exception(int tt)
{
    env->exception_index = tt;
    cpu_loop_exit();
}   

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
    FT0 = float32_abs(FT1);
}

#ifdef TARGET_SPARC64
void do_fabsd(void)
{
    DT0 = float64_abs(DT1);
}
#endif

void do_fsqrts(void)
{
    FT0 = float32_sqrt(FT1, &env->fp_status);
}

void do_fsqrtd(void)
{
    DT0 = float64_sqrt(DT1, &env->fp_status);
}

#define FS 0
void do_fcmps (void)
{
    env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);
    if (isnan(FT0) || isnan(FT1)) {
        T0 = (FSR_FCC1 | FSR_FCC0) << FS;
	if (env->fsr & FSR_NVM) {
	    env->fsr |= T0;
	    raise_exception(TT_FP_EXCP);
	} else {
	    env->fsr |= FSR_NVA;
	}
    } else if (FT0 < FT1) {
        T0 = FSR_FCC0 << FS;
    } else if (FT0 > FT1) {
        T0 = FSR_FCC1 << FS;
    } else {
        T0 = 0;
    }
    env->fsr |= T0;
}

void do_fcmpd (void)
{
    env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);
    if (isnan(DT0) || isnan(DT1)) {
        T0 = (FSR_FCC1 | FSR_FCC0) << FS;
	if (env->fsr & FSR_NVM) {
	    env->fsr |= T0;
	    raise_exception(TT_FP_EXCP);
	} else {
	    env->fsr |= FSR_NVA;
	}
    } else if (DT0 < DT1) {
        T0 = FSR_FCC0 << FS;
    } else if (DT0 > DT1) {
        T0 = FSR_FCC1 << FS;
    } else {
        T0 = 0;
    }
    env->fsr |= T0;
}

#ifdef TARGET_SPARC64
#undef FS
#define FS 22
void do_fcmps_fcc1 (void)
{
    env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);
    if (isnan(FT0) || isnan(FT1)) {
        T0 = (FSR_FCC1 | FSR_FCC0) << FS;
	if (env->fsr & FSR_NVM) {
	    env->fsr |= T0;
	    raise_exception(TT_FP_EXCP);
	} else {
	    env->fsr |= FSR_NVA;
	}
    } else if (FT0 < FT1) {
        T0 = FSR_FCC0 << FS;
    } else if (FT0 > FT1) {
        T0 = FSR_FCC1 << FS;
    } else {
        T0 = 0;
    }
    env->fsr |= T0;
}

void do_fcmpd_fcc1 (void)
{
    env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);
    if (isnan(DT0) || isnan(DT1)) {
        T0 = (FSR_FCC1 | FSR_FCC0) << FS;
	if (env->fsr & FSR_NVM) {
	    env->fsr |= T0;
	    raise_exception(TT_FP_EXCP);
	} else {
	    env->fsr |= FSR_NVA;
	}
    } else if (DT0 < DT1) {
        T0 = FSR_FCC0 << FS;
    } else if (DT0 > DT1) {
        T0 = FSR_FCC1 << FS;
    } else {
        T0 = 0;
    }
    env->fsr |= T0;
}

#undef FS
#define FS 24
void do_fcmps_fcc2 (void)
{
    env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);
    if (isnan(FT0) || isnan(FT1)) {
        T0 = (FSR_FCC1 | FSR_FCC0) << FS;
	if (env->fsr & FSR_NVM) {
	    env->fsr |= T0;
	    raise_exception(TT_FP_EXCP);
	} else {
	    env->fsr |= FSR_NVA;
	}
    } else if (FT0 < FT1) {
        T0 = FSR_FCC0 << FS;
    } else if (FT0 > FT1) {
        T0 = FSR_FCC1 << FS;
    } else {
        T0 = 0;
    }
    env->fsr |= T0;
}

void do_fcmpd_fcc2 (void)
{
    env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);
    if (isnan(DT0) || isnan(DT1)) {
        T0 = (FSR_FCC1 | FSR_FCC0) << FS;
	if (env->fsr & FSR_NVM) {
	    env->fsr |= T0;
	    raise_exception(TT_FP_EXCP);
	} else {
	    env->fsr |= FSR_NVA;
	}
    } else if (DT0 < DT1) {
        T0 = FSR_FCC0 << FS;
    } else if (DT0 > DT1) {
        T0 = FSR_FCC1 << FS;
    } else {
        T0 = 0;
    }
    env->fsr |= T0;
}

#undef FS
#define FS 26
void do_fcmps_fcc3 (void)
{
    env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);
    if (isnan(FT0) || isnan(FT1)) {
        T0 = (FSR_FCC1 | FSR_FCC0) << FS;
	if (env->fsr & FSR_NVM) {
	    env->fsr |= T0;
	    raise_exception(TT_FP_EXCP);
	} else {
	    env->fsr |= FSR_NVA;
	}
    } else if (FT0 < FT1) {
        T0 = FSR_FCC0 << FS;
    } else if (FT0 > FT1) {
        T0 = FSR_FCC1 << FS;
    } else {
        T0 = 0;
    }
    env->fsr |= T0;
}

void do_fcmpd_fcc3 (void)
{
    env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);
    if (isnan(DT0) || isnan(DT1)) {
        T0 = (FSR_FCC1 | FSR_FCC0) << FS;
	if (env->fsr & FSR_NVM) {
	    env->fsr |= T0;
	    raise_exception(TT_FP_EXCP);
	} else {
	    env->fsr |= FSR_NVA;
	}
    } else if (DT0 < DT1) {
        T0 = FSR_FCC0 << FS;
    } else if (DT0 > DT1) {
        T0 = FSR_FCC1 << FS;
    } else {
        T0 = 0;
    }
    env->fsr |= T0;
}
#undef FS
#endif

#ifndef TARGET_SPARC64
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
	    if (reg == 3) /* Fault status cleared on read */
		env->mmuregs[reg] = 0;
#ifdef DEBUG_MMU
	    printf("mmu_read: reg[%d] = 0x%08x\n", reg, ret);
#endif
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
#ifdef DEBUG_MMU
	    printf("mmu flush level %d\n", mmulev);
#endif
	    switch (mmulev) {
	    case 0: // flush page
		tlb_flush_page(env, T0 & 0xfffff000);
		break;
	    case 1: // flush segment (256k)
	    case 2: // flush region (16M)
	    case 3: // flush context (4G)
	    case 4: // flush entire
		tlb_flush(env, 1);
		break;
	    default:
		break;
	    }
#ifdef DEBUG_MMU
	    dump_mmu();
#endif
	    return;
	}
    case 4: /* write MMU regs */
	{
	    int reg = (T0 >> 8) & 0xf, oldreg;
	    
	    oldreg = env->mmuregs[reg];
            switch(reg) {
            case 0:
		env->mmuregs[reg] &= ~(MMU_E | MMU_NF);
		env->mmuregs[reg] |= T1 & (MMU_E | MMU_NF);
		// Mappings generated during no-fault mode or MMU
		// disabled mode are invalid in normal mode
                if (oldreg != env->mmuregs[reg])
                    tlb_flush(env, 1);
                break;
            case 2:
		env->mmuregs[reg] = T1;
                if (oldreg != env->mmuregs[reg]) {
                    /* we flush when the MMU context changes because
                       QEMU has no MMU context support */
                    tlb_flush(env, 1);
                }
                break;
            case 3:
            case 4:
                break;
            default:
		env->mmuregs[reg] = T1;
                break;
            }
#ifdef DEBUG_MMU
            if (oldreg != env->mmuregs[reg]) {
                printf("mmu change reg[%d]: 0x%08x -> 0x%08x\n", reg, oldreg, env->mmuregs[reg]);
            }
	    dump_mmu();
#endif
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

#else

void helper_ld_asi(int asi, int size, int sign)
{
    uint64_t ret;

    if (asi < 0x80 && (env->pstate & PS_PRIV) == 0)
	raise_exception(TT_PRIV_INSN);

    switch (asi) {
    case 0x14: // Bypass
    case 0x15: // Bypass, non-cacheable
	{
	    cpu_physical_memory_read(T0, (void *) &ret, size);
	    if (size == 8)
		tswap64s(&ret);
	    if (size == 4)
		tswap32s((uint32_t *)&ret);
	    else if (size == 2)
		tswap16s((uint16_t *)&ret);
	    break;
	}
    case 0x1c: // Bypass LE
    case 0x1d: // Bypass, non-cacheable LE
	// XXX
	break;
    case 0x45: // LSU
	ret = env->lsu;
	break;
    case 0x50: // I-MMU regs
	{
	    int reg = (T0 >> 3) & 0xf;

	    ret = env->immuregs[reg];
	    break;
	}
    case 0x51: // I-MMU 8k TSB pointer
    case 0x52: // I-MMU 64k TSB pointer
    case 0x55: // I-MMU data access
    case 0x56: // I-MMU tag read
	break;
    case 0x58: // D-MMU regs
	{
	    int reg = (T0 >> 3) & 0xf;

	    ret = env->dmmuregs[reg];
	    break;
	}
    case 0x59: // D-MMU 8k TSB pointer
    case 0x5a: // D-MMU 64k TSB pointer
    case 0x5b: // D-MMU data pointer
    case 0x5d: // D-MMU data access
    case 0x5e: // D-MMU tag read
	break;
    case 0x54: // I-MMU data in, WO
    case 0x57: // I-MMU demap, WO
    case 0x5c: // D-MMU data in, WO
    case 0x5f: // D-MMU demap, WO
    default:
	ret = 0;
	break;
    }
    T1 = ret;
}

void helper_st_asi(int asi, int size, int sign)
{
    if (asi < 0x80 && (env->pstate & PS_PRIV) == 0)
	raise_exception(TT_PRIV_INSN);

    switch(asi) {
    case 0x14: // Bypass
    case 0x15: // Bypass, non-cacheable
	{
	    target_ulong temp = T1;
	    if (size == 8)
		tswap64s(&temp);
	    else if (size == 4)
		tswap32s((uint32_t *)&temp);
	    else if (size == 2)
		tswap16s((uint16_t *)&temp);
	    cpu_physical_memory_write(T0, (void *) &temp, size);
	}
	return;
    case 0x1c: // Bypass LE
    case 0x1d: // Bypass, non-cacheable LE
	// XXX
	return;
    case 0x45: // LSU
	{
	    uint64_t oldreg;

	    oldreg = env->lsu;
	    env->lsu = T1 & (DMMU_E | IMMU_E);
	    // Mappings generated during D/I MMU disabled mode are
	    // invalid in normal mode
	    if (oldreg != env->lsu)
		tlb_flush(env, 1);
	    return;
	}
    case 0x50: // I-MMU regs
	{
	    int reg = (T0 >> 3) & 0xf;
	    uint64_t oldreg;
	    
	    oldreg = env->immuregs[reg];
            switch(reg) {
            case 0: // RO
            case 4:
                return;
            case 1: // Not in I-MMU
            case 2:
            case 7:
            case 8:
                return;
            case 3: // SFSR
		if ((T1 & 1) == 0)
		    T1 = 0; // Clear SFSR
                break;
            case 5: // TSB access
            case 6: // Tag access
            default:
                break;
            }
	    env->immuregs[reg] = T1;
#ifdef DEBUG_MMU
            if (oldreg != env->immuregs[reg]) {
                printf("mmu change reg[%d]: 0x%08x -> 0x%08x\n", reg, oldreg, env->immuregs[reg]);
            }
	    dump_mmu();
#endif
	    return;
	}
    case 0x54: // I-MMU data in
	{
	    unsigned int i;

	    // Try finding an invalid entry
	    for (i = 0; i < 64; i++) {
		if ((env->itlb_tte[i] & 0x8000000000000000ULL) == 0) {
		    env->itlb_tag[i] = env->immuregs[6];
		    env->itlb_tte[i] = T1;
		    return;
		}
	    }
	    // Try finding an unlocked entry
	    for (i = 0; i < 64; i++) {
		if ((env->itlb_tte[i] & 0x40) == 0) {
		    env->itlb_tag[i] = env->immuregs[6];
		    env->itlb_tte[i] = T1;
		    return;
		}
	    }
	    // error state?
	    return;
	}
    case 0x55: // I-MMU data access
	{
	    unsigned int i = (T0 >> 3) & 0x3f;

	    env->itlb_tag[i] = env->immuregs[6];
	    env->itlb_tte[i] = T1;
	    return;
	}
    case 0x57: // I-MMU demap
	return;
    case 0x58: // D-MMU regs
	{
	    int reg = (T0 >> 3) & 0xf;
	    uint64_t oldreg;
	    
	    oldreg = env->dmmuregs[reg];
            switch(reg) {
            case 0: // RO
            case 4:
                return;
            case 3: // SFSR
		if ((T1 & 1) == 0) {
		    T1 = 0; // Clear SFSR, Fault address
		    env->dmmuregs[4] = 0;
		}
		env->dmmuregs[reg] = T1;
                break;
            case 1: // Primary context
            case 2: // Secondary context
            case 5: // TSB access
            case 6: // Tag access
            case 7: // Virtual Watchpoint
            case 8: // Physical Watchpoint
            default:
                break;
            }
	    env->dmmuregs[reg] = T1;
#ifdef DEBUG_MMU
            if (oldreg != env->dmmuregs[reg]) {
                printf("mmu change reg[%d]: 0x%08x -> 0x%08x\n", reg, oldreg, env->dmmuregs[reg]);
            }
	    dump_mmu();
#endif
	    return;
	}
    case 0x5c: // D-MMU data in
	{
	    unsigned int i;

	    // Try finding an invalid entry
	    for (i = 0; i < 64; i++) {
		if ((env->dtlb_tte[i] & 0x8000000000000000ULL) == 0) {
		    env->dtlb_tag[i] = env->dmmuregs[6];
		    env->dtlb_tte[i] = T1;
		    return;
		}
	    }
	    // Try finding an unlocked entry
	    for (i = 0; i < 64; i++) {
		if ((env->dtlb_tte[i] & 0x40) == 0) {
		    env->dtlb_tag[i] = env->dmmuregs[6];
		    env->dtlb_tte[i] = T1;
		    return;
		}
	    }
	    // error state?
	    return;
	}
    case 0x5d: // D-MMU data access
	{
	    unsigned int i = (T0 >> 3) & 0x3f;

	    env->dtlb_tag[i] = env->dmmuregs[6];
	    env->dtlb_tte[i] = T1;
	    return;
	}
    case 0x5f: // D-MMU demap
	return;
    case 0x51: // I-MMU 8k TSB pointer, RO
    case 0x52: // I-MMU 64k TSB pointer, RO
    case 0x56: // I-MMU tag read, RO
    case 0x59: // D-MMU 8k TSB pointer, RO
    case 0x5a: // D-MMU 64k TSB pointer, RO
    case 0x5b: // D-MMU data pointer, RO
    case 0x5e: // D-MMU tag read, RO
    default:
	return;
    }
}

#endif

#ifndef TARGET_SPARC64
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
#endif

void helper_ldfsr(void)
{
    int rnd_mode;
    switch (env->fsr & FSR_RD_MASK) {
    case FSR_RD_NEAREST:
        rnd_mode = float_round_nearest_even;
	break;
    default:
    case FSR_RD_ZERO:
        rnd_mode = float_round_to_zero;
	break;
    case FSR_RD_POS:
        rnd_mode = float_round_up;
	break;
    case FSR_RD_NEG:
        rnd_mode = float_round_down;
	break;
    }
    set_float_rounding_mode(rnd_mode, &env->fp_status);
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

#ifndef TARGET_SPARC64
void do_wrpsr()
{
    PUT_PSR(env, T0);
}

void do_rdpsr()
{
    T0 = GET_PSR(env);
}

#else

void do_popc()
{
    T0 = (T1 & 0x5555555555555555ULL) + ((T1 >> 1) & 0x5555555555555555ULL);
    T0 = (T0 & 0x3333333333333333ULL) + ((T0 >> 2) & 0x3333333333333333ULL);
    T0 = (T0 & 0x0f0f0f0f0f0f0f0fULL) + ((T0 >> 4) & 0x0f0f0f0f0f0f0f0fULL);
    T0 = (T0 & 0x00ff00ff00ff00ffULL) + ((T0 >> 8) & 0x00ff00ff00ff00ffULL);
    T0 = (T0 & 0x0000ffff0000ffffULL) + ((T0 >> 16) & 0x0000ffff0000ffffULL);
    T0 = (T0 & 0x00000000ffffffffULL) + ((T0 >> 32) & 0x00000000ffffffffULL);
}
#endif
