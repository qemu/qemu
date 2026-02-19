/*
 * ARM debug helpers used by TCG
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "helper.h"
#include "internals.h"
#include "cpu-features.h"
#include "cpregs.h"
#include "exec/watchpoint.h"
#include "system/tcg.h"

/* Return the Exception Level targeted by debug exceptions. */
static int arm_debug_target_el(CPUARMState *env)
{
    bool secure = arm_is_secure(env);
    bool route_to_el2 = false;

    if (arm_feature(env, ARM_FEATURE_M)) {
        return 1;
    }

    if (arm_is_el2_enabled(env)) {
        route_to_el2 = env->cp15.hcr_el2 & HCR_TGE ||
                       env->cp15.mdcr_el2 & MDCR_TDE;
    }

    if (route_to_el2) {
        return 2;
    } else if (arm_feature(env, ARM_FEATURE_EL3) &&
               !arm_el_is_aa64(env, 3) && secure) {
        return 3;
    } else {
        return 1;
    }
}

/*
 * Raise an exception to the debug target el.
 * Modify syndrome to indicate when origin and target EL are the same.
 */
static G_NORETURN void
raise_exception_debug(CPUARMState *env, uint32_t excp, uint32_t syndrome)
{
    int debug_el = arm_debug_target_el(env);
    int cur_el = arm_current_el(env);

    /*
     * If singlestep is targeting a lower EL than the current one, then
     * DisasContext.ss_active must be false and we can never get here.
     * Similarly for watchpoint and breakpoint matches.
     */
    assert(debug_el >= cur_el);
    syndrome |= (debug_el == cur_el) << ARM_EL_EC_SHIFT;
    raise_exception(env, excp, syndrome, debug_el);
}

/* See AArch64.GenerateDebugExceptionsFrom() in ARM ARM pseudocode */
static bool aa64_generate_debug_exceptions(CPUARMState *env)
{
    int cur_el = arm_current_el(env);
    int debug_el;

    if (cur_el == 3) {
        return false;
    }

    /* MDCR_EL3.SDD disables debug events from Secure state */
    if (arm_is_secure_below_el3(env)
        && extract32(env->cp15.mdcr_el3, 16, 1)) {
        return false;
    }

    /*
     * Same EL to same EL debug exceptions need MDSCR_KDE enabled
     * while not masking the (D)ebug bit in DAIF.
     */
    debug_el = arm_debug_target_el(env);

    if (cur_el == debug_el) {
        return extract32(env->cp15.mdscr_el1, 13, 1)
            && !(env->daif & PSTATE_D);
    }

    /* Otherwise the debug target needs to be a higher EL */
    return debug_el > cur_el;
}

static bool aa32_generate_debug_exceptions(CPUARMState *env)
{
    int el = arm_current_el(env);

    if (el == 0 && arm_el_is_aa64(env, 1)) {
        return aa64_generate_debug_exceptions(env);
    }

    if (arm_is_secure(env)) {
        int spd;

        if (el == 0 && (env->cp15.sder & 1)) {
            /*
             * SDER.SUIDEN means debug exceptions from Secure EL0
             * are always enabled. Otherwise they are controlled by
             * SDCR.SPD like those from other Secure ELs.
             */
            return true;
        }

        spd = extract32(env->cp15.mdcr_el3, 14, 2);
        switch (spd) {
        case 1:
            /* SPD == 0b01 is reserved, but behaves as 0b00. */
        case 0:
            /*
             * For 0b00 we return true if external secure invasive debug
             * is enabled. On real hardware this is controlled by external
             * signals to the core. QEMU always permits debug, and behaves
             * as if DBGEN, SPIDEN, NIDEN and SPNIDEN are all tied high.
             */
            return true;
        case 2:
            return false;
        case 3:
            return true;
        }
    }

    return el != 2;
}

/*
 * Return true if debugging exceptions are currently enabled.
 * This corresponds to what in ARM ARM pseudocode would be
 *    if UsingAArch32() then
 *        return AArch32.GenerateDebugExceptions()
 *    else
 *        return AArch64.GenerateDebugExceptions()
 * We choose to push the if() down into this function for clarity,
 * since the pseudocode has it at all callsites except for the one in
 * CheckSoftwareStep(), where it is elided because both branches would
 * always return the same value.
 */
