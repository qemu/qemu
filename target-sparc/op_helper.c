#include "exec.h"

//#define DEBUG_PCALL
//#define DEBUG_MMU

void raise_exception(int tt)
{
    env->exception_index = tt;
    cpu_loop_exit();
}   

#ifdef USE_INT_TO_FLOAT_HELPERS
void do_fitos(void)
{
    FT0 = int32_to_float32(*((int32_t *)&FT1), &env->fp_status);
}

void do_fitod(void)
{
    DT0 = int32_to_float64(*((int32_t *)&FT1), &env->fp_status);
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

#define GEN_FCMP(name, size, reg1, reg2, FS)                            \
    void glue(do_, name) (void)                                         \
    {                                                                   \
        env->fsr &= ~((FSR_FCC1 | FSR_FCC0) << FS);                     \
        switch (glue(size, _compare) (reg1, reg2, &env->fp_status)) {   \
        case float_relation_unordered:                                  \
            T0 = (FSR_FCC1 | FSR_FCC0) << FS;                           \
            if (env->fsr & FSR_NVM) {                                   \
                env->fsr |= T0;                                         \
                raise_exception(TT_FP_EXCP);                            \
            } else {                                                    \
                env->fsr |= FSR_NVA;                                    \
            }                                                           \
            break;                                                      \
        case float_relation_less:                                       \
            T0 = FSR_FCC0 << FS;                                        \
            break;                                                      \
        case float_relation_greater:                                    \
            T0 = FSR_FCC1 << FS;                                        \
            break;                                                      \
        default:                                                        \
            T0 = 0;                                                     \
            break;                                                      \
        }                                                               \
        env->fsr |= T0;                                                 \
    }

GEN_FCMP(fcmps, float32, FT0, FT1, 0);
GEN_FCMP(fcmpd, float64, DT0, DT1, 0);

#ifdef TARGET_SPARC64
GEN_FCMP(fcmps_fcc1, float32, FT0, FT1, 22);
GEN_FCMP(fcmpd_fcc1, float64, DT0, DT1, 22);

GEN_FCMP(fcmps_fcc2, float32, FT0, FT1, 24);
GEN_FCMP(fcmpd_fcc2, float64, DT0, DT1, 24);

GEN_FCMP(fcmps_fcc3, float32, FT0, FT1, 26);
GEN_FCMP(fcmpd_fcc3, float64, DT0, DT1, 26);
#endif

#if defined(CONFIG_USER_ONLY) 
void helper_ld_asi(int asi, int size, int sign)
{
}

void helper_st_asi(int asi, int size, int sign)
{
}
#else
#ifndef TARGET_SPARC64
void helper_ld_asi(int asi, int size, int sign)
{
    uint32_t ret = 0;

    switch (asi) {
    case 3: /* MMU probe */
	{
	    int mmulev;

	    mmulev = (T0 >> 8) & 15;
	    if (mmulev > 4)
		ret = 0;
	    else {
		ret = mmu_probe(env, T0, mmulev);
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
        switch(size) {
        case 1:
            ret = ldub_phys(T0);
            break;
        case 2:
            ret = lduw_phys(T0 & ~1);
            break;
        default:
        case 4:
            ret = ldl_phys(T0 & ~3);
            break;
        case 8:
	    ret = ldl_phys(T0 & ~3);
	    T0 = ldl_phys((T0 + 4) & ~3);
	    break;
        }
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
	    dump_mmu(env);
#endif
	    return;
	}
    case 4: /* write MMU regs */
	{
	    int reg = (T0 >> 8) & 0xf;
	    uint32_t oldreg;
	    
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
	    dump_mmu(env);
#endif
	    return;
	}
    case 0x17: /* Block copy, sta access */
	{
	    // value (T1) = src
	    // address (T0) = dst
	    // copy 32 bytes
	    uint32_t src = T1, dst = T0;
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
	    int i;
	    uint32_t dst = T0;
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
            switch(size) {
            case 1:
                stb_phys(T0, T1);
                break;
            case 2:
                stw_phys(T0 & ~1, T1);
                break;
            case 4:
            default:
                stl_phys(T0 & ~3, T1);
                break;
            case 8:
                stl_phys(T0 & ~3, T1);
                stl_phys((T0 + 4) & ~3, T2);
                break;
            }
	}
	return;
    default:
	return;
    }
}

#else

void helper_ld_asi(int asi, int size, int sign)
{
    uint64_t ret = 0;

    if (asi < 0x80 && (env->pstate & PS_PRIV) == 0)
	raise_exception(TT_PRIV_ACT);

    switch (asi) {
    case 0x14: // Bypass
    case 0x15: // Bypass, non-cacheable
	{
            switch(size) {
            case 1:
                ret = ldub_phys(T0);
                break;
            case 2:
                ret = lduw_phys(T0 & ~1);
                break;
            case 4:
                ret = ldl_phys(T0 & ~3);
                break;
            default:
            case 8:
                ret = ldq_phys(T0 & ~7);
                break;
            }
	    break;
	}
    case 0x04: // Nucleus
    case 0x0c: // Nucleus Little Endian (LE)
    case 0x10: // As if user primary
    case 0x11: // As if user secondary
    case 0x18: // As if user primary LE
    case 0x19: // As if user secondary LE
    case 0x1c: // Bypass LE
    case 0x1d: // Bypass, non-cacheable LE
    case 0x24: // Nucleus quad LDD 128 bit atomic
    case 0x2c: // Nucleus quad LDD 128 bit atomic
    case 0x4a: // UPA config
    case 0x82: // Primary no-fault
    case 0x83: // Secondary no-fault
    case 0x88: // Primary LE
    case 0x89: // Secondary LE
    case 0x8a: // Primary no-fault LE
    case 0x8b: // Secondary no-fault LE
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
	// XXX
	break;
    case 0x56: // I-MMU tag read
	{
	    unsigned int i;
	    
	    for (i = 0; i < 64; i++) {
		// Valid, ctx match, vaddr match
		if ((env->itlb_tte[i] & 0x8000000000000000ULL) != 0 &&
		    env->itlb_tag[i] == T0) {
		    ret = env->itlb_tag[i];
		    break;
		}
	    }
	    break;
	}
    case 0x58: // D-MMU regs
	{
	    int reg = (T0 >> 3) & 0xf;

	    ret = env->dmmuregs[reg];
	    break;
	}
    case 0x5e: // D-MMU tag read
	{
	    unsigned int i;
	    
	    for (i = 0; i < 64; i++) {
		// Valid, ctx match, vaddr match
		if ((env->dtlb_tte[i] & 0x8000000000000000ULL) != 0 &&
		    env->dtlb_tag[i] == T0) {
		    ret = env->dtlb_tag[i];
		    break;
		}
	    }
	    break;
	}
    case 0x59: // D-MMU 8k TSB pointer
    case 0x5a: // D-MMU 64k TSB pointer
    case 0x5b: // D-MMU data pointer
    case 0x5d: // D-MMU data access
    case 0x48: // Interrupt dispatch, RO
    case 0x49: // Interrupt data receive
    case 0x7f: // Incoming interrupt vector, RO
	// XXX
	break;
    case 0x54: // I-MMU data in, WO
    case 0x57: // I-MMU demap, WO
    case 0x5c: // D-MMU data in, WO
    case 0x5f: // D-MMU demap, WO
    case 0x77: // Interrupt vector, WO
    default:
	ret = 0;
	break;
    }
    T1 = ret;
}

void helper_st_asi(int asi, int size, int sign)
{
    if (asi < 0x80 && (env->pstate & PS_PRIV) == 0)
	raise_exception(TT_PRIV_ACT);

    switch(asi) {
    case 0x14: // Bypass
    case 0x15: // Bypass, non-cacheable
	{
            switch(size) {
            case 1:
                stb_phys(T0, T1);
                break;
            case 2:
                stw_phys(T0 & ~1, T1);
                break;
            case 4:
                stl_phys(T0 & ~3, T1);
                break;
            case 8:
            default:
                stq_phys(T0 & ~7, T1);
                break;
            }
	}
	return;
    case 0x04: // Nucleus
    case 0x0c: // Nucleus Little Endian (LE)
    case 0x10: // As if user primary
    case 0x11: // As if user secondary
    case 0x18: // As if user primary LE
    case 0x19: // As if user secondary LE
    case 0x1c: // Bypass LE
    case 0x1d: // Bypass, non-cacheable LE
    case 0x24: // Nucleus quad LDD 128 bit atomic
    case 0x2c: // Nucleus quad LDD 128 bit atomic
    case 0x4a: // UPA config
    case 0x88: // Primary LE
    case 0x89: // Secondary LE
	// XXX
	return;
    case 0x45: // LSU
	{
	    uint64_t oldreg;

	    oldreg = env->lsu;
	    env->lsu = T1 & (DMMU_E | IMMU_E);
	    // Mappings generated during D/I MMU disabled mode are
	    // invalid in normal mode
	    if (oldreg != env->lsu) {
#ifdef DEBUG_MMU
                printf("LSU change: 0x%" PRIx64 " -> 0x%" PRIx64 "\n", oldreg, env->lsu);
		dump_mmu(env);
#endif
		tlb_flush(env, 1);
	    }
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
                printf("mmu change reg[%d]: 0x%08" PRIx64 " -> 0x%08" PRIx64 "\n", reg, oldreg, env->immuregs[reg]);
            }
	    dump_mmu(env);
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
	// XXX
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
                printf("mmu change reg[%d]: 0x%08" PRIx64 " -> 0x%08" PRIx64 "\n", reg, oldreg, env->dmmuregs[reg]);
            }
	    dump_mmu(env);
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
    case 0x49: // Interrupt data receive
	// XXX
	return;
    case 0x51: // I-MMU 8k TSB pointer, RO
    case 0x52: // I-MMU 64k TSB pointer, RO
    case 0x56: // I-MMU tag read, RO
    case 0x59: // D-MMU 8k TSB pointer, RO
    case 0x5a: // D-MMU 64k TSB pointer, RO
    case 0x5b: // D-MMU data pointer, RO
    case 0x5e: // D-MMU tag read, RO
    case 0x48: // Interrupt dispatch, RO
    case 0x7f: // Incoming interrupt vector, RO
    case 0x82: // Primary no-fault, RO
    case 0x83: // Secondary no-fault, RO
    case 0x8a: // Primary no-fault LE, RO
    case 0x8b: // Secondary no-fault LE, RO
    default:
	return;
    }
}
#endif
#endif /* !CONFIG_USER_ONLY */

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

static inline uint64_t *get_gregset(uint64_t pstate)
{
    switch (pstate) {
    default:
    case 0:
	return env->bgregs;
    case PS_AG:
	return env->agregs;
    case PS_MG:
	return env->mgregs;
    case PS_IG:
	return env->igregs;
    }
}

void do_wrpstate()
{
    uint64_t new_pstate, pstate_regs, new_pstate_regs;
    uint64_t *src, *dst;

    new_pstate = T0 & 0xf3f;
    pstate_regs = env->pstate & 0xc01;
    new_pstate_regs = new_pstate & 0xc01;
    if (new_pstate_regs != pstate_regs) {
	// Switch global register bank
	src = get_gregset(new_pstate_regs);
	dst = get_gregset(pstate_regs);
	memcpy32(dst, env->gregs);
	memcpy32(env->gregs, src);
    }
    env->pstate = new_pstate;
}

void do_done(void)
{
    env->tl--;
    env->pc = env->tnpc[env->tl];
    env->npc = env->tnpc[env->tl] + 4;
    PUT_CCR(env, env->tstate[env->tl] >> 32);
    env->asi = (env->tstate[env->tl] >> 24) & 0xff;
    env->pstate = (env->tstate[env->tl] >> 8) & 0xfff;
    set_cwp(env->tstate[env->tl] & 0xff);
}

void do_retry(void)
{
    env->tl--;
    env->pc = env->tpc[env->tl];
    env->npc = env->tnpc[env->tl];
    PUT_CCR(env, env->tstate[env->tl] >> 32);
    env->asi = (env->tstate[env->tl] >> 24) & 0xff;
    env->pstate = (env->tstate[env->tl] >> 8) & 0xfff;
    set_cwp(env->tstate[env->tl] & 0xff);
}
#endif

void set_cwp(int new_cwp)
{
    /* put the modified wrap registers at their proper location */
    if (env->cwp == (NWINDOWS - 1))
        memcpy32(env->regbase, env->regbase + NWINDOWS * 16);
    env->cwp = new_cwp;
    /* put the wrap registers at their temporary location */
    if (new_cwp == (NWINDOWS - 1))
        memcpy32(env->regbase + NWINDOWS * 16, env->regbase);
    env->regwptr = env->regbase + (new_cwp * 16);
    REGWPTR = env->regwptr;
}

void cpu_set_cwp(CPUState *env1, int new_cwp)
{
    CPUState *saved_env;
#ifdef reg_REGWPTR
    target_ulong *saved_regwptr;
#endif

    saved_env = env;
#ifdef reg_REGWPTR
    saved_regwptr = REGWPTR;
#endif
    env = env1;
    set_cwp(new_cwp);
    env = saved_env;
#ifdef reg_REGWPTR
    REGWPTR = saved_regwptr;
#endif
}

#ifdef TARGET_SPARC64
void do_interrupt(int intno)
{
#ifdef DEBUG_PCALL
    if (loglevel & CPU_LOG_INT) {
	static int count;
	fprintf(logfile, "%6d: v=%04x pc=%016" PRIx64 " npc=%016" PRIx64 " SP=%016" PRIx64 "\n",
                count, intno,
                env->pc,
                env->npc, env->regwptr[6]);
	cpu_dump_state(env, logfile, fprintf, 0);
#if 0
	{
	    int i;
	    uint8_t *ptr;

	    fprintf(logfile, "       code=");
	    ptr = (uint8_t *)env->pc;
	    for(i = 0; i < 16; i++) {
		fprintf(logfile, " %02x", ldub(ptr + i));
	    }
	    fprintf(logfile, "\n");
	}
#endif
	count++;
    }
#endif
#if !defined(CONFIG_USER_ONLY) 
    if (env->tl == MAXTL) {
        cpu_abort(env, "Trap 0x%04x while trap level is MAXTL, Error state", env->exception_index);
	return;
    }
#endif
    env->tstate[env->tl] = ((uint64_t)GET_CCR(env) << 32) | ((env->asi & 0xff) << 24) |
	((env->pstate & 0xfff) << 8) | (env->cwp & 0xff);
    env->tpc[env->tl] = env->pc;
    env->tnpc[env->tl] = env->npc;
    env->tt[env->tl] = intno;
    env->pstate = PS_PEF | PS_PRIV | PS_AG;
    env->tbr &= ~0x7fffULL;
    env->tbr |= ((env->tl > 1) ? 1 << 14 : 0) | (intno << 5);
    if (env->tl < MAXTL - 1) {
	env->tl++;
    } else {
	env->pstate |= PS_RED;
	if (env->tl != MAXTL)
	    env->tl++;
    }
    env->pc = env->tbr;
    env->npc = env->pc + 4;
    env->exception_index = 0;
}
#else
void do_interrupt(int intno)
{
    int cwp;

#ifdef DEBUG_PCALL
    if (loglevel & CPU_LOG_INT) {
	static int count;
	fprintf(logfile, "%6d: v=%02x pc=%08x npc=%08x SP=%08x\n",
                count, intno,
                env->pc,
                env->npc, env->regwptr[6]);
	cpu_dump_state(env, logfile, fprintf, 0);
#if 0
	{
	    int i;
	    uint8_t *ptr;

	    fprintf(logfile, "       code=");
	    ptr = (uint8_t *)env->pc;
	    for(i = 0; i < 16; i++) {
		fprintf(logfile, " %02x", ldub(ptr + i));
	    }
	    fprintf(logfile, "\n");
	}
#endif
	count++;
    }
#endif
#if !defined(CONFIG_USER_ONLY) 
    if (env->psret == 0) {
        cpu_abort(env, "Trap 0x%02x while interrupts disabled, Error state", env->exception_index);
	return;
    }
#endif
    env->psret = 0;
    cwp = (env->cwp - 1) & (NWINDOWS - 1); 
    set_cwp(cwp);
    env->regwptr[9] = env->pc;
    env->regwptr[10] = env->npc;
    env->psrps = env->psrs;
    env->psrs = 1;
    env->tbr = (env->tbr & TBR_BASE_MASK) | (intno << 4);
    env->pc = env->tbr;
    env->npc = env->pc + 4;
    env->exception_index = 0;
}
#endif

#if !defined(CONFIG_USER_ONLY) 

#define MMUSUFFIX _mmu
#define GETPC() (__builtin_return_address(0))

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"


/* try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(target_ulong addr, int is_write, int is_user, void *retaddr)
{
    TranslationBlock *tb;
    int ret;
    unsigned long pc;
    CPUState *saved_env;

    /* XXX: hack to restore env in all cases, even if not called from
       generated code */
    saved_env = env;
    env = cpu_single_env;

    ret = cpu_sparc_handle_mmu_fault(env, addr, is_write, is_user, 1);
    if (ret) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc, (void *)T2);
            }
        }
        cpu_loop_exit();
    }
    env = saved_env;
}

#endif