bool arm_generate_debug_exceptions(CPUARMState *env)
{
    if ((env->cp15.oslsr_el1 & 1) || (env->cp15.osdlr_el1 & 1)) {
        return false;
    }
    if (is_a64(env)) {
        return aa64_generate_debug_exceptions(env);
    } else {
        return aa32_generate_debug_exceptions(env);
    }
}

/*
 * Is single-stepping active? (Note that the "is EL_D AArch64?" check
 * implicitly means this always returns false in pre-v8 CPUs.)
 */
bool arm_singlestep_active(CPUARMState *env)
{
    return extract32(env->cp15.mdscr_el1, 0, 1)
        && arm_el_is_aa64(env, arm_debug_target_el(env))
        && arm_generate_debug_exceptions(env);
}

/* Return true if the linked breakpoint entry lbn passes its checks */
static bool linked_bp_matches(ARMCPU *cpu, int lbn)
{
    CPUARMState *env = &cpu->env;
    uint64_t bcr = env->cp15.dbgbcr[lbn];
    int brps = arm_num_brps(cpu);
    int ctx_cmps = arm_num_ctx_cmps(cpu);
    int bt;
    uint32_t contextidr;
    uint64_t hcr_el2;

    /*
     * Links to unimplemented or non-context aware breakpoints are
     * CONSTRAINED UNPREDICTABLE: either behave as if disabled, or
     * as if linked to an UNKNOWN context-aware breakpoint (in which
     * case DBGWCR<n>_EL1.LBN must indicate that breakpoint).
     * We choose the former.
     */
    if (lbn >= brps || lbn < (brps - ctx_cmps)) {
        return false;
    }

    bcr = env->cp15.dbgbcr[lbn];

    if (extract64(bcr, 0, 1) == 0) {
        /* Linked breakpoint disabled : generate no events */
        return false;
    }

    bt = extract64(bcr, 20, 4);
    hcr_el2 = arm_hcr_el2_eff(env);

    switch (bt) {
    case 3: /* linked context ID match */
        switch (arm_current_el(env)) {
        default:
            /* Context matches never fire in AArch64 EL3 */
            return false;
        case 2:
            if (!(hcr_el2 & HCR_E2H)) {
                /* Context matches never fire in EL2 without E2H enabled. */
                return false;
            }
            contextidr = env->cp15.contextidr_el[2];
            break;
        case 1:
            contextidr = env->cp15.contextidr_el[1];
            break;
        case 0:
            if ((hcr_el2 & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
                contextidr = env->cp15.contextidr_el[2];
            } else {
                contextidr = env->cp15.contextidr_el[1];
            }
            break;
        }
        break;

    case 7:  /* linked contextidr_el1 match */
        contextidr = env->cp15.contextidr_el[1];
        break;
    case 13: /* linked contextidr_el2 match */
        contextidr = env->cp15.contextidr_el[2];
        break;

    case 9: /* linked VMID match (reserved if no EL2) */
    case 11: /* linked context ID and VMID match (reserved if no EL2) */
    case 15: /* linked full context ID match */
    default:
        /*
         * Links to Unlinked context breakpoints must generate no
         * events; we choose to do the same for reserved values too.
         */
        return false;
    }

    /*
     * We match the whole register even if this is AArch32 using the
     * short descriptor format (in which case it holds both PROCID and ASID),
     * since we don't implement the optional v7 context ID masking.
     */
    return contextidr == (uint32_t)env->cp15.dbgbvr[lbn];
}

static bool bp_wp_matches(ARMCPU *cpu, int n, bool is_wp)
{
    CPUARMState *env = &cpu->env;
    uint64_t cr;
    int pac, hmc, ssc, wt, lbn;
    /*
     * Note that for watchpoints the check is against the CPU security
     * state, not the S/NS attribute on the offending data access.
     */
    bool is_secure = arm_is_secure(env);
    int access_el = arm_current_el(env);

    if (is_wp) {
        CPUWatchpoint *wp = env->cpu_watchpoint[n];

        if (!wp || !(wp->flags & BP_WATCHPOINT_HIT)) {
            return false;
        }
        cr = env->cp15.dbgwcr[n];
        if (wp->hitattrs.user) {
            /*
             * The LDRT/STRT/LDT/STT "unprivileged access" instructions should
             * match watchpoints as if they were accesses done at EL0, even if
             * the CPU is at EL1 or higher.
             */
            access_el = 0;
        }
    } else {
        uint64_t pc = is_a64(env) ? env->pc : env->regs[15];

        if (!env->cpu_breakpoint[n] || env->cpu_breakpoint[n]->pc != pc) {
            return false;
        }
        cr = env->cp15.dbgbcr[n];
    }
    /*
     * The WATCHPOINT_HIT flag guarantees us that the watchpoint is
     * enabled and that the address and access type match; for breakpoints
     * we know the address matched; check the remaining fields, including
     * linked breakpoints. We rely on WCR and BCR having the same layout
     * for the LBN, SSC, HMC, PAC/PMC and is-linked fields.
     * Note that some combinations of {PAC, HMC, SSC} are reserved and
     * must act either like some valid combination or as if the watchpoint
     * were disabled. We choose the former, and use this together with
     * the fact that EL3 must always be Secure and EL2 must always be
     * Non-Secure to simplify the code slightly compared to the full
     * table in the ARM ARM.
     */
    pac = FIELD_EX64(cr, DBGWCR, PAC);
    hmc = FIELD_EX64(cr, DBGWCR, HMC);
    ssc = FIELD_EX64(cr, DBGWCR, SSC);

    switch (ssc) {
    case 0:
        break;
    case 1:
    case 3:
        if (is_secure) {
            return false;
        }
        break;
    case 2:
        if (!is_secure) {
            return false;
        }
        break;
    }

    switch (access_el) {
    case 3:
    case 2:
        if (!hmc) {
            return false;
        }
        break;
    case 1:
        if (extract32(pac, 0, 1) == 0) {
            return false;
        }
        break;
    case 0:
        if (extract32(pac, 1, 1) == 0) {
            return false;
        }
        break;
    default:
        g_assert_not_reached();
    }

    wt = FIELD_EX64(cr, DBGWCR, WT);
    lbn = FIELD_EX64(cr, DBGWCR, LBN);

    if (wt && !linked_bp_matches(cpu, lbn)) {
        return false;
    }

    return true;
}

static bool check_watchpoints(ARMCPU *cpu)
{
    CPUARMState *env = &cpu->env;
    int n;

    /*
     * If watchpoints are disabled globally or we can't take debug
     * exceptions here then watchpoint firings are ignored.
     */
    if (extract32(env->cp15.mdscr_el1, 15, 1) == 0
        || !arm_generate_debug_exceptions(env)) {
        return false;
    }

    for (n = 0; n < ARRAY_SIZE(env->cpu_watchpoint); n++) {
        if (bp_wp_matches(cpu, n, true)) {
            return true;
        }
    }
    return false;
}

bool arm_debug_check_breakpoint(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    vaddr pc;
    int n;

    /*
     * If breakpoints are disabled globally or we can't take debug
     * exceptions here then breakpoint firings are ignored.
     */
    if (extract32(env->cp15.mdscr_el1, 15, 1) == 0
        || !arm_generate_debug_exceptions(env)) {
        return false;
    }

    /*
     * Single-step exceptions have priority over breakpoint exceptions.
     * If single-step state is active-pending, suppress the bp.
     */
    if (arm_singlestep_active(env) && !(env->pstate & PSTATE_SS)) {
        return false;
    }

    /*
     * PC alignment faults have priority over breakpoint exceptions.
     */
    pc = is_a64(env) ? env->pc : env->regs[15];
    if ((is_a64(env) || !env->thumb) && (pc & 3) != 0) {
        return false;
    }

    /*
     * Instruction aborts have priority over breakpoint exceptions.
     * TODO: We would need to look up the page for PC and verify that
     * it is present and executable.
     */

    for (n = 0; n < ARRAY_SIZE(env->cpu_breakpoint); n++) {
        if (bp_wp_matches(cpu, n, false)) {
            return true;
        }
    }
    return false;
}

bool arm_debug_check_watchpoint(CPUState *cs, CPUWatchpoint *wp)
{
    /*
     * Called by core code when a CPU watchpoint fires; need to check if this
     * is also an architectural watchpoint match.
     */
    ARMCPU *cpu = ARM_CPU(cs);

    return check_watchpoints(cpu);
}

/*
 * Return the FSR value for a debug exception (watchpoint, hardware
 * breakpoint or BKPT insn) targeting the specified exception level.
 */
static uint32_t arm_debug_exception_fsr(CPUARMState *env)
{
    ARMMMUFaultInfo fi = { .type = ARMFault_Debug };
    int target_el = arm_debug_target_el(env);
    bool using_lpae;

    if (arm_feature(env, ARM_FEATURE_M)) {
        using_lpae = false;
    } else if (target_el == 2 || arm_el_is_aa64(env, target_el)) {
        using_lpae = true;
    } else if (arm_feature(env, ARM_FEATURE_PMSA) &&
               arm_feature(env, ARM_FEATURE_V8)) {
        using_lpae = true;
    } else if (arm_feature(env, ARM_FEATURE_LPAE) &&
               (env->cp15.tcr_el[target_el] & TTBCR_EAE)) {
        using_lpae = true;
    } else {
        using_lpae = false;
    }

    if (using_lpae) {
        return arm_fi_to_lfsc(&fi);
    } else {
        return arm_fi_to_sfsc(&fi);
    }
}

void arm_debug_excp_handler(CPUState *cs)
{
    /*
     * Called by core code when a watchpoint or breakpoint fires;
     * need to check which one and raise the appropriate exception.
     */
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    CPUWatchpoint *wp_hit = cs->watchpoint_hit;

    if (wp_hit) {
        if (wp_hit->flags & BP_CPU) {
            bool wnr = (wp_hit->flags & BP_WATCHPOINT_HIT_WRITE) != 0;

            cs->watchpoint_hit = NULL;

            env->exception.fsr = arm_debug_exception_fsr(env);
            env->exception.vaddress = wp_hit->hitaddr;
            raise_exception_debug(env, EXCP_DATA_ABORT,
                                  syn_watchpoint(0, 0, wnr));
        }
    } else {
        uint64_t pc = is_a64(env) ? env->pc : env->regs[15];

        /*
         * (1) GDB breakpoints should be handled first.
         * (2) Do not raise a CPU exception if no CPU breakpoint has fired,
         * since singlestep is also done by generating a debug internal
         * exception.
         */
        if (cpu_breakpoint_test(cs, pc, BP_GDB)
            || !cpu_breakpoint_test(cs, pc, BP_CPU)) {
            return;
        }

        env->exception.fsr = arm_debug_exception_fsr(env);
        /*
         * FAR is UNKNOWN: clear vaddress to avoid potentially exposing
         * values to the guest that it shouldn't be able to see at its
         * exception/security level.
         */
        env->exception.vaddress = 0;
        raise_exception_debug(env, EXCP_PREFETCH_ABORT, syn_breakpoint(0));
    }
}

/*
 * Raise an EXCP_BKPT with the specified syndrome register value,
 * targeting the correct exception level for debug exceptions.
 */
void HELPER(exception_bkpt_insn)(CPUARMState *env, uint32_t syndrome)
{
    int debug_el = arm_debug_target_el(env);
    int cur_el = arm_current_el(env);

    /* FSR will only be used if the debug target EL is AArch32. */
    env->exception.fsr = arm_debug_exception_fsr(env);
    /*
     * FAR is UNKNOWN: clear vaddress to avoid potentially exposing
     * values to the guest that it shouldn't be able to see at its
     * exception/security level.
     */
    env->exception.vaddress = 0;
    /*
     * Other kinds of architectural debug exception are ignored if
     * they target an exception level below the current one (in QEMU
     * this is checked by arm_generate_debug_exceptions()). Breakpoint
     * instructions are special because they always generate an exception
     * to somewhere: if they can't go to the configured debug exception
     * level they are taken to the current exception level.
     */
    if (debug_el < cur_el) {
        debug_el = cur_el;
    }
    raise_exception(env, EXCP_BKPT, syndrome, debug_el);
}

void HELPER(exception_swstep)(CPUARMState *env, uint32_t syndrome)
{
    raise_exception_debug(env, EXCP_UDEF, syndrome);
}

void hw_watchpoint_update(ARMCPU *cpu, int n)
{
    CPUARMState *env = &cpu->env;
    vaddr len = 0;
    vaddr wvr = env->cp15.dbgwvr[n];
    uint64_t wcr = env->cp15.dbgwcr[n];
    int mask;
    int flags = BP_CPU | BP_STOP_BEFORE_ACCESS;

    if (env->cpu_watchpoint[n]) {
        cpu_watchpoint_remove_by_ref(CPU(cpu), env->cpu_watchpoint[n]);
        env->cpu_watchpoint[n] = NULL;
    }

    if (!FIELD_EX64(wcr, DBGWCR, E)) {
        /* E bit clear : watchpoint disabled */
        return;
    }

    switch (FIELD_EX64(wcr, DBGWCR, LSC)) {
    case 0:
        /* LSC 00 is reserved and must behave as if the wp is disabled */
        return;
    case 1:
        flags |= BP_MEM_READ;
        break;
    case 2:
        flags |= BP_MEM_WRITE;
        break;
    case 3:
        flags |= BP_MEM_ACCESS;
        break;
    }

    /*
     * Attempts to use both MASK and BAS fields simultaneously are
     * CONSTRAINED UNPREDICTABLE; we opt to ignore BAS in this case,
     * thus generating a watchpoint for every byte in the masked region.
     */
    mask = FIELD_EX64(wcr, DBGWCR, MASK);
    if (mask == 1 || mask == 2) {
        /*
         * Reserved values of MASK; we must act as if the mask value was
         * some non-reserved value, or as if the watchpoint were disabled.
         * We choose the latter.
         */
        return;
    } else if (mask) {
        /* Watchpoint covers an aligned area up to 2GB in size */
        len = 1ULL << mask;
        /*
         * If masked bits in WVR are not zero it's CONSTRAINED UNPREDICTABLE
         * whether the watchpoint fires when the unmasked bits match; we opt
         * to generate the exceptions.
         */
        wvr &= ~(len - 1);
    } else {
        /* Watchpoint covers bytes defined by the byte address select bits */
        int bas = FIELD_EX64(wcr, DBGWCR, BAS);
        int basstart;

        if (extract64(wvr, 2, 1)) {
            /*
             * Deprecated case of an only 4-aligned address. BAS[7:4] are
             * ignored, and BAS[3:0] define which bytes to watch.
             */
            bas &= 0xf;
        }

        if (bas == 0) {
            /* This must act as if the watchpoint is disabled */
            return;
        }

        /*
         * The BAS bits are supposed to be programmed to indicate a contiguous
         * range of bytes. Otherwise it is CONSTRAINED UNPREDICTABLE whether
         * we fire for each byte in the word/doubleword addressed by the WVR.
         * We choose to ignore any non-zero bits after the first range of 1s.
         */
        basstart = ctz32(bas);
        len = cto32(bas >> basstart);
        wvr += basstart;
    }

    cpu_watchpoint_insert(CPU(cpu), wvr, len, flags,
                          &env->cpu_watchpoint[n]);
}

void hw_watchpoint_update_all(ARMCPU *cpu)
{
    int i;
    CPUARMState *env = &cpu->env;

    /*
     * Completely clear out existing QEMU watchpoints and our array, to
     * avoid possible stale entries following migration load.
     */
    cpu_watchpoint_remove_all(CPU(cpu), BP_CPU);
    memset(env->cpu_watchpoint, 0, sizeof(env->cpu_watchpoint));

    for (i = 0; i < ARRAY_SIZE(cpu->env.cpu_watchpoint); i++) {
        hw_watchpoint_update(cpu, i);
    }
}

void hw_breakpoint_update(ARMCPU *cpu, int n)
{
    CPUARMState *env = &cpu->env;
    uint64_t bvr = env->cp15.dbgbvr[n];
    uint64_t bcr = env->cp15.dbgbcr[n];
    vaddr addr;
    int bt;
    int flags = BP_CPU;

    if (env->cpu_breakpoint[n]) {
        cpu_breakpoint_remove_by_ref(CPU(cpu), env->cpu_breakpoint[n]);
        env->cpu_breakpoint[n] = NULL;
    }

    if (!extract64(bcr, 0, 1)) {
        /* E bit clear : watchpoint disabled */
        return;
    }

    bt = extract64(bcr, 20, 4);

    switch (bt) {
    case 4: /* unlinked address mismatch (reserved if AArch64) */
    case 5: /* linked address mismatch (reserved if AArch64) */
        qemu_log_mask(LOG_UNIMP,
                      "arm: address mismatch breakpoint types not implemented\n");
        return;
    case 0: /* unlinked address match */
    case 1: /* linked address match */
    {
        /*
         * Bits [1:0] are RES0.
         *
         * It is IMPLEMENTATION DEFINED whether bits [63:49]
         * ([63:53] for FEAT_LVA) are hardwired to a copy of the sign bit
         * of the VA field ([48] or [52] for FEAT_LVA), or whether the
         * value is read as written.  It is CONSTRAINED UNPREDICTABLE
         * whether the RESS bits are ignored when comparing an address.
         * Therefore we are allowed to compare the entire register, which
         * lets us avoid considering whether FEAT_LVA is actually enabled.
         *
         * The BAS field is used to allow setting breakpoints on 16-bit
         * wide instructions; it is CONSTRAINED UNPREDICTABLE whether
         * a bp will fire if the addresses covered by the bp and the addresses
         * covered by the insn overlap but the insn doesn't start at the
         * start of the bp address range. We choose to require the insn and
         * the bp to have the same address. The constraints on writing to
         * BAS enforced in dbgbcr_write mean we have only four cases:
         *  0b0000  => no breakpoint
         *  0b0011  => breakpoint on addr
         *  0b1100  => breakpoint on addr + 2
         *  0b1111  => breakpoint on addr
         * See also figure D2-3 in the v8 ARM ARM (DDI0487A.c).
         */
        int bas = extract64(bcr, 5, 4);
        addr = bvr & ~3ULL;
        if (bas == 0) {
            return;
        }
        if (bas == 0xc) {
            addr += 2;
        }
        break;
    }
    case 2: /* unlinked context ID match */
    case 8: /* unlinked VMID match (reserved if no EL2) */
    case 10: /* unlinked context ID and VMID match (reserved if no EL2) */
        qemu_log_mask(LOG_UNIMP,
                      "arm: unlinked context breakpoint types not implemented\n");
        return;
    case 9: /* linked VMID match (reserved if no EL2) */
    case 11: /* linked context ID and VMID match (reserved if no EL2) */
    case 3: /* linked context ID match */
    default:
        /*
         * We must generate no events for Linked context matches (unless
         * they are linked to by some other bp/wp, which is handled in
         * updates for the linking bp/wp). We choose to also generate no events
         * for reserved values.
         */
        return;
    }

    cpu_breakpoint_insert(CPU(cpu), addr, flags, &env->cpu_breakpoint[n]);
}

void hw_breakpoint_update_all(ARMCPU *cpu)
{
    int i;
    CPUARMState *env = &cpu->env;

    /*
     * Completely clear out existing QEMU breakpoints and our array, to
     * avoid possible stale entries following migration load.
     */
    cpu_breakpoint_remove_all(CPU(cpu), BP_CPU);
    memset(env->cpu_breakpoint, 0, sizeof(env->cpu_breakpoint));

    for (i = 0; i < ARRAY_SIZE(cpu->env.cpu_breakpoint); i++) {
        hw_breakpoint_update(cpu, i);
    }
}

#if !defined(CONFIG_USER_ONLY)

vaddr arm_adjust_watchpoint_address(CPUState *cs, vaddr addr, int len)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    /*
     * In BE32 system mode, target memory is stored byteswapped (on a
     * little-endian host system), and by the time we reach here (via an
     * opcode helper) the addresses of subword accesses have been adjusted
     * to account for that, which means that watchpoints will not match.
     * Undo the adjustment here.
     */
    if (arm_sctlr_b(env)) {
        if (len == 1) {
            addr ^= 3;
        } else if (len == 2) {
            addr ^= 2;
        }
    }

    return addr;
}

#endif /* !CONFIG_USER_ONLY */
