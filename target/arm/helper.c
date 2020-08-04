/*
 * ARM generic helpers.
 *
 * This code is licensed under the GNU GPL v2 or later.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "target/arm/idau.h"
#include "trace.h"
#include "cpu.h"
#include "internals.h"
#include "exec/gdbstub.h"
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"
#include "qemu/main-loop.h"
#include "qemu/bitops.h"
#include "qemu/crc32c.h"
#include "qemu/qemu-print.h"
#include "exec/exec-all.h"
#include <zlib.h> /* For crc32 */
#include "hw/irq.h"
#include "hw/semihosting/semihost.h"
#include "sysemu/cpus.h"
#include "sysemu/kvm.h"
#include "sysemu/tcg.h"
#include "qemu/range.h"
#include "qapi/qapi-commands-machine-target.h"
#include "qapi/error.h"
#include "qemu/guest-random.h"
#ifdef CONFIG_TCG
#include "arm_ldst.h"
#include "exec/cpu_ldst.h"
#endif

#define ARM_CPU_FREQ 1000000000 /* FIXME: 1 GHz, should be configurable */

#ifndef CONFIG_USER_ONLY

static bool get_phys_addr_lpae(CPUARMState *env, target_ulong address,
                               MMUAccessType access_type, ARMMMUIdx mmu_idx,
                               bool s1_is_el0,
                               hwaddr *phys_ptr, MemTxAttrs *txattrs, int *prot,
                               target_ulong *page_size_ptr,
                               ARMMMUFaultInfo *fi, ARMCacheAttrs *cacheattrs)
    __attribute__((nonnull));
#endif

static void switch_mode(CPUARMState *env, int mode);

static int vfp_gdb_get_reg(CPUARMState *env, GByteArray *buf, int reg)
{
    ARMCPU *cpu = env_archcpu(env);
    int nregs = cpu_isar_feature(aa32_simd_r32, cpu) ? 32 : 16;

    /* VFP data registers are always little-endian.  */
    if (reg < nregs) {
        return gdb_get_reg64(buf, *aa32_vfp_dreg(env, reg));
    }
    if (arm_feature(env, ARM_FEATURE_NEON)) {
        /* Aliases for Q regs.  */
        nregs += 16;
        if (reg < nregs) {
            uint64_t *q = aa32_vfp_qreg(env, reg - 32);
            return gdb_get_reg128(buf, q[0], q[1]);
        }
    }
    switch (reg - nregs) {
    case 0: return gdb_get_reg32(buf, env->vfp.xregs[ARM_VFP_FPSID]); break;
    case 1: return gdb_get_reg32(buf, vfp_get_fpscr(env)); break;
    case 2: return gdb_get_reg32(buf, env->vfp.xregs[ARM_VFP_FPEXC]); break;
    }
    return 0;
}

static int vfp_gdb_set_reg(CPUARMState *env, uint8_t *buf, int reg)
{
    ARMCPU *cpu = env_archcpu(env);
    int nregs = cpu_isar_feature(aa32_simd_r32, cpu) ? 32 : 16;

    if (reg < nregs) {
        *aa32_vfp_dreg(env, reg) = ldq_le_p(buf);
        return 8;
    }
    if (arm_feature(env, ARM_FEATURE_NEON)) {
        nregs += 16;
        if (reg < nregs) {
            uint64_t *q = aa32_vfp_qreg(env, reg - 32);
            q[0] = ldq_le_p(buf);
            q[1] = ldq_le_p(buf + 8);
            return 16;
        }
    }
    switch (reg - nregs) {
    case 0: env->vfp.xregs[ARM_VFP_FPSID] = ldl_p(buf); return 4;
    case 1: vfp_set_fpscr(env, ldl_p(buf)); return 4;
    case 2: env->vfp.xregs[ARM_VFP_FPEXC] = ldl_p(buf) & (1 << 30); return 4;
    }
    return 0;
}

static int aarch64_fpu_gdb_get_reg(CPUARMState *env, GByteArray *buf, int reg)
{
    switch (reg) {
    case 0 ... 31:
    {
        /* 128 bit FP register - quads are in LE order */
        uint64_t *q = aa64_vfp_qreg(env, reg);
        return gdb_get_reg128(buf, q[1], q[0]);
    }
    case 32:
        /* FPSR */
        return gdb_get_reg32(buf, vfp_get_fpsr(env));
    case 33:
        /* FPCR */
        return gdb_get_reg32(buf,vfp_get_fpcr(env));
    default:
        return 0;
    }
}

static int aarch64_fpu_gdb_set_reg(CPUARMState *env, uint8_t *buf, int reg)
{
    switch (reg) {
    case 0 ... 31:
        /* 128 bit FP register */
        {
            uint64_t *q = aa64_vfp_qreg(env, reg);
            q[0] = ldq_le_p(buf);
            q[1] = ldq_le_p(buf + 8);
            return 16;
        }
    case 32:
        /* FPSR */
        vfp_set_fpsr(env, ldl_p(buf));
        return 4;
    case 33:
        /* FPCR */
        vfp_set_fpcr(env, ldl_p(buf));
        return 4;
    default:
        return 0;
    }
}

static uint64_t raw_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    assert(ri->fieldoffset);
    if (cpreg_field_is_64bit(ri)) {
        return CPREG_FIELD64(env, ri);
    } else {
        return CPREG_FIELD32(env, ri);
    }
}

static void raw_write(CPUARMState *env, const ARMCPRegInfo *ri,
                      uint64_t value)
{
    assert(ri->fieldoffset);
    if (cpreg_field_is_64bit(ri)) {
        CPREG_FIELD64(env, ri) = value;
    } else {
        CPREG_FIELD32(env, ri) = value;
    }
}

static void *raw_ptr(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return (char *)env + ri->fieldoffset;
}

uint64_t read_raw_cp_reg(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* Raw read of a coprocessor register (as needed for migration, etc). */
    if (ri->type & ARM_CP_CONST) {
        return ri->resetvalue;
    } else if (ri->raw_readfn) {
        return ri->raw_readfn(env, ri);
    } else if (ri->readfn) {
        return ri->readfn(env, ri);
    } else {
        return raw_read(env, ri);
    }
}

static void write_raw_cp_reg(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t v)
{
    /* Raw write of a coprocessor register (as needed for migration, etc).
     * Note that constant registers are treated as write-ignored; the
     * caller should check for success by whether a readback gives the
     * value written.
     */
    if (ri->type & ARM_CP_CONST) {
        return;
    } else if (ri->raw_writefn) {
        ri->raw_writefn(env, ri, v);
    } else if (ri->writefn) {
        ri->writefn(env, ri, v);
    } else {
        raw_write(env, ri, v);
    }
}

/**
 * arm_get/set_gdb_*: get/set a gdb register
 * @env: the CPU state
 * @buf: a buffer to copy to/from
 * @reg: register number (offset from start of group)
 *
 * We return the number of bytes copied
 */

static int arm_gdb_get_sysreg(CPUARMState *env, GByteArray *buf, int reg)
{
    ARMCPU *cpu = env_archcpu(env);
    const ARMCPRegInfo *ri;
    uint32_t key;

    key = cpu->dyn_sysreg_xml.data.cpregs.keys[reg];
    ri = get_arm_cp_reginfo(cpu->cp_regs, key);
    if (ri) {
        if (cpreg_field_is_64bit(ri)) {
            return gdb_get_reg64(buf, (uint64_t)read_raw_cp_reg(env, ri));
        } else {
            return gdb_get_reg32(buf, (uint32_t)read_raw_cp_reg(env, ri));
        }
    }
    return 0;
}

static int arm_gdb_set_sysreg(CPUARMState *env, uint8_t *buf, int reg)
{
    return 0;
}

#ifdef TARGET_AARCH64
static int arm_gdb_get_svereg(CPUARMState *env, GByteArray *buf, int reg)
{
    ARMCPU *cpu = env_archcpu(env);

    switch (reg) {
    /* The first 32 registers are the zregs */
    case 0 ... 31:
    {
        int vq, len = 0;
        for (vq = 0; vq < cpu->sve_max_vq; vq++) {
            len += gdb_get_reg128(buf,
                                  env->vfp.zregs[reg].d[vq * 2 + 1],
                                  env->vfp.zregs[reg].d[vq * 2]);
        }
        return len;
    }
    case 32:
        return gdb_get_reg32(buf, vfp_get_fpsr(env));
    case 33:
        return gdb_get_reg32(buf, vfp_get_fpcr(env));
    /* then 16 predicates and the ffr */
    case 34 ... 50:
    {
        int preg = reg - 34;
        int vq, len = 0;
        for (vq = 0; vq < cpu->sve_max_vq; vq = vq + 4) {
            len += gdb_get_reg64(buf, env->vfp.pregs[preg].p[vq / 4]);
        }
        return len;
    }
    case 51:
    {
        /*
         * We report in Vector Granules (VG) which is 64bit in a Z reg
         * while the ZCR works in Vector Quads (VQ) which is 128bit chunks.
         */
        int vq = sve_zcr_len_for_el(env, arm_current_el(env)) + 1;
        return gdb_get_reg32(buf, vq * 2);
    }
    default:
        /* gdbstub asked for something out our range */
        qemu_log_mask(LOG_UNIMP, "%s: out of range register %d", __func__, reg);
        break;
    }

    return 0;
}

static int arm_gdb_set_svereg(CPUARMState *env, uint8_t *buf, int reg)
{
    ARMCPU *cpu = env_archcpu(env);

    /* The first 32 registers are the zregs */
    switch (reg) {
    /* The first 32 registers are the zregs */
    case 0 ... 31:
    {
        int vq, len = 0;
        uint64_t *p = (uint64_t *) buf;
        for (vq = 0; vq < cpu->sve_max_vq; vq++) {
            env->vfp.zregs[reg].d[vq * 2 + 1] = *p++;
            env->vfp.zregs[reg].d[vq * 2] = *p++;
            len += 16;
        }
        return len;
    }
    case 32:
        vfp_set_fpsr(env, *(uint32_t *)buf);
        return 4;
    case 33:
        vfp_set_fpcr(env, *(uint32_t *)buf);
        return 4;
    case 34 ... 50:
    {
        int preg = reg - 34;
        int vq, len = 0;
        uint64_t *p = (uint64_t *) buf;
        for (vq = 0; vq < cpu->sve_max_vq; vq = vq + 4) {
            env->vfp.pregs[preg].p[vq / 4] = *p++;
            len += 8;
        }
        return len;
    }
    case 51:
        /* cannot set vg via gdbstub */
        return 0;
    default:
        /* gdbstub asked for something out our range */
        break;
    }

    return 0;
}
#endif /* TARGET_AARCH64 */

static bool raw_accessors_invalid(const ARMCPRegInfo *ri)
{
   /* Return true if the regdef would cause an assertion if you called
    * read_raw_cp_reg() or write_raw_cp_reg() on it (ie if it is a
    * program bug for it not to have the NO_RAW flag).
    * NB that returning false here doesn't necessarily mean that calling
    * read/write_raw_cp_reg() is safe, because we can't distinguish "has
    * read/write access functions which are safe for raw use" from "has
    * read/write access functions which have side effects but has forgotten
    * to provide raw access functions".
    * The tests here line up with the conditions in read/write_raw_cp_reg()
    * and assertions in raw_read()/raw_write().
    */
    if ((ri->type & ARM_CP_CONST) ||
        ri->fieldoffset ||
        ((ri->raw_writefn || ri->writefn) && (ri->raw_readfn || ri->readfn))) {
        return false;
    }
    return true;
}

bool write_cpustate_to_list(ARMCPU *cpu, bool kvm_sync)
{
    /* Write the coprocessor state from cpu->env to the (index,value) list. */
    int i;
    bool ok = true;

    for (i = 0; i < cpu->cpreg_array_len; i++) {
        uint32_t regidx = kvm_to_cpreg_id(cpu->cpreg_indexes[i]);
        const ARMCPRegInfo *ri;
        uint64_t newval;

        ri = get_arm_cp_reginfo(cpu->cp_regs, regidx);
        if (!ri) {
            ok = false;
            continue;
        }
        if (ri->type & ARM_CP_NO_RAW) {
            continue;
        }

        newval = read_raw_cp_reg(&cpu->env, ri);
        if (kvm_sync) {
            /*
             * Only sync if the previous list->cpustate sync succeeded.
             * Rather than tracking the success/failure state for every
             * item in the list, we just recheck "does the raw write we must
             * have made in write_list_to_cpustate() read back OK" here.
             */
            uint64_t oldval = cpu->cpreg_values[i];

            if (oldval == newval) {
                continue;
            }

            write_raw_cp_reg(&cpu->env, ri, oldval);
            if (read_raw_cp_reg(&cpu->env, ri) != oldval) {
                continue;
            }

            write_raw_cp_reg(&cpu->env, ri, newval);
        }
        cpu->cpreg_values[i] = newval;
    }
    return ok;
}

bool write_list_to_cpustate(ARMCPU *cpu)
{
    int i;
    bool ok = true;

    for (i = 0; i < cpu->cpreg_array_len; i++) {
        uint32_t regidx = kvm_to_cpreg_id(cpu->cpreg_indexes[i]);
        uint64_t v = cpu->cpreg_values[i];
        const ARMCPRegInfo *ri;

        ri = get_arm_cp_reginfo(cpu->cp_regs, regidx);
        if (!ri) {
            ok = false;
            continue;
        }
        if (ri->type & ARM_CP_NO_RAW) {
            continue;
        }
        /* Write value and confirm it reads back as written
         * (to catch read-only registers and partially read-only
         * registers where the incoming migration value doesn't match)
         */
        write_raw_cp_reg(&cpu->env, ri, v);
        if (read_raw_cp_reg(&cpu->env, ri) != v) {
            ok = false;
        }
    }
    return ok;
}

static void add_cpreg_to_list(gpointer key, gpointer opaque)
{
    ARMCPU *cpu = opaque;
    uint64_t regidx;
    const ARMCPRegInfo *ri;

    regidx = *(uint32_t *)key;
    ri = get_arm_cp_reginfo(cpu->cp_regs, regidx);

    if (!(ri->type & (ARM_CP_NO_RAW|ARM_CP_ALIAS))) {
        cpu->cpreg_indexes[cpu->cpreg_array_len] = cpreg_to_kvm_id(regidx);
        /* The value array need not be initialized at this point */
        cpu->cpreg_array_len++;
    }
}

static void count_cpreg(gpointer key, gpointer opaque)
{
    ARMCPU *cpu = opaque;
    uint64_t regidx;
    const ARMCPRegInfo *ri;

    regidx = *(uint32_t *)key;
    ri = get_arm_cp_reginfo(cpu->cp_regs, regidx);

    if (!(ri->type & (ARM_CP_NO_RAW|ARM_CP_ALIAS))) {
        cpu->cpreg_array_len++;
    }
}

static gint cpreg_key_compare(gconstpointer a, gconstpointer b)
{
    uint64_t aidx = cpreg_to_kvm_id(*(uint32_t *)a);
    uint64_t bidx = cpreg_to_kvm_id(*(uint32_t *)b);

    if (aidx > bidx) {
        return 1;
    }
    if (aidx < bidx) {
        return -1;
    }
    return 0;
}

void init_cpreg_list(ARMCPU *cpu)
{
    /* Initialise the cpreg_tuples[] array based on the cp_regs hash.
     * Note that we require cpreg_tuples[] to be sorted by key ID.
     */
    GList *keys;
    int arraylen;

    keys = g_hash_table_get_keys(cpu->cp_regs);
    keys = g_list_sort(keys, cpreg_key_compare);

    cpu->cpreg_array_len = 0;

    g_list_foreach(keys, count_cpreg, cpu);

    arraylen = cpu->cpreg_array_len;
    cpu->cpreg_indexes = g_new(uint64_t, arraylen);
    cpu->cpreg_values = g_new(uint64_t, arraylen);
    cpu->cpreg_vmstate_indexes = g_new(uint64_t, arraylen);
    cpu->cpreg_vmstate_values = g_new(uint64_t, arraylen);
    cpu->cpreg_vmstate_array_len = cpu->cpreg_array_len;
    cpu->cpreg_array_len = 0;

    g_list_foreach(keys, add_cpreg_to_list, cpu);

    assert(cpu->cpreg_array_len == arraylen);

    g_list_free(keys);
}

/*
 * Some registers are not accessible from AArch32 EL3 if SCR.NS == 0.
 */
static CPAccessResult access_el3_aa32ns(CPUARMState *env,
                                        const ARMCPRegInfo *ri,
                                        bool isread)
{
    if (!is_a64(env) && arm_current_el(env) == 3 &&
        arm_is_secure_below_el3(env)) {
        return CP_ACCESS_TRAP_UNCATEGORIZED;
    }
    return CP_ACCESS_OK;
}

/* Some secure-only AArch32 registers trap to EL3 if used from
 * Secure EL1 (but are just ordinary UNDEF in other non-EL3 contexts).
 * Note that an access from Secure EL1 can only happen if EL3 is AArch64.
 * We assume that the .access field is set to PL1_RW.
 */
static CPAccessResult access_trap_aa32s_el1(CPUARMState *env,
                                            const ARMCPRegInfo *ri,
                                            bool isread)
{
    if (arm_current_el(env) == 3) {
        return CP_ACCESS_OK;
    }
    if (arm_is_secure_below_el3(env)) {
        return CP_ACCESS_TRAP_EL3;
    }
    /* This will be EL1 NS and EL2 NS, which just UNDEF */
    return CP_ACCESS_TRAP_UNCATEGORIZED;
}

/* Check for traps to "powerdown debug" registers, which are controlled
 * by MDCR.TDOSA
 */
static CPAccessResult access_tdosa(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    int el = arm_current_el(env);
    bool mdcr_el2_tdosa = (env->cp15.mdcr_el2 & MDCR_TDOSA) ||
        (env->cp15.mdcr_el2 & MDCR_TDE) ||
        (arm_hcr_el2_eff(env) & HCR_TGE);

    if (el < 2 && mdcr_el2_tdosa && !arm_is_secure_below_el3(env)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 && (env->cp15.mdcr_el3 & MDCR_TDOSA)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

/* Check for traps to "debug ROM" registers, which are controlled
 * by MDCR_EL2.TDRA for EL2 but by the more general MDCR_EL3.TDA for EL3.
 */
static CPAccessResult access_tdra(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    int el = arm_current_el(env);
    bool mdcr_el2_tdra = (env->cp15.mdcr_el2 & MDCR_TDRA) ||
        (env->cp15.mdcr_el2 & MDCR_TDE) ||
        (arm_hcr_el2_eff(env) & HCR_TGE);

    if (el < 2 && mdcr_el2_tdra && !arm_is_secure_below_el3(env)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 && (env->cp15.mdcr_el3 & MDCR_TDA)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

/* Check for traps to general debug registers, which are controlled
 * by MDCR_EL2.TDA for EL2 and MDCR_EL3.TDA for EL3.
 */
static CPAccessResult access_tda(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    int el = arm_current_el(env);
    bool mdcr_el2_tda = (env->cp15.mdcr_el2 & MDCR_TDA) ||
        (env->cp15.mdcr_el2 & MDCR_TDE) ||
        (arm_hcr_el2_eff(env) & HCR_TGE);

    if (el < 2 && mdcr_el2_tda && !arm_is_secure_below_el3(env)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 && (env->cp15.mdcr_el3 & MDCR_TDA)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

/* Check for traps to performance monitor registers, which are controlled
 * by MDCR_EL2.TPM for EL2 and MDCR_EL3.TPM for EL3.
 */
static CPAccessResult access_tpm(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    int el = arm_current_el(env);

    if (el < 2 && (env->cp15.mdcr_el2 & MDCR_TPM)
        && !arm_is_secure_below_el3(env)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 && (env->cp15.mdcr_el3 & MDCR_TPM)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

/* Check for traps from EL1 due to HCR_EL2.TVM and HCR_EL2.TRVM.  */
static CPAccessResult access_tvm_trvm(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    if (arm_current_el(env) == 1) {
        uint64_t trap = isread ? HCR_TRVM : HCR_TVM;
        if (arm_hcr_el2_eff(env) & trap) {
            return CP_ACCESS_TRAP_EL2;
        }
    }
    return CP_ACCESS_OK;
}

/* Check for traps from EL1 due to HCR_EL2.TSW.  */
static CPAccessResult access_tsw(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    if (arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_TSW)) {
        return CP_ACCESS_TRAP_EL2;
    }
    return CP_ACCESS_OK;
}

/* Check for traps from EL1 due to HCR_EL2.TACR.  */
static CPAccessResult access_tacr(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    if (arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_TACR)) {
        return CP_ACCESS_TRAP_EL2;
    }
    return CP_ACCESS_OK;
}

/* Check for traps from EL1 due to HCR_EL2.TTLB. */
static CPAccessResult access_ttlb(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    if (arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_TTLB)) {
        return CP_ACCESS_TRAP_EL2;
    }
    return CP_ACCESS_OK;
}

static void dacr_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    raw_write(env, ri, value);
    tlb_flush(CPU(cpu)); /* Flush TLB as domain not tracked in TLB */
}

static void fcse_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    if (raw_read(env, ri) != value) {
        /* Unlike real hardware the qemu TLB uses virtual addresses,
         * not modified virtual addresses, so this causes a TLB flush.
         */
        tlb_flush(CPU(cpu));
        raw_write(env, ri, value);
    }
}

static void contextidr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    if (raw_read(env, ri) != value && !arm_feature(env, ARM_FEATURE_PMSA)
        && !extended_addresses_enabled(env)) {
        /* For VMSA (when not using the LPAE long descriptor page table
         * format) this register includes the ASID, so do a TLB flush.
         * For PMSA it is purely a process ID and no action is needed.
         */
        tlb_flush(CPU(cpu));
    }
    raw_write(env, ri, value);
}

/* IS variants of TLB operations must affect all cores */
static void tlbiall_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_all_cpus_synced(cs);
}

static void tlbiasid_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_all_cpus_synced(cs);
}

static void tlbimva_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_page_all_cpus_synced(cs, value & TARGET_PAGE_MASK);
}

static void tlbimvaa_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_page_all_cpus_synced(cs, value & TARGET_PAGE_MASK);
}

/*
 * Non-IS variants of TLB operations are upgraded to
 * IS versions if we are at NS EL1 and HCR_EL2.FB is set to
 * force broadcast of these operations.
 */
static bool tlb_force_broadcast(CPUARMState *env)
{
    return (env->cp15.hcr_el2 & HCR_FB) &&
        arm_current_el(env) == 1 && arm_is_secure_below_el3(env);
}

static void tlbiall_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    /* Invalidate all (TLBIALL) */
    CPUState *cs = env_cpu(env);

    if (tlb_force_broadcast(env)) {
        tlb_flush_all_cpus_synced(cs);
    } else {
        tlb_flush(cs);
    }
}

static void tlbimva_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    /* Invalidate single TLB entry by MVA and ASID (TLBIMVA) */
    CPUState *cs = env_cpu(env);

    value &= TARGET_PAGE_MASK;
    if (tlb_force_broadcast(env)) {
        tlb_flush_page_all_cpus_synced(cs, value);
    } else {
        tlb_flush_page(cs, value);
    }
}

static void tlbiasid_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    /* Invalidate by ASID (TLBIASID) */
    CPUState *cs = env_cpu(env);

    if (tlb_force_broadcast(env)) {
        tlb_flush_all_cpus_synced(cs);
    } else {
        tlb_flush(cs);
    }
}

static void tlbimvaa_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    /* Invalidate single entry by MVA, all ASIDs (TLBIMVAA) */
    CPUState *cs = env_cpu(env);

    value &= TARGET_PAGE_MASK;
    if (tlb_force_broadcast(env)) {
        tlb_flush_page_all_cpus_synced(cs, value);
    } else {
        tlb_flush_page(cs, value);
    }
}

static void tlbiall_nsnh_write(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_by_mmuidx(cs,
                        ARMMMUIdxBit_E10_1 |
                        ARMMMUIdxBit_E10_1_PAN |
                        ARMMMUIdxBit_E10_0);
}

static void tlbiall_nsnh_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_by_mmuidx_all_cpus_synced(cs,
                                        ARMMMUIdxBit_E10_1 |
                                        ARMMMUIdxBit_E10_1_PAN |
                                        ARMMMUIdxBit_E10_0);
}


static void tlbiall_hyp_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_by_mmuidx(cs, ARMMMUIdxBit_E2);
}

static void tlbiall_hyp_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_by_mmuidx_all_cpus_synced(cs, ARMMMUIdxBit_E2);
}

static void tlbimva_hyp_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    CPUState *cs = env_cpu(env);
    uint64_t pageaddr = value & ~MAKE_64BIT_MASK(0, 12);

    tlb_flush_page_by_mmuidx(cs, pageaddr, ARMMMUIdxBit_E2);
}

static void tlbimva_hyp_is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    CPUState *cs = env_cpu(env);
    uint64_t pageaddr = value & ~MAKE_64BIT_MASK(0, 12);

    tlb_flush_page_by_mmuidx_all_cpus_synced(cs, pageaddr,
                                             ARMMMUIdxBit_E2);
}

static const ARMCPRegInfo cp_reginfo[] = {
    /* Define the secure and non-secure FCSE identifier CP registers
     * separately because there is no secure bank in V8 (no _EL3).  This allows
     * the secure register to be properly reset and migrated. There is also no
     * v8 EL1 version of the register so the non-secure instance stands alone.
     */
    { .name = "FCSEIDR",
      .cp = 15, .opc1 = 0, .crn = 13, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .secure = ARM_CP_SECSTATE_NS,
      .fieldoffset = offsetof(CPUARMState, cp15.fcseidr_ns),
      .resetvalue = 0, .writefn = fcse_write, .raw_writefn = raw_write, },
    { .name = "FCSEIDR_S",
      .cp = 15, .opc1 = 0, .crn = 13, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .secure = ARM_CP_SECSTATE_S,
      .fieldoffset = offsetof(CPUARMState, cp15.fcseidr_s),
      .resetvalue = 0, .writefn = fcse_write, .raw_writefn = raw_write, },
    /* Define the secure and non-secure context identifier CP registers
     * separately because there is no secure bank in V8 (no _EL3).  This allows
     * the secure register to be properly reset and migrated.  In the
     * non-secure case, the 32-bit register will have reset and migration
     * disabled during registration as it is handled by the 64-bit instance.
     */
    { .name = "CONTEXTIDR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 13, .crm = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .secure = ARM_CP_SECSTATE_NS,
      .fieldoffset = offsetof(CPUARMState, cp15.contextidr_el[1]),
      .resetvalue = 0, .writefn = contextidr_write, .raw_writefn = raw_write, },
    { .name = "CONTEXTIDR_S", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 13, .crm = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .secure = ARM_CP_SECSTATE_S,
      .fieldoffset = offsetof(CPUARMState, cp15.contextidr_s),
      .resetvalue = 0, .writefn = contextidr_write, .raw_writefn = raw_write, },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo not_v8_cp_reginfo[] = {
    /* NB: Some of these registers exist in v8 but with more precise
     * definitions that don't use CP_ANY wildcards (mostly in v8_cp_reginfo[]).
     */
    /* MMU Domain access control / MPU write buffer control */
    { .name = "DACR",
      .cp = 15, .opc1 = CP_ANY, .crn = 3, .crm = CP_ANY, .opc2 = CP_ANY,
      .access = PL1_RW, .accessfn = access_tvm_trvm, .resetvalue = 0,
      .writefn = dacr_write, .raw_writefn = raw_write,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.dacr_s),
                             offsetoflow32(CPUARMState, cp15.dacr_ns) } },
    /* ARMv7 allocates a range of implementation defined TLB LOCKDOWN regs.
     * For v6 and v5, these mappings are overly broad.
     */
    { .name = "TLB_LOCKDOWN", .cp = 15, .crn = 10, .crm = 0,
      .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_RW, .type = ARM_CP_NOP },
    { .name = "TLB_LOCKDOWN", .cp = 15, .crn = 10, .crm = 1,
      .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_RW, .type = ARM_CP_NOP },
    { .name = "TLB_LOCKDOWN", .cp = 15, .crn = 10, .crm = 4,
      .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_RW, .type = ARM_CP_NOP },
    { .name = "TLB_LOCKDOWN", .cp = 15, .crn = 10, .crm = 8,
      .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_RW, .type = ARM_CP_NOP },
    /* Cache maintenance ops; some of this space may be overridden later. */
    { .name = "CACHEMAINT", .cp = 15, .crn = 7, .crm = CP_ANY,
      .opc1 = 0, .opc2 = CP_ANY, .access = PL1_W,
      .type = ARM_CP_NOP | ARM_CP_OVERRIDE },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo not_v6_cp_reginfo[] = {
    /* Not all pre-v6 cores implemented this WFI, so this is slightly
     * over-broad.
     */
    { .name = "WFI_v5", .cp = 15, .crn = 7, .crm = 8, .opc1 = 0, .opc2 = 2,
      .access = PL1_W, .type = ARM_CP_WFI },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo not_v7_cp_reginfo[] = {
    /* Standard v6 WFI (also used in some pre-v6 cores); not in v7 (which
     * is UNPREDICTABLE; we choose to NOP as most implementations do).
     */
    { .name = "WFI_v6", .cp = 15, .crn = 7, .crm = 0, .opc1 = 0, .opc2 = 4,
      .access = PL1_W, .type = ARM_CP_WFI },
    /* L1 cache lockdown. Not architectural in v6 and earlier but in practice
     * implemented in 926, 946, 1026, 1136, 1176 and 11MPCore. StrongARM and
     * OMAPCP will override this space.
     */
    { .name = "DLOCKDOWN", .cp = 15, .crn = 9, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .fieldoffset = offsetof(CPUARMState, cp15.c9_data),
      .resetvalue = 0 },
    { .name = "ILOCKDOWN", .cp = 15, .crn = 9, .crm = 0, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .fieldoffset = offsetof(CPUARMState, cp15.c9_insn),
      .resetvalue = 0 },
    /* v6 doesn't have the cache ID registers but Linux reads them anyway */
    { .name = "DUMMY", .cp = 15, .crn = 0, .crm = 0, .opc1 = 1, .opc2 = CP_ANY,
      .access = PL1_R, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
      .resetvalue = 0 },
    /* We don't implement pre-v7 debug but most CPUs had at least a DBGDIDR;
     * implementing it as RAZ means the "debug architecture version" bits
     * will read as a reserved value, which should cause Linux to not try
     * to use the debug hardware.
     */
    { .name = "DBGDIDR", .cp = 14, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL0_R, .type = ARM_CP_CONST, .resetvalue = 0 },
    /* MMU TLB control. Note that the wildcarding means we cover not just
     * the unified TLB ops but also the dside/iside/inner-shareable variants.
     */
    { .name = "TLBIALL", .cp = 15, .crn = 8, .crm = CP_ANY,
      .opc1 = CP_ANY, .opc2 = 0, .access = PL1_W, .writefn = tlbiall_write,
      .type = ARM_CP_NO_RAW },
    { .name = "TLBIMVA", .cp = 15, .crn = 8, .crm = CP_ANY,
      .opc1 = CP_ANY, .opc2 = 1, .access = PL1_W, .writefn = tlbimva_write,
      .type = ARM_CP_NO_RAW },
    { .name = "TLBIASID", .cp = 15, .crn = 8, .crm = CP_ANY,
      .opc1 = CP_ANY, .opc2 = 2, .access = PL1_W, .writefn = tlbiasid_write,
      .type = ARM_CP_NO_RAW },
    { .name = "TLBIMVAA", .cp = 15, .crn = 8, .crm = CP_ANY,
      .opc1 = CP_ANY, .opc2 = 3, .access = PL1_W, .writefn = tlbimvaa_write,
      .type = ARM_CP_NO_RAW },
    { .name = "PRRR", .cp = 15, .crn = 10, .crm = 2,
      .opc1 = 0, .opc2 = 0, .access = PL1_RW, .type = ARM_CP_NOP },
    { .name = "NMRR", .cp = 15, .crn = 10, .crm = 2,
      .opc1 = 0, .opc2 = 1, .access = PL1_RW, .type = ARM_CP_NOP },
    REGINFO_SENTINEL
};

static void cpacr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    uint32_t mask = 0;

    /* In ARMv8 most bits of CPACR_EL1 are RES0. */
    if (!arm_feature(env, ARM_FEATURE_V8)) {
        /* ARMv7 defines bits for unimplemented coprocessors as RAZ/WI.
         * ASEDIS [31] and D32DIS [30] are both UNK/SBZP without VFP.
         * TRCDIS [28] is RAZ/WI since we do not implement a trace macrocell.
         */
        if (cpu_isar_feature(aa32_vfp_simd, env_archcpu(env))) {
            /* VFP coprocessor: cp10 & cp11 [23:20] */
            mask |= (1 << 31) | (1 << 30) | (0xf << 20);

            if (!arm_feature(env, ARM_FEATURE_NEON)) {
                /* ASEDIS [31] bit is RAO/WI */
                value |= (1 << 31);
            }

            /* VFPv3 and upwards with NEON implement 32 double precision
             * registers (D0-D31).
             */
            if (!cpu_isar_feature(aa32_simd_r32, env_archcpu(env))) {
                /* D32DIS [30] is RAO/WI if D16-31 are not implemented. */
                value |= (1 << 30);
            }
        }
        value &= mask;
    }

    /*
     * For A-profile AArch32 EL3 (but not M-profile secure mode), if NSACR.CP10
     * is 0 then CPACR.{CP11,CP10} ignore writes and read as 0b00.
     */
    if (arm_feature(env, ARM_FEATURE_EL3) && !arm_el_is_aa64(env, 3) &&
        !arm_is_secure(env) && !extract32(env->cp15.nsacr, 10, 1)) {
        value &= ~(0xf << 20);
        value |= env->cp15.cpacr_el1 & (0xf << 20);
    }

    env->cp15.cpacr_el1 = value;
}

static uint64_t cpacr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /*
     * For A-profile AArch32 EL3 (but not M-profile secure mode), if NSACR.CP10
     * is 0 then CPACR.{CP11,CP10} ignore writes and read as 0b00.
     */
    uint64_t value = env->cp15.cpacr_el1;

    if (arm_feature(env, ARM_FEATURE_EL3) && !arm_el_is_aa64(env, 3) &&
        !arm_is_secure(env) && !extract32(env->cp15.nsacr, 10, 1)) {
        value &= ~(0xf << 20);
    }
    return value;
}


static void cpacr_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* Call cpacr_write() so that we reset with the correct RAO bits set
     * for our CPU features.
     */
    cpacr_write(env, ri, 0);
}

static CPAccessResult cpacr_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    if (arm_feature(env, ARM_FEATURE_V8)) {
        /* Check if CPACR accesses are to be trapped to EL2 */
        if (arm_current_el(env) == 1 &&
            (env->cp15.cptr_el[2] & CPTR_TCPAC) && !arm_is_secure(env)) {
            return CP_ACCESS_TRAP_EL2;
        /* Check if CPACR accesses are to be trapped to EL3 */
        } else if (arm_current_el(env) < 3 &&
                   (env->cp15.cptr_el[3] & CPTR_TCPAC)) {
            return CP_ACCESS_TRAP_EL3;
        }
    }

    return CP_ACCESS_OK;
}

static CPAccessResult cptr_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                  bool isread)
{
    /* Check if CPTR accesses are set to trap to EL3 */
    if (arm_current_el(env) == 2 && (env->cp15.cptr_el[3] & CPTR_TCPAC)) {
        return CP_ACCESS_TRAP_EL3;
    }

    return CP_ACCESS_OK;
}

static const ARMCPRegInfo v6_cp_reginfo[] = {
    /* prefetch by MVA in v6, NOP in v7 */
    { .name = "MVA_prefetch",
      .cp = 15, .crn = 7, .crm = 13, .opc1 = 0, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NOP },
    /* We need to break the TB after ISB to execute self-modifying code
     * correctly and also to take any pending interrupts immediately.
     * So use arm_cp_write_ignore() function instead of ARM_CP_NOP flag.
     */
    { .name = "ISB", .cp = 15, .crn = 7, .crm = 5, .opc1 = 0, .opc2 = 4,
      .access = PL0_W, .type = ARM_CP_NO_RAW, .writefn = arm_cp_write_ignore },
    { .name = "DSB", .cp = 15, .crn = 7, .crm = 10, .opc1 = 0, .opc2 = 4,
      .access = PL0_W, .type = ARM_CP_NOP },
    { .name = "DMB", .cp = 15, .crn = 7, .crm = 10, .opc1 = 0, .opc2 = 5,
      .access = PL0_W, .type = ARM_CP_NOP },
    { .name = "IFAR", .cp = 15, .crn = 6, .crm = 0, .opc1 = 0, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.ifar_s),
                             offsetof(CPUARMState, cp15.ifar_ns) },
      .resetvalue = 0, },
    /* Watchpoint Fault Address Register : should actually only be present
     * for 1136, 1176, 11MPCore.
     */
    { .name = "WFAR", .cp = 15, .crn = 6, .crm = 0, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0, },
    { .name = "CPACR", .state = ARM_CP_STATE_BOTH, .opc0 = 3,
      .crn = 1, .crm = 0, .opc1 = 0, .opc2 = 2, .accessfn = cpacr_access,
      .access = PL1_RW, .fieldoffset = offsetof(CPUARMState, cp15.cpacr_el1),
      .resetfn = cpacr_reset, .writefn = cpacr_write, .readfn = cpacr_read },
    REGINFO_SENTINEL
};

/* Definitions for the PMU registers */
#define PMCRN_MASK  0xf800
#define PMCRN_SHIFT 11
#define PMCRLC  0x40
#define PMCRDP  0x20
#define PMCRX   0x10
#define PMCRD   0x8
#define PMCRC   0x4
#define PMCRP   0x2
#define PMCRE   0x1
/*
 * Mask of PMCR bits writeable by guest (not including WO bits like C, P,
 * which can be written as 1 to trigger behaviour but which stay RAZ).
 */
#define PMCR_WRITEABLE_MASK (PMCRLC | PMCRDP | PMCRX | PMCRD | PMCRE)

#define PMXEVTYPER_P          0x80000000
#define PMXEVTYPER_U          0x40000000
#define PMXEVTYPER_NSK        0x20000000
#define PMXEVTYPER_NSU        0x10000000
#define PMXEVTYPER_NSH        0x08000000
#define PMXEVTYPER_M          0x04000000
#define PMXEVTYPER_MT         0x02000000
#define PMXEVTYPER_EVTCOUNT   0x0000ffff
#define PMXEVTYPER_MASK       (PMXEVTYPER_P | PMXEVTYPER_U | PMXEVTYPER_NSK | \
                               PMXEVTYPER_NSU | PMXEVTYPER_NSH | \
                               PMXEVTYPER_M | PMXEVTYPER_MT | \
                               PMXEVTYPER_EVTCOUNT)

#define PMCCFILTR             0xf8000000
#define PMCCFILTR_M           PMXEVTYPER_M
#define PMCCFILTR_EL0         (PMCCFILTR | PMCCFILTR_M)

static inline uint32_t pmu_num_counters(CPUARMState *env)
{
  return (env->cp15.c9_pmcr & PMCRN_MASK) >> PMCRN_SHIFT;
}

/* Bits allowed to be set/cleared for PMCNTEN* and PMINTEN* */
static inline uint64_t pmu_counter_mask(CPUARMState *env)
{
  return (1 << 31) | ((1 << pmu_num_counters(env)) - 1);
}

typedef struct pm_event {
    uint16_t number; /* PMEVTYPER.evtCount is 16 bits wide */
    /* If the event is supported on this CPU (used to generate PMCEID[01]) */
    bool (*supported)(CPUARMState *);
    /*
     * Retrieve the current count of the underlying event. The programmed
     * counters hold a difference from the return value from this function
     */
    uint64_t (*get_count)(CPUARMState *);
    /*
     * Return how many nanoseconds it will take (at a minimum) for count events
     * to occur. A negative value indicates the counter will never overflow, or
     * that the counter has otherwise arranged for the overflow bit to be set
     * and the PMU interrupt to be raised on overflow.
     */
    int64_t (*ns_per_count)(uint64_t);
} pm_event;

static bool event_always_supported(CPUARMState *env)
{
    return true;
}

static uint64_t swinc_get_count(CPUARMState *env)
{
    /*
     * SW_INCR events are written directly to the pmevcntr's by writes to
     * PMSWINC, so there is no underlying count maintained by the PMU itself
     */
    return 0;
}

static int64_t swinc_ns_per(uint64_t ignored)
{
    return -1;
}

/*
 * Return the underlying cycle count for the PMU cycle counters. If we're in
 * usermode, simply return 0.
 */
static uint64_t cycles_get_count(CPUARMState *env)
{
#ifndef CONFIG_USER_ONLY
    return muldiv64(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                   ARM_CPU_FREQ, NANOSECONDS_PER_SECOND);
#else
    return cpu_get_host_ticks();
#endif
}

#ifndef CONFIG_USER_ONLY
static int64_t cycles_ns_per(uint64_t cycles)
{
    return (ARM_CPU_FREQ / NANOSECONDS_PER_SECOND) * cycles;
}

static bool instructions_supported(CPUARMState *env)
{
    return use_icount == 1 /* Precise instruction counting */;
}

static uint64_t instructions_get_count(CPUARMState *env)
{
    return (uint64_t)cpu_get_icount_raw();
}

static int64_t instructions_ns_per(uint64_t icount)
{
    return cpu_icount_to_ns((int64_t)icount);
}
#endif

static bool pmu_8_1_events_supported(CPUARMState *env)
{
    /* For events which are supported in any v8.1 PMU */
    return cpu_isar_feature(any_pmu_8_1, env_archcpu(env));
}

static bool pmu_8_4_events_supported(CPUARMState *env)
{
    /* For events which are supported in any v8.1 PMU */
    return cpu_isar_feature(any_pmu_8_4, env_archcpu(env));
}

static uint64_t zero_event_get_count(CPUARMState *env)
{
    /* For events which on QEMU never fire, so their count is always zero */
    return 0;
}

static int64_t zero_event_ns_per(uint64_t cycles)
{
    /* An event which never fires can never overflow */
    return -1;
}

static const pm_event pm_events[] = {
    { .number = 0x000, /* SW_INCR */
      .supported = event_always_supported,
      .get_count = swinc_get_count,
      .ns_per_count = swinc_ns_per,
    },
#ifndef CONFIG_USER_ONLY
    { .number = 0x008, /* INST_RETIRED, Instruction architecturally executed */
      .supported = instructions_supported,
      .get_count = instructions_get_count,
      .ns_per_count = instructions_ns_per,
    },
    { .number = 0x011, /* CPU_CYCLES, Cycle */
      .supported = event_always_supported,
      .get_count = cycles_get_count,
      .ns_per_count = cycles_ns_per,
    },
#endif
    { .number = 0x023, /* STALL_FRONTEND */
      .supported = pmu_8_1_events_supported,
      .get_count = zero_event_get_count,
      .ns_per_count = zero_event_ns_per,
    },
    { .number = 0x024, /* STALL_BACKEND */
      .supported = pmu_8_1_events_supported,
      .get_count = zero_event_get_count,
      .ns_per_count = zero_event_ns_per,
    },
    { .number = 0x03c, /* STALL */
      .supported = pmu_8_4_events_supported,
      .get_count = zero_event_get_count,
      .ns_per_count = zero_event_ns_per,
    },
};

/*
 * Note: Before increasing MAX_EVENT_ID beyond 0x3f into the 0x40xx range of
 * events (i.e. the statistical profiling extension), this implementation
 * should first be updated to something sparse instead of the current
 * supported_event_map[] array.
 */
#define MAX_EVENT_ID 0x3c
#define UNSUPPORTED_EVENT UINT16_MAX
static uint16_t supported_event_map[MAX_EVENT_ID + 1];

/*
 * Called upon CPU initialization to initialize PMCEID[01]_EL0 and build a map
 * of ARM event numbers to indices in our pm_events array.
 *
 * Note: Events in the 0x40XX range are not currently supported.
 */
void pmu_init(ARMCPU *cpu)
{
    unsigned int i;

    /*
     * Empty supported_event_map and cpu->pmceid[01] before adding supported
     * events to them
     */
    for (i = 0; i < ARRAY_SIZE(supported_event_map); i++) {
        supported_event_map[i] = UNSUPPORTED_EVENT;
    }
    cpu->pmceid0 = 0;
    cpu->pmceid1 = 0;

    for (i = 0; i < ARRAY_SIZE(pm_events); i++) {
        const pm_event *cnt = &pm_events[i];
        assert(cnt->number <= MAX_EVENT_ID);
        /* We do not currently support events in the 0x40xx range */
        assert(cnt->number <= 0x3f);

        if (cnt->supported(&cpu->env)) {
            supported_event_map[cnt->number] = i;
            uint64_t event_mask = 1ULL << (cnt->number & 0x1f);
            if (cnt->number & 0x20) {
                cpu->pmceid1 |= event_mask;
            } else {
                cpu->pmceid0 |= event_mask;
            }
        }
    }
}

/*
 * Check at runtime whether a PMU event is supported for the current machine
 */
static bool event_supported(uint16_t number)
{
    if (number > MAX_EVENT_ID) {
        return false;
    }
    return supported_event_map[number] != UNSUPPORTED_EVENT;
}

static CPAccessResult pmreg_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    /* Performance monitor registers user accessibility is controlled
     * by PMUSERENR. MDCR_EL2.TPM and MDCR_EL3.TPM allow configurable
     * trapping to EL2 or EL3 for other accesses.
     */
    int el = arm_current_el(env);

    if (el == 0 && !(env->cp15.c9_pmuserenr & 1)) {
        return CP_ACCESS_TRAP;
    }
    if (el < 2 && (env->cp15.mdcr_el2 & MDCR_TPM)
        && !arm_is_secure_below_el3(env)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 && (env->cp15.mdcr_el3 & MDCR_TPM)) {
        return CP_ACCESS_TRAP_EL3;
    }

    return CP_ACCESS_OK;
}

static CPAccessResult pmreg_access_xevcntr(CPUARMState *env,
                                           const ARMCPRegInfo *ri,
                                           bool isread)
{
    /* ER: event counter read trap control */
    if (arm_feature(env, ARM_FEATURE_V8)
        && arm_current_el(env) == 0
        && (env->cp15.c9_pmuserenr & (1 << 3)) != 0
        && isread) {
        return CP_ACCESS_OK;
    }

    return pmreg_access(env, ri, isread);
}

static CPAccessResult pmreg_access_swinc(CPUARMState *env,
                                         const ARMCPRegInfo *ri,
                                         bool isread)
{
    /* SW: software increment write trap control */
    if (arm_feature(env, ARM_FEATURE_V8)
        && arm_current_el(env) == 0
        && (env->cp15.c9_pmuserenr & (1 << 1)) != 0
        && !isread) {
        return CP_ACCESS_OK;
    }

    return pmreg_access(env, ri, isread);
}

static CPAccessResult pmreg_access_selr(CPUARMState *env,
                                        const ARMCPRegInfo *ri,
                                        bool isread)
{
    /* ER: event counter read trap control */
    if (arm_feature(env, ARM_FEATURE_V8)
        && arm_current_el(env) == 0
        && (env->cp15.c9_pmuserenr & (1 << 3)) != 0) {
        return CP_ACCESS_OK;
    }

    return pmreg_access(env, ri, isread);
}

static CPAccessResult pmreg_access_ccntr(CPUARMState *env,
                                         const ARMCPRegInfo *ri,
                                         bool isread)
{
    /* CR: cycle counter read trap control */
    if (arm_feature(env, ARM_FEATURE_V8)
        && arm_current_el(env) == 0
        && (env->cp15.c9_pmuserenr & (1 << 2)) != 0
        && isread) {
        return CP_ACCESS_OK;
    }

    return pmreg_access(env, ri, isread);
}

/* Returns true if the counter (pass 31 for PMCCNTR) should count events using
 * the current EL, security state, and register configuration.
 */
static bool pmu_counter_enabled(CPUARMState *env, uint8_t counter)
{
    uint64_t filter;
    bool e, p, u, nsk, nsu, nsh, m;
    bool enabled, prohibited, filtered;
    bool secure = arm_is_secure(env);
    int el = arm_current_el(env);
    uint8_t hpmn = env->cp15.mdcr_el2 & MDCR_HPMN;

    if (!arm_feature(env, ARM_FEATURE_PMU)) {
        return false;
    }

    if (!arm_feature(env, ARM_FEATURE_EL2) ||
            (counter < hpmn || counter == 31)) {
        e = env->cp15.c9_pmcr & PMCRE;
    } else {
        e = env->cp15.mdcr_el2 & MDCR_HPME;
    }
    enabled = e && (env->cp15.c9_pmcnten & (1 << counter));

    if (!secure) {
        if (el == 2 && (counter < hpmn || counter == 31)) {
            prohibited = env->cp15.mdcr_el2 & MDCR_HPMD;
        } else {
            prohibited = false;
        }
    } else {
        prohibited = arm_feature(env, ARM_FEATURE_EL3) &&
           (env->cp15.mdcr_el3 & MDCR_SPME);
    }

    if (prohibited && counter == 31) {
        prohibited = env->cp15.c9_pmcr & PMCRDP;
    }

    if (counter == 31) {
        filter = env->cp15.pmccfiltr_el0;
    } else {
        filter = env->cp15.c14_pmevtyper[counter];
    }

    p   = filter & PMXEVTYPER_P;
    u   = filter & PMXEVTYPER_U;
    nsk = arm_feature(env, ARM_FEATURE_EL3) && (filter & PMXEVTYPER_NSK);
    nsu = arm_feature(env, ARM_FEATURE_EL3) && (filter & PMXEVTYPER_NSU);
    nsh = arm_feature(env, ARM_FEATURE_EL2) && (filter & PMXEVTYPER_NSH);
    m   = arm_el_is_aa64(env, 1) &&
              arm_feature(env, ARM_FEATURE_EL3) && (filter & PMXEVTYPER_M);

    if (el == 0) {
        filtered = secure ? u : u != nsu;
    } else if (el == 1) {
        filtered = secure ? p : p != nsk;
    } else if (el == 2) {
        filtered = !nsh;
    } else { /* EL3 */
        filtered = m != p;
    }

    if (counter != 31) {
        /*
         * If not checking PMCCNTR, ensure the counter is setup to an event we
         * support
         */
        uint16_t event = filter & PMXEVTYPER_EVTCOUNT;
        if (!event_supported(event)) {
            return false;
        }
    }

    return enabled && !prohibited && !filtered;
}

static void pmu_update_irq(CPUARMState *env)
{
    ARMCPU *cpu = env_archcpu(env);
    qemu_set_irq(cpu->pmu_interrupt, (env->cp15.c9_pmcr & PMCRE) &&
            (env->cp15.c9_pminten & env->cp15.c9_pmovsr));
}

/*
 * Ensure c15_ccnt is the guest-visible count so that operations such as
 * enabling/disabling the counter or filtering, modifying the count itself,
 * etc. can be done logically. This is essentially a no-op if the counter is
 * not enabled at the time of the call.
 */
static void pmccntr_op_start(CPUARMState *env)
{
    uint64_t cycles = cycles_get_count(env);

    if (pmu_counter_enabled(env, 31)) {
        uint64_t eff_cycles = cycles;
        if (env->cp15.c9_pmcr & PMCRD) {
            /* Increment once every 64 processor clock cycles */
            eff_cycles /= 64;
        }

        uint64_t new_pmccntr = eff_cycles - env->cp15.c15_ccnt_delta;

        uint64_t overflow_mask = env->cp15.c9_pmcr & PMCRLC ? \
                                 1ull << 63 : 1ull << 31;
        if (env->cp15.c15_ccnt & ~new_pmccntr & overflow_mask) {
            env->cp15.c9_pmovsr |= (1 << 31);
            pmu_update_irq(env);
        }

        env->cp15.c15_ccnt = new_pmccntr;
    }
    env->cp15.c15_ccnt_delta = cycles;
}

/*
 * If PMCCNTR is enabled, recalculate the delta between the clock and the
 * guest-visible count. A call to pmccntr_op_finish should follow every call to
 * pmccntr_op_start.
 */
static void pmccntr_op_finish(CPUARMState *env)
{
    if (pmu_counter_enabled(env, 31)) {
#ifndef CONFIG_USER_ONLY
        /* Calculate when the counter will next overflow */
        uint64_t remaining_cycles = -env->cp15.c15_ccnt;
        if (!(env->cp15.c9_pmcr & PMCRLC)) {
            remaining_cycles = (uint32_t)remaining_cycles;
        }
        int64_t overflow_in = cycles_ns_per(remaining_cycles);

        if (overflow_in > 0) {
            int64_t overflow_at = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                overflow_in;
            ARMCPU *cpu = env_archcpu(env);
            timer_mod_anticipate_ns(cpu->pmu_timer, overflow_at);
        }
#endif

        uint64_t prev_cycles = env->cp15.c15_ccnt_delta;
        if (env->cp15.c9_pmcr & PMCRD) {
            /* Increment once every 64 processor clock cycles */
            prev_cycles /= 64;
        }
        env->cp15.c15_ccnt_delta = prev_cycles - env->cp15.c15_ccnt;
    }
}

static void pmevcntr_op_start(CPUARMState *env, uint8_t counter)
{

    uint16_t event = env->cp15.c14_pmevtyper[counter] & PMXEVTYPER_EVTCOUNT;
    uint64_t count = 0;
    if (event_supported(event)) {
        uint16_t event_idx = supported_event_map[event];
        count = pm_events[event_idx].get_count(env);
    }

    if (pmu_counter_enabled(env, counter)) {
        uint32_t new_pmevcntr = count - env->cp15.c14_pmevcntr_delta[counter];

        if (env->cp15.c14_pmevcntr[counter] & ~new_pmevcntr & INT32_MIN) {
            env->cp15.c9_pmovsr |= (1 << counter);
            pmu_update_irq(env);
        }
        env->cp15.c14_pmevcntr[counter] = new_pmevcntr;
    }
    env->cp15.c14_pmevcntr_delta[counter] = count;
}

static void pmevcntr_op_finish(CPUARMState *env, uint8_t counter)
{
    if (pmu_counter_enabled(env, counter)) {
#ifndef CONFIG_USER_ONLY
        uint16_t event = env->cp15.c14_pmevtyper[counter] & PMXEVTYPER_EVTCOUNT;
        uint16_t event_idx = supported_event_map[event];
        uint64_t delta = UINT32_MAX -
            (uint32_t)env->cp15.c14_pmevcntr[counter] + 1;
        int64_t overflow_in = pm_events[event_idx].ns_per_count(delta);

        if (overflow_in > 0) {
            int64_t overflow_at = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                overflow_in;
            ARMCPU *cpu = env_archcpu(env);
            timer_mod_anticipate_ns(cpu->pmu_timer, overflow_at);
        }
#endif

        env->cp15.c14_pmevcntr_delta[counter] -=
            env->cp15.c14_pmevcntr[counter];
    }
}

void pmu_op_start(CPUARMState *env)
{
    unsigned int i;
    pmccntr_op_start(env);
    for (i = 0; i < pmu_num_counters(env); i++) {
        pmevcntr_op_start(env, i);
    }
}

void pmu_op_finish(CPUARMState *env)
{
    unsigned int i;
    pmccntr_op_finish(env);
    for (i = 0; i < pmu_num_counters(env); i++) {
        pmevcntr_op_finish(env, i);
    }
}

void pmu_pre_el_change(ARMCPU *cpu, void *ignored)
{
    pmu_op_start(&cpu->env);
}

void pmu_post_el_change(ARMCPU *cpu, void *ignored)
{
    pmu_op_finish(&cpu->env);
}

void arm_pmu_timer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    /*
     * Update all the counter values based on the current underlying counts,
     * triggering interrupts to be raised, if necessary. pmu_op_finish() also
     * has the effect of setting the cpu->pmu_timer to the next earliest time a
     * counter may expire.
     */
    pmu_op_start(&cpu->env);
    pmu_op_finish(&cpu->env);
}

static void pmcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                       uint64_t value)
{
    pmu_op_start(env);

    if (value & PMCRC) {
        /* The counter has been reset */
        env->cp15.c15_ccnt = 0;
    }

    if (value & PMCRP) {
        unsigned int i;
        for (i = 0; i < pmu_num_counters(env); i++) {
            env->cp15.c14_pmevcntr[i] = 0;
        }
    }

    env->cp15.c9_pmcr &= ~PMCR_WRITEABLE_MASK;
    env->cp15.c9_pmcr |= (value & PMCR_WRITEABLE_MASK);

    pmu_op_finish(env);
}

static void pmswinc_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    unsigned int i;
    for (i = 0; i < pmu_num_counters(env); i++) {
        /* Increment a counter's count iff: */
        if ((value & (1 << i)) && /* counter's bit is set */
                /* counter is enabled and not filtered */
                pmu_counter_enabled(env, i) &&
                /* counter is SW_INCR */
                (env->cp15.c14_pmevtyper[i] & PMXEVTYPER_EVTCOUNT) == 0x0) {
            pmevcntr_op_start(env, i);

            /*
             * Detect if this write causes an overflow since we can't predict
             * PMSWINC overflows like we can for other events
             */
            uint32_t new_pmswinc = env->cp15.c14_pmevcntr[i] + 1;

            if (env->cp15.c14_pmevcntr[i] & ~new_pmswinc & INT32_MIN) {
                env->cp15.c9_pmovsr |= (1 << i);
                pmu_update_irq(env);
            }

            env->cp15.c14_pmevcntr[i] = new_pmswinc;

            pmevcntr_op_finish(env, i);
        }
    }
}

static uint64_t pmccntr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint64_t ret;
    pmccntr_op_start(env);
    ret = env->cp15.c15_ccnt;
    pmccntr_op_finish(env);
    return ret;
}

static void pmselr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    /* The value of PMSELR.SEL affects the behavior of PMXEVTYPER and
     * PMXEVCNTR. We allow [0..31] to be written to PMSELR here; in the
     * meanwhile, we check PMSELR.SEL when PMXEVTYPER and PMXEVCNTR are
     * accessed.
     */
    env->cp15.c9_pmselr = value & 0x1f;
}

static void pmccntr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    pmccntr_op_start(env);
    env->cp15.c15_ccnt = value;
    pmccntr_op_finish(env);
}

static void pmccntr_write32(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    uint64_t cur_val = pmccntr_read(env, NULL);

    pmccntr_write(env, ri, deposit64(cur_val, 0, 32, value));
}

static void pmccfiltr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    pmccntr_op_start(env);
    env->cp15.pmccfiltr_el0 = value & PMCCFILTR_EL0;
    pmccntr_op_finish(env);
}

static void pmccfiltr_write_a32(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    pmccntr_op_start(env);
    /* M is not accessible from AArch32 */
    env->cp15.pmccfiltr_el0 = (env->cp15.pmccfiltr_el0 & PMCCFILTR_M) |
        (value & PMCCFILTR);
    pmccntr_op_finish(env);
}

static uint64_t pmccfiltr_read_a32(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* M is not visible in AArch32 */
    return env->cp15.pmccfiltr_el0 & PMCCFILTR;
}

static void pmcntenset_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    value &= pmu_counter_mask(env);
    env->cp15.c9_pmcnten |= value;
}

static void pmcntenclr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    value &= pmu_counter_mask(env);
    env->cp15.c9_pmcnten &= ~value;
}

static void pmovsr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    value &= pmu_counter_mask(env);
    env->cp15.c9_pmovsr &= ~value;
    pmu_update_irq(env);
}

static void pmovsset_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    value &= pmu_counter_mask(env);
    env->cp15.c9_pmovsr |= value;
    pmu_update_irq(env);
}

static void pmevtyper_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value, const uint8_t counter)
{
    if (counter == 31) {
        pmccfiltr_write(env, ri, value);
    } else if (counter < pmu_num_counters(env)) {
        pmevcntr_op_start(env, counter);

        /*
         * If this counter's event type is changing, store the current
         * underlying count for the new type in c14_pmevcntr_delta[counter] so
         * pmevcntr_op_finish has the correct baseline when it converts back to
         * a delta.
         */
        uint16_t old_event = env->cp15.c14_pmevtyper[counter] &
            PMXEVTYPER_EVTCOUNT;
        uint16_t new_event = value & PMXEVTYPER_EVTCOUNT;
        if (old_event != new_event) {
            uint64_t count = 0;
            if (event_supported(new_event)) {
                uint16_t event_idx = supported_event_map[new_event];
                count = pm_events[event_idx].get_count(env);
            }
            env->cp15.c14_pmevcntr_delta[counter] = count;
        }

        env->cp15.c14_pmevtyper[counter] = value & PMXEVTYPER_MASK;
        pmevcntr_op_finish(env, counter);
    }
    /* Attempts to access PMXEVTYPER are CONSTRAINED UNPREDICTABLE when
     * PMSELR value is equal to or greater than the number of implemented
     * counters, but not equal to 0x1f. We opt to behave as a RAZ/WI.
     */
}

static uint64_t pmevtyper_read(CPUARMState *env, const ARMCPRegInfo *ri,
                               const uint8_t counter)
{
    if (counter == 31) {
        return env->cp15.pmccfiltr_el0;
    } else if (counter < pmu_num_counters(env)) {
        return env->cp15.c14_pmevtyper[counter];
    } else {
      /*
       * We opt to behave as a RAZ/WI when attempts to access PMXEVTYPER
       * are CONSTRAINED UNPREDICTABLE. See comments in pmevtyper_write().
       */
        return 0;
    }
}

static void pmevtyper_writefn(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    pmevtyper_write(env, ri, value, counter);
}

static void pmevtyper_rawwrite(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    env->cp15.c14_pmevtyper[counter] = value;

    /*
     * pmevtyper_rawwrite is called between a pair of pmu_op_start and
     * pmu_op_finish calls when loading saved state for a migration. Because
     * we're potentially updating the type of event here, the value written to
     * c14_pmevcntr_delta by the preceeding pmu_op_start call may be for a
     * different counter type. Therefore, we need to set this value to the
     * current count for the counter type we're writing so that pmu_op_finish
     * has the correct count for its calculation.
     */
    uint16_t event = value & PMXEVTYPER_EVTCOUNT;
    if (event_supported(event)) {
        uint16_t event_idx = supported_event_map[event];
        env->cp15.c14_pmevcntr_delta[counter] =
            pm_events[event_idx].get_count(env);
    }
}

static uint64_t pmevtyper_readfn(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    return pmevtyper_read(env, ri, counter);
}

static void pmxevtyper_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    pmevtyper_write(env, ri, value, env->cp15.c9_pmselr & 31);
}

static uint64_t pmxevtyper_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return pmevtyper_read(env, ri, env->cp15.c9_pmselr & 31);
}

static void pmevcntr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value, uint8_t counter)
{
    if (counter < pmu_num_counters(env)) {
        pmevcntr_op_start(env, counter);
        env->cp15.c14_pmevcntr[counter] = value;
        pmevcntr_op_finish(env, counter);
    }
    /*
     * We opt to behave as a RAZ/WI when attempts to access PM[X]EVCNTR
     * are CONSTRAINED UNPREDICTABLE.
     */
}

static uint64_t pmevcntr_read(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint8_t counter)
{
    if (counter < pmu_num_counters(env)) {
        uint64_t ret;
        pmevcntr_op_start(env, counter);
        ret = env->cp15.c14_pmevcntr[counter];
        pmevcntr_op_finish(env, counter);
        return ret;
    } else {
      /* We opt to behave as a RAZ/WI when attempts to access PM[X]EVCNTR
       * are CONSTRAINED UNPREDICTABLE. */
        return 0;
    }
}

static void pmevcntr_writefn(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    pmevcntr_write(env, ri, value, counter);
}

static uint64_t pmevcntr_readfn(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    return pmevcntr_read(env, ri, counter);
}

static void pmevcntr_rawwrite(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    assert(counter < pmu_num_counters(env));
    env->cp15.c14_pmevcntr[counter] = value;
    pmevcntr_write(env, ri, value, counter);
}

static uint64_t pmevcntr_rawread(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint8_t counter = ((ri->crm & 3) << 3) | (ri->opc2 & 7);
    assert(counter < pmu_num_counters(env));
    return env->cp15.c14_pmevcntr[counter];
}

static void pmxevcntr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    pmevcntr_write(env, ri, value, env->cp15.c9_pmselr & 31);
}

static uint64_t pmxevcntr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return pmevcntr_read(env, ri, env->cp15.c9_pmselr & 31);
}

static void pmuserenr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    if (arm_feature(env, ARM_FEATURE_V8)) {
        env->cp15.c9_pmuserenr = value & 0xf;
    } else {
        env->cp15.c9_pmuserenr = value & 1;
    }
}

static void pmintenset_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    /* We have no event counters so only the C bit can be changed */
    value &= pmu_counter_mask(env);
    env->cp15.c9_pminten |= value;
    pmu_update_irq(env);
}

static void pmintenclr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    value &= pmu_counter_mask(env);
    env->cp15.c9_pminten &= ~value;
    pmu_update_irq(env);
}

static void vbar_write(CPUARMState *env, const ARMCPRegInfo *ri,
                       uint64_t value)
{
    /* Note that even though the AArch64 view of this register has bits
     * [10:0] all RES0 we can only mask the bottom 5, to comply with the
     * architectural requirements for bits which are RES0 only in some
     * contexts. (ARMv8 would permit us to do no masking at all, but ARMv7
     * requires the bottom five bits to be RAZ/WI because they're UNK/SBZP.)
     */
    raw_write(env, ri, value & ~0x1FULL);
}

static void scr_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    /* Begin with base v8.0 state.  */
    uint32_t valid_mask = 0x3fff;
    ARMCPU *cpu = env_archcpu(env);

    if (ri->state == ARM_CP_STATE_AA64) {
        value |= SCR_FW | SCR_AW;   /* these two bits are RES1.  */
        valid_mask &= ~SCR_NET;

        if (cpu_isar_feature(aa64_lor, cpu)) {
            valid_mask |= SCR_TLOR;
        }
        if (cpu_isar_feature(aa64_pauth, cpu)) {
            valid_mask |= SCR_API | SCR_APK;
        }
        if (cpu_isar_feature(aa64_mte, cpu)) {
            valid_mask |= SCR_ATA;
        }
    } else {
        valid_mask &= ~(SCR_RW | SCR_ST);
    }

    if (!arm_feature(env, ARM_FEATURE_EL2)) {
        valid_mask &= ~SCR_HCE;

        /* On ARMv7, SMD (or SCD as it is called in v7) is only
         * supported if EL2 exists. The bit is UNK/SBZP when
         * EL2 is unavailable. In QEMU ARMv7, we force it to always zero
         * when EL2 is unavailable.
         * On ARMv8, this bit is always available.
         */
        if (arm_feature(env, ARM_FEATURE_V7) &&
            !arm_feature(env, ARM_FEATURE_V8)) {
            valid_mask &= ~SCR_SMD;
        }
    }

    /* Clear all-context RES0 bits.  */
    value &= valid_mask;
    raw_write(env, ri, value);
}

static CPAccessResult access_aa64_tid2(CPUARMState *env,
                                       const ARMCPRegInfo *ri,
                                       bool isread)
{
    if (arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_TID2)) {
        return CP_ACCESS_TRAP_EL2;
    }

    return CP_ACCESS_OK;
}

static uint64_t ccsidr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);

    /* Acquire the CSSELR index from the bank corresponding to the CCSIDR
     * bank
     */
    uint32_t index = A32_BANKED_REG_GET(env, csselr,
                                        ri->secure & ARM_CP_SECSTATE_S);

    return cpu->ccsidr[index];
}

static void csselr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    raw_write(env, ri, value & 0xf);
}

static uint64_t isr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    CPUState *cs = env_cpu(env);
    uint64_t hcr_el2 = arm_hcr_el2_eff(env);
    uint64_t ret = 0;
    bool allow_virt = (arm_current_el(env) == 1 &&
                       (!arm_is_secure_below_el3(env) ||
                        (env->cp15.scr_el3 & SCR_EEL2)));

    if (allow_virt && (hcr_el2 & HCR_IMO)) {
        if (cs->interrupt_request & CPU_INTERRUPT_VIRQ) {
            ret |= CPSR_I;
        }
    } else {
        if (cs->interrupt_request & CPU_INTERRUPT_HARD) {
            ret |= CPSR_I;
        }
    }

    if (allow_virt && (hcr_el2 & HCR_FMO)) {
        if (cs->interrupt_request & CPU_INTERRUPT_VFIQ) {
            ret |= CPSR_F;
        }
    } else {
        if (cs->interrupt_request & CPU_INTERRUPT_FIQ) {
            ret |= CPSR_F;
        }
    }

    /* External aborts are not possible in QEMU so A bit is always clear */
    return ret;
}

static CPAccessResult access_aa64_tid1(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    if (arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_TID1)) {
        return CP_ACCESS_TRAP_EL2;
    }

    return CP_ACCESS_OK;
}

static CPAccessResult access_aa32_tid1(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    if (arm_feature(env, ARM_FEATURE_V8)) {
        return access_aa64_tid1(env, ri, isread);
    }

    return CP_ACCESS_OK;
}

static const ARMCPRegInfo v7_cp_reginfo[] = {
    /* the old v6 WFI, UNPREDICTABLE in v7 but we choose to NOP */
    { .name = "NOP", .cp = 15, .crn = 7, .crm = 0, .opc1 = 0, .opc2 = 4,
      .access = PL1_W, .type = ARM_CP_NOP },
    /* Performance monitors are implementation defined in v7,
     * but with an ARM recommended set of registers, which we
     * follow.
     *
     * Performance registers fall into three categories:
     *  (a) always UNDEF in PL0, RW in PL1 (PMINTENSET, PMINTENCLR)
     *  (b) RO in PL0 (ie UNDEF on write), RW in PL1 (PMUSERENR)
     *  (c) UNDEF in PL0 if PMUSERENR.EN==0, otherwise accessible (all others)
     * For the cases controlled by PMUSERENR we must set .access to PL0_RW
     * or PL0_RO as appropriate and then check PMUSERENR in the helper fn.
     */
    { .name = "PMCNTENSET", .cp = 15, .crn = 9, .crm = 12, .opc1 = 0, .opc2 = 1,
      .access = PL0_RW, .type = ARM_CP_ALIAS,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmcnten),
      .writefn = pmcntenset_write,
      .accessfn = pmreg_access,
      .raw_writefn = raw_write },
    { .name = "PMCNTENSET_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 1,
      .access = PL0_RW, .accessfn = pmreg_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pmcnten), .resetvalue = 0,
      .writefn = pmcntenset_write, .raw_writefn = raw_write },
    { .name = "PMCNTENCLR", .cp = 15, .crn = 9, .crm = 12, .opc1 = 0, .opc2 = 2,
      .access = PL0_RW,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmcnten),
      .accessfn = pmreg_access,
      .writefn = pmcntenclr_write,
      .type = ARM_CP_ALIAS },
    { .name = "PMCNTENCLR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 2,
      .access = PL0_RW, .accessfn = pmreg_access,
      .type = ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pmcnten),
      .writefn = pmcntenclr_write },
    { .name = "PMOVSR", .cp = 15, .crn = 9, .crm = 12, .opc1 = 0, .opc2 = 3,
      .access = PL0_RW, .type = ARM_CP_IO,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmovsr),
      .accessfn = pmreg_access,
      .writefn = pmovsr_write,
      .raw_writefn = raw_write },
    { .name = "PMOVSCLR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 3,
      .access = PL0_RW, .accessfn = pmreg_access,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pmovsr),
      .writefn = pmovsr_write,
      .raw_writefn = raw_write },
    { .name = "PMSWINC", .cp = 15, .crn = 9, .crm = 12, .opc1 = 0, .opc2 = 4,
      .access = PL0_W, .accessfn = pmreg_access_swinc,
      .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .writefn = pmswinc_write },
    { .name = "PMSWINC_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 4,
      .access = PL0_W, .accessfn = pmreg_access_swinc,
      .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .writefn = pmswinc_write },
    { .name = "PMSELR", .cp = 15, .crn = 9, .crm = 12, .opc1 = 0, .opc2 = 5,
      .access = PL0_RW, .type = ARM_CP_ALIAS,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmselr),
      .accessfn = pmreg_access_selr, .writefn = pmselr_write,
      .raw_writefn = raw_write},
    { .name = "PMSELR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 5,
      .access = PL0_RW, .accessfn = pmreg_access_selr,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pmselr),
      .writefn = pmselr_write, .raw_writefn = raw_write, },
    { .name = "PMCCNTR", .cp = 15, .crn = 9, .crm = 13, .opc1 = 0, .opc2 = 0,
      .access = PL0_RW, .resetvalue = 0, .type = ARM_CP_ALIAS | ARM_CP_IO,
      .readfn = pmccntr_read, .writefn = pmccntr_write32,
      .accessfn = pmreg_access_ccntr },
    { .name = "PMCCNTR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 13, .opc2 = 0,
      .access = PL0_RW, .accessfn = pmreg_access_ccntr,
      .type = ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_ccnt),
      .readfn = pmccntr_read, .writefn = pmccntr_write,
      .raw_readfn = raw_read, .raw_writefn = raw_write, },
    { .name = "PMCCFILTR", .cp = 15, .opc1 = 0, .crn = 14, .crm = 15, .opc2 = 7,
      .writefn = pmccfiltr_write_a32, .readfn = pmccfiltr_read_a32,
      .access = PL0_RW, .accessfn = pmreg_access,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .resetvalue = 0, },
    { .name = "PMCCFILTR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 15, .opc2 = 7,
      .writefn = pmccfiltr_write, .raw_writefn = raw_write,
      .access = PL0_RW, .accessfn = pmreg_access,
      .type = ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.pmccfiltr_el0),
      .resetvalue = 0, },
    { .name = "PMXEVTYPER", .cp = 15, .crn = 9, .crm = 13, .opc1 = 0, .opc2 = 1,
      .access = PL0_RW, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = pmreg_access,
      .writefn = pmxevtyper_write, .readfn = pmxevtyper_read },
    { .name = "PMXEVTYPER_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 13, .opc2 = 1,
      .access = PL0_RW, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = pmreg_access,
      .writefn = pmxevtyper_write, .readfn = pmxevtyper_read },
    { .name = "PMXEVCNTR", .cp = 15, .crn = 9, .crm = 13, .opc1 = 0, .opc2 = 2,
      .access = PL0_RW, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = pmreg_access_xevcntr,
      .writefn = pmxevcntr_write, .readfn = pmxevcntr_read },
    { .name = "PMXEVCNTR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 13, .opc2 = 2,
      .access = PL0_RW, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = pmreg_access_xevcntr,
      .writefn = pmxevcntr_write, .readfn = pmxevcntr_read },
    { .name = "PMUSERENR", .cp = 15, .crn = 9, .crm = 14, .opc1 = 0, .opc2 = 0,
      .access = PL0_R | PL1_RW, .accessfn = access_tpm,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmuserenr),
      .resetvalue = 0,
      .writefn = pmuserenr_write, .raw_writefn = raw_write },
    { .name = "PMUSERENR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 14, .opc2 = 0,
      .access = PL0_R | PL1_RW, .accessfn = access_tpm, .type = ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pmuserenr),
      .resetvalue = 0,
      .writefn = pmuserenr_write, .raw_writefn = raw_write },
    { .name = "PMINTENSET", .cp = 15, .crn = 9, .crm = 14, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tpm,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pminten),
      .resetvalue = 0,
      .writefn = pmintenset_write, .raw_writefn = raw_write },
    { .name = "PMINTENSET_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 9, .crm = 14, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tpm,
      .type = ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pminten),
      .writefn = pmintenset_write, .raw_writefn = raw_write,
      .resetvalue = 0x0 },
    { .name = "PMINTENCLR", .cp = 15, .crn = 9, .crm = 14, .opc1 = 0, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tpm,
      .type = ARM_CP_ALIAS | ARM_CP_IO | ARM_CP_NO_RAW,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pminten),
      .writefn = pmintenclr_write, },
    { .name = "PMINTENCLR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 9, .crm = 14, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tpm,
      .type = ARM_CP_ALIAS | ARM_CP_IO | ARM_CP_NO_RAW,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pminten),
      .writefn = pmintenclr_write },
    { .name = "CCSIDR", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .crn = 0, .crm = 0, .opc1 = 1, .opc2 = 0,
      .access = PL1_R,
      .accessfn = access_aa64_tid2,
      .readfn = ccsidr_read, .type = ARM_CP_NO_RAW },
    { .name = "CSSELR", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .crn = 0, .crm = 0, .opc1 = 2, .opc2 = 0,
      .access = PL1_RW,
      .accessfn = access_aa64_tid2,
      .writefn = csselr_write, .resetvalue = 0,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.csselr_s),
                             offsetof(CPUARMState, cp15.csselr_ns) } },
    /* Auxiliary ID register: this actually has an IMPDEF value but for now
     * just RAZ for all cores:
     */
    { .name = "AIDR", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 1, .crn = 0, .crm = 0, .opc2 = 7,
      .access = PL1_R, .type = ARM_CP_CONST,
      .accessfn = access_aa64_tid1,
      .resetvalue = 0 },
    /* Auxiliary fault status registers: these also are IMPDEF, and we
     * choose to RAZ/WI for all cores.
     */
    { .name = "AFSR0_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 5, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "AFSR1_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 5, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    /* MAIR can just read-as-written because we don't implement caches
     * and so don't need to care about memory attributes.
     */
    { .name = "MAIR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .fieldoffset = offsetof(CPUARMState, cp15.mair_el[1]),
      .resetvalue = 0 },
    { .name = "MAIR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 10, .crm = 2, .opc2 = 0,
      .access = PL3_RW, .fieldoffset = offsetof(CPUARMState, cp15.mair_el[3]),
      .resetvalue = 0 },
    /* For non-long-descriptor page tables these are PRRR and NMRR;
     * regardless they still act as reads-as-written for QEMU.
     */
     /* MAIR0/1 are defined separately from their 64-bit counterpart which
      * allows them to assign the correct fieldoffset based on the endianness
      * handled in the field definitions.
      */
    { .name = "MAIR0", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 10, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.mair0_s),
                             offsetof(CPUARMState, cp15.mair0_ns) },
      .resetfn = arm_cp_reset_ignore },
    { .name = "MAIR1", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 10, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.mair1_s),
                             offsetof(CPUARMState, cp15.mair1_ns) },
      .resetfn = arm_cp_reset_ignore },
    { .name = "ISR_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 1, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL1_R, .readfn = isr_read },
    /* 32 bit ITLB invalidates */
    { .name = "ITLBIALL", .cp = 15, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiall_write },
    { .name = "ITLBIMVA", .cp = 15, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimva_write },
    { .name = "ITLBIASID", .cp = 15, .opc1 = 0, .crn = 8, .crm = 5, .opc2 = 2,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiasid_write },
    /* 32 bit DTLB invalidates */
    { .name = "DTLBIALL", .cp = 15, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiall_write },
    { .name = "DTLBIMVA", .cp = 15, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimva_write },
    { .name = "DTLBIASID", .cp = 15, .opc1 = 0, .crn = 8, .crm = 6, .opc2 = 2,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiasid_write },
    /* 32 bit TLB invalidates */
    { .name = "TLBIALL", .cp = 15, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiall_write },
    { .name = "TLBIMVA", .cp = 15, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimva_write },
    { .name = "TLBIASID", .cp = 15, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 2,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiasid_write },
    { .name = "TLBIMVAA", .cp = 15, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 3,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimvaa_write },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo v7mp_cp_reginfo[] = {
    /* 32 bit TLB invalidates, Inner Shareable */
    { .name = "TLBIALLIS", .cp = 15, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiall_is_write },
    { .name = "TLBIMVAIS", .cp = 15, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimva_is_write },
    { .name = "TLBIASIDIS", .cp = 15, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 2,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbiasid_is_write },
    { .name = "TLBIMVAAIS", .cp = 15, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 3,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimvaa_is_write },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo pmovsset_cp_reginfo[] = {
    /* PMOVSSET is not implemented in v7 before v7ve */
    { .name = "PMOVSSET", .cp = 15, .opc1 = 0, .crn = 9, .crm = 14, .opc2 = 3,
      .access = PL0_RW, .accessfn = pmreg_access,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmovsr),
      .writefn = pmovsset_write,
      .raw_writefn = raw_write },
    { .name = "PMOVSSET_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 14, .opc2 = 3,
      .access = PL0_RW, .accessfn = pmreg_access,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.c9_pmovsr),
      .writefn = pmovsset_write,
      .raw_writefn = raw_write },
    REGINFO_SENTINEL
};

static void teecr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    value &= 1;
    env->teecr = value;
}

static CPAccessResult teehbr_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                    bool isread)
{
    if (arm_current_el(env) == 0 && (env->teecr & 1)) {
        return CP_ACCESS_TRAP;
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo t2ee_cp_reginfo[] = {
    { .name = "TEECR", .cp = 14, .crn = 0, .crm = 0, .opc1 = 6, .opc2 = 0,
      .access = PL1_RW, .fieldoffset = offsetof(CPUARMState, teecr),
      .resetvalue = 0,
      .writefn = teecr_write },
    { .name = "TEEHBR", .cp = 14, .crn = 1, .crm = 0, .opc1 = 6, .opc2 = 0,
      .access = PL0_RW, .fieldoffset = offsetof(CPUARMState, teehbr),
      .accessfn = teehbr_access, .resetvalue = 0 },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo v6k_cp_reginfo[] = {
    { .name = "TPIDR_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 2, .crn = 13, .crm = 0,
      .access = PL0_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.tpidr_el[0]), .resetvalue = 0 },
    { .name = "TPIDRURW", .cp = 15, .crn = 13, .crm = 0, .opc1 = 0, .opc2 = 2,
      .access = PL0_RW,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.tpidrurw_s),
                             offsetoflow32(CPUARMState, cp15.tpidrurw_ns) },
      .resetfn = arm_cp_reset_ignore },
    { .name = "TPIDRRO_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 3, .crn = 13, .crm = 0,
      .access = PL0_R|PL1_W,
      .fieldoffset = offsetof(CPUARMState, cp15.tpidrro_el[0]),
      .resetvalue = 0},
    { .name = "TPIDRURO", .cp = 15, .crn = 13, .crm = 0, .opc1 = 0, .opc2 = 3,
      .access = PL0_R|PL1_W,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.tpidruro_s),
                             offsetoflow32(CPUARMState, cp15.tpidruro_ns) },
      .resetfn = arm_cp_reset_ignore },
    { .name = "TPIDR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .opc2 = 4, .crn = 13, .crm = 0,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.tpidr_el[1]), .resetvalue = 0 },
    { .name = "TPIDRPRW", .opc1 = 0, .cp = 15, .crn = 13, .crm = 0, .opc2 = 4,
      .access = PL1_RW,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.tpidrprw_s),
                             offsetoflow32(CPUARMState, cp15.tpidrprw_ns) },
      .resetvalue = 0 },
    REGINFO_SENTINEL
};

#ifndef CONFIG_USER_ONLY

static CPAccessResult gt_cntfrq_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    /* CNTFRQ: not visible from PL0 if both PL0PCTEN and PL0VCTEN are zero.
     * Writable only at the highest implemented exception level.
     */
    int el = arm_current_el(env);
    uint64_t hcr;
    uint32_t cntkctl;

    switch (el) {
    case 0:
        hcr = arm_hcr_el2_eff(env);
        if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
            cntkctl = env->cp15.cnthctl_el2;
        } else {
            cntkctl = env->cp15.c14_cntkctl;
        }
        if (!extract32(cntkctl, 0, 2)) {
            return CP_ACCESS_TRAP;
        }
        break;
    case 1:
        if (!isread && ri->state == ARM_CP_STATE_AA32 &&
            arm_is_secure_below_el3(env)) {
            /* Accesses from 32-bit Secure EL1 UNDEF (*not* trap to EL3!) */
            return CP_ACCESS_TRAP_UNCATEGORIZED;
        }
        break;
    case 2:
    case 3:
        break;
    }

    if (!isread && el < arm_highest_el(env)) {
        return CP_ACCESS_TRAP_UNCATEGORIZED;
    }

    return CP_ACCESS_OK;
}

static CPAccessResult gt_counter_access(CPUARMState *env, int timeridx,
                                        bool isread)
{
    unsigned int cur_el = arm_current_el(env);
    bool secure = arm_is_secure(env);
    uint64_t hcr = arm_hcr_el2_eff(env);

    switch (cur_el) {
    case 0:
        /* If HCR_EL2.<E2H,TGE> == '11': check CNTHCTL_EL2.EL0[PV]CTEN. */
        if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
            return (extract32(env->cp15.cnthctl_el2, timeridx, 1)
                    ? CP_ACCESS_OK : CP_ACCESS_TRAP_EL2);
        }

        /* CNT[PV]CT: not visible from PL0 if EL0[PV]CTEN is zero */
        if (!extract32(env->cp15.c14_cntkctl, timeridx, 1)) {
            return CP_ACCESS_TRAP;
        }

        /* If HCR_EL2.<E2H,TGE> == '10': check CNTHCTL_EL2.EL1PCTEN. */
        if (hcr & HCR_E2H) {
            if (timeridx == GTIMER_PHYS &&
                !extract32(env->cp15.cnthctl_el2, 10, 1)) {
                return CP_ACCESS_TRAP_EL2;
            }
        } else {
            /* If HCR_EL2.<E2H> == 0: check CNTHCTL_EL2.EL1PCEN. */
            if (arm_feature(env, ARM_FEATURE_EL2) &&
                timeridx == GTIMER_PHYS && !secure &&
                !extract32(env->cp15.cnthctl_el2, 1, 1)) {
                return CP_ACCESS_TRAP_EL2;
            }
        }
        break;

    case 1:
        /* Check CNTHCTL_EL2.EL1PCTEN, which changes location based on E2H. */
        if (arm_feature(env, ARM_FEATURE_EL2) &&
            timeridx == GTIMER_PHYS && !secure &&
            (hcr & HCR_E2H
             ? !extract32(env->cp15.cnthctl_el2, 10, 1)
             : !extract32(env->cp15.cnthctl_el2, 0, 1))) {
            return CP_ACCESS_TRAP_EL2;
        }
        break;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult gt_timer_access(CPUARMState *env, int timeridx,
                                      bool isread)
{
    unsigned int cur_el = arm_current_el(env);
    bool secure = arm_is_secure(env);
    uint64_t hcr = arm_hcr_el2_eff(env);

    switch (cur_el) {
    case 0:
        if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
            /* If HCR_EL2.<E2H,TGE> == '11': check CNTHCTL_EL2.EL0[PV]TEN. */
            return (extract32(env->cp15.cnthctl_el2, 9 - timeridx, 1)
                    ? CP_ACCESS_OK : CP_ACCESS_TRAP_EL2);
        }

        /*
         * CNT[PV]_CVAL, CNT[PV]_CTL, CNT[PV]_TVAL: not visible from
         * EL0 if EL0[PV]TEN is zero.
         */
        if (!extract32(env->cp15.c14_cntkctl, 9 - timeridx, 1)) {
            return CP_ACCESS_TRAP;
        }
        /* fall through */

    case 1:
        if (arm_feature(env, ARM_FEATURE_EL2) &&
            timeridx == GTIMER_PHYS && !secure) {
            if (hcr & HCR_E2H) {
                /* If HCR_EL2.<E2H,TGE> == '10': check CNTHCTL_EL2.EL1PTEN. */
                if (!extract32(env->cp15.cnthctl_el2, 11, 1)) {
                    return CP_ACCESS_TRAP_EL2;
                }
            } else {
                /* If HCR_EL2.<E2H> == 0: check CNTHCTL_EL2.EL1PCEN. */
                if (!extract32(env->cp15.cnthctl_el2, 1, 1)) {
                    return CP_ACCESS_TRAP_EL2;
                }
            }
        }
        break;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult gt_pct_access(CPUARMState *env,
                                    const ARMCPRegInfo *ri,
                                    bool isread)
{
    return gt_counter_access(env, GTIMER_PHYS, isread);
}

static CPAccessResult gt_vct_access(CPUARMState *env,
                                    const ARMCPRegInfo *ri,
                                    bool isread)
{
    return gt_counter_access(env, GTIMER_VIRT, isread);
}

static CPAccessResult gt_ptimer_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    return gt_timer_access(env, GTIMER_PHYS, isread);
}

static CPAccessResult gt_vtimer_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    return gt_timer_access(env, GTIMER_VIRT, isread);
}

static CPAccessResult gt_stimer_access(CPUARMState *env,
                                       const ARMCPRegInfo *ri,
                                       bool isread)
{
    /* The AArch64 register view of the secure physical timer is
     * always accessible from EL3, and configurably accessible from
     * Secure EL1.
     */
    switch (arm_current_el(env)) {
    case 1:
        if (!arm_is_secure(env)) {
            return CP_ACCESS_TRAP;
        }
        if (!(env->cp15.scr_el3 & SCR_ST)) {
            return CP_ACCESS_TRAP_EL3;
        }
        return CP_ACCESS_OK;
    case 0:
    case 2:
        return CP_ACCESS_TRAP;
    case 3:
        return CP_ACCESS_OK;
    default:
        g_assert_not_reached();
    }
}

static uint64_t gt_get_countervalue(CPUARMState *env)
{
    ARMCPU *cpu = env_archcpu(env);

    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / gt_cntfrq_period_ns(cpu);
}

static void gt_recalc_timer(ARMCPU *cpu, int timeridx)
{
    ARMGenericTimer *gt = &cpu->env.cp15.c14_timer[timeridx];

    if (gt->ctl & 1) {
        /* Timer enabled: calculate and set current ISTATUS, irq, and
         * reset timer to when ISTATUS next has to change
         */
        uint64_t offset = timeridx == GTIMER_VIRT ?
                                      cpu->env.cp15.cntvoff_el2 : 0;
        uint64_t count = gt_get_countervalue(&cpu->env);
        /* Note that this must be unsigned 64 bit arithmetic: */
        int istatus = count - offset >= gt->cval;
        uint64_t nexttick;
        int irqstate;

        gt->ctl = deposit32(gt->ctl, 2, 1, istatus);

        irqstate = (istatus && !(gt->ctl & 2));
        qemu_set_irq(cpu->gt_timer_outputs[timeridx], irqstate);

        if (istatus) {
            /* Next transition is when count rolls back over to zero */
            nexttick = UINT64_MAX;
        } else {
            /* Next transition is when we hit cval */
            nexttick = gt->cval + offset;
        }
        /* Note that the desired next expiry time might be beyond the
         * signed-64-bit range of a QEMUTimer -- in this case we just
         * set the timer for as far in the future as possible. When the
         * timer expires we will reset the timer for any remaining period.
         */
        if (nexttick > INT64_MAX / gt_cntfrq_period_ns(cpu)) {
            timer_mod_ns(cpu->gt_timer[timeridx], INT64_MAX);
        } else {
            timer_mod(cpu->gt_timer[timeridx], nexttick);
        }
        trace_arm_gt_recalc(timeridx, irqstate, nexttick);
    } else {
        /* Timer disabled: ISTATUS and timer output always clear */
        gt->ctl &= ~4;
        qemu_set_irq(cpu->gt_timer_outputs[timeridx], 0);
        timer_del(cpu->gt_timer[timeridx]);
        trace_arm_gt_recalc_disabled(timeridx);
    }
}

static void gt_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri,
                           int timeridx)
{
    ARMCPU *cpu = env_archcpu(env);

    timer_del(cpu->gt_timer[timeridx]);
}

static uint64_t gt_cnt_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return gt_get_countervalue(env);
}

static uint64_t gt_virt_cnt_offset(CPUARMState *env)
{
    uint64_t hcr;

    switch (arm_current_el(env)) {
    case 2:
        hcr = arm_hcr_el2_eff(env);
        if (hcr & HCR_E2H) {
            return 0;
        }
        break;
    case 0:
        hcr = arm_hcr_el2_eff(env);
        if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
            return 0;
        }
        break;
    }

    return env->cp15.cntvoff_el2;
}

static uint64_t gt_virt_cnt_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return gt_get_countervalue(env) - gt_virt_cnt_offset(env);
}

static void gt_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          int timeridx,
                          uint64_t value)
{
    trace_arm_gt_cval_write(timeridx, value);
    env->cp15.c14_timer[timeridx].cval = value;
    gt_recalc_timer(env_archcpu(env), timeridx);
}

static uint64_t gt_tval_read(CPUARMState *env, const ARMCPRegInfo *ri,
                             int timeridx)
{
    uint64_t offset = 0;

    switch (timeridx) {
    case GTIMER_VIRT:
    case GTIMER_HYPVIRT:
        offset = gt_virt_cnt_offset(env);
        break;
    }

    return (uint32_t)(env->cp15.c14_timer[timeridx].cval -
                      (gt_get_countervalue(env) - offset));
}

static void gt_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          int timeridx,
                          uint64_t value)
{
    uint64_t offset = 0;

    switch (timeridx) {
    case GTIMER_VIRT:
    case GTIMER_HYPVIRT:
        offset = gt_virt_cnt_offset(env);
        break;
    }

    trace_arm_gt_tval_write(timeridx, value);
    env->cp15.c14_timer[timeridx].cval = gt_get_countervalue(env) - offset +
                                         sextract64(value, 0, 32);
    gt_recalc_timer(env_archcpu(env), timeridx);
}

static void gt_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         int timeridx,
                         uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    uint32_t oldval = env->cp15.c14_timer[timeridx].ctl;

    trace_arm_gt_ctl_write(timeridx, value);
    env->cp15.c14_timer[timeridx].ctl = deposit64(oldval, 0, 2, value);
    if ((oldval ^ value) & 1) {
        /* Enable toggled */
        gt_recalc_timer(cpu, timeridx);
    } else if ((oldval ^ value) & 2) {
        /* IMASK toggled: don't need to recalculate,
         * just set the interrupt line based on ISTATUS
         */
        int irqstate = (oldval & 4) && !(value & 2);

        trace_arm_gt_imask_toggle(timeridx, irqstate);
        qemu_set_irq(cpu->gt_timer_outputs[timeridx], irqstate);
    }
}

static void gt_phys_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    gt_timer_reset(env, ri, GTIMER_PHYS);
}

static void gt_phys_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    gt_cval_write(env, ri, GTIMER_PHYS, value);
}

static uint64_t gt_phys_tval_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return gt_tval_read(env, ri, GTIMER_PHYS);
}

static void gt_phys_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    gt_tval_write(env, ri, GTIMER_PHYS, value);
}

static void gt_phys_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_ctl_write(env, ri, GTIMER_PHYS, value);
}

static int gt_phys_redir_timeridx(CPUARMState *env)
{
    switch (arm_mmu_idx(env)) {
    case ARMMMUIdx_E20_0:
    case ARMMMUIdx_E20_2:
    case ARMMMUIdx_E20_2_PAN:
        return GTIMER_HYP;
    default:
        return GTIMER_PHYS;
    }
}

static int gt_virt_redir_timeridx(CPUARMState *env)
{
    switch (arm_mmu_idx(env)) {
    case ARMMMUIdx_E20_0:
    case ARMMMUIdx_E20_2:
    case ARMMMUIdx_E20_2_PAN:
        return GTIMER_HYPVIRT;
    default:
        return GTIMER_VIRT;
    }
}

static uint64_t gt_phys_redir_cval_read(CPUARMState *env,
                                        const ARMCPRegInfo *ri)
{
    int timeridx = gt_phys_redir_timeridx(env);
    return env->cp15.c14_timer[timeridx].cval;
}

static void gt_phys_redir_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                     uint64_t value)
{
    int timeridx = gt_phys_redir_timeridx(env);
    gt_cval_write(env, ri, timeridx, value);
}

static uint64_t gt_phys_redir_tval_read(CPUARMState *env,
                                        const ARMCPRegInfo *ri)
{
    int timeridx = gt_phys_redir_timeridx(env);
    return gt_tval_read(env, ri, timeridx);
}

static void gt_phys_redir_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                     uint64_t value)
{
    int timeridx = gt_phys_redir_timeridx(env);
    gt_tval_write(env, ri, timeridx, value);
}

static uint64_t gt_phys_redir_ctl_read(CPUARMState *env,
                                       const ARMCPRegInfo *ri)
{
    int timeridx = gt_phys_redir_timeridx(env);
    return env->cp15.c14_timer[timeridx].ctl;
}

static void gt_phys_redir_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    int timeridx = gt_phys_redir_timeridx(env);
    gt_ctl_write(env, ri, timeridx, value);
}

static void gt_virt_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    gt_timer_reset(env, ri, GTIMER_VIRT);
}

static void gt_virt_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    gt_cval_write(env, ri, GTIMER_VIRT, value);
}

static uint64_t gt_virt_tval_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return gt_tval_read(env, ri, GTIMER_VIRT);
}

static void gt_virt_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    gt_tval_write(env, ri, GTIMER_VIRT, value);
}

static void gt_virt_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_ctl_write(env, ri, GTIMER_VIRT, value);
}

static void gt_cntvoff_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    trace_arm_gt_cntvoff_write(value);
    raw_write(env, ri, value);
    gt_recalc_timer(cpu, GTIMER_VIRT);
}

static uint64_t gt_virt_redir_cval_read(CPUARMState *env,
                                        const ARMCPRegInfo *ri)
{
    int timeridx = gt_virt_redir_timeridx(env);
    return env->cp15.c14_timer[timeridx].cval;
}

static void gt_virt_redir_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                     uint64_t value)
{
    int timeridx = gt_virt_redir_timeridx(env);
    gt_cval_write(env, ri, timeridx, value);
}

static uint64_t gt_virt_redir_tval_read(CPUARMState *env,
                                        const ARMCPRegInfo *ri)
{
    int timeridx = gt_virt_redir_timeridx(env);
    return gt_tval_read(env, ri, timeridx);
}

static void gt_virt_redir_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                     uint64_t value)
{
    int timeridx = gt_virt_redir_timeridx(env);
    gt_tval_write(env, ri, timeridx, value);
}

static uint64_t gt_virt_redir_ctl_read(CPUARMState *env,
                                       const ARMCPRegInfo *ri)
{
    int timeridx = gt_virt_redir_timeridx(env);
    return env->cp15.c14_timer[timeridx].ctl;
}

static void gt_virt_redir_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    int timeridx = gt_virt_redir_timeridx(env);
    gt_ctl_write(env, ri, timeridx, value);
}

static void gt_hyp_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    gt_timer_reset(env, ri, GTIMER_HYP);
}

static void gt_hyp_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_cval_write(env, ri, GTIMER_HYP, value);
}

static uint64_t gt_hyp_tval_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return gt_tval_read(env, ri, GTIMER_HYP);
}

static void gt_hyp_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_tval_write(env, ri, GTIMER_HYP, value);
}

static void gt_hyp_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_ctl_write(env, ri, GTIMER_HYP, value);
}

static void gt_sec_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    gt_timer_reset(env, ri, GTIMER_SEC);
}

static void gt_sec_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_cval_write(env, ri, GTIMER_SEC, value);
}

static uint64_t gt_sec_tval_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return gt_tval_read(env, ri, GTIMER_SEC);
}

static void gt_sec_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_tval_write(env, ri, GTIMER_SEC, value);
}

static void gt_sec_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    gt_ctl_write(env, ri, GTIMER_SEC, value);
}

static void gt_hv_timer_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    gt_timer_reset(env, ri, GTIMER_HYPVIRT);
}

static void gt_hv_cval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    gt_cval_write(env, ri, GTIMER_HYPVIRT, value);
}

static uint64_t gt_hv_tval_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return gt_tval_read(env, ri, GTIMER_HYPVIRT);
}

static void gt_hv_tval_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    gt_tval_write(env, ri, GTIMER_HYPVIRT, value);
}

static void gt_hv_ctl_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    gt_ctl_write(env, ri, GTIMER_HYPVIRT, value);
}

void arm_gt_ptimer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    gt_recalc_timer(cpu, GTIMER_PHYS);
}

void arm_gt_vtimer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    gt_recalc_timer(cpu, GTIMER_VIRT);
}

void arm_gt_htimer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    gt_recalc_timer(cpu, GTIMER_HYP);
}

void arm_gt_stimer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    gt_recalc_timer(cpu, GTIMER_SEC);
}

void arm_gt_hvtimer_cb(void *opaque)
{
    ARMCPU *cpu = opaque;

    gt_recalc_timer(cpu, GTIMER_HYPVIRT);
}

static void arm_gt_cntfrq_reset(CPUARMState *env, const ARMCPRegInfo *opaque)
{
    ARMCPU *cpu = env_archcpu(env);

    cpu->env.cp15.c14_cntfrq = cpu->gt_cntfrq_hz;
}

static const ARMCPRegInfo generic_timer_cp_reginfo[] = {
    /* Note that CNTFRQ is purely reads-as-written for the benefit
     * of software; writing it doesn't actually change the timer frequency.
     * Our reset value matches the fixed frequency we implement the timer at.
     */
    { .name = "CNTFRQ", .cp = 15, .crn = 14, .crm = 0, .opc1 = 0, .opc2 = 0,
      .type = ARM_CP_ALIAS,
      .access = PL1_RW | PL0_R, .accessfn = gt_cntfrq_access,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.c14_cntfrq),
    },
    { .name = "CNTFRQ_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 0, .opc2 = 0,
      .access = PL1_RW | PL0_R, .accessfn = gt_cntfrq_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_cntfrq),
      .resetfn = arm_gt_cntfrq_reset,
    },
    /* overall control: mostly access permissions */
    { .name = "CNTKCTL", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 14, .crm = 1, .opc2 = 0,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_cntkctl),
      .resetvalue = 0,
    },
    /* per-timer control */
    { .name = "CNTP_CTL", .cp = 15, .crn = 14, .crm = 2, .opc1 = 0, .opc2 = 1,
      .secure = ARM_CP_SECSTATE_NS,
      .type = ARM_CP_IO | ARM_CP_ALIAS, .access = PL0_RW,
      .accessfn = gt_ptimer_access,
      .fieldoffset = offsetoflow32(CPUARMState,
                                   cp15.c14_timer[GTIMER_PHYS].ctl),
      .readfn = gt_phys_redir_ctl_read, .raw_readfn = raw_read,
      .writefn = gt_phys_redir_ctl_write, .raw_writefn = raw_write,
    },
    { .name = "CNTP_CTL_S",
      .cp = 15, .crn = 14, .crm = 2, .opc1 = 0, .opc2 = 1,
      .secure = ARM_CP_SECSTATE_S,
      .type = ARM_CP_IO | ARM_CP_ALIAS, .access = PL0_RW,
      .accessfn = gt_ptimer_access,
      .fieldoffset = offsetoflow32(CPUARMState,
                                   cp15.c14_timer[GTIMER_SEC].ctl),
      .writefn = gt_sec_ctl_write, .raw_writefn = raw_write,
    },
    { .name = "CNTP_CTL_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 2, .opc2 = 1,
      .type = ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_ptimer_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_PHYS].ctl),
      .resetvalue = 0,
      .readfn = gt_phys_redir_ctl_read, .raw_readfn = raw_read,
      .writefn = gt_phys_redir_ctl_write, .raw_writefn = raw_write,
    },
    { .name = "CNTV_CTL", .cp = 15, .crn = 14, .crm = 3, .opc1 = 0, .opc2 = 1,
      .type = ARM_CP_IO | ARM_CP_ALIAS, .access = PL0_RW,
      .accessfn = gt_vtimer_access,
      .fieldoffset = offsetoflow32(CPUARMState,
                                   cp15.c14_timer[GTIMER_VIRT].ctl),
      .readfn = gt_virt_redir_ctl_read, .raw_readfn = raw_read,
      .writefn = gt_virt_redir_ctl_write, .raw_writefn = raw_write,
    },
    { .name = "CNTV_CTL_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 3, .opc2 = 1,
      .type = ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_vtimer_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_VIRT].ctl),
      .resetvalue = 0,
      .readfn = gt_virt_redir_ctl_read, .raw_readfn = raw_read,
      .writefn = gt_virt_redir_ctl_write, .raw_writefn = raw_write,
    },
    /* TimerValue views: a 32 bit downcounting view of the underlying state */
    { .name = "CNTP_TVAL", .cp = 15, .crn = 14, .crm = 2, .opc1 = 0, .opc2 = 0,
      .secure = ARM_CP_SECSTATE_NS,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_ptimer_access,
      .readfn = gt_phys_redir_tval_read, .writefn = gt_phys_redir_tval_write,
    },
    { .name = "CNTP_TVAL_S",
      .cp = 15, .crn = 14, .crm = 2, .opc1 = 0, .opc2 = 0,
      .secure = ARM_CP_SECSTATE_S,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_ptimer_access,
      .readfn = gt_sec_tval_read, .writefn = gt_sec_tval_write,
    },
    { .name = "CNTP_TVAL_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 2, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_ptimer_access, .resetfn = gt_phys_timer_reset,
      .readfn = gt_phys_redir_tval_read, .writefn = gt_phys_redir_tval_write,
    },
    { .name = "CNTV_TVAL", .cp = 15, .crn = 14, .crm = 3, .opc1 = 0, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_vtimer_access,
      .readfn = gt_virt_redir_tval_read, .writefn = gt_virt_redir_tval_write,
    },
    { .name = "CNTV_TVAL_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 3, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL0_RW,
      .accessfn = gt_vtimer_access, .resetfn = gt_virt_timer_reset,
      .readfn = gt_virt_redir_tval_read, .writefn = gt_virt_redir_tval_write,
    },
    /* The counter itself */
    { .name = "CNTPCT", .cp = 15, .crm = 14, .opc1 = 0,
      .access = PL0_R, .type = ARM_CP_64BIT | ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = gt_pct_access,
      .readfn = gt_cnt_read, .resetfn = arm_cp_reset_ignore,
    },
    { .name = "CNTPCT_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 0, .opc2 = 1,
      .access = PL0_R, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = gt_pct_access, .readfn = gt_cnt_read,
    },
    { .name = "CNTVCT", .cp = 15, .crm = 14, .opc1 = 1,
      .access = PL0_R, .type = ARM_CP_64BIT | ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = gt_vct_access,
      .readfn = gt_virt_cnt_read, .resetfn = arm_cp_reset_ignore,
    },
    { .name = "CNTVCT_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 0, .opc2 = 2,
      .access = PL0_R, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .accessfn = gt_vct_access, .readfn = gt_virt_cnt_read,
    },
    /* Comparison value, indicating when the timer goes off */
    { .name = "CNTP_CVAL", .cp = 15, .crm = 14, .opc1 = 2,
      .secure = ARM_CP_SECSTATE_NS,
      .access = PL0_RW,
      .type = ARM_CP_64BIT | ARM_CP_IO | ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_PHYS].cval),
      .accessfn = gt_ptimer_access,
      .readfn = gt_phys_redir_cval_read, .raw_readfn = raw_read,
      .writefn = gt_phys_redir_cval_write, .raw_writefn = raw_write,
    },
    { .name = "CNTP_CVAL_S", .cp = 15, .crm = 14, .opc1 = 2,
      .secure = ARM_CP_SECSTATE_S,
      .access = PL0_RW,
      .type = ARM_CP_64BIT | ARM_CP_IO | ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_SEC].cval),
      .accessfn = gt_ptimer_access,
      .writefn = gt_sec_cval_write, .raw_writefn = raw_write,
    },
    { .name = "CNTP_CVAL_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 2, .opc2 = 2,
      .access = PL0_RW,
      .type = ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_PHYS].cval),
      .resetvalue = 0, .accessfn = gt_ptimer_access,
      .readfn = gt_phys_redir_cval_read, .raw_readfn = raw_read,
      .writefn = gt_phys_redir_cval_write, .raw_writefn = raw_write,
    },
    { .name = "CNTV_CVAL", .cp = 15, .crm = 14, .opc1 = 3,
      .access = PL0_RW,
      .type = ARM_CP_64BIT | ARM_CP_IO | ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_VIRT].cval),
      .accessfn = gt_vtimer_access,
      .readfn = gt_virt_redir_cval_read, .raw_readfn = raw_read,
      .writefn = gt_virt_redir_cval_write, .raw_writefn = raw_write,
    },
    { .name = "CNTV_CVAL_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 3, .opc2 = 2,
      .access = PL0_RW,
      .type = ARM_CP_IO,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_VIRT].cval),
      .resetvalue = 0, .accessfn = gt_vtimer_access,
      .readfn = gt_virt_redir_cval_read, .raw_readfn = raw_read,
      .writefn = gt_virt_redir_cval_write, .raw_writefn = raw_write,
    },
    /* Secure timer -- this is actually restricted to only EL3
     * and configurably Secure-EL1 via the accessfn.
     */
    { .name = "CNTPS_TVAL_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 7, .crn = 14, .crm = 2, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL1_RW,
      .accessfn = gt_stimer_access,
      .readfn = gt_sec_tval_read,
      .writefn = gt_sec_tval_write,
      .resetfn = gt_sec_timer_reset,
    },
    { .name = "CNTPS_CTL_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 7, .crn = 14, .crm = 2, .opc2 = 1,
      .type = ARM_CP_IO, .access = PL1_RW,
      .accessfn = gt_stimer_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_SEC].ctl),
      .resetvalue = 0,
      .writefn = gt_sec_ctl_write, .raw_writefn = raw_write,
    },
    { .name = "CNTPS_CVAL_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 7, .crn = 14, .crm = 2, .opc2 = 2,
      .type = ARM_CP_IO, .access = PL1_RW,
      .accessfn = gt_stimer_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_SEC].cval),
      .writefn = gt_sec_cval_write, .raw_writefn = raw_write,
    },
    REGINFO_SENTINEL
};

static CPAccessResult e2h_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    if (!(arm_hcr_el2_eff(env) & HCR_E2H)) {
        return CP_ACCESS_TRAP;
    }
    return CP_ACCESS_OK;
}

#else

/* In user-mode most of the generic timer registers are inaccessible
 * however modern kernels (4.12+) allow access to cntvct_el0
 */

static uint64_t gt_virt_cnt_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);

    /* Currently we have no support for QEMUTimer in linux-user so we
     * can't call gt_get_countervalue(env), instead we directly
     * call the lower level functions.
     */
    return cpu_get_clock() / gt_cntfrq_period_ns(cpu);
}

static const ARMCPRegInfo generic_timer_cp_reginfo[] = {
    { .name = "CNTFRQ_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 0, .opc2 = 0,
      .type = ARM_CP_CONST, .access = PL0_R /* no PL1_RW in linux-user */,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_cntfrq),
      .resetvalue = NANOSECONDS_PER_SECOND / GTIMER_SCALE,
    },
    { .name = "CNTVCT_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 0, .opc2 = 2,
      .access = PL0_R, .type = ARM_CP_NO_RAW | ARM_CP_IO,
      .readfn = gt_virt_cnt_read,
    },
    REGINFO_SENTINEL
};

#endif

static void par_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    if (arm_feature(env, ARM_FEATURE_LPAE)) {
        raw_write(env, ri, value);
    } else if (arm_feature(env, ARM_FEATURE_V7)) {
        raw_write(env, ri, value & 0xfffff6ff);
    } else {
        raw_write(env, ri, value & 0xfffff1ff);
    }
}

#ifndef CONFIG_USER_ONLY
/* get_phys_addr() isn't present for user-mode-only targets */

static CPAccessResult ats_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    if (ri->opc2 & 4) {
        /* The ATS12NSO* operations must trap to EL3 if executed in
         * Secure EL1 (which can only happen if EL3 is AArch64).
         * They are simply UNDEF if executed from NS EL1.
         * They function normally from EL2 or EL3.
         */
        if (arm_current_el(env) == 1) {
            if (arm_is_secure_below_el3(env)) {
                return CP_ACCESS_TRAP_UNCATEGORIZED_EL3;
            }
            return CP_ACCESS_TRAP_UNCATEGORIZED;
        }
    }
    return CP_ACCESS_OK;
}

#ifdef CONFIG_TCG
static uint64_t do_ats_write(CPUARMState *env, uint64_t value,
                             MMUAccessType access_type, ARMMMUIdx mmu_idx)
{
    hwaddr phys_addr;
    target_ulong page_size;
    int prot;
    bool ret;
    uint64_t par64;
    bool format64 = false;
    MemTxAttrs attrs = {};
    ARMMMUFaultInfo fi = {};
    ARMCacheAttrs cacheattrs = {};

    ret = get_phys_addr(env, value, access_type, mmu_idx, &phys_addr, &attrs,
                        &prot, &page_size, &fi, &cacheattrs);

    if (ret) {
        /*
         * Some kinds of translation fault must cause exceptions rather
         * than being reported in the PAR.
         */
        int current_el = arm_current_el(env);
        int target_el;
        uint32_t syn, fsr, fsc;
        bool take_exc = false;

        if (fi.s1ptw && current_el == 1 && !arm_is_secure(env)
            && arm_mmu_idx_is_stage1_of_2(mmu_idx)) {
            /*
             * Synchronous stage 2 fault on an access made as part of the
             * translation table walk for AT S1E0* or AT S1E1* insn
             * executed from NS EL1. If this is a synchronous external abort
             * and SCR_EL3.EA == 1, then we take a synchronous external abort
             * to EL3. Otherwise the fault is taken as an exception to EL2,
             * and HPFAR_EL2 holds the faulting IPA.
             */
            if (fi.type == ARMFault_SyncExternalOnWalk &&
                (env->cp15.scr_el3 & SCR_EA)) {
                target_el = 3;
            } else {
                env->cp15.hpfar_el2 = extract64(fi.s2addr, 12, 47) << 4;
                target_el = 2;
            }
            take_exc = true;
        } else if (fi.type == ARMFault_SyncExternalOnWalk) {
            /*
             * Synchronous external aborts during a translation table walk
             * are taken as Data Abort exceptions.
             */
            if (fi.stage2) {
                if (current_el == 3) {
                    target_el = 3;
                } else {
                    target_el = 2;
                }
            } else {
                target_el = exception_target_el(env);
            }
            take_exc = true;
        }

        if (take_exc) {
            /* Construct FSR and FSC using same logic as arm_deliver_fault() */
            if (target_el == 2 || arm_el_is_aa64(env, target_el) ||
                arm_s1_regime_using_lpae_format(env, mmu_idx)) {
                fsr = arm_fi_to_lfsc(&fi);
                fsc = extract32(fsr, 0, 6);
            } else {
                fsr = arm_fi_to_sfsc(&fi);
                fsc = 0x3f;
            }
            /*
             * Report exception with ESR indicating a fault due to a
             * translation table walk for a cache maintenance instruction.
             */
            syn = syn_data_abort_no_iss(current_el == target_el, 0,
                                        fi.ea, 1, fi.s1ptw, 1, fsc);
            env->exception.vaddress = value;
            env->exception.fsr = fsr;
            raise_exception(env, EXCP_DATA_ABORT, syn, target_el);
        }
    }

    if (is_a64(env)) {
        format64 = true;
    } else if (arm_feature(env, ARM_FEATURE_LPAE)) {
        /*
         * ATS1Cxx:
         * * TTBCR.EAE determines whether the result is returned using the
         *   32-bit or the 64-bit PAR format
         * * Instructions executed in Hyp mode always use the 64bit format
         *
         * ATS1S2NSOxx uses the 64bit format if any of the following is true:
         * * The Non-secure TTBCR.EAE bit is set to 1
         * * The implementation includes EL2, and the value of HCR.VM is 1
         *
         * (Note that HCR.DC makes HCR.VM behave as if it is 1.)
         *
         * ATS1Hx always uses the 64bit format.
         */
        format64 = arm_s1_regime_using_lpae_format(env, mmu_idx);

        if (arm_feature(env, ARM_FEATURE_EL2)) {
            if (mmu_idx == ARMMMUIdx_E10_0 ||
                mmu_idx == ARMMMUIdx_E10_1 ||
                mmu_idx == ARMMMUIdx_E10_1_PAN) {
                format64 |= env->cp15.hcr_el2 & (HCR_VM | HCR_DC);
            } else {
                format64 |= arm_current_el(env) == 2;
            }
        }
    }

    if (format64) {
        /* Create a 64-bit PAR */
        par64 = (1 << 11); /* LPAE bit always set */
        if (!ret) {
            par64 |= phys_addr & ~0xfffULL;
            if (!attrs.secure) {
                par64 |= (1 << 9); /* NS */
            }
            par64 |= (uint64_t)cacheattrs.attrs << 56; /* ATTR */
            par64 |= cacheattrs.shareability << 7; /* SH */
        } else {
            uint32_t fsr = arm_fi_to_lfsc(&fi);

            par64 |= 1; /* F */
            par64 |= (fsr & 0x3f) << 1; /* FS */
            if (fi.stage2) {
                par64 |= (1 << 9); /* S */
            }
            if (fi.s1ptw) {
                par64 |= (1 << 8); /* PTW */
            }
        }
    } else {
        /* fsr is a DFSR/IFSR value for the short descriptor
         * translation table format (with WnR always clear).
         * Convert it to a 32-bit PAR.
         */
        if (!ret) {
            /* We do not set any attribute bits in the PAR */
            if (page_size == (1 << 24)
                && arm_feature(env, ARM_FEATURE_V7)) {
                par64 = (phys_addr & 0xff000000) | (1 << 1);
            } else {
                par64 = phys_addr & 0xfffff000;
            }
            if (!attrs.secure) {
                par64 |= (1 << 9); /* NS */
            }
        } else {
            uint32_t fsr = arm_fi_to_sfsc(&fi);

            par64 = ((fsr & (1 << 10)) >> 5) | ((fsr & (1 << 12)) >> 6) |
                    ((fsr & 0xf) << 1) | 1;
        }
    }
    return par64;
}
#endif /* CONFIG_TCG */

static void ats_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
#ifdef CONFIG_TCG
    MMUAccessType access_type = ri->opc2 & 1 ? MMU_DATA_STORE : MMU_DATA_LOAD;
    uint64_t par64;
    ARMMMUIdx mmu_idx;
    int el = arm_current_el(env);
    bool secure = arm_is_secure_below_el3(env);

    switch (ri->opc2 & 6) {
    case 0:
        /* stage 1 current state PL1: ATS1CPR, ATS1CPW, ATS1CPRP, ATS1CPWP */
        switch (el) {
        case 3:
            mmu_idx = ARMMMUIdx_SE3;
            break;
        case 2:
            g_assert(!secure);  /* TODO: ARMv8.4-SecEL2 */
            /* fall through */
        case 1:
            if (ri->crm == 9 && (env->uncached_cpsr & CPSR_PAN)) {
                mmu_idx = (secure ? ARMMMUIdx_SE10_1_PAN
                           : ARMMMUIdx_Stage1_E1_PAN);
            } else {
                mmu_idx = secure ? ARMMMUIdx_SE10_1 : ARMMMUIdx_Stage1_E1;
            }
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case 2:
        /* stage 1 current state PL0: ATS1CUR, ATS1CUW */
        switch (el) {
        case 3:
            mmu_idx = ARMMMUIdx_SE10_0;
            break;
        case 2:
            mmu_idx = ARMMMUIdx_Stage1_E0;
            break;
        case 1:
            mmu_idx = secure ? ARMMMUIdx_SE10_0 : ARMMMUIdx_Stage1_E0;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case 4:
        /* stage 1+2 NonSecure PL1: ATS12NSOPR, ATS12NSOPW */
        mmu_idx = ARMMMUIdx_E10_1;
        break;
    case 6:
        /* stage 1+2 NonSecure PL0: ATS12NSOUR, ATS12NSOUW */
        mmu_idx = ARMMMUIdx_E10_0;
        break;
    default:
        g_assert_not_reached();
    }

    par64 = do_ats_write(env, value, access_type, mmu_idx);

    A32_BANKED_CURRENT_REG_SET(env, par, par64);
#else
    /* Handled by hardware accelerator. */
    g_assert_not_reached();
#endif /* CONFIG_TCG */
}

static void ats1h_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
#ifdef CONFIG_TCG
    MMUAccessType access_type = ri->opc2 & 1 ? MMU_DATA_STORE : MMU_DATA_LOAD;
    uint64_t par64;

    par64 = do_ats_write(env, value, access_type, ARMMMUIdx_E2);

    A32_BANKED_CURRENT_REG_SET(env, par, par64);
#else
    /* Handled by hardware accelerator. */
    g_assert_not_reached();
#endif /* CONFIG_TCG */
}

static CPAccessResult at_s1e2_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    if (arm_current_el(env) == 3 && !(env->cp15.scr_el3 & SCR_NS)) {
        return CP_ACCESS_TRAP;
    }
    return CP_ACCESS_OK;
}

static void ats_write64(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
#ifdef CONFIG_TCG
    MMUAccessType access_type = ri->opc2 & 1 ? MMU_DATA_STORE : MMU_DATA_LOAD;
    ARMMMUIdx mmu_idx;
    int secure = arm_is_secure_below_el3(env);

    switch (ri->opc2 & 6) {
    case 0:
        switch (ri->opc1) {
        case 0: /* AT S1E1R, AT S1E1W, AT S1E1RP, AT S1E1WP */
            if (ri->crm == 9 && (env->pstate & PSTATE_PAN)) {
                mmu_idx = (secure ? ARMMMUIdx_SE10_1_PAN
                           : ARMMMUIdx_Stage1_E1_PAN);
            } else {
                mmu_idx = secure ? ARMMMUIdx_SE10_1 : ARMMMUIdx_Stage1_E1;
            }
            break;
        case 4: /* AT S1E2R, AT S1E2W */
            mmu_idx = ARMMMUIdx_E2;
            break;
        case 6: /* AT S1E3R, AT S1E3W */
            mmu_idx = ARMMMUIdx_SE3;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case 2: /* AT S1E0R, AT S1E0W */
        mmu_idx = secure ? ARMMMUIdx_SE10_0 : ARMMMUIdx_Stage1_E0;
        break;
    case 4: /* AT S12E1R, AT S12E1W */
        mmu_idx = secure ? ARMMMUIdx_SE10_1 : ARMMMUIdx_E10_1;
        break;
    case 6: /* AT S12E0R, AT S12E0W */
        mmu_idx = secure ? ARMMMUIdx_SE10_0 : ARMMMUIdx_E10_0;
        break;
    default:
        g_assert_not_reached();
    }

    env->cp15.par_el[1] = do_ats_write(env, value, access_type, mmu_idx);
#else
    /* Handled by hardware accelerator. */
    g_assert_not_reached();
#endif /* CONFIG_TCG */
}
#endif

static const ARMCPRegInfo vapa_cp_reginfo[] = {
    { .name = "PAR", .cp = 15, .crn = 7, .crm = 4, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .resetvalue = 0,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.par_s),
                             offsetoflow32(CPUARMState, cp15.par_ns) },
      .writefn = par_write },
#ifndef CONFIG_USER_ONLY
    /* This underdecoding is safe because the reginfo is NO_RAW. */
    { .name = "ATS", .cp = 15, .crn = 7, .crm = 8, .opc1 = 0, .opc2 = CP_ANY,
      .access = PL1_W, .accessfn = ats_access,
      .writefn = ats_write, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC },
#endif
    REGINFO_SENTINEL
};

/* Return basic MPU access permission bits.  */
static uint32_t simple_mpu_ap_bits(uint32_t val)
{
    uint32_t ret;
    uint32_t mask;
    int i;
    ret = 0;
    mask = 3;
    for (i = 0; i < 16; i += 2) {
        ret |= (val >> i) & mask;
        mask <<= 2;
    }
    return ret;
}

/* Pad basic MPU access permission bits to extended format.  */
static uint32_t extended_mpu_ap_bits(uint32_t val)
{
    uint32_t ret;
    uint32_t mask;
    int i;
    ret = 0;
    mask = 3;
    for (i = 0; i < 16; i += 2) {
        ret |= (val & mask) << i;
        mask <<= 2;
    }
    return ret;
}

static void pmsav5_data_ap_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    env->cp15.pmsav5_data_ap = extended_mpu_ap_bits(value);
}

static uint64_t pmsav5_data_ap_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return simple_mpu_ap_bits(env->cp15.pmsav5_data_ap);
}

static void pmsav5_insn_ap_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    env->cp15.pmsav5_insn_ap = extended_mpu_ap_bits(value);
}

static uint64_t pmsav5_insn_ap_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return simple_mpu_ap_bits(env->cp15.pmsav5_insn_ap);
}

static uint64_t pmsav7_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    uint32_t *u32p = *(uint32_t **)raw_ptr(env, ri);

    if (!u32p) {
        return 0;
    }

    u32p += env->pmsav7.rnr[M_REG_NS];
    return *u32p;
}

static void pmsav7_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    uint32_t *u32p = *(uint32_t **)raw_ptr(env, ri);

    if (!u32p) {
        return;
    }

    u32p += env->pmsav7.rnr[M_REG_NS];
    tlb_flush(CPU(cpu)); /* Mappings may have changed - purge! */
    *u32p = value;
}

static void pmsav7_rgnr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    uint32_t nrgs = cpu->pmsav7_dregion;

    if (value >= nrgs) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "PMSAv7 RGNR write >= # supported regions, %" PRIu32
                      " > %" PRIu32 "\n", (uint32_t)value, nrgs);
        return;
    }

    raw_write(env, ri, value);
}

static const ARMCPRegInfo pmsav7_cp_reginfo[] = {
    /* Reset for all these registers is handled in arm_cpu_reset(),
     * because the PMSAv7 is also used by M-profile CPUs, which do
     * not register cpregs but still need the state to be reset.
     */
    { .name = "DRBAR", .cp = 15, .crn = 6, .opc1 = 0, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_NO_RAW,
      .fieldoffset = offsetof(CPUARMState, pmsav7.drbar),
      .readfn = pmsav7_read, .writefn = pmsav7_write,
      .resetfn = arm_cp_reset_ignore },
    { .name = "DRSR", .cp = 15, .crn = 6, .opc1 = 0, .crm = 1, .opc2 = 2,
      .access = PL1_RW, .type = ARM_CP_NO_RAW,
      .fieldoffset = offsetof(CPUARMState, pmsav7.drsr),
      .readfn = pmsav7_read, .writefn = pmsav7_write,
      .resetfn = arm_cp_reset_ignore },
    { .name = "DRACR", .cp = 15, .crn = 6, .opc1 = 0, .crm = 1, .opc2 = 4,
      .access = PL1_RW, .type = ARM_CP_NO_RAW,
      .fieldoffset = offsetof(CPUARMState, pmsav7.dracr),
      .readfn = pmsav7_read, .writefn = pmsav7_write,
      .resetfn = arm_cp_reset_ignore },
    { .name = "RGNR", .cp = 15, .crn = 6, .opc1 = 0, .crm = 2, .opc2 = 0,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, pmsav7.rnr[M_REG_NS]),
      .writefn = pmsav7_rgnr_write,
      .resetfn = arm_cp_reset_ignore },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo pmsav5_cp_reginfo[] = {
    { .name = "DATA_AP", .cp = 15, .crn = 5, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.pmsav5_data_ap),
      .readfn = pmsav5_data_ap_read, .writefn = pmsav5_data_ap_write, },
    { .name = "INSN_AP", .cp = 15, .crn = 5, .crm = 0, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .type = ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.pmsav5_insn_ap),
      .readfn = pmsav5_insn_ap_read, .writefn = pmsav5_insn_ap_write, },
    { .name = "DATA_EXT_AP", .cp = 15, .crn = 5, .crm = 0, .opc1 = 0, .opc2 = 2,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.pmsav5_data_ap),
      .resetvalue = 0, },
    { .name = "INSN_EXT_AP", .cp = 15, .crn = 5, .crm = 0, .opc1 = 0, .opc2 = 3,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.pmsav5_insn_ap),
      .resetvalue = 0, },
    { .name = "DCACHE_CFG", .cp = 15, .crn = 2, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c2_data), .resetvalue = 0, },
    { .name = "ICACHE_CFG", .cp = 15, .crn = 2, .crm = 0, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c2_insn), .resetvalue = 0, },
    /* Protection region base and size registers */
    { .name = "946_PRBS0", .cp = 15, .crn = 6, .crm = 0, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[0]) },
    { .name = "946_PRBS1", .cp = 15, .crn = 6, .crm = 1, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[1]) },
    { .name = "946_PRBS2", .cp = 15, .crn = 6, .crm = 2, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[2]) },
    { .name = "946_PRBS3", .cp = 15, .crn = 6, .crm = 3, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[3]) },
    { .name = "946_PRBS4", .cp = 15, .crn = 6, .crm = 4, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[4]) },
    { .name = "946_PRBS5", .cp = 15, .crn = 6, .crm = 5, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[5]) },
    { .name = "946_PRBS6", .cp = 15, .crn = 6, .crm = 6, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[6]) },
    { .name = "946_PRBS7", .cp = 15, .crn = 6, .crm = 7, .opc1 = 0,
      .opc2 = CP_ANY, .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.c6_region[7]) },
    REGINFO_SENTINEL
};

static void vmsa_ttbcr_raw_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    TCR *tcr = raw_ptr(env, ri);
    int maskshift = extract32(value, 0, 3);

    if (!arm_feature(env, ARM_FEATURE_V8)) {
        if (arm_feature(env, ARM_FEATURE_LPAE) && (value & TTBCR_EAE)) {
            /* Pre ARMv8 bits [21:19], [15:14] and [6:3] are UNK/SBZP when
             * using Long-desciptor translation table format */
            value &= ~((7 << 19) | (3 << 14) | (0xf << 3));
        } else if (arm_feature(env, ARM_FEATURE_EL3)) {
            /* In an implementation that includes the Security Extensions
             * TTBCR has additional fields PD0 [4] and PD1 [5] for
             * Short-descriptor translation table format.
             */
            value &= TTBCR_PD1 | TTBCR_PD0 | TTBCR_N;
        } else {
            value &= TTBCR_N;
        }
    }

    /* Update the masks corresponding to the TCR bank being written
     * Note that we always calculate mask and base_mask, but
     * they are only used for short-descriptor tables (ie if EAE is 0);
     * for long-descriptor tables the TCR fields are used differently
     * and the mask and base_mask values are meaningless.
     */
    tcr->raw_tcr = value;
    tcr->mask = ~(((uint32_t)0xffffffffu) >> maskshift);
    tcr->base_mask = ~((uint32_t)0x3fffu >> maskshift);
}

static void vmsa_ttbcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                             uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    TCR *tcr = raw_ptr(env, ri);

    if (arm_feature(env, ARM_FEATURE_LPAE)) {
        /* With LPAE the TTBCR could result in a change of ASID
         * via the TTBCR.A1 bit, so do a TLB flush.
         */
        tlb_flush(CPU(cpu));
    }
    /* Preserve the high half of TCR_EL1, set via TTBCR2.  */
    value = deposit64(tcr->raw_tcr, 0, 32, value);
    vmsa_ttbcr_raw_write(env, ri, value);
}

static void vmsa_ttbcr_reset(CPUARMState *env, const ARMCPRegInfo *ri)
{
    TCR *tcr = raw_ptr(env, ri);

    /* Reset both the TCR as well as the masks corresponding to the bank of
     * the TCR being reset.
     */
    tcr->raw_tcr = 0;
    tcr->mask = 0;
    tcr->base_mask = 0xffffc000u;
}

static void vmsa_tcr_el12_write(CPUARMState *env, const ARMCPRegInfo *ri,
                               uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    TCR *tcr = raw_ptr(env, ri);

    /* For AArch64 the A1 bit could result in a change of ASID, so TLB flush. */
    tlb_flush(CPU(cpu));
    tcr->raw_tcr = value;
}

static void vmsa_ttbr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    /* If the ASID changes (with a 64-bit write), we must flush the TLB.  */
    if (cpreg_field_is_64bit(ri) &&
        extract64(raw_read(env, ri) ^ value, 48, 16) != 0) {
        ARMCPU *cpu = env_archcpu(env);
        tlb_flush(CPU(cpu));
    }
    raw_write(env, ri, value);
}

static void vmsa_tcr_ttbr_el2_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    /*
     * If we are running with E2&0 regime, then an ASID is active.
     * Flush if that might be changing.  Note we're not checking
     * TCR_EL2.A1 to know if this is really the TTBRx_EL2 that
     * holds the active ASID, only checking the field that might.
     */
    if (extract64(raw_read(env, ri) ^ value, 48, 16) &&
        (arm_hcr_el2_eff(env) & HCR_E2H)) {
        tlb_flush_by_mmuidx(env_cpu(env),
                            ARMMMUIdxBit_E20_2 |
                            ARMMMUIdxBit_E20_2_PAN |
                            ARMMMUIdxBit_E20_0);
    }
    raw_write(env, ri, value);
}

static void vttbr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    CPUState *cs = CPU(cpu);

    /*
     * A change in VMID to the stage2 page table (Stage2) invalidates
     * the combined stage 1&2 tlbs (EL10_1 and EL10_0).
     */
    if (raw_read(env, ri) != value) {
        tlb_flush_by_mmuidx(cs,
                            ARMMMUIdxBit_E10_1 |
                            ARMMMUIdxBit_E10_1_PAN |
                            ARMMMUIdxBit_E10_0);
        raw_write(env, ri, value);
    }
}

static const ARMCPRegInfo vmsa_pmsa_cp_reginfo[] = {
    { .name = "DFSR", .cp = 15, .crn = 5, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm, .type = ARM_CP_ALIAS,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.dfsr_s),
                             offsetoflow32(CPUARMState, cp15.dfsr_ns) }, },
    { .name = "IFSR", .cp = 15, .crn = 5, .crm = 0, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm, .resetvalue = 0,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.ifsr_s),
                             offsetoflow32(CPUARMState, cp15.ifsr_ns) } },
    { .name = "DFAR", .cp = 15, .opc1 = 0, .crn = 6, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm, .resetvalue = 0,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.dfar_s),
                             offsetof(CPUARMState, cp15.dfar_ns) } },
    { .name = "FAR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .crn = 6, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .fieldoffset = offsetof(CPUARMState, cp15.far_el[1]),
      .resetvalue = 0, },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo vmsa_cp_reginfo[] = {
    { .name = "ESR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .crn = 5, .crm = 2, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .fieldoffset = offsetof(CPUARMState, cp15.esr_el[1]), .resetvalue = 0, },
    { .name = "TTBR0_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .writefn = vmsa_ttbr_write, .resetvalue = 0,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.ttbr0_s),
                             offsetof(CPUARMState, cp15.ttbr0_ns) } },
    { .name = "TTBR1_EL1", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .writefn = vmsa_ttbr_write, .resetvalue = 0,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.ttbr1_s),
                             offsetof(CPUARMState, cp15.ttbr1_ns) } },
    { .name = "TCR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .crn = 2, .crm = 0, .opc1 = 0, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .writefn = vmsa_tcr_el12_write,
      .resetfn = vmsa_ttbcr_reset, .raw_writefn = raw_write,
      .fieldoffset = offsetof(CPUARMState, cp15.tcr_el[1]) },
    { .name = "TTBCR", .cp = 15, .crn = 2, .crm = 0, .opc1 = 0, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .type = ARM_CP_ALIAS, .writefn = vmsa_ttbcr_write,
      .raw_writefn = vmsa_ttbcr_raw_write,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.tcr_el[3]),
                             offsetoflow32(CPUARMState, cp15.tcr_el[1])} },
    REGINFO_SENTINEL
};

/* Note that unlike TTBCR, writing to TTBCR2 does not require flushing
 * qemu tlbs nor adjusting cached masks.
 */
static const ARMCPRegInfo ttbcr2_reginfo = {
    .name = "TTBCR2", .cp = 15, .opc1 = 0, .crn = 2, .crm = 0, .opc2 = 3,
    .access = PL1_RW, .accessfn = access_tvm_trvm,
    .type = ARM_CP_ALIAS,
    .bank_fieldoffsets = { offsetofhigh32(CPUARMState, cp15.tcr_el[3]),
                           offsetofhigh32(CPUARMState, cp15.tcr_el[1]) },
};

static void omap_ticonfig_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                uint64_t value)
{
    env->cp15.c15_ticonfig = value & 0xe7;
    /* The OS_TYPE bit in this register changes the reported CPUID! */
    env->cp15.c0_cpuid = (value & (1 << 5)) ?
        ARM_CPUID_TI915T : ARM_CPUID_TI925T;
}

static void omap_threadid_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                uint64_t value)
{
    env->cp15.c15_threadid = value & 0xffff;
}

static void omap_wfi_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    /* Wait-for-interrupt (deprecated) */
    cpu_interrupt(env_cpu(env), CPU_INTERRUPT_HALT);
}

static void omap_cachemaint_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    /* On OMAP there are registers indicating the max/min index of dcache lines
     * containing a dirty line; cache flush operations have to reset these.
     */
    env->cp15.c15_i_max = 0x000;
    env->cp15.c15_i_min = 0xff0;
}

static const ARMCPRegInfo omap_cp_reginfo[] = {
    { .name = "DFSR", .cp = 15, .crn = 5, .crm = CP_ANY,
      .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_RW, .type = ARM_CP_OVERRIDE,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.esr_el[1]),
      .resetvalue = 0, },
    { .name = "", .cp = 15, .crn = 15, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_NOP },
    { .name = "TICONFIG", .cp = 15, .crn = 15, .crm = 1, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_ticonfig), .resetvalue = 0,
      .writefn = omap_ticonfig_write },
    { .name = "IMAX", .cp = 15, .crn = 15, .crm = 2, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_i_max), .resetvalue = 0, },
    { .name = "IMIN", .cp = 15, .crn = 15, .crm = 3, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .resetvalue = 0xff0,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_i_min) },
    { .name = "THREADID", .cp = 15, .crn = 15, .crm = 4, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_threadid), .resetvalue = 0,
      .writefn = omap_threadid_write },
    { .name = "TI925T_STATUS", .cp = 15, .crn = 15,
      .crm = 8, .opc1 = 0, .opc2 = 0, .access = PL1_RW,
      .type = ARM_CP_NO_RAW,
      .readfn = arm_cp_read_zero, .writefn = omap_wfi_write, },
    /* TODO: Peripheral port remap register:
     * On OMAP2 mcr p15, 0, rn, c15, c2, 4 sets up the interrupt controller
     * base address at $rn & ~0xfff and map size of 0x200 << ($rn & 0xfff),
     * when MMU is off.
     */
    { .name = "OMAP_CACHEMAINT", .cp = 15, .crn = 7, .crm = CP_ANY,
      .opc1 = 0, .opc2 = CP_ANY, .access = PL1_W,
      .type = ARM_CP_OVERRIDE | ARM_CP_NO_RAW,
      .writefn = omap_cachemaint_write },
    { .name = "C9", .cp = 15, .crn = 9,
      .crm = CP_ANY, .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_RW,
      .type = ARM_CP_CONST | ARM_CP_OVERRIDE, .resetvalue = 0 },
    REGINFO_SENTINEL
};

static void xscale_cpar_write(CPUARMState *env, const ARMCPRegInfo *ri,
                              uint64_t value)
{
    env->cp15.c15_cpar = value & 0x3fff;
}

static const ARMCPRegInfo xscale_cp_reginfo[] = {
    { .name = "XSCALE_CPAR",
      .cp = 15, .crn = 15, .crm = 1, .opc1 = 0, .opc2 = 0, .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c15_cpar), .resetvalue = 0,
      .writefn = xscale_cpar_write, },
    { .name = "XSCALE_AUXCR",
      .cp = 15, .crn = 1, .crm = 0, .opc1 = 0, .opc2 = 1, .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c1_xscaleauxcr),
      .resetvalue = 0, },
    /* XScale specific cache-lockdown: since we have no cache we NOP these
     * and hope the guest does not really rely on cache behaviour.
     */
    { .name = "XSCALE_LOCK_ICACHE_LINE",
      .cp = 15, .opc1 = 0, .crn = 9, .crm = 1, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NOP },
    { .name = "XSCALE_UNLOCK_ICACHE",
      .cp = 15, .opc1 = 0, .crn = 9, .crm = 1, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NOP },
    { .name = "XSCALE_DCACHE_LOCK",
      .cp = 15, .opc1 = 0, .crn = 9, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_NOP },
    { .name = "XSCALE_UNLOCK_DCACHE",
      .cp = 15, .opc1 = 0, .crn = 9, .crm = 2, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NOP },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo dummy_c15_cp_reginfo[] = {
    /* RAZ/WI the whole crn=15 space, when we don't have a more specific
     * implementation of this implementation-defined space.
     * Ideally this should eventually disappear in favour of actually
     * implementing the correct behaviour for all cores.
     */
    { .name = "C15_IMPDEF", .cp = 15, .crn = 15,
      .crm = CP_ANY, .opc1 = CP_ANY, .opc2 = CP_ANY,
      .access = PL1_RW,
      .type = ARM_CP_CONST | ARM_CP_NO_RAW | ARM_CP_OVERRIDE,
      .resetvalue = 0 },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo cache_dirty_status_cp_reginfo[] = {
    /* Cache status: RAZ because we have no cache so it's always clean */
    { .name = "CDSR", .cp = 15, .crn = 7, .crm = 10, .opc1 = 0, .opc2 = 6,
      .access = PL1_R, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
      .resetvalue = 0 },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo cache_block_ops_cp_reginfo[] = {
    /* We never have a a block transfer operation in progress */
    { .name = "BXSR", .cp = 15, .crn = 7, .crm = 12, .opc1 = 0, .opc2 = 4,
      .access = PL0_R, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
      .resetvalue = 0 },
    /* The cache ops themselves: these all NOP for QEMU */
    { .name = "IICR", .cp = 15, .crm = 5, .opc1 = 0,
      .access = PL1_W, .type = ARM_CP_NOP|ARM_CP_64BIT },
    { .name = "IDCR", .cp = 15, .crm = 6, .opc1 = 0,
      .access = PL1_W, .type = ARM_CP_NOP|ARM_CP_64BIT },
    { .name = "CDCR", .cp = 15, .crm = 12, .opc1 = 0,
      .access = PL0_W, .type = ARM_CP_NOP|ARM_CP_64BIT },
    { .name = "PIR", .cp = 15, .crm = 12, .opc1 = 1,
      .access = PL0_W, .type = ARM_CP_NOP|ARM_CP_64BIT },
    { .name = "PDR", .cp = 15, .crm = 12, .opc1 = 2,
      .access = PL0_W, .type = ARM_CP_NOP|ARM_CP_64BIT },
    { .name = "CIDCR", .cp = 15, .crm = 14, .opc1 = 0,
      .access = PL1_W, .type = ARM_CP_NOP|ARM_CP_64BIT },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo cache_test_clean_cp_reginfo[] = {
    /* The cache test-and-clean instructions always return (1 << 30)
     * to indicate that there are no dirty cache lines.
     */
    { .name = "TC_DCACHE", .cp = 15, .crn = 7, .crm = 10, .opc1 = 0, .opc2 = 3,
      .access = PL0_R, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
      .resetvalue = (1 << 30) },
    { .name = "TCI_DCACHE", .cp = 15, .crn = 7, .crm = 14, .opc1 = 0, .opc2 = 3,
      .access = PL0_R, .type = ARM_CP_CONST | ARM_CP_NO_RAW,
      .resetvalue = (1 << 30) },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo strongarm_cp_reginfo[] = {
    /* Ignore ReadBuffer accesses */
    { .name = "C9_READBUFFER", .cp = 15, .crn = 9,
      .crm = CP_ANY, .opc1 = CP_ANY, .opc2 = CP_ANY,
      .access = PL1_RW, .resetvalue = 0,
      .type = ARM_CP_CONST | ARM_CP_OVERRIDE | ARM_CP_NO_RAW },
    REGINFO_SENTINEL
};

static uint64_t midr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);
    unsigned int cur_el = arm_current_el(env);
    bool secure = arm_is_secure(env);

    if (arm_feature(&cpu->env, ARM_FEATURE_EL2) && !secure && cur_el == 1) {
        return env->cp15.vpidr_el2;
    }
    return raw_read(env, ri);
}

static uint64_t mpidr_read_val(CPUARMState *env)
{
    ARMCPU *cpu = env_archcpu(env);
    uint64_t mpidr = cpu->mp_affinity;

    if (arm_feature(env, ARM_FEATURE_V7MP)) {
        mpidr |= (1U << 31);
        /* Cores which are uniprocessor (non-coherent)
         * but still implement the MP extensions set
         * bit 30. (For instance, Cortex-R5).
         */
        if (cpu->mp_is_up) {
            mpidr |= (1u << 30);
        }
    }
    return mpidr;
}

static uint64_t mpidr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    unsigned int cur_el = arm_current_el(env);
    bool secure = arm_is_secure(env);

    if (arm_feature(env, ARM_FEATURE_EL2) && !secure && cur_el == 1) {
        return env->cp15.vmpidr_el2;
    }
    return mpidr_read_val(env);
}

static const ARMCPRegInfo lpae_cp_reginfo[] = {
    /* NOP AMAIR0/1 */
    { .name = "AMAIR0", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .crn = 10, .crm = 3, .opc1 = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    /* AMAIR1 is mapped to AMAIR_EL1[63:32] */
    { .name = "AMAIR1", .cp = 15, .crn = 10, .crm = 3, .opc1 = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "PAR", .cp = 15, .crm = 7, .opc1 = 0,
      .access = PL1_RW, .type = ARM_CP_64BIT, .resetvalue = 0,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.par_s),
                             offsetof(CPUARMState, cp15.par_ns)} },
    { .name = "TTBR0", .cp = 15, .crm = 2, .opc1 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .type = ARM_CP_64BIT | ARM_CP_ALIAS,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.ttbr0_s),
                             offsetof(CPUARMState, cp15.ttbr0_ns) },
      .writefn = vmsa_ttbr_write, },
    { .name = "TTBR1", .cp = 15, .crm = 2, .opc1 = 1,
      .access = PL1_RW, .accessfn = access_tvm_trvm,
      .type = ARM_CP_64BIT | ARM_CP_ALIAS,
      .bank_fieldoffsets = { offsetof(CPUARMState, cp15.ttbr1_s),
                             offsetof(CPUARMState, cp15.ttbr1_ns) },
      .writefn = vmsa_ttbr_write, },
    REGINFO_SENTINEL
};

static uint64_t aa64_fpcr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return vfp_get_fpcr(env);
}

static void aa64_fpcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    vfp_set_fpcr(env, value);
}

static uint64_t aa64_fpsr_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return vfp_get_fpsr(env);
}

static void aa64_fpsr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    vfp_set_fpsr(env, value);
}

static CPAccessResult aa64_daif_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    if (arm_current_el(env) == 0 && !(arm_sctlr(env, 0) & SCTLR_UMA)) {
        return CP_ACCESS_TRAP;
    }
    return CP_ACCESS_OK;
}

static void aa64_daif_write(CPUARMState *env, const ARMCPRegInfo *ri,
                            uint64_t value)
{
    env->daif = value & PSTATE_DAIF;
}

static uint64_t aa64_pan_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pstate & PSTATE_PAN;
}

static void aa64_pan_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    env->pstate = (env->pstate & ~PSTATE_PAN) | (value & PSTATE_PAN);
}

static const ARMCPRegInfo pan_reginfo = {
    .name = "PAN", .state = ARM_CP_STATE_AA64,
    .opc0 = 3, .opc1 = 0, .crn = 4, .crm = 2, .opc2 = 3,
    .type = ARM_CP_NO_RAW, .access = PL1_RW,
    .readfn = aa64_pan_read, .writefn = aa64_pan_write
};

static uint64_t aa64_uao_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pstate & PSTATE_UAO;
}

static void aa64_uao_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    env->pstate = (env->pstate & ~PSTATE_UAO) | (value & PSTATE_UAO);
}

static const ARMCPRegInfo uao_reginfo = {
    .name = "UAO", .state = ARM_CP_STATE_AA64,
    .opc0 = 3, .opc1 = 0, .crn = 4, .crm = 2, .opc2 = 4,
    .type = ARM_CP_NO_RAW, .access = PL1_RW,
    .readfn = aa64_uao_read, .writefn = aa64_uao_write
};

static CPAccessResult aa64_cacheop_poc_access(CPUARMState *env,
                                              const ARMCPRegInfo *ri,
                                              bool isread)
{
    /* Cache invalidate/clean to Point of Coherency or Persistence...  */
    switch (arm_current_el(env)) {
    case 0:
        /* ... EL0 must UNDEF unless SCTLR_EL1.UCI is set.  */
        if (!(arm_sctlr(env, 0) & SCTLR_UCI)) {
            return CP_ACCESS_TRAP;
        }
        /* fall through */
    case 1:
        /* ... EL1 must trap to EL2 if HCR_EL2.TPCP is set.  */
        if (arm_hcr_el2_eff(env) & HCR_TPCP) {
            return CP_ACCESS_TRAP_EL2;
        }
        break;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult aa64_cacheop_pou_access(CPUARMState *env,
                                              const ARMCPRegInfo *ri,
                                              bool isread)
{
    /* Cache invalidate/clean to Point of Unification... */
    switch (arm_current_el(env)) {
    case 0:
        /* ... EL0 must UNDEF unless SCTLR_EL1.UCI is set.  */
        if (!(arm_sctlr(env, 0) & SCTLR_UCI)) {
            return CP_ACCESS_TRAP;
        }
        /* fall through */
    case 1:
        /* ... EL1 must trap to EL2 if HCR_EL2.TPU is set.  */
        if (arm_hcr_el2_eff(env) & HCR_TPU) {
            return CP_ACCESS_TRAP_EL2;
        }
        break;
    }
    return CP_ACCESS_OK;
}

/* See: D4.7.2 TLB maintenance requirements and the TLB maintenance instructions
 * Page D4-1736 (DDI0487A.b)
 */

static int vae1_tlbmask(CPUARMState *env)
{
    /* Since we exclude secure first, we may read HCR_EL2 directly. */
    if (arm_is_secure_below_el3(env)) {
        return ARMMMUIdxBit_SE10_1 |
               ARMMMUIdxBit_SE10_1_PAN |
               ARMMMUIdxBit_SE10_0;
    } else if ((env->cp15.hcr_el2 & (HCR_E2H | HCR_TGE))
               == (HCR_E2H | HCR_TGE)) {
        return ARMMMUIdxBit_E20_2 |
               ARMMMUIdxBit_E20_2_PAN |
               ARMMMUIdxBit_E20_0;
    } else {
        return ARMMMUIdxBit_E10_1 |
               ARMMMUIdxBit_E10_1_PAN |
               ARMMMUIdxBit_E10_0;
    }
}

static void tlbi_aa64_vmalle1is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                      uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = vae1_tlbmask(env);

    tlb_flush_by_mmuidx_all_cpus_synced(cs, mask);
}

static void tlbi_aa64_vmalle1_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = vae1_tlbmask(env);

    if (tlb_force_broadcast(env)) {
        tlb_flush_by_mmuidx_all_cpus_synced(cs, mask);
    } else {
        tlb_flush_by_mmuidx(cs, mask);
    }
}

static int alle1_tlbmask(CPUARMState *env)
{
    /*
     * Note that the 'ALL' scope must invalidate both stage 1 and
     * stage 2 translations, whereas most other scopes only invalidate
     * stage 1 translations.
     */
    if (arm_is_secure_below_el3(env)) {
        return ARMMMUIdxBit_SE10_1 |
               ARMMMUIdxBit_SE10_1_PAN |
               ARMMMUIdxBit_SE10_0;
    } else {
        return ARMMMUIdxBit_E10_1 |
               ARMMMUIdxBit_E10_1_PAN |
               ARMMMUIdxBit_E10_0;
    }
}

static int e2_tlbmask(CPUARMState *env)
{
    /* TODO: ARMv8.4-SecEL2 */
    return ARMMMUIdxBit_E20_0 |
           ARMMMUIdxBit_E20_2 |
           ARMMMUIdxBit_E20_2_PAN |
           ARMMMUIdxBit_E2;
}

static void tlbi_aa64_alle1_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = alle1_tlbmask(env);

    tlb_flush_by_mmuidx(cs, mask);
}

static void tlbi_aa64_alle2_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = e2_tlbmask(env);

    tlb_flush_by_mmuidx(cs, mask);
}

static void tlbi_aa64_alle3_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                  uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    CPUState *cs = CPU(cpu);

    tlb_flush_by_mmuidx(cs, ARMMMUIdxBit_SE3);
}

static void tlbi_aa64_alle1is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = alle1_tlbmask(env);

    tlb_flush_by_mmuidx_all_cpus_synced(cs, mask);
}

static void tlbi_aa64_alle2is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = e2_tlbmask(env);

    tlb_flush_by_mmuidx_all_cpus_synced(cs, mask);
}

static void tlbi_aa64_alle3is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                    uint64_t value)
{
    CPUState *cs = env_cpu(env);

    tlb_flush_by_mmuidx_all_cpus_synced(cs, ARMMMUIdxBit_SE3);
}

static void tlbi_aa64_vae2_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    /* Invalidate by VA, EL2
     * Currently handles both VAE2 and VALE2, since we don't support
     * flush-last-level-only.
     */
    CPUState *cs = env_cpu(env);
    int mask = e2_tlbmask(env);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);

    tlb_flush_page_by_mmuidx(cs, pageaddr, mask);
}

static void tlbi_aa64_vae3_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    /* Invalidate by VA, EL3
     * Currently handles both VAE3 and VALE3, since we don't support
     * flush-last-level-only.
     */
    ARMCPU *cpu = env_archcpu(env);
    CPUState *cs = CPU(cpu);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);

    tlb_flush_page_by_mmuidx(cs, pageaddr, ARMMMUIdxBit_SE3);
}

static void tlbi_aa64_vae1is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                   uint64_t value)
{
    CPUState *cs = env_cpu(env);
    int mask = vae1_tlbmask(env);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);

    tlb_flush_page_by_mmuidx_all_cpus_synced(cs, pageaddr, mask);
}

static void tlbi_aa64_vae1_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                 uint64_t value)
{
    /* Invalidate by VA, EL1&0 (AArch64 version).
     * Currently handles all of VAE1, VAAE1, VAALE1 and VALE1,
     * since we don't support flush-for-specific-ASID-only or
     * flush-last-level-only.
     */
    CPUState *cs = env_cpu(env);
    int mask = vae1_tlbmask(env);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);

    if (tlb_force_broadcast(env)) {
        tlb_flush_page_by_mmuidx_all_cpus_synced(cs, pageaddr, mask);
    } else {
        tlb_flush_page_by_mmuidx(cs, pageaddr, mask);
    }
}

static void tlbi_aa64_vae2is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                   uint64_t value)
{
    CPUState *cs = env_cpu(env);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);

    tlb_flush_page_by_mmuidx_all_cpus_synced(cs, pageaddr,
                                             ARMMMUIdxBit_E2);
}

static void tlbi_aa64_vae3is_write(CPUARMState *env, const ARMCPRegInfo *ri,
                                   uint64_t value)
{
    CPUState *cs = env_cpu(env);
    uint64_t pageaddr = sextract64(value << 12, 0, 56);

    tlb_flush_page_by_mmuidx_all_cpus_synced(cs, pageaddr,
                                             ARMMMUIdxBit_SE3);
}

static CPAccessResult aa64_zva_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                      bool isread)
{
    int cur_el = arm_current_el(env);

    if (cur_el < 2) {
        uint64_t hcr = arm_hcr_el2_eff(env);

        if (cur_el == 0) {
            if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
                if (!(env->cp15.sctlr_el[2] & SCTLR_DZE)) {
                    return CP_ACCESS_TRAP_EL2;
                }
            } else {
                if (!(env->cp15.sctlr_el[1] & SCTLR_DZE)) {
                    return CP_ACCESS_TRAP;
                }
                if (hcr & HCR_TDZ) {
                    return CP_ACCESS_TRAP_EL2;
                }
            }
        } else if (hcr & HCR_TDZ) {
            return CP_ACCESS_TRAP_EL2;
        }
    }
    return CP_ACCESS_OK;
}

static uint64_t aa64_dczid_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);
    int dzp_bit = 1 << 4;

    /* DZP indicates whether DC ZVA access is allowed */
    if (aa64_zva_access(env, NULL, false) == CP_ACCESS_OK) {
        dzp_bit = 0;
    }
    return cpu->dcz_blocksize | dzp_bit;
}

static CPAccessResult sp_el0_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                    bool isread)
{
    if (!(env->pstate & PSTATE_SP)) {
        /* Access to SP_EL0 is undefined if it's being used as
         * the stack pointer.
         */
        return CP_ACCESS_TRAP_UNCATEGORIZED;
    }
    return CP_ACCESS_OK;
}

static uint64_t spsel_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pstate & PSTATE_SP;
}

static void spsel_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t val)
{
    update_spsel(env, val);
}

static void sctlr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);

    if (arm_feature(env, ARM_FEATURE_PMSA) && !cpu->has_mpu) {
        /* M bit is RAZ/WI for PMSA with no MPU implemented */
        value &= ~SCTLR_M;
    }

    /* ??? Lots of these bits are not implemented.  */

    if (ri->state == ARM_CP_STATE_AA64 && !cpu_isar_feature(aa64_mte, cpu)) {
        if (ri->opc1 == 6) { /* SCTLR_EL3 */
            value &= ~(SCTLR_ITFSB | SCTLR_TCF | SCTLR_ATA);
        } else {
            value &= ~(SCTLR_ITFSB | SCTLR_TCF0 | SCTLR_TCF |
                       SCTLR_ATA0 | SCTLR_ATA);
        }
    }

    if (raw_read(env, ri) == value) {
        /* Skip the TLB flush if nothing actually changed; Linux likes
         * to do a lot of pointless SCTLR writes.
         */
        return;
    }

    raw_write(env, ri, value);

    /* This may enable/disable the MMU, so do a TLB flush.  */
    tlb_flush(CPU(cpu));

    if (ri->type & ARM_CP_SUPPRESS_TB_END) {
        /*
         * Normally we would always end the TB on an SCTLR write; see the
         * comment in ARMCPRegInfo sctlr initialization below for why Xscale
         * is special.  Setting ARM_CP_SUPPRESS_TB_END also stops the rebuild
         * of hflags from the translator, so do it here.
         */
        arm_rebuild_hflags(env);
    }
}

static CPAccessResult fpexc32_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    if ((env->cp15.cptr_el[2] & CPTR_TFP) && arm_current_el(env) == 2) {
        return CP_ACCESS_TRAP_FP_EL2;
    }
    if (env->cp15.cptr_el[3] & CPTR_TFP) {
        return CP_ACCESS_TRAP_FP_EL3;
    }
    return CP_ACCESS_OK;
}

static void sdcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                       uint64_t value)
{
    env->cp15.mdcr_el3 = value & SDCR_VALID_MASK;
}

static const ARMCPRegInfo v8_cp_reginfo[] = {
    /* Minimal set of EL0-visible registers. This will need to be expanded
     * significantly for system emulation of AArch64 CPUs.
     */
    { .name = "NZCV", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 0, .crn = 4, .crm = 2,
      .access = PL0_RW, .type = ARM_CP_NZCV },
    { .name = "DAIF", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 1, .crn = 4, .crm = 2,
      .type = ARM_CP_NO_RAW,
      .access = PL0_RW, .accessfn = aa64_daif_access,
      .fieldoffset = offsetof(CPUARMState, daif),
      .writefn = aa64_daif_write, .resetfn = arm_cp_reset_ignore },
    { .name = "FPCR", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 0, .crn = 4, .crm = 4,
      .access = PL0_RW, .type = ARM_CP_FPU | ARM_CP_SUPPRESS_TB_END,
      .readfn = aa64_fpcr_read, .writefn = aa64_fpcr_write },
    { .name = "FPSR", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 1, .crn = 4, .crm = 4,
      .access = PL0_RW, .type = ARM_CP_FPU | ARM_CP_SUPPRESS_TB_END,
      .readfn = aa64_fpsr_read, .writefn = aa64_fpsr_write },
    { .name = "DCZID_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .opc2 = 7, .crn = 0, .crm = 0,
      .access = PL0_R, .type = ARM_CP_NO_RAW,
      .readfn = aa64_dczid_read },
    { .name = "DC_ZVA", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 4, .opc2 = 1,
      .access = PL0_W, .type = ARM_CP_DC_ZVA,
#ifndef CONFIG_USER_ONLY
      /* Avoid overhead of an access check that always passes in user-mode */
      .accessfn = aa64_zva_access,
#endif
    },
    { .name = "CURRENTEL", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .opc2 = 2, .crn = 4, .crm = 2,
      .access = PL1_R, .type = ARM_CP_CURRENTEL },
    /* Cache ops: all NOPs since we don't emulate caches */
    { .name = "IC_IALLUIS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 1, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NOP,
      .accessfn = aa64_cacheop_pou_access },
    { .name = "IC_IALLU", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 5, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NOP,
      .accessfn = aa64_cacheop_pou_access },
    { .name = "IC_IVAU", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 5, .opc2 = 1,
      .access = PL0_W, .type = ARM_CP_NOP,
      .accessfn = aa64_cacheop_pou_access },
    { .name = "DC_IVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 1,
      .access = PL1_W, .accessfn = aa64_cacheop_poc_access,
      .type = ARM_CP_NOP },
    { .name = "DC_ISW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 2,
      .access = PL1_W, .accessfn = access_tsw, .type = ARM_CP_NOP },
    { .name = "DC_CVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 10, .opc2 = 1,
      .access = PL0_W, .type = ARM_CP_NOP,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 10, .opc2 = 2,
      .access = PL1_W, .accessfn = access_tsw, .type = ARM_CP_NOP },
    { .name = "DC_CVAU", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 11, .opc2 = 1,
      .access = PL0_W, .type = ARM_CP_NOP,
      .accessfn = aa64_cacheop_pou_access },
    { .name = "DC_CIVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 14, .opc2 = 1,
      .access = PL0_W, .type = ARM_CP_NOP,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CISW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 14, .opc2 = 2,
      .access = PL1_W, .accessfn = access_tsw, .type = ARM_CP_NOP },
    /* TLBI operations */
    { .name = "TLBI_VMALLE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 0,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vmalle1is_write },
    { .name = "TLBI_VAE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 1,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_ASIDE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 2,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vmalle1is_write },
    { .name = "TLBI_VAAE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 3,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_VALE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 5,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_VAALE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 7,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae1is_write },
    { .name = "TLBI_VMALLE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 0,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vmalle1_write },
    { .name = "TLBI_VAE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 1,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae1_write },
    { .name = "TLBI_ASIDE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 2,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vmalle1_write },
    { .name = "TLBI_VAAE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 3,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae1_write },
    { .name = "TLBI_VALE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 5,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae1_write },
    { .name = "TLBI_VAALE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 7,
      .access = PL1_W, .accessfn = access_ttlb, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae1_write },
    { .name = "TLBI_IPAS2E1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 1,
      .access = PL2_W, .type = ARM_CP_NOP },
    { .name = "TLBI_IPAS2LE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NOP },
    { .name = "TLBI_ALLE1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 4,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_alle1is_write },
    { .name = "TLBI_VMALLS12E1IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_alle1is_write },
    { .name = "TLBI_IPAS2E1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 1,
      .access = PL2_W, .type = ARM_CP_NOP },
    { .name = "TLBI_IPAS2LE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NOP },
    { .name = "TLBI_ALLE1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 4,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_alle1_write },
    { .name = "TLBI_VMALLS12E1", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_alle1is_write },
#ifndef CONFIG_USER_ONLY
    /* 64 bit address translation operations */
    { .name = "AT_S1E1R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 8, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    { .name = "AT_S1E1W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 8, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    { .name = "AT_S1E0R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 8, .opc2 = 2,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    { .name = "AT_S1E0W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 8, .opc2 = 3,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    { .name = "AT_S12E1R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 4,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    { .name = "AT_S12E1W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    { .name = "AT_S12E0R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 6,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    { .name = "AT_S12E0W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 7,
      .access = PL2_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    /* AT S1E2* are elsewhere as they UNDEF from EL3 if EL2 is not present */
    { .name = "AT_S1E3R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 7, .crm = 8, .opc2 = 0,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    { .name = "AT_S1E3W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 7, .crm = 8, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    { .name = "PAR_EL1", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 0, .crn = 7, .crm = 4, .opc2 = 0,
      .access = PL1_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.par_el[1]),
      .writefn = par_write },
#endif
    /* TLB invalidate last level of translation table walk */
    { .name = "TLBIMVALIS", .cp = 15, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 5,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimva_is_write },
    { .name = "TLBIMVAALIS", .cp = 15, .opc1 = 0, .crn = 8, .crm = 3, .opc2 = 7,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimvaa_is_write },
    { .name = "TLBIMVAL", .cp = 15, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 5,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimva_write },
    { .name = "TLBIMVAAL", .cp = 15, .opc1 = 0, .crn = 8, .crm = 7, .opc2 = 7,
      .type = ARM_CP_NO_RAW, .access = PL1_W, .accessfn = access_ttlb,
      .writefn = tlbimvaa_write },
    { .name = "TLBIMVALH", .cp = 15, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 5,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbimva_hyp_write },
    { .name = "TLBIMVALHIS",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 5,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbimva_hyp_is_write },
    { .name = "TLBIIPAS2",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 1,
      .type = ARM_CP_NOP, .access = PL2_W },
    { .name = "TLBIIPAS2IS",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 1,
      .type = ARM_CP_NOP, .access = PL2_W },
    { .name = "TLBIIPAS2L",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 4, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL2_W },
    { .name = "TLBIIPAS2LIS",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 0, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL2_W },
    /* 32 bit cache operations */
    { .name = "ICIALLUIS", .cp = 15, .opc1 = 0, .crn = 7, .crm = 1, .opc2 = 0,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = aa64_cacheop_pou_access },
    { .name = "BPIALLUIS", .cp = 15, .opc1 = 0, .crn = 7, .crm = 1, .opc2 = 6,
      .type = ARM_CP_NOP, .access = PL1_W },
    { .name = "ICIALLU", .cp = 15, .opc1 = 0, .crn = 7, .crm = 5, .opc2 = 0,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = aa64_cacheop_pou_access },
    { .name = "ICIMVAU", .cp = 15, .opc1 = 0, .crn = 7, .crm = 5, .opc2 = 1,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = aa64_cacheop_pou_access },
    { .name = "BPIALL", .cp = 15, .opc1 = 0, .crn = 7, .crm = 5, .opc2 = 6,
      .type = ARM_CP_NOP, .access = PL1_W },
    { .name = "BPIMVA", .cp = 15, .opc1 = 0, .crn = 7, .crm = 5, .opc2 = 7,
      .type = ARM_CP_NOP, .access = PL1_W },
    { .name = "DCIMVAC", .cp = 15, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 1,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = aa64_cacheop_poc_access },
    { .name = "DCISW", .cp = 15, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 2,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DCCMVAC", .cp = 15, .opc1 = 0, .crn = 7, .crm = 10, .opc2 = 1,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = aa64_cacheop_poc_access },
    { .name = "DCCSW", .cp = 15, .opc1 = 0, .crn = 7, .crm = 10, .opc2 = 2,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DCCMVAU", .cp = 15, .opc1 = 0, .crn = 7, .crm = 11, .opc2 = 1,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = aa64_cacheop_pou_access },
    { .name = "DCCIMVAC", .cp = 15, .opc1 = 0, .crn = 7, .crm = 14, .opc2 = 1,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = aa64_cacheop_poc_access },
    { .name = "DCCISW", .cp = 15, .opc1 = 0, .crn = 7, .crm = 14, .opc2 = 2,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    /* MMU Domain access control / MPU write buffer control */
    { .name = "DACR", .cp = 15, .opc1 = 0, .crn = 3, .crm = 0, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tvm_trvm, .resetvalue = 0,
      .writefn = dacr_write, .raw_writefn = raw_write,
      .bank_fieldoffsets = { offsetoflow32(CPUARMState, cp15.dacr_s),
                             offsetoflow32(CPUARMState, cp15.dacr_ns) } },
    { .name = "ELR_EL1", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 0, .crn = 4, .crm = 0, .opc2 = 1,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, elr_el[1]) },
    { .name = "SPSR_EL1", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 0, .crn = 4, .crm = 0, .opc2 = 0,
      .access = PL1_RW,
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_SVC]) },
    /* We rely on the access checks not allowing the guest to write to the
     * state field when SPSel indicates that it's being used as the stack
     * pointer.
     */
    { .name = "SP_EL0", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 4, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .accessfn = sp_el0_access,
      .type = ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, sp_el[0]) },
    { .name = "SP_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 1, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, sp_el[1]) },
    { .name = "SPSel", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 4, .crm = 2, .opc2 = 0,
      .type = ARM_CP_NO_RAW,
      .access = PL1_RW, .readfn = spsel_read, .writefn = spsel_write },
    { .name = "FPEXC32_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 3, .opc2 = 0,
      .type = ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, vfp.xregs[ARM_VFP_FPEXC]),
      .access = PL2_RW, .accessfn = fpexc32_access },
    { .name = "DACR32_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 3, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .resetvalue = 0,
      .writefn = dacr_write, .raw_writefn = raw_write,
      .fieldoffset = offsetof(CPUARMState, cp15.dacr32_el2) },
    { .name = "IFSR32_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 0, .opc2 = 1,
      .access = PL2_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.ifsr32_el2) },
    { .name = "SPSR_IRQ", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 3, .opc2 = 0,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_IRQ]) },
    { .name = "SPSR_ABT", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 3, .opc2 = 1,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_ABT]) },
    { .name = "SPSR_UND", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 3, .opc2 = 2,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_UND]) },
    { .name = "SPSR_FIQ", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 3, .opc2 = 3,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_FIQ]) },
    { .name = "MDCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 3, .opc2 = 1,
      .resetvalue = 0,
      .access = PL3_RW, .fieldoffset = offsetof(CPUARMState, cp15.mdcr_el3) },
    { .name = "SDCR", .type = ARM_CP_ALIAS,
      .cp = 15, .opc1 = 0, .crn = 1, .crm = 3, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_trap_aa32s_el1,
      .writefn = sdcr_write,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.mdcr_el3) },
    REGINFO_SENTINEL
};

/* Used to describe the behaviour of EL2 regs when EL2 does not exist.  */
static const ARMCPRegInfo el3_no_el2_cp_reginfo[] = {
    { .name = "VBAR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 12, .crm = 0, .opc2 = 0,
      .access = PL2_RW,
      .readfn = arm_cp_read_zero, .writefn = arm_cp_write_ignore },
    { .name = "HCR_EL2", .state = ARM_CP_STATE_BOTH,
      .type = ARM_CP_NO_RAW,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 0,
      .access = PL2_RW,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "HACR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 7,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "ESR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 2, .opc2 = 0,
      .access = PL2_RW,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CPTR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 2,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "MAIR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 10, .crm = 2, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "HMAIR1", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 4, .crn = 10, .crm = 2, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "AMAIR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 10, .crm = 3, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "HAMAIR1", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 4, .crn = 10, .crm = 3, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "AFSR0_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 1, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "AFSR1_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 1, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "TCR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 0, .opc2 = 2,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "VTCR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 1, .opc2 = 2,
      .access = PL2_RW, .accessfn = access_el3_aa32ns,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "VTTBR", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 6, .crm = 2,
      .access = PL2_RW, .accessfn = access_el3_aa32ns,
      .type = ARM_CP_CONST | ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "VTTBR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 1, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "SCTLR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "TPIDR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 13, .crm = 0, .opc2 = 2,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "TTBR0_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "HTTBR", .cp = 15, .opc1 = 4, .crm = 2,
      .access = PL2_RW, .type = ARM_CP_64BIT | ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "CNTHCTL_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 1, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CNTVOFF_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 0, .opc2 = 3,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CNTVOFF", .cp = 15, .opc1 = 4, .crm = 14,
      .access = PL2_RW, .type = ARM_CP_64BIT | ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "CNTHP_CVAL_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 2, .opc2 = 2,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CNTHP_CVAL", .cp = 15, .opc1 = 6, .crm = 14,
      .access = PL2_RW, .type = ARM_CP_64BIT | ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "CNTHP_TVAL_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 2, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "CNTHP_CTL_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 2, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "MDCR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 1,
      .access = PL2_RW, .accessfn = access_tda,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "HPFAR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 6, .crm = 0, .opc2 = 4,
      .access = PL2_RW, .accessfn = access_el3_aa32ns,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "HSTR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 3,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "FAR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 6, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "HIFAR", .state = ARM_CP_STATE_AA32,
      .type = ARM_CP_CONST,
      .cp = 15, .opc1 = 4, .crn = 6, .crm = 0, .opc2 = 2,
      .access = PL2_RW, .resetvalue = 0 },
    REGINFO_SENTINEL
};

/* Ditto, but for registers which exist in ARMv8 but not v7 */
static const ARMCPRegInfo el3_no_el2_v8_cp_reginfo[] = {
    { .name = "HCR2", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 4,
      .access = PL2_RW,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    REGINFO_SENTINEL
};

static void do_hcr_write(CPUARMState *env, uint64_t value, uint64_t valid_mask)
{
    ARMCPU *cpu = env_archcpu(env);

    if (arm_feature(env, ARM_FEATURE_V8)) {
        valid_mask |= MAKE_64BIT_MASK(0, 34);  /* ARMv8.0 */
    } else {
        valid_mask |= MAKE_64BIT_MASK(0, 28);  /* ARMv7VE */
    }

    if (arm_feature(env, ARM_FEATURE_EL3)) {
        valid_mask &= ~HCR_HCD;
    } else if (cpu->psci_conduit != QEMU_PSCI_CONDUIT_SMC) {
        /* Architecturally HCR.TSC is RES0 if EL3 is not implemented.
         * However, if we're using the SMC PSCI conduit then QEMU is
         * effectively acting like EL3 firmware and so the guest at
         * EL2 should retain the ability to prevent EL1 from being
         * able to make SMC calls into the ersatz firmware, so in
         * that case HCR.TSC should be read/write.
         */
        valid_mask &= ~HCR_TSC;
    }

    if (arm_feature(env, ARM_FEATURE_AARCH64)) {
        if (cpu_isar_feature(aa64_vh, cpu)) {
            valid_mask |= HCR_E2H;
        }
        if (cpu_isar_feature(aa64_lor, cpu)) {
            valid_mask |= HCR_TLOR;
        }
        if (cpu_isar_feature(aa64_pauth, cpu)) {
            valid_mask |= HCR_API | HCR_APK;
        }
        if (cpu_isar_feature(aa64_mte, cpu)) {
            valid_mask |= HCR_ATA | HCR_DCT | HCR_TID5;
        }
    }

    /* Clear RES0 bits.  */
    value &= valid_mask;

    /*
     * These bits change the MMU setup:
     * HCR_VM enables stage 2 translation
     * HCR_PTW forbids certain page-table setups
     * HCR_DC disables stage1 and enables stage2 translation
     * HCR_DCT enables tagging on (disabled) stage1 translation
     */
    if ((env->cp15.hcr_el2 ^ value) & (HCR_VM | HCR_PTW | HCR_DC | HCR_DCT)) {
        tlb_flush(CPU(cpu));
    }
    env->cp15.hcr_el2 = value;

    /*
     * Updates to VI and VF require us to update the status of
     * virtual interrupts, which are the logical OR of these bits
     * and the state of the input lines from the GIC. (This requires
     * that we have the iothread lock, which is done by marking the
     * reginfo structs as ARM_CP_IO.)
     * Note that if a write to HCR pends a VIRQ or VFIQ it is never
     * possible for it to be taken immediately, because VIRQ and
     * VFIQ are masked unless running at EL0 or EL1, and HCR
     * can only be written at EL2.
     */
    g_assert(qemu_mutex_iothread_locked());
    arm_cpu_update_virq(cpu);
    arm_cpu_update_vfiq(cpu);
}

static void hcr_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t value)
{
    do_hcr_write(env, value, 0);
}

static void hcr_writehigh(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    /* Handle HCR2 write, i.e. write to high half of HCR_EL2 */
    value = deposit64(env->cp15.hcr_el2, 32, 32, value);
    do_hcr_write(env, value, MAKE_64BIT_MASK(0, 32));
}

static void hcr_writelow(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    /* Handle HCR write, i.e. write to low half of HCR_EL2 */
    value = deposit64(env->cp15.hcr_el2, 0, 32, value);
    do_hcr_write(env, value, MAKE_64BIT_MASK(32, 32));
}

/*
 * Return the effective value of HCR_EL2.
 * Bits that are not included here:
 * RW       (read from SCR_EL3.RW as needed)
 */
uint64_t arm_hcr_el2_eff(CPUARMState *env)
{
    uint64_t ret = env->cp15.hcr_el2;

    if (arm_is_secure_below_el3(env)) {
        /*
         * "This register has no effect if EL2 is not enabled in the
         * current Security state".  This is ARMv8.4-SecEL2 speak for
         * !(SCR_EL3.NS==1 || SCR_EL3.EEL2==1).
         *
         * Prior to that, the language was "In an implementation that
         * includes EL3, when the value of SCR_EL3.NS is 0 the PE behaves
         * as if this field is 0 for all purposes other than a direct
         * read or write access of HCR_EL2".  With lots of enumeration
         * on a per-field basis.  In current QEMU, this is condition
         * is arm_is_secure_below_el3.
         *
         * Since the v8.4 language applies to the entire register, and
         * appears to be backward compatible, use that.
         */
        return 0;
    }

    /*
     * For a cpu that supports both aarch64 and aarch32, we can set bits
     * in HCR_EL2 (e.g. via EL3) that are RES0 when we enter EL2 as aa32.
     * Ignore all of the bits in HCR+HCR2 that are not valid for aarch32.
     */
    if (!arm_el_is_aa64(env, 2)) {
        uint64_t aa32_valid;

        /*
         * These bits are up-to-date as of ARMv8.6.
         * For HCR, it's easiest to list just the 2 bits that are invalid.
         * For HCR2, list those that are valid.
         */
        aa32_valid = MAKE_64BIT_MASK(0, 32) & ~(HCR_RW | HCR_TDZ);
        aa32_valid |= (HCR_CD | HCR_ID | HCR_TERR | HCR_TEA | HCR_MIOCNCE |
                       HCR_TID4 | HCR_TICAB | HCR_TOCU | HCR_TTLBIS);
        ret &= aa32_valid;
    }

    if (ret & HCR_TGE) {
        /* These bits are up-to-date as of ARMv8.6.  */
        if (ret & HCR_E2H) {
            ret &= ~(HCR_VM | HCR_FMO | HCR_IMO | HCR_AMO |
                     HCR_BSU_MASK | HCR_DC | HCR_TWI | HCR_TWE |
                     HCR_TID0 | HCR_TID2 | HCR_TPCP | HCR_TPU |
                     HCR_TDZ | HCR_CD | HCR_ID | HCR_MIOCNCE |
                     HCR_TID4 | HCR_TICAB | HCR_TOCU | HCR_ENSCXT |
                     HCR_TTLBIS | HCR_TTLBOS | HCR_TID5);
        } else {
            ret |= HCR_FMO | HCR_IMO | HCR_AMO;
        }
        ret &= ~(HCR_SWIO | HCR_PTW | HCR_VF | HCR_VI | HCR_VSE |
                 HCR_FB | HCR_TID1 | HCR_TID3 | HCR_TSC | HCR_TACR |
                 HCR_TSW | HCR_TTLB | HCR_TVM | HCR_HCD | HCR_TRVM |
                 HCR_TLOR);
    }

    return ret;
}

static void cptr_el2_write(CPUARMState *env, const ARMCPRegInfo *ri,
                           uint64_t value)
{
    /*
     * For A-profile AArch32 EL3, if NSACR.CP10
     * is 0 then HCPTR.{TCP11,TCP10} ignore writes and read as 1.
     */
    if (arm_feature(env, ARM_FEATURE_EL3) && !arm_el_is_aa64(env, 3) &&
        !arm_is_secure(env) && !extract32(env->cp15.nsacr, 10, 1)) {
        value &= ~(0x3 << 10);
        value |= env->cp15.cptr_el[2] & (0x3 << 10);
    }
    env->cp15.cptr_el[2] = value;
}

static uint64_t cptr_el2_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /*
     * For A-profile AArch32 EL3, if NSACR.CP10
     * is 0 then HCPTR.{TCP11,TCP10} ignore writes and read as 1.
     */
    uint64_t value = env->cp15.cptr_el[2];

    if (arm_feature(env, ARM_FEATURE_EL3) && !arm_el_is_aa64(env, 3) &&
        !arm_is_secure(env) && !extract32(env->cp15.nsacr, 10, 1)) {
        value |= 0x3 << 10;
    }
    return value;
}

static const ARMCPRegInfo el2_cp_reginfo[] = {
    { .name = "HCR_EL2", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_IO,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 0,
      .access = PL2_RW, .fieldoffset = offsetof(CPUARMState, cp15.hcr_el2),
      .writefn = hcr_write },
    { .name = "HCR", .state = ARM_CP_STATE_AA32,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .cp = 15, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 0,
      .access = PL2_RW, .fieldoffset = offsetof(CPUARMState, cp15.hcr_el2),
      .writefn = hcr_writelow },
    { .name = "HACR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 7,
      .access = PL2_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "ELR_EL2", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 0, .opc2 = 1,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, elr_el[2]) },
    { .name = "ESR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 2, .opc2 = 0,
      .access = PL2_RW, .fieldoffset = offsetof(CPUARMState, cp15.esr_el[2]) },
    { .name = "FAR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 6, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .fieldoffset = offsetof(CPUARMState, cp15.far_el[2]) },
    { .name = "HIFAR", .state = ARM_CP_STATE_AA32,
      .type = ARM_CP_ALIAS,
      .cp = 15, .opc1 = 4, .crn = 6, .crm = 0, .opc2 = 2,
      .access = PL2_RW,
      .fieldoffset = offsetofhigh32(CPUARMState, cp15.far_el[2]) },
    { .name = "SPSR_EL2", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 4, .crn = 4, .crm = 0, .opc2 = 0,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_HYP]) },
    { .name = "VBAR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 12, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .writefn = vbar_write,
      .fieldoffset = offsetof(CPUARMState, cp15.vbar_el[2]),
      .resetvalue = 0 },
    { .name = "SP_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 4, .crm = 1, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, sp_el[2]) },
    { .name = "CPTR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 2,
      .access = PL2_RW, .accessfn = cptr_access, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.cptr_el[2]),
      .readfn = cptr_el2_read, .writefn = cptr_el2_write },
    { .name = "MAIR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 10, .crm = 2, .opc2 = 0,
      .access = PL2_RW, .fieldoffset = offsetof(CPUARMState, cp15.mair_el[2]),
      .resetvalue = 0 },
    { .name = "HMAIR1", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 4, .crn = 10, .crm = 2, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_ALIAS,
      .fieldoffset = offsetofhigh32(CPUARMState, cp15.mair_el[2]) },
    { .name = "AMAIR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 10, .crm = 3, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    /* HAMAIR1 is mapped to AMAIR_EL2[63:32] */
    { .name = "HAMAIR1", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 4, .crn = 10, .crm = 3, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "AFSR0_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 1, .opc2 = 0,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "AFSR1_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 1, .opc2 = 1,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "TCR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 0, .opc2 = 2,
      .access = PL2_RW, .writefn = vmsa_tcr_el12_write,
      /* no .raw_writefn or .resetfn needed as we never use mask/base_mask */
      .fieldoffset = offsetof(CPUARMState, cp15.tcr_el[2]) },
    { .name = "VTCR", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 4, .crn = 2, .crm = 1, .opc2 = 2,
      .type = ARM_CP_ALIAS,
      .access = PL2_RW, .accessfn = access_el3_aa32ns,
      .fieldoffset = offsetof(CPUARMState, cp15.vtcr_el2) },
    { .name = "VTCR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 1, .opc2 = 2,
      .access = PL2_RW,
      /* no .writefn needed as this can't cause an ASID change;
       * no .raw_writefn or .resetfn needed as we never use mask/base_mask
       */
      .fieldoffset = offsetof(CPUARMState, cp15.vtcr_el2) },
    { .name = "VTTBR", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 6, .crm = 2,
      .type = ARM_CP_64BIT | ARM_CP_ALIAS,
      .access = PL2_RW, .accessfn = access_el3_aa32ns,
      .fieldoffset = offsetof(CPUARMState, cp15.vttbr_el2),
      .writefn = vttbr_write },
    { .name = "VTTBR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 1, .opc2 = 0,
      .access = PL2_RW, .writefn = vttbr_write,
      .fieldoffset = offsetof(CPUARMState, cp15.vttbr_el2) },
    { .name = "SCTLR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .raw_writefn = raw_write, .writefn = sctlr_write,
      .fieldoffset = offsetof(CPUARMState, cp15.sctlr_el[2]) },
    { .name = "TPIDR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 13, .crm = 0, .opc2 = 2,
      .access = PL2_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.tpidr_el[2]) },
    { .name = "TTBR0_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 0, .opc2 = 0,
      .access = PL2_RW, .resetvalue = 0, .writefn = vmsa_tcr_ttbr_el2_write,
      .fieldoffset = offsetof(CPUARMState, cp15.ttbr0_el[2]) },
    { .name = "HTTBR", .cp = 15, .opc1 = 4, .crm = 2,
      .access = PL2_RW, .type = ARM_CP_64BIT | ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.ttbr0_el[2]) },
    { .name = "TLBIALLNSNH",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 4,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbiall_nsnh_write },
    { .name = "TLBIALLNSNHIS",
      .cp = 15, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 4,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbiall_nsnh_is_write },
    { .name = "TLBIALLH", .cp = 15, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbiall_hyp_write },
    { .name = "TLBIALLHIS", .cp = 15, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbiall_hyp_is_write },
    { .name = "TLBIMVAH", .cp = 15, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbimva_hyp_write },
    { .name = "TLBIMVAHIS", .cp = 15, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbimva_hyp_is_write },
    { .name = "TLBI_ALLE2", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 0,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbi_aa64_alle2_write },
    { .name = "TLBI_VAE2", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbi_aa64_vae2_write },
    { .name = "TLBI_VALE2", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 7, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae2_write },
    { .name = "TLBI_ALLE2IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 0,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_alle2is_write },
    { .name = "TLBI_VAE2IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 1,
      .type = ARM_CP_NO_RAW, .access = PL2_W,
      .writefn = tlbi_aa64_vae2is_write },
    { .name = "TLBI_VALE2IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 8, .crm = 3, .opc2 = 5,
      .access = PL2_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae2is_write },
#ifndef CONFIG_USER_ONLY
    /* Unlike the other EL2-related AT operations, these must
     * UNDEF from EL3 if EL2 is not implemented, which is why we
     * define them here rather than with the rest of the AT ops.
     */
    { .name = "AT_S1E2R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 0,
      .access = PL2_W, .accessfn = at_s1e2_access,
      .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC, .writefn = ats_write64 },
    { .name = "AT_S1E2W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 1,
      .access = PL2_W, .accessfn = at_s1e2_access,
      .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC, .writefn = ats_write64 },
    /* The AArch32 ATS1H* operations are CONSTRAINED UNPREDICTABLE
     * if EL2 is not implemented; we choose to UNDEF. Behaviour at EL3
     * with SCR.NS == 0 outside Monitor mode is UNPREDICTABLE; we choose
     * to behave as if SCR.NS was 1.
     */
    { .name = "ATS1HR", .cp = 15, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 0,
      .access = PL2_W,
      .writefn = ats1h_write, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC },
    { .name = "ATS1HW", .cp = 15, .opc1 = 4, .crn = 7, .crm = 8, .opc2 = 1,
      .access = PL2_W,
      .writefn = ats1h_write, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC },
    { .name = "CNTHCTL_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 1, .opc2 = 0,
      /* ARMv7 requires bit 0 and 1 to reset to 1. ARMv8 defines the
       * reset values as IMPDEF. We choose to reset to 3 to comply with
       * both ARMv7 and ARMv8.
       */
      .access = PL2_RW, .resetvalue = 3,
      .fieldoffset = offsetof(CPUARMState, cp15.cnthctl_el2) },
    { .name = "CNTVOFF_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 0, .opc2 = 3,
      .access = PL2_RW, .type = ARM_CP_IO, .resetvalue = 0,
      .writefn = gt_cntvoff_write,
      .fieldoffset = offsetof(CPUARMState, cp15.cntvoff_el2) },
    { .name = "CNTVOFF", .cp = 15, .opc1 = 4, .crm = 14,
      .access = PL2_RW, .type = ARM_CP_64BIT | ARM_CP_ALIAS | ARM_CP_IO,
      .writefn = gt_cntvoff_write,
      .fieldoffset = offsetof(CPUARMState, cp15.cntvoff_el2) },
    { .name = "CNTHP_CVAL_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 2, .opc2 = 2,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_HYP].cval),
      .type = ARM_CP_IO, .access = PL2_RW,
      .writefn = gt_hyp_cval_write, .raw_writefn = raw_write },
    { .name = "CNTHP_CVAL", .cp = 15, .opc1 = 6, .crm = 14,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_HYP].cval),
      .access = PL2_RW, .type = ARM_CP_64BIT | ARM_CP_IO,
      .writefn = gt_hyp_cval_write, .raw_writefn = raw_write },
    { .name = "CNTHP_TVAL_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 2, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL2_RW,
      .resetfn = gt_hyp_timer_reset,
      .readfn = gt_hyp_tval_read, .writefn = gt_hyp_tval_write },
    { .name = "CNTHP_CTL_EL2", .state = ARM_CP_STATE_BOTH,
      .type = ARM_CP_IO,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 2, .opc2 = 1,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_HYP].ctl),
      .resetvalue = 0,
      .writefn = gt_hyp_ctl_write, .raw_writefn = raw_write },
#endif
    /* The only field of MDCR_EL2 that has a defined architectural reset value
     * is MDCR_EL2.HPMN which should reset to the value of PMCR_EL0.N; but we
     * don't implement any PMU event counters, so using zero as a reset
     * value for MDCR_EL2 is okay
     */
    { .name = "MDCR_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 1,
      .access = PL2_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.mdcr_el2), },
    { .name = "HPFAR", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 4, .crn = 6, .crm = 0, .opc2 = 4,
      .access = PL2_RW, .accessfn = access_el3_aa32ns,
      .fieldoffset = offsetof(CPUARMState, cp15.hpfar_el2) },
    { .name = "HPFAR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 6, .crm = 0, .opc2 = 4,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.hpfar_el2) },
    { .name = "HSTR_EL2", .state = ARM_CP_STATE_BOTH,
      .cp = 15, .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 3,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.hstr_el2) },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo el2_v8_cp_reginfo[] = {
    { .name = "HCR2", .state = ARM_CP_STATE_AA32,
      .type = ARM_CP_ALIAS | ARM_CP_IO,
      .cp = 15, .opc1 = 4, .crn = 1, .crm = 1, .opc2 = 4,
      .access = PL2_RW,
      .fieldoffset = offsetofhigh32(CPUARMState, cp15.hcr_el2),
      .writefn = hcr_writehigh },
    REGINFO_SENTINEL
};

static CPAccessResult nsacr_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    /* The NSACR is RW at EL3, and RO for NS EL1 and NS EL2.
     * At Secure EL1 it traps to EL3.
     */
    if (arm_current_el(env) == 3) {
        return CP_ACCESS_OK;
    }
    if (arm_is_secure_below_el3(env)) {
        return CP_ACCESS_TRAP_EL3;
    }
    /* Accesses from EL1 NS and EL2 NS are UNDEF for write but allow reads. */
    if (isread) {
        return CP_ACCESS_OK;
    }
    return CP_ACCESS_TRAP_UNCATEGORIZED;
}

static const ARMCPRegInfo el3_cp_reginfo[] = {
    { .name = "SCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 1, .opc2 = 0,
      .access = PL3_RW, .fieldoffset = offsetof(CPUARMState, cp15.scr_el3),
      .resetvalue = 0, .writefn = scr_write },
    { .name = "SCR",  .type = ARM_CP_ALIAS | ARM_CP_NEWEL,
      .cp = 15, .opc1 = 0, .crn = 1, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_trap_aa32s_el1,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.scr_el3),
      .writefn = scr_write },
    { .name = "SDER32_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 1, .opc2 = 1,
      .access = PL3_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.sder) },
    { .name = "SDER",
      .cp = 15, .opc1 = 0, .crn = 1, .crm = 1, .opc2 = 1,
      .access = PL3_RW, .resetvalue = 0,
      .fieldoffset = offsetoflow32(CPUARMState, cp15.sder) },
    { .name = "MVBAR", .cp = 15, .opc1 = 0, .crn = 12, .crm = 0, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_trap_aa32s_el1,
      .writefn = vbar_write, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.mvbar) },
    { .name = "TTBR0_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 2, .crm = 0, .opc2 = 0,
      .access = PL3_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.ttbr0_el[3]) },
    { .name = "TCR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 2, .crm = 0, .opc2 = 2,
      .access = PL3_RW,
      /* no .writefn needed as this can't cause an ASID change;
       * we must provide a .raw_writefn and .resetfn because we handle
       * reset and migration for the AArch32 TTBCR(S), which might be
       * using mask and base_mask.
       */
      .resetfn = vmsa_ttbcr_reset, .raw_writefn = vmsa_ttbcr_raw_write,
      .fieldoffset = offsetof(CPUARMState, cp15.tcr_el[3]) },
    { .name = "ELR_EL3", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 6, .crn = 4, .crm = 0, .opc2 = 1,
      .access = PL3_RW,
      .fieldoffset = offsetof(CPUARMState, elr_el[3]) },
    { .name = "ESR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 5, .crm = 2, .opc2 = 0,
      .access = PL3_RW, .fieldoffset = offsetof(CPUARMState, cp15.esr_el[3]) },
    { .name = "FAR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 6, .crm = 0, .opc2 = 0,
      .access = PL3_RW, .fieldoffset = offsetof(CPUARMState, cp15.far_el[3]) },
    { .name = "SPSR_EL3", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_ALIAS,
      .opc0 = 3, .opc1 = 6, .crn = 4, .crm = 0, .opc2 = 0,
      .access = PL3_RW,
      .fieldoffset = offsetof(CPUARMState, banked_spsr[BANK_MON]) },
    { .name = "VBAR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 12, .crm = 0, .opc2 = 0,
      .access = PL3_RW, .writefn = vbar_write,
      .fieldoffset = offsetof(CPUARMState, cp15.vbar_el[3]),
      .resetvalue = 0 },
    { .name = "CPTR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 1, .opc2 = 2,
      .access = PL3_RW, .accessfn = cptr_access, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.cptr_el[3]) },
    { .name = "TPIDR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 13, .crm = 0, .opc2 = 2,
      .access = PL3_RW, .resetvalue = 0,
      .fieldoffset = offsetof(CPUARMState, cp15.tpidr_el[3]) },
    { .name = "AMAIR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 10, .crm = 3, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "AFSR0_EL3", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 6, .crn = 5, .crm = 1, .opc2 = 0,
      .access = PL3_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "AFSR1_EL3", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 6, .crn = 5, .crm = 1, .opc2 = 1,
      .access = PL3_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    { .name = "TLBI_ALLE3IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 3, .opc2 = 0,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_alle3is_write },
    { .name = "TLBI_VAE3IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 3, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae3is_write },
    { .name = "TLBI_VALE3IS", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 3, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae3is_write },
    { .name = "TLBI_ALLE3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 7, .opc2 = 0,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_alle3_write },
    { .name = "TLBI_VAE3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 7, .opc2 = 1,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae3_write },
    { .name = "TLBI_VALE3", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 6, .crn = 8, .crm = 7, .opc2 = 5,
      .access = PL3_W, .type = ARM_CP_NO_RAW,
      .writefn = tlbi_aa64_vae3_write },
    REGINFO_SENTINEL
};

#ifndef CONFIG_USER_ONLY
/* Test if system register redirection is to occur in the current state.  */
static bool redirect_for_e2h(CPUARMState *env)
{
    return arm_current_el(env) == 2 && (arm_hcr_el2_eff(env) & HCR_E2H);
}

static uint64_t el2_e2h_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    CPReadFn *readfn;

    if (redirect_for_e2h(env)) {
        /* Switch to the saved EL2 version of the register.  */
        ri = ri->opaque;
        readfn = ri->readfn;
    } else {
        readfn = ri->orig_readfn;
    }
    if (readfn == NULL) {
        readfn = raw_read;
    }
    return readfn(env, ri);
}

static void el2_e2h_write(CPUARMState *env, const ARMCPRegInfo *ri,
                          uint64_t value)
{
    CPWriteFn *writefn;

    if (redirect_for_e2h(env)) {
        /* Switch to the saved EL2 version of the register.  */
        ri = ri->opaque;
        writefn = ri->writefn;
    } else {
        writefn = ri->orig_writefn;
    }
    if (writefn == NULL) {
        writefn = raw_write;
    }
    writefn(env, ri, value);
}

static void define_arm_vh_e2h_redirects_aliases(ARMCPU *cpu)
{
    struct E2HAlias {
        uint32_t src_key, dst_key, new_key;
        const char *src_name, *dst_name, *new_name;
        bool (*feature)(const ARMISARegisters *id);
    };

#define K(op0, op1, crn, crm, op2) \
    ENCODE_AA64_CP_REG(CP_REG_ARM64_SYSREG_CP, crn, crm, op0, op1, op2)

    static const struct E2HAlias aliases[] = {
        { K(3, 0,  1, 0, 0), K(3, 4,  1, 0, 0), K(3, 5, 1, 0, 0),
          "SCTLR", "SCTLR_EL2", "SCTLR_EL12" },
        { K(3, 0,  1, 0, 2), K(3, 4,  1, 1, 2), K(3, 5, 1, 0, 2),
          "CPACR", "CPTR_EL2", "CPACR_EL12" },
        { K(3, 0,  2, 0, 0), K(3, 4,  2, 0, 0), K(3, 5, 2, 0, 0),
          "TTBR0_EL1", "TTBR0_EL2", "TTBR0_EL12" },
        { K(3, 0,  2, 0, 1), K(3, 4,  2, 0, 1), K(3, 5, 2, 0, 1),
          "TTBR1_EL1", "TTBR1_EL2", "TTBR1_EL12" },
        { K(3, 0,  2, 0, 2), K(3, 4,  2, 0, 2), K(3, 5, 2, 0, 2),
          "TCR_EL1", "TCR_EL2", "TCR_EL12" },
        { K(3, 0,  4, 0, 0), K(3, 4,  4, 0, 0), K(3, 5, 4, 0, 0),
          "SPSR_EL1", "SPSR_EL2", "SPSR_EL12" },
        { K(3, 0,  4, 0, 1), K(3, 4,  4, 0, 1), K(3, 5, 4, 0, 1),
          "ELR_EL1", "ELR_EL2", "ELR_EL12" },
        { K(3, 0,  5, 1, 0), K(3, 4,  5, 1, 0), K(3, 5, 5, 1, 0),
          "AFSR0_EL1", "AFSR0_EL2", "AFSR0_EL12" },
        { K(3, 0,  5, 1, 1), K(3, 4,  5, 1, 1), K(3, 5, 5, 1, 1),
          "AFSR1_EL1", "AFSR1_EL2", "AFSR1_EL12" },
        { K(3, 0,  5, 2, 0), K(3, 4,  5, 2, 0), K(3, 5, 5, 2, 0),
          "ESR_EL1", "ESR_EL2", "ESR_EL12" },
        { K(3, 0,  6, 0, 0), K(3, 4,  6, 0, 0), K(3, 5, 6, 0, 0),
          "FAR_EL1", "FAR_EL2", "FAR_EL12" },
        { K(3, 0, 10, 2, 0), K(3, 4, 10, 2, 0), K(3, 5, 10, 2, 0),
          "MAIR_EL1", "MAIR_EL2", "MAIR_EL12" },
        { K(3, 0, 10, 3, 0), K(3, 4, 10, 3, 0), K(3, 5, 10, 3, 0),
          "AMAIR0", "AMAIR_EL2", "AMAIR_EL12" },
        { K(3, 0, 12, 0, 0), K(3, 4, 12, 0, 0), K(3, 5, 12, 0, 0),
          "VBAR", "VBAR_EL2", "VBAR_EL12" },
        { K(3, 0, 13, 0, 1), K(3, 4, 13, 0, 1), K(3, 5, 13, 0, 1),
          "CONTEXTIDR_EL1", "CONTEXTIDR_EL2", "CONTEXTIDR_EL12" },
        { K(3, 0, 14, 1, 0), K(3, 4, 14, 1, 0), K(3, 5, 14, 1, 0),
          "CNTKCTL", "CNTHCTL_EL2", "CNTKCTL_EL12" },

        /*
         * Note that redirection of ZCR is mentioned in the description
         * of ZCR_EL2, and aliasing in the description of ZCR_EL1, but
         * not in the summary table.
         */
        { K(3, 0,  1, 2, 0), K(3, 4,  1, 2, 0), K(3, 5, 1, 2, 0),
          "ZCR_EL1", "ZCR_EL2", "ZCR_EL12", isar_feature_aa64_sve },

        { K(3, 0,  5, 6, 0), K(3, 4,  5, 6, 0), K(3, 5, 5, 6, 0),
          "TFSR_EL1", "TFSR_EL2", "TFSR_EL12", isar_feature_aa64_mte },

        /* TODO: ARMv8.2-SPE -- PMSCR_EL2 */
        /* TODO: ARMv8.4-Trace -- TRFCR_EL2 */
    };
#undef K

    size_t i;

    for (i = 0; i < ARRAY_SIZE(aliases); i++) {
        const struct E2HAlias *a = &aliases[i];
        ARMCPRegInfo *src_reg, *dst_reg;

        if (a->feature && !a->feature(&cpu->isar)) {
            continue;
        }

        src_reg = g_hash_table_lookup(cpu->cp_regs, &a->src_key);
        dst_reg = g_hash_table_lookup(cpu->cp_regs, &a->dst_key);
        g_assert(src_reg != NULL);
        g_assert(dst_reg != NULL);

        /* Cross-compare names to detect typos in the keys.  */
        g_assert(strcmp(src_reg->name, a->src_name) == 0);
        g_assert(strcmp(dst_reg->name, a->dst_name) == 0);

        /* None of the core system registers use opaque; we will.  */
        g_assert(src_reg->opaque == NULL);

        /* Create alias before redirection so we dup the right data. */
        if (a->new_key) {
            ARMCPRegInfo *new_reg = g_memdup(src_reg, sizeof(ARMCPRegInfo));
            uint32_t *new_key = g_memdup(&a->new_key, sizeof(uint32_t));
            bool ok;

            new_reg->name = a->new_name;
            new_reg->type |= ARM_CP_ALIAS;
            /* Remove PL1/PL0 access, leaving PL2/PL3 R/W in place.  */
            new_reg->access &= PL2_RW | PL3_RW;

            ok = g_hash_table_insert(cpu->cp_regs, new_key, new_reg);
            g_assert(ok);
        }

        src_reg->opaque = dst_reg;
        src_reg->orig_readfn = src_reg->readfn ?: raw_read;
        src_reg->orig_writefn = src_reg->writefn ?: raw_write;
        if (!src_reg->raw_readfn) {
            src_reg->raw_readfn = raw_read;
        }
        if (!src_reg->raw_writefn) {
            src_reg->raw_writefn = raw_write;
        }
        src_reg->readfn = el2_e2h_read;
        src_reg->writefn = el2_e2h_write;
    }
}
#endif

static CPAccessResult ctr_el0_access(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    int cur_el = arm_current_el(env);

    if (cur_el < 2) {
        uint64_t hcr = arm_hcr_el2_eff(env);

        if (cur_el == 0) {
            if ((hcr & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)) {
                if (!(env->cp15.sctlr_el[2] & SCTLR_UCT)) {
                    return CP_ACCESS_TRAP_EL2;
                }
            } else {
                if (!(env->cp15.sctlr_el[1] & SCTLR_UCT)) {
                    return CP_ACCESS_TRAP;
                }
                if (hcr & HCR_TID2) {
                    return CP_ACCESS_TRAP_EL2;
                }
            }
        } else if (hcr & HCR_TID2) {
            return CP_ACCESS_TRAP_EL2;
        }
    }

    if (arm_current_el(env) < 2 && arm_hcr_el2_eff(env) & HCR_TID2) {
        return CP_ACCESS_TRAP_EL2;
    }

    return CP_ACCESS_OK;
}

static void oslar_write(CPUARMState *env, const ARMCPRegInfo *ri,
                        uint64_t value)
{
    /* Writes to OSLAR_EL1 may update the OS lock status, which can be
     * read via a bit in OSLSR_EL1.
     */
    int oslock;

    if (ri->state == ARM_CP_STATE_AA32) {
        oslock = (value == 0xC5ACCE55);
    } else {
        oslock = value & 1;
    }

    env->cp15.oslsr_el1 = deposit32(env->cp15.oslsr_el1, 1, 1, oslock);
}

static const ARMCPRegInfo debug_cp_reginfo[] = {
    /* DBGDRAR, DBGDSAR: always RAZ since we don't implement memory mapped
     * debug components. The AArch64 version of DBGDRAR is named MDRAR_EL1;
     * unlike DBGDRAR it is never accessible from EL0.
     * DBGDSAR is deprecated and must RAZ from v8 anyway, so it has no AArch64
     * accessor.
     */
    { .name = "DBGDRAR", .cp = 14, .crn = 1, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL0_R, .accessfn = access_tdra,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "MDRAR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 2, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 0,
      .access = PL1_R, .accessfn = access_tdra,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "DBGDSAR", .cp = 14, .crn = 2, .crm = 0, .opc1 = 0, .opc2 = 0,
      .access = PL0_R, .accessfn = access_tdra,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    /* Monitor debug system control register; the 32-bit alias is DBGDSCRext. */
    { .name = "MDSCR_EL1", .state = ARM_CP_STATE_BOTH,
      .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_tda,
      .fieldoffset = offsetof(CPUARMState, cp15.mdscr_el1),
      .resetvalue = 0 },
    /* MDCCSR_EL0, aka DBGDSCRint. This is a read-only mirror of MDSCR_EL1.
     * We don't implement the configurable EL0 access.
     */
    { .name = "MDCCSR_EL0", .state = ARM_CP_STATE_BOTH,
      .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 0,
      .type = ARM_CP_ALIAS,
      .access = PL1_R, .accessfn = access_tda,
      .fieldoffset = offsetof(CPUARMState, cp15.mdscr_el1), },
    { .name = "OSLAR_EL1", .state = ARM_CP_STATE_BOTH,
      .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 4,
      .access = PL1_W, .type = ARM_CP_NO_RAW,
      .accessfn = access_tdosa,
      .writefn = oslar_write },
    { .name = "OSLSR_EL1", .state = ARM_CP_STATE_BOTH,
      .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 1, .crm = 1, .opc2 = 4,
      .access = PL1_R, .resetvalue = 10,
      .accessfn = access_tdosa,
      .fieldoffset = offsetof(CPUARMState, cp15.oslsr_el1) },
    /* Dummy OSDLR_EL1: 32-bit Linux will read this */
    { .name = "OSDLR_EL1", .state = ARM_CP_STATE_BOTH,
      .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 1, .crm = 3, .opc2 = 4,
      .access = PL1_RW, .accessfn = access_tdosa,
      .type = ARM_CP_NOP },
    /* Dummy DBGVCR: Linux wants to clear this on startup, but we don't
     * implement vector catch debug events yet.
     */
    { .name = "DBGVCR",
      .cp = 14, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tda,
      .type = ARM_CP_NOP },
    /* Dummy DBGVCR32_EL2 (which is only for a 64-bit hypervisor
     * to save and restore a 32-bit guest's DBGVCR)
     */
    { .name = "DBGVCR32_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 2, .opc1 = 4, .crn = 0, .crm = 7, .opc2 = 0,
      .access = PL2_RW, .accessfn = access_tda,
      .type = ARM_CP_NOP },
    /* Dummy MDCCINT_EL1, since we don't implement the Debug Communications
     * Channel but Linux may try to access this register. The 32-bit
     * alias is DBGDCCINT.
     */
    { .name = "MDCCINT_EL1", .state = ARM_CP_STATE_BOTH,
      .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_tda,
      .type = ARM_CP_NOP },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo debug_lpae_cp_reginfo[] = {
    /* 64 bit access versions of the (dummy) debug registers */
    { .name = "DBGDRAR", .cp = 14, .crm = 1, .opc1 = 0,
      .access = PL0_R, .type = ARM_CP_CONST|ARM_CP_64BIT, .resetvalue = 0 },
    { .name = "DBGDSAR", .cp = 14, .crm = 2, .opc1 = 0,
      .access = PL0_R, .type = ARM_CP_CONST|ARM_CP_64BIT, .resetvalue = 0 },
    REGINFO_SENTINEL
};

/* Return the exception level to which exceptions should be taken
 * via SVEAccessTrap.  If an exception should be routed through
 * AArch64.AdvSIMDFPAccessTrap, return 0; fp_exception_el should
 * take care of raising that exception.
 * C.f. the ARM pseudocode function CheckSVEEnabled.
 */
int sve_exception_el(CPUARMState *env, int el)
{
#ifndef CONFIG_USER_ONLY
    uint64_t hcr_el2 = arm_hcr_el2_eff(env);

    if (el <= 1 && (hcr_el2 & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
        bool disabled = false;

        /* The CPACR.ZEN controls traps to EL1:
         * 0, 2 : trap EL0 and EL1 accesses
         * 1    : trap only EL0 accesses
         * 3    : trap no accesses
         */
        if (!extract32(env->cp15.cpacr_el1, 16, 1)) {
            disabled = true;
        } else if (!extract32(env->cp15.cpacr_el1, 17, 1)) {
            disabled = el == 0;
        }
        if (disabled) {
            /* route_to_el2 */
            return hcr_el2 & HCR_TGE ? 2 : 1;
        }

        /* Check CPACR.FPEN.  */
        if (!extract32(env->cp15.cpacr_el1, 20, 1)) {
            disabled = true;
        } else if (!extract32(env->cp15.cpacr_el1, 21, 1)) {
            disabled = el == 0;
        }
        if (disabled) {
            return 0;
        }
    }

    /* CPTR_EL2.  Since TZ and TFP are positive,
     * they will be zero when EL2 is not present.
     */
    if (el <= 2 && !arm_is_secure_below_el3(env)) {
        if (env->cp15.cptr_el[2] & CPTR_TZ) {
            return 2;
        }
        if (env->cp15.cptr_el[2] & CPTR_TFP) {
            return 0;
        }
    }

    /* CPTR_EL3.  Since EZ is negative we must check for EL3.  */
    if (arm_feature(env, ARM_FEATURE_EL3)
        && !(env->cp15.cptr_el[3] & CPTR_EZ)) {
        return 3;
    }
#endif
    return 0;
}

static uint32_t sve_zcr_get_valid_len(ARMCPU *cpu, uint32_t start_len)
{
    uint32_t end_len;

    end_len = start_len &= 0xf;
    if (!test_bit(start_len, cpu->sve_vq_map)) {
        end_len = find_last_bit(cpu->sve_vq_map, start_len);
        assert(end_len < start_len);
    }
    return end_len;
}

/*
 * Given that SVE is enabled, return the vector length for EL.
 */
uint32_t sve_zcr_len_for_el(CPUARMState *env, int el)
{
    ARMCPU *cpu = env_archcpu(env);
    uint32_t zcr_len = cpu->sve_max_vq - 1;

    if (el <= 1) {
        zcr_len = MIN(zcr_len, 0xf & (uint32_t)env->vfp.zcr_el[1]);
    }
    if (el <= 2 && arm_feature(env, ARM_FEATURE_EL2)) {
        zcr_len = MIN(zcr_len, 0xf & (uint32_t)env->vfp.zcr_el[2]);
    }
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        zcr_len = MIN(zcr_len, 0xf & (uint32_t)env->vfp.zcr_el[3]);
    }

    return sve_zcr_get_valid_len(cpu, zcr_len);
}

static void zcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                      uint64_t value)
{
    int cur_el = arm_current_el(env);
    int old_len = sve_zcr_len_for_el(env, cur_el);
    int new_len;

    /* Bits other than [3:0] are RAZ/WI.  */
    QEMU_BUILD_BUG_ON(ARM_MAX_VQ > 16);
    raw_write(env, ri, value & 0xf);

    /*
     * Because we arrived here, we know both FP and SVE are enabled;
     * otherwise we would have trapped access to the ZCR_ELn register.
     */
    new_len = sve_zcr_len_for_el(env, cur_el);
    if (new_len < old_len) {
        aarch64_sve_narrow_vq(env, new_len + 1);
    }
}

static const ARMCPRegInfo zcr_el1_reginfo = {
    .name = "ZCR_EL1", .state = ARM_CP_STATE_AA64,
    .opc0 = 3, .opc1 = 0, .crn = 1, .crm = 2, .opc2 = 0,
    .access = PL1_RW, .type = ARM_CP_SVE,
    .fieldoffset = offsetof(CPUARMState, vfp.zcr_el[1]),
    .writefn = zcr_write, .raw_writefn = raw_write
};

static const ARMCPRegInfo zcr_el2_reginfo = {
    .name = "ZCR_EL2", .state = ARM_CP_STATE_AA64,
    .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 2, .opc2 = 0,
    .access = PL2_RW, .type = ARM_CP_SVE,
    .fieldoffset = offsetof(CPUARMState, vfp.zcr_el[2]),
    .writefn = zcr_write, .raw_writefn = raw_write
};

static const ARMCPRegInfo zcr_no_el2_reginfo = {
    .name = "ZCR_EL2", .state = ARM_CP_STATE_AA64,
    .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 2, .opc2 = 0,
    .access = PL2_RW, .type = ARM_CP_SVE,
    .readfn = arm_cp_read_zero, .writefn = arm_cp_write_ignore
};

static const ARMCPRegInfo zcr_el3_reginfo = {
    .name = "ZCR_EL3", .state = ARM_CP_STATE_AA64,
    .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 2, .opc2 = 0,
    .access = PL3_RW, .type = ARM_CP_SVE,
    .fieldoffset = offsetof(CPUARMState, vfp.zcr_el[3]),
    .writefn = zcr_write, .raw_writefn = raw_write
};

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

    if (!extract64(wcr, 0, 1)) {
        /* E bit clear : watchpoint disabled */
        return;
    }

    switch (extract64(wcr, 3, 2)) {
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

    /* Attempts to use both MASK and BAS fields simultaneously are
     * CONSTRAINED UNPREDICTABLE; we opt to ignore BAS in this case,
     * thus generating a watchpoint for every byte in the masked region.
     */
    mask = extract64(wcr, 24, 4);
    if (mask == 1 || mask == 2) {
        /* Reserved values of MASK; we must act as if the mask value was
         * some non-reserved value, or as if the watchpoint were disabled.
         * We choose the latter.
         */
        return;
    } else if (mask) {
        /* Watchpoint covers an aligned area up to 2GB in size */
        len = 1ULL << mask;
        /* If masked bits in WVR are not zero it's CONSTRAINED UNPREDICTABLE
         * whether the watchpoint fires when the unmasked bits match; we opt
         * to generate the exceptions.
         */
        wvr &= ~(len - 1);
    } else {
        /* Watchpoint covers bytes defined by the byte address select bits */
        int bas = extract64(wcr, 5, 8);
        int basstart;

        if (extract64(wvr, 2, 1)) {
            /* Deprecated case of an only 4-aligned address. BAS[7:4] are
             * ignored, and BAS[3:0] define which bytes to watch.
             */
            bas &= 0xf;
        }

        if (bas == 0) {
            /* This must act as if the watchpoint is disabled */
            return;
        }

        /* The BAS bits are supposed to be programmed to indicate a contiguous
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

    /* Completely clear out existing QEMU watchpoints and our array, to
     * avoid possible stale entries following migration load.
     */
    cpu_watchpoint_remove_all(CPU(cpu), BP_CPU);
    memset(env->cpu_watchpoint, 0, sizeof(env->cpu_watchpoint));

    for (i = 0; i < ARRAY_SIZE(cpu->env.cpu_watchpoint); i++) {
        hw_watchpoint_update(cpu, i);
    }
}

static void dbgwvr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    int i = ri->crm;

    /* Bits [63:49] are hardwired to the value of bit [48]; that is, the
     * register reads and behaves as if values written are sign extended.
     * Bits [1:0] are RES0.
     */
    value = sextract64(value, 0, 49) & ~3ULL;

    raw_write(env, ri, value);
    hw_watchpoint_update(cpu, i);
}

static void dbgwcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    int i = ri->crm;

    raw_write(env, ri, value);
    hw_watchpoint_update(cpu, i);
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
        /* Bits [63:49] are hardwired to the value of bit [48]; that is,
         * we behave as if the register was sign extended. Bits [1:0] are
         * RES0. The BAS field is used to allow setting breakpoints on 16
         * bit wide instructions; it is CONSTRAINED UNPREDICTABLE whether
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
        addr = sextract64(bvr, 0, 49) & ~3ULL;
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
        /* We must generate no events for Linked context matches (unless
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

    /* Completely clear out existing QEMU breakpoints and our array, to
     * avoid possible stale entries following migration load.
     */
    cpu_breakpoint_remove_all(CPU(cpu), BP_CPU);
    memset(env->cpu_breakpoint, 0, sizeof(env->cpu_breakpoint));

    for (i = 0; i < ARRAY_SIZE(cpu->env.cpu_breakpoint); i++) {
        hw_breakpoint_update(cpu, i);
    }
}

static void dbgbvr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    int i = ri->crm;

    raw_write(env, ri, value);
    hw_breakpoint_update(cpu, i);
}

static void dbgbcr_write(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    int i = ri->crm;

    /* BAS[3] is a read-only copy of BAS[2], and BAS[1] a read-only
     * copy of BAS[0].
     */
    value = deposit64(value, 6, 1, extract64(value, 5, 1));
    value = deposit64(value, 8, 1, extract64(value, 7, 1));

    raw_write(env, ri, value);
    hw_breakpoint_update(cpu, i);
}

static void define_debug_regs(ARMCPU *cpu)
{
    /* Define v7 and v8 architectural debug registers.
     * These are just dummy implementations for now.
     */
    int i;
    int wrps, brps, ctx_cmps;
    ARMCPRegInfo dbgdidr = {
        .name = "DBGDIDR", .cp = 14, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 0,
        .access = PL0_R, .accessfn = access_tda,
        .type = ARM_CP_CONST, .resetvalue = cpu->isar.dbgdidr,
    };

    /* Note that all these register fields hold "number of Xs minus 1". */
    brps = arm_num_brps(cpu);
    wrps = arm_num_wrps(cpu);
    ctx_cmps = arm_num_ctx_cmps(cpu);

    assert(ctx_cmps <= brps);

    define_one_arm_cp_reg(cpu, &dbgdidr);
    define_arm_cp_regs(cpu, debug_cp_reginfo);

    if (arm_feature(&cpu->env, ARM_FEATURE_LPAE)) {
        define_arm_cp_regs(cpu, debug_lpae_cp_reginfo);
    }

    for (i = 0; i < brps; i++) {
        ARMCPRegInfo dbgregs[] = {
            { .name = "DBGBVR", .state = ARM_CP_STATE_BOTH,
              .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 0, .crm = i, .opc2 = 4,
              .access = PL1_RW, .accessfn = access_tda,
              .fieldoffset = offsetof(CPUARMState, cp15.dbgbvr[i]),
              .writefn = dbgbvr_write, .raw_writefn = raw_write
            },
            { .name = "DBGBCR", .state = ARM_CP_STATE_BOTH,
              .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 0, .crm = i, .opc2 = 5,
              .access = PL1_RW, .accessfn = access_tda,
              .fieldoffset = offsetof(CPUARMState, cp15.dbgbcr[i]),
              .writefn = dbgbcr_write, .raw_writefn = raw_write
            },
            REGINFO_SENTINEL
        };
        define_arm_cp_regs(cpu, dbgregs);
    }

    for (i = 0; i < wrps; i++) {
        ARMCPRegInfo dbgregs[] = {
            { .name = "DBGWVR", .state = ARM_CP_STATE_BOTH,
              .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 0, .crm = i, .opc2 = 6,
              .access = PL1_RW, .accessfn = access_tda,
              .fieldoffset = offsetof(CPUARMState, cp15.dbgwvr[i]),
              .writefn = dbgwvr_write, .raw_writefn = raw_write
            },
            { .name = "DBGWCR", .state = ARM_CP_STATE_BOTH,
              .cp = 14, .opc0 = 2, .opc1 = 0, .crn = 0, .crm = i, .opc2 = 7,
              .access = PL1_RW, .accessfn = access_tda,
              .fieldoffset = offsetof(CPUARMState, cp15.dbgwcr[i]),
              .writefn = dbgwcr_write, .raw_writefn = raw_write
            },
            REGINFO_SENTINEL
        };
        define_arm_cp_regs(cpu, dbgregs);
    }
}

static void define_pmu_regs(ARMCPU *cpu)
{
    /*
     * v7 performance monitor control register: same implementor
     * field as main ID register, and we implement four counters in
     * addition to the cycle count register.
     */
    unsigned int i, pmcrn = 4;
    ARMCPRegInfo pmcr = {
        .name = "PMCR", .cp = 15, .crn = 9, .crm = 12, .opc1 = 0, .opc2 = 0,
        .access = PL0_RW,
        .type = ARM_CP_IO | ARM_CP_ALIAS,
        .fieldoffset = offsetoflow32(CPUARMState, cp15.c9_pmcr),
        .accessfn = pmreg_access, .writefn = pmcr_write,
        .raw_writefn = raw_write,
    };
    ARMCPRegInfo pmcr64 = {
        .name = "PMCR_EL0", .state = ARM_CP_STATE_AA64,
        .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 0,
        .access = PL0_RW, .accessfn = pmreg_access,
        .type = ARM_CP_IO,
        .fieldoffset = offsetof(CPUARMState, cp15.c9_pmcr),
        .resetvalue = (cpu->midr & 0xff000000) | (pmcrn << PMCRN_SHIFT) |
                      PMCRLC,
        .writefn = pmcr_write, .raw_writefn = raw_write,
    };
    define_one_arm_cp_reg(cpu, &pmcr);
    define_one_arm_cp_reg(cpu, &pmcr64);
    for (i = 0; i < pmcrn; i++) {
        char *pmevcntr_name = g_strdup_printf("PMEVCNTR%d", i);
        char *pmevcntr_el0_name = g_strdup_printf("PMEVCNTR%d_EL0", i);
        char *pmevtyper_name = g_strdup_printf("PMEVTYPER%d", i);
        char *pmevtyper_el0_name = g_strdup_printf("PMEVTYPER%d_EL0", i);
        ARMCPRegInfo pmev_regs[] = {
            { .name = pmevcntr_name, .cp = 15, .crn = 14,
              .crm = 8 | (3 & (i >> 3)), .opc1 = 0, .opc2 = i & 7,
              .access = PL0_RW, .type = ARM_CP_IO | ARM_CP_ALIAS,
              .readfn = pmevcntr_readfn, .writefn = pmevcntr_writefn,
              .accessfn = pmreg_access },
            { .name = pmevcntr_el0_name, .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 8 | (3 & (i >> 3)),
              .opc2 = i & 7, .access = PL0_RW, .accessfn = pmreg_access,
              .type = ARM_CP_IO,
              .readfn = pmevcntr_readfn, .writefn = pmevcntr_writefn,
              .raw_readfn = pmevcntr_rawread,
              .raw_writefn = pmevcntr_rawwrite },
            { .name = pmevtyper_name, .cp = 15, .crn = 14,
              .crm = 12 | (3 & (i >> 3)), .opc1 = 0, .opc2 = i & 7,
              .access = PL0_RW, .type = ARM_CP_IO | ARM_CP_ALIAS,
              .readfn = pmevtyper_readfn, .writefn = pmevtyper_writefn,
              .accessfn = pmreg_access },
            { .name = pmevtyper_el0_name, .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 3, .crn = 14, .crm = 12 | (3 & (i >> 3)),
              .opc2 = i & 7, .access = PL0_RW, .accessfn = pmreg_access,
              .type = ARM_CP_IO,
              .readfn = pmevtyper_readfn, .writefn = pmevtyper_writefn,
              .raw_writefn = pmevtyper_rawwrite },
            REGINFO_SENTINEL
        };
        define_arm_cp_regs(cpu, pmev_regs);
        g_free(pmevcntr_name);
        g_free(pmevcntr_el0_name);
        g_free(pmevtyper_name);
        g_free(pmevtyper_el0_name);
    }
    if (cpu_isar_feature(aa32_pmu_8_1, cpu)) {
        ARMCPRegInfo v81_pmu_regs[] = {
            { .name = "PMCEID2", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 0, .crn = 9, .crm = 14, .opc2 = 4,
              .access = PL0_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
              .resetvalue = extract64(cpu->pmceid0, 32, 32) },
            { .name = "PMCEID3", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 0, .crn = 9, .crm = 14, .opc2 = 5,
              .access = PL0_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
              .resetvalue = extract64(cpu->pmceid1, 32, 32) },
            REGINFO_SENTINEL
        };
        define_arm_cp_regs(cpu, v81_pmu_regs);
    }
    if (cpu_isar_feature(any_pmu_8_4, cpu)) {
        static const ARMCPRegInfo v84_pmmir = {
            .name = "PMMIR_EL1", .state = ARM_CP_STATE_BOTH,
            .opc0 = 3, .opc1 = 0, .crn = 9, .crm = 14, .opc2 = 6,
            .access = PL1_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
            .resetvalue = 0
        };
        define_one_arm_cp_reg(cpu, &v84_pmmir);
    }
}

/* We don't know until after realize whether there's a GICv3
 * attached, and that is what registers the gicv3 sysregs.
 * So we have to fill in the GIC fields in ID_PFR/ID_PFR1_EL1/ID_AA64PFR0_EL1
 * at runtime.
 */
static uint64_t id_pfr1_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);
    uint64_t pfr1 = cpu->id_pfr1;

    if (env->gicv3state) {
        pfr1 |= 1 << 28;
    }
    return pfr1;
}

#ifndef CONFIG_USER_ONLY
static uint64_t id_aa64pfr0_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    ARMCPU *cpu = env_archcpu(env);
    uint64_t pfr0 = cpu->isar.id_aa64pfr0;

    if (env->gicv3state) {
        pfr0 |= 1 << 24;
    }
    return pfr0;
}
#endif

/* Shared logic between LORID and the rest of the LOR* registers.
 * Secure state has already been delt with.
 */
static CPAccessResult access_lor_ns(CPUARMState *env)
{
    int el = arm_current_el(env);

    if (el < 2 && (arm_hcr_el2_eff(env) & HCR_TLOR)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 && (env->cp15.scr_el3 & SCR_TLOR)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static CPAccessResult access_lorid(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    if (arm_is_secure_below_el3(env)) {
        /* Access ok in secure mode.  */
        return CP_ACCESS_OK;
    }
    return access_lor_ns(env);
}

static CPAccessResult access_lor_other(CPUARMState *env,
                                       const ARMCPRegInfo *ri, bool isread)
{
    if (arm_is_secure_below_el3(env)) {
        /* Access denied in secure mode.  */
        return CP_ACCESS_TRAP;
    }
    return access_lor_ns(env);
}

/*
 * A trivial implementation of ARMv8.1-LOR leaves all of these
 * registers fixed at 0, which indicates that there are zero
 * supported Limited Ordering regions.
 */
static const ARMCPRegInfo lor_reginfo[] = {
    { .name = "LORSA_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 4, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_lor_other,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "LOREA_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 4, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_lor_other,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "LORN_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 4, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_lor_other,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "LORC_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 4, .opc2 = 3,
      .access = PL1_RW, .accessfn = access_lor_other,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "LORID_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 10, .crm = 4, .opc2 = 7,
      .access = PL1_R, .accessfn = access_lorid,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    REGINFO_SENTINEL
};

#ifdef TARGET_AARCH64
static CPAccessResult access_pauth(CPUARMState *env, const ARMCPRegInfo *ri,
                                   bool isread)
{
    int el = arm_current_el(env);

    if (el < 2 &&
        arm_feature(env, ARM_FEATURE_EL2) &&
        !(arm_hcr_el2_eff(env) & HCR_APK)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 &&
        arm_feature(env, ARM_FEATURE_EL3) &&
        !(env->cp15.scr_el3 & SCR_APK)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo pauth_reginfo[] = {
    { .name = "APDAKEYLO_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 2, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_pauth,
      .fieldoffset = offsetof(CPUARMState, keys.apda.lo) },
    { .name = "APDAKEYHI_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 2, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_pauth,
      .fieldoffset = offsetof(CPUARMState, keys.apda.hi) },
    { .name = "APDBKEYLO_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 2, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_pauth,
      .fieldoffset = offsetof(CPUARMState, keys.apdb.lo) },
    { .name = "APDBKEYHI_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 2, .opc2 = 3,
      .access = PL1_RW, .accessfn = access_pauth,
      .fieldoffset = offsetof(CPUARMState, keys.apdb.hi) },
    { .name = "APGAKEYLO_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 3, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_pauth,
      .fieldoffset = offsetof(CPUARMState, keys.apga.lo) },
    { .name = "APGAKEYHI_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 3, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_pauth,
      .fieldoffset = offsetof(CPUARMState, keys.apga.hi) },
    { .name = "APIAKEYLO_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 1, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_pauth,
      .fieldoffset = offsetof(CPUARMState, keys.apia.lo) },
    { .name = "APIAKEYHI_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 1, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_pauth,
      .fieldoffset = offsetof(CPUARMState, keys.apia.hi) },
    { .name = "APIBKEYLO_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 1, .opc2 = 2,
      .access = PL1_RW, .accessfn = access_pauth,
      .fieldoffset = offsetof(CPUARMState, keys.apib.lo) },
    { .name = "APIBKEYHI_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 2, .crm = 1, .opc2 = 3,
      .access = PL1_RW, .accessfn = access_pauth,
      .fieldoffset = offsetof(CPUARMState, keys.apib.hi) },
    REGINFO_SENTINEL
};

static uint64_t rndr_readfn(CPUARMState *env, const ARMCPRegInfo *ri)
{
    Error *err = NULL;
    uint64_t ret;

    /* Success sets NZCV = 0000.  */
    env->NF = env->CF = env->VF = 0, env->ZF = 1;

    if (qemu_guest_getrandom(&ret, sizeof(ret), &err) < 0) {
        /*
         * ??? Failed, for unknown reasons in the crypto subsystem.
         * The best we can do is log the reason and return the
         * timed-out indication to the guest.  There is no reason
         * we know to expect this failure to be transitory, so the
         * guest may well hang retrying the operation.
         */
        qemu_log_mask(LOG_UNIMP, "%s: Crypto failure: %s",
                      ri->name, error_get_pretty(err));
        error_free(err);

        env->ZF = 0; /* NZCF = 0100 */
        return 0;
    }
    return ret;
}

/* We do not support re-seeding, so the two registers operate the same.  */
static const ARMCPRegInfo rndr_reginfo[] = {
    { .name = "RNDR", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_NO_RAW | ARM_CP_SUPPRESS_TB_END | ARM_CP_IO,
      .opc0 = 3, .opc1 = 3, .crn = 2, .crm = 4, .opc2 = 0,
      .access = PL0_R, .readfn = rndr_readfn },
    { .name = "RNDRRS", .state = ARM_CP_STATE_AA64,
      .type = ARM_CP_NO_RAW | ARM_CP_SUPPRESS_TB_END | ARM_CP_IO,
      .opc0 = 3, .opc1 = 3, .crn = 2, .crm = 4, .opc2 = 1,
      .access = PL0_R, .readfn = rndr_readfn },
    REGINFO_SENTINEL
};

#ifndef CONFIG_USER_ONLY
static void dccvap_writefn(CPUARMState *env, const ARMCPRegInfo *opaque,
                          uint64_t value)
{
    ARMCPU *cpu = env_archcpu(env);
    /* CTR_EL0 System register -> DminLine, bits [19:16] */
    uint64_t dline_size = 4 << ((cpu->ctr >> 16) & 0xF);
    uint64_t vaddr_in = (uint64_t) value;
    uint64_t vaddr = vaddr_in & ~(dline_size - 1);
    void *haddr;
    int mem_idx = cpu_mmu_index(env, false);

    /* This won't be crossing page boundaries */
    haddr = probe_read(env, vaddr, dline_size, mem_idx, GETPC());
    if (haddr) {

        ram_addr_t offset;
        MemoryRegion *mr;

        /* RCU lock is already being held */
        mr = memory_region_from_host(haddr, &offset);

        if (mr) {
            memory_region_writeback(mr, offset, dline_size);
        }
    }
}

static const ARMCPRegInfo dcpop_reg[] = {
    { .name = "DC_CVAP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 12, .opc2 = 1,
      .access = PL0_W, .type = ARM_CP_NO_RAW | ARM_CP_SUPPRESS_TB_END,
      .accessfn = aa64_cacheop_poc_access, .writefn = dccvap_writefn },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo dcpodp_reg[] = {
    { .name = "DC_CVADP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 13, .opc2 = 1,
      .access = PL0_W, .type = ARM_CP_NO_RAW | ARM_CP_SUPPRESS_TB_END,
      .accessfn = aa64_cacheop_poc_access, .writefn = dccvap_writefn },
    REGINFO_SENTINEL
};
#endif /*CONFIG_USER_ONLY*/

static CPAccessResult access_aa64_tid5(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    if ((arm_current_el(env) < 2) && (arm_hcr_el2_eff(env) & HCR_TID5)) {
        return CP_ACCESS_TRAP_EL2;
    }

    return CP_ACCESS_OK;
}

static CPAccessResult access_mte(CPUARMState *env, const ARMCPRegInfo *ri,
                                 bool isread)
{
    int el = arm_current_el(env);

    if (el < 2 &&
        arm_feature(env, ARM_FEATURE_EL2) &&
        !(arm_hcr_el2_eff(env) & HCR_ATA)) {
        return CP_ACCESS_TRAP_EL2;
    }
    if (el < 3 &&
        arm_feature(env, ARM_FEATURE_EL3) &&
        !(env->cp15.scr_el3 & SCR_ATA)) {
        return CP_ACCESS_TRAP_EL3;
    }
    return CP_ACCESS_OK;
}

static uint64_t tco_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    return env->pstate & PSTATE_TCO;
}

static void tco_write(CPUARMState *env, const ARMCPRegInfo *ri, uint64_t val)
{
    env->pstate = (env->pstate & ~PSTATE_TCO) | (val & PSTATE_TCO);
}

static const ARMCPRegInfo mte_reginfo[] = {
    { .name = "TFSRE0_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 5, .crm = 6, .opc2 = 1,
      .access = PL1_RW, .accessfn = access_mte,
      .fieldoffset = offsetof(CPUARMState, cp15.tfsr_el[0]) },
    { .name = "TFSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 5, .crm = 6, .opc2 = 0,
      .access = PL1_RW, .accessfn = access_mte,
      .fieldoffset = offsetof(CPUARMState, cp15.tfsr_el[1]) },
    { .name = "TFSR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 5, .crm = 6, .opc2 = 0,
      .access = PL2_RW, .accessfn = access_mte,
      .fieldoffset = offsetof(CPUARMState, cp15.tfsr_el[2]) },
    { .name = "TFSR_EL3", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 6, .crn = 5, .crm = 6, .opc2 = 0,
      .access = PL3_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.tfsr_el[3]) },
    { .name = "RGSR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 5,
      .access = PL1_RW, .accessfn = access_mte,
      .fieldoffset = offsetof(CPUARMState, cp15.rgsr_el1) },
    { .name = "GCR_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 6,
      .access = PL1_RW, .accessfn = access_mte,
      .fieldoffset = offsetof(CPUARMState, cp15.gcr_el1) },
    { .name = "GMID_EL1", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 1, .crn = 0, .crm = 0, .opc2 = 4,
      .access = PL1_R, .accessfn = access_aa64_tid5,
      .type = ARM_CP_CONST, .resetvalue = GMID_EL1_BS },
    { .name = "TCO", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 4, .crm = 2, .opc2 = 7,
      .type = ARM_CP_NO_RAW,
      .access = PL0_RW, .readfn = tco_read, .writefn = tco_write },
    { .name = "DC_IGVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 3,
      .type = ARM_CP_NOP, .access = PL1_W,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_IGSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 4,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DC_IGDVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL1_W,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_IGDSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 6, .opc2 = 6,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DC_CGSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 10, .opc2 = 4,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DC_CGDSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 10, .opc2 = 6,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DC_CIGSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 14, .opc2 = 4,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    { .name = "DC_CIGDSW", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 14, .opc2 = 6,
      .type = ARM_CP_NOP, .access = PL1_W, .accessfn = access_tsw },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo mte_tco_ro_reginfo[] = {
    { .name = "TCO", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 3, .crn = 4, .crm = 2, .opc2 = 7,
      .type = ARM_CP_CONST, .access = PL0_RW, },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo mte_el0_cacheop_reginfo[] = {
    { .name = "DC_CGVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 10, .opc2 = 3,
      .type = ARM_CP_NOP, .access = PL0_W,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CGDVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 10, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL0_W,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CGVAP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 12, .opc2 = 3,
      .type = ARM_CP_NOP, .access = PL0_W,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CGDVAP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 12, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL0_W,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CGVADP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 13, .opc2 = 3,
      .type = ARM_CP_NOP, .access = PL0_W,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CGDVADP", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 13, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL0_W,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CIGVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 14, .opc2 = 3,
      .type = ARM_CP_NOP, .access = PL0_W,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_CIGDVAC", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 14, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL0_W,
      .accessfn = aa64_cacheop_poc_access },
    { .name = "DC_GVA", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 4, .opc2 = 3,
      .access = PL0_W, .type = ARM_CP_DC_GVA,
#ifndef CONFIG_USER_ONLY
      /* Avoid overhead of an access check that always passes in user-mode */
      .accessfn = aa64_zva_access,
#endif
    },
    { .name = "DC_GZVA", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 4, .opc2 = 4,
      .access = PL0_W, .type = ARM_CP_DC_GZVA,
#ifndef CONFIG_USER_ONLY
      /* Avoid overhead of an access check that always passes in user-mode */
      .accessfn = aa64_zva_access,
#endif
    },
    REGINFO_SENTINEL
};

#endif

static CPAccessResult access_predinv(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    int el = arm_current_el(env);

    if (el == 0) {
        uint64_t sctlr = arm_sctlr(env, el);
        if (!(sctlr & SCTLR_EnRCTX)) {
            return CP_ACCESS_TRAP;
        }
    } else if (el == 1) {
        uint64_t hcr = arm_hcr_el2_eff(env);
        if (hcr & HCR_NV) {
            return CP_ACCESS_TRAP_EL2;
        }
    }
    return CP_ACCESS_OK;
}

static const ARMCPRegInfo predinv_reginfo[] = {
    { .name = "CFP_RCTX", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 3, .opc2 = 4,
      .type = ARM_CP_NOP, .access = PL0_W, .accessfn = access_predinv },
    { .name = "DVP_RCTX", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 3, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL0_W, .accessfn = access_predinv },
    { .name = "CPP_RCTX", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 3, .crn = 7, .crm = 3, .opc2 = 7,
      .type = ARM_CP_NOP, .access = PL0_W, .accessfn = access_predinv },
    /*
     * Note the AArch32 opcodes have a different OPC1.
     */
    { .name = "CFPRCTX", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 7, .crm = 3, .opc2 = 4,
      .type = ARM_CP_NOP, .access = PL0_W, .accessfn = access_predinv },
    { .name = "DVPRCTX", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 7, .crm = 3, .opc2 = 5,
      .type = ARM_CP_NOP, .access = PL0_W, .accessfn = access_predinv },
    { .name = "CPPRCTX", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 7, .crm = 3, .opc2 = 7,
      .type = ARM_CP_NOP, .access = PL0_W, .accessfn = access_predinv },
    REGINFO_SENTINEL
};

static uint64_t ccsidr2_read(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* Read the high 32 bits of the current CCSIDR */
    return extract64(ccsidr_read(env, ri), 32, 32);
}

static const ARMCPRegInfo ccsidr2_reginfo[] = {
    { .name = "CCSIDR2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 1, .crn = 0, .crm = 0, .opc2 = 2,
      .access = PL1_R,
      .accessfn = access_aa64_tid2,
      .readfn = ccsidr2_read, .type = ARM_CP_NO_RAW },
    REGINFO_SENTINEL
};

static CPAccessResult access_aa64_tid3(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    if ((arm_current_el(env) < 2) && (arm_hcr_el2_eff(env) & HCR_TID3)) {
        return CP_ACCESS_TRAP_EL2;
    }

    return CP_ACCESS_OK;
}

static CPAccessResult access_aa32_tid3(CPUARMState *env, const ARMCPRegInfo *ri,
                                       bool isread)
{
    if (arm_feature(env, ARM_FEATURE_V8)) {
        return access_aa64_tid3(env, ri, isread);
    }

    return CP_ACCESS_OK;
}

static CPAccessResult access_jazelle(CPUARMState *env, const ARMCPRegInfo *ri,
                                     bool isread)
{
    if (arm_current_el(env) == 1 && (arm_hcr_el2_eff(env) & HCR_TID0)) {
        return CP_ACCESS_TRAP_EL2;
    }

    return CP_ACCESS_OK;
}

static const ARMCPRegInfo jazelle_regs[] = {
    { .name = "JIDR",
      .cp = 14, .crn = 0, .crm = 0, .opc1 = 7, .opc2 = 0,
      .access = PL1_R, .accessfn = access_jazelle,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "JOSCR",
      .cp = 14, .crn = 1, .crm = 0, .opc1 = 7, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "JMCR",
      .cp = 14, .crn = 2, .crm = 0, .opc1 = 7, .opc2 = 0,
      .access = PL1_RW, .type = ARM_CP_CONST, .resetvalue = 0 },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo vhe_reginfo[] = {
    { .name = "CONTEXTIDR_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 13, .crm = 0, .opc2 = 1,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.contextidr_el[2]) },
    { .name = "TTBR1_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 2, .crm = 0, .opc2 = 1,
      .access = PL2_RW, .writefn = vmsa_tcr_ttbr_el2_write,
      .fieldoffset = offsetof(CPUARMState, cp15.ttbr1_el[2]) },
#ifndef CONFIG_USER_ONLY
    { .name = "CNTHV_CVAL_EL2", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 3, .opc2 = 2,
      .fieldoffset =
        offsetof(CPUARMState, cp15.c14_timer[GTIMER_HYPVIRT].cval),
      .type = ARM_CP_IO, .access = PL2_RW,
      .writefn = gt_hv_cval_write, .raw_writefn = raw_write },
    { .name = "CNTHV_TVAL_EL2", .state = ARM_CP_STATE_BOTH,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 3, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO, .access = PL2_RW,
      .resetfn = gt_hv_timer_reset,
      .readfn = gt_hv_tval_read, .writefn = gt_hv_tval_write },
    { .name = "CNTHV_CTL_EL2", .state = ARM_CP_STATE_BOTH,
      .type = ARM_CP_IO,
      .opc0 = 3, .opc1 = 4, .crn = 14, .crm = 3, .opc2 = 1,
      .access = PL2_RW,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_HYPVIRT].ctl),
      .writefn = gt_hv_ctl_write, .raw_writefn = raw_write },
    { .name = "CNTP_CTL_EL02", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 14, .crm = 2, .opc2 = 1,
      .type = ARM_CP_IO | ARM_CP_ALIAS,
      .access = PL2_RW, .accessfn = e2h_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_PHYS].ctl),
      .writefn = gt_phys_ctl_write, .raw_writefn = raw_write },
    { .name = "CNTV_CTL_EL02", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 14, .crm = 3, .opc2 = 1,
      .type = ARM_CP_IO | ARM_CP_ALIAS,
      .access = PL2_RW, .accessfn = e2h_access,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_VIRT].ctl),
      .writefn = gt_virt_ctl_write, .raw_writefn = raw_write },
    { .name = "CNTP_TVAL_EL02", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 14, .crm = 2, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO | ARM_CP_ALIAS,
      .access = PL2_RW, .accessfn = e2h_access,
      .readfn = gt_phys_tval_read, .writefn = gt_phys_tval_write },
    { .name = "CNTV_TVAL_EL02", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 14, .crm = 3, .opc2 = 0,
      .type = ARM_CP_NO_RAW | ARM_CP_IO | ARM_CP_ALIAS,
      .access = PL2_RW, .accessfn = e2h_access,
      .readfn = gt_virt_tval_read, .writefn = gt_virt_tval_write },
    { .name = "CNTP_CVAL_EL02", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 14, .crm = 2, .opc2 = 2,
      .type = ARM_CP_IO | ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_PHYS].cval),
      .access = PL2_RW, .accessfn = e2h_access,
      .writefn = gt_phys_cval_write, .raw_writefn = raw_write },
    { .name = "CNTV_CVAL_EL02", .state = ARM_CP_STATE_AA64,
      .opc0 = 3, .opc1 = 5, .crn = 14, .crm = 3, .opc2 = 2,
      .type = ARM_CP_IO | ARM_CP_ALIAS,
      .fieldoffset = offsetof(CPUARMState, cp15.c14_timer[GTIMER_VIRT].cval),
      .access = PL2_RW, .accessfn = e2h_access,
      .writefn = gt_virt_cval_write, .raw_writefn = raw_write },
#endif
    REGINFO_SENTINEL
};

#ifndef CONFIG_USER_ONLY
static const ARMCPRegInfo ats1e1_reginfo[] = {
    { .name = "AT_S1E1R", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    { .name = "AT_S1E1W", .state = ARM_CP_STATE_AA64,
      .opc0 = 1, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write64 },
    REGINFO_SENTINEL
};

static const ARMCPRegInfo ats1cp_reginfo[] = {
    { .name = "ATS1CPRP",
      .cp = 15, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 0,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write },
    { .name = "ATS1CPWP",
      .cp = 15, .opc1 = 0, .crn = 7, .crm = 9, .opc2 = 1,
      .access = PL1_W, .type = ARM_CP_NO_RAW | ARM_CP_RAISES_EXC,
      .writefn = ats_write },
    REGINFO_SENTINEL
};
#endif

/*
 * ACTLR2 and HACTLR2 map to ACTLR_EL1[63:32] and
 * ACTLR_EL2[63:32]. They exist only if the ID_MMFR4.AC2 field
 * is non-zero, which is never for ARMv7, optionally in ARMv8
 * and mandatorily for ARMv8.2 and up.
 * ACTLR2 is banked for S and NS if EL3 is AArch32. Since QEMU's
 * implementation is RAZ/WI we can ignore this detail, as we
 * do for ACTLR.
 */
static const ARMCPRegInfo actlr2_hactlr2_reginfo[] = {
    { .name = "ACTLR2", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 3,
      .access = PL1_RW, .accessfn = access_tacr,
      .type = ARM_CP_CONST, .resetvalue = 0 },
    { .name = "HACTLR2", .state = ARM_CP_STATE_AA32,
      .cp = 15, .opc1 = 4, .crn = 1, .crm = 0, .opc2 = 3,
      .access = PL2_RW, .type = ARM_CP_CONST,
      .resetvalue = 0 },
    REGINFO_SENTINEL
};

void register_cp_regs_for_features(ARMCPU *cpu)
{
    /* Register all the coprocessor registers based on feature bits */
    CPUARMState *env = &cpu->env;
    if (arm_feature(env, ARM_FEATURE_M)) {
        /* M profile has no coprocessor registers */
        return;
    }

    define_arm_cp_regs(cpu, cp_reginfo);
    if (!arm_feature(env, ARM_FEATURE_V8)) {
        /* Must go early as it is full of wildcards that may be
         * overridden by later definitions.
         */
        define_arm_cp_regs(cpu, not_v8_cp_reginfo);
    }

    if (arm_feature(env, ARM_FEATURE_V6)) {
        /* The ID registers all have impdef reset values */
        ARMCPRegInfo v6_idregs[] = {
            { .name = "ID_PFR0", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->id_pfr0 },
            /* ID_PFR1 is not a plain ARM_CP_CONST because we don't know
             * the value of the GIC field until after we define these regs.
             */
            { .name = "ID_PFR1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_NO_RAW,
              .accessfn = access_aa32_tid3,
              .readfn = id_pfr1_read,
              .writefn = arm_cp_write_ignore },
            { .name = "ID_DFR0", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->isar.id_dfr0 },
            { .name = "ID_AFR0", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->id_afr0 },
            { .name = "ID_MMFR0", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->isar.id_mmfr0 },
            { .name = "ID_MMFR1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->isar.id_mmfr1 },
            { .name = "ID_MMFR2", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->isar.id_mmfr2 },
            { .name = "ID_MMFR3", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 1, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->isar.id_mmfr3 },
            { .name = "ID_ISAR0", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->isar.id_isar0 },
            { .name = "ID_ISAR1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->isar.id_isar1 },
            { .name = "ID_ISAR2", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->isar.id_isar2 },
            { .name = "ID_ISAR3", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->isar.id_isar3 },
            { .name = "ID_ISAR4", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->isar.id_isar4 },
            { .name = "ID_ISAR5", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->isar.id_isar5 },
            { .name = "ID_MMFR4", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->isar.id_mmfr4 },
            { .name = "ID_ISAR6", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 2, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa32_tid3,
              .resetvalue = cpu->isar.id_isar6 },
            REGINFO_SENTINEL
        };
        define_arm_cp_regs(cpu, v6_idregs);
        define_arm_cp_regs(cpu, v6_cp_reginfo);
    } else {
        define_arm_cp_regs(cpu, not_v6_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_V6K)) {
        define_arm_cp_regs(cpu, v6k_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_V7MP) &&
        !arm_feature(env, ARM_FEATURE_PMSA)) {
        define_arm_cp_regs(cpu, v7mp_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_V7VE)) {
        define_arm_cp_regs(cpu, pmovsset_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_V7)) {
        ARMCPRegInfo clidr = {
            .name = "CLIDR", .state = ARM_CP_STATE_BOTH,
            .opc0 = 3, .crn = 0, .crm = 0, .opc1 = 1, .opc2 = 1,
            .access = PL1_R, .type = ARM_CP_CONST,
            .accessfn = access_aa64_tid2,
            .resetvalue = cpu->clidr
        };
        define_one_arm_cp_reg(cpu, &clidr);
        define_arm_cp_regs(cpu, v7_cp_reginfo);
        define_debug_regs(cpu);
        define_pmu_regs(cpu);
    } else {
        define_arm_cp_regs(cpu, not_v7_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_V8)) {
        /* AArch64 ID registers, which all have impdef reset values.
         * Note that within the ID register ranges the unused slots
         * must all RAZ, not UNDEF; future architecture versions may
         * define new registers here.
         */
        ARMCPRegInfo v8_idregs[] = {
            /*
             * ID_AA64PFR0_EL1 is not a plain ARM_CP_CONST in system
             * emulation because we don't know the right value for the
             * GIC field until after we define these regs.
             */
            { .name = "ID_AA64PFR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 0,
              .access = PL1_R,
#ifdef CONFIG_USER_ONLY
              .type = ARM_CP_CONST,
              .resetvalue = cpu->isar.id_aa64pfr0
#else
              .type = ARM_CP_NO_RAW,
              .accessfn = access_aa64_tid3,
              .readfn = id_aa64pfr0_read,
              .writefn = arm_cp_write_ignore
#endif
            },
            { .name = "ID_AA64PFR1_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.id_aa64pfr1},
            { .name = "ID_AA64PFR2_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64PFR3_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64ZFR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              /* At present, only SVEver == 0 is defined anyway.  */
              .resetvalue = 0 },
            { .name = "ID_AA64PFR5_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64PFR6_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64PFR7_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 4, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64DFR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.id_aa64dfr0 },
            { .name = "ID_AA64DFR1_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.id_aa64dfr1 },
            { .name = "ID_AA64DFR2_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64DFR3_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64AFR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->id_aa64afr0 },
            { .name = "ID_AA64AFR1_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->id_aa64afr1 },
            { .name = "ID_AA64AFR2_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64AFR3_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 5, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64ISAR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.id_aa64isar0 },
            { .name = "ID_AA64ISAR1_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.id_aa64isar1 },
            { .name = "ID_AA64ISAR2_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64ISAR3_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64ISAR4_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64ISAR5_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64ISAR6_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64ISAR7_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 6, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64MMFR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.id_aa64mmfr0 },
            { .name = "ID_AA64MMFR1_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.id_aa64mmfr1 },
            { .name = "ID_AA64MMFR2_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.id_aa64mmfr2 },
            { .name = "ID_AA64MMFR3_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64MMFR4_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64MMFR5_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64MMFR6_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "ID_AA64MMFR7_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 7, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "MVFR0_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.mvfr0 },
            { .name = "MVFR1_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 1,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.mvfr1 },
            { .name = "MVFR2_EL1", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 2,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = cpu->isar.mvfr2 },
            { .name = "MVFR3_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 3,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "MVFR4_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "MVFR5_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 5,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "MVFR6_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 6,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "MVFR7_EL1_RESERVED", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 3, .opc2 = 7,
              .access = PL1_R, .type = ARM_CP_CONST,
              .accessfn = access_aa64_tid3,
              .resetvalue = 0 },
            { .name = "PMCEID0", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 0, .crn = 9, .crm = 12, .opc2 = 6,
              .access = PL0_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
              .resetvalue = extract64(cpu->pmceid0, 0, 32) },
            { .name = "PMCEID0_EL0", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 6,
              .access = PL0_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
              .resetvalue = cpu->pmceid0 },
            { .name = "PMCEID1", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 0, .crn = 9, .crm = 12, .opc2 = 7,
              .access = PL0_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
              .resetvalue = extract64(cpu->pmceid1, 0, 32) },
            { .name = "PMCEID1_EL0", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 3, .crn = 9, .crm = 12, .opc2 = 7,
              .access = PL0_R, .accessfn = pmreg_access, .type = ARM_CP_CONST,
              .resetvalue = cpu->pmceid1 },
            REGINFO_SENTINEL
        };
#ifdef CONFIG_USER_ONLY
        ARMCPRegUserSpaceInfo v8_user_idregs[] = {
            { .name = "ID_AA64PFR0_EL1",
              .exported_bits = 0x000f000f00ff0000,
              .fixed_bits    = 0x0000000000000011 },
            { .name = "ID_AA64PFR1_EL1",
              .exported_bits = 0x00000000000000f0 },
            { .name = "ID_AA64PFR*_EL1_RESERVED",
              .is_glob = true                     },
            { .name = "ID_AA64ZFR0_EL1"           },
            { .name = "ID_AA64MMFR0_EL1",
              .fixed_bits    = 0x00000000ff000000 },
            { .name = "ID_AA64MMFR1_EL1"          },
            { .name = "ID_AA64MMFR*_EL1_RESERVED",
              .is_glob = true                     },
            { .name = "ID_AA64DFR0_EL1",
              .fixed_bits    = 0x0000000000000006 },
            { .name = "ID_AA64DFR1_EL1"           },
            { .name = "ID_AA64DFR*_EL1_RESERVED",
              .is_glob = true                     },
            { .name = "ID_AA64AFR*",
              .is_glob = true                     },
            { .name = "ID_AA64ISAR0_EL1",
              .exported_bits = 0x00fffffff0fffff0 },
            { .name = "ID_AA64ISAR1_EL1",
              .exported_bits = 0x000000f0ffffffff },
            { .name = "ID_AA64ISAR*_EL1_RESERVED",
              .is_glob = true                     },
            REGUSERINFO_SENTINEL
        };
        modify_arm_cp_regs(v8_idregs, v8_user_idregs);
#endif
        /* RVBAR_EL1 is only implemented if EL1 is the highest EL */
        if (!arm_feature(env, ARM_FEATURE_EL3) &&
            !arm_feature(env, ARM_FEATURE_EL2)) {
            ARMCPRegInfo rvbar = {
                .name = "RVBAR_EL1", .state = ARM_CP_STATE_AA64,
                .opc0 = 3, .opc1 = 0, .crn = 12, .crm = 0, .opc2 = 1,
                .type = ARM_CP_CONST, .access = PL1_R, .resetvalue = cpu->rvbar
            };
            define_one_arm_cp_reg(cpu, &rvbar);
        }
        define_arm_cp_regs(cpu, v8_idregs);
        define_arm_cp_regs(cpu, v8_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_EL2)) {
        uint64_t vmpidr_def = mpidr_read_val(env);
        ARMCPRegInfo vpidr_regs[] = {
            { .name = "VPIDR", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 4, .crn = 0, .crm = 0, .opc2 = 0,
              .access = PL2_RW, .accessfn = access_el3_aa32ns,
              .resetvalue = cpu->midr, .type = ARM_CP_ALIAS,
              .fieldoffset = offsetoflow32(CPUARMState, cp15.vpidr_el2) },
            { .name = "VPIDR_EL2", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 4, .crn = 0, .crm = 0, .opc2 = 0,
              .access = PL2_RW, .resetvalue = cpu->midr,
              .fieldoffset = offsetof(CPUARMState, cp15.vpidr_el2) },
            { .name = "VMPIDR", .state = ARM_CP_STATE_AA32,
              .cp = 15, .opc1 = 4, .crn = 0, .crm = 0, .opc2 = 5,
              .access = PL2_RW, .accessfn = access_el3_aa32ns,
              .resetvalue = vmpidr_def, .type = ARM_CP_ALIAS,
              .fieldoffset = offsetoflow32(CPUARMState, cp15.vmpidr_el2) },
            { .name = "VMPIDR_EL2", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 4, .crn = 0, .crm = 0, .opc2 = 5,
              .access = PL2_RW,
              .resetvalue = vmpidr_def,
              .fieldoffset = offsetof(CPUARMState, cp15.vmpidr_el2) },
            REGINFO_SENTINEL
        };
        define_arm_cp_regs(cpu, vpidr_regs);
        define_arm_cp_regs(cpu, el2_cp_reginfo);
        if (arm_feature(env, ARM_FEATURE_V8)) {
            define_arm_cp_regs(cpu, el2_v8_cp_reginfo);
        }
        /* RVBAR_EL2 is only implemented if EL2 is the highest EL */
        if (!arm_feature(env, ARM_FEATURE_EL3)) {
            ARMCPRegInfo rvbar = {
                .name = "RVBAR_EL2", .state = ARM_CP_STATE_AA64,
                .opc0 = 3, .opc1 = 4, .crn = 12, .crm = 0, .opc2 = 1,
                .type = ARM_CP_CONST, .access = PL2_R, .resetvalue = cpu->rvbar
            };
            define_one_arm_cp_reg(cpu, &rvbar);
        }
    } else {
        /* If EL2 is missing but higher ELs are enabled, we need to
         * register the no_el2 reginfos.
         */
        if (arm_feature(env, ARM_FEATURE_EL3)) {
            /* When EL3 exists but not EL2, VPIDR and VMPIDR take the value
             * of MIDR_EL1 and MPIDR_EL1.
             */
            ARMCPRegInfo vpidr_regs[] = {
                { .name = "VPIDR_EL2", .state = ARM_CP_STATE_BOTH,
                  .opc0 = 3, .opc1 = 4, .crn = 0, .crm = 0, .opc2 = 0,
                  .access = PL2_RW, .accessfn = access_el3_aa32ns,
                  .type = ARM_CP_CONST, .resetvalue = cpu->midr,
                  .fieldoffset = offsetof(CPUARMState, cp15.vpidr_el2) },
                { .name = "VMPIDR_EL2", .state = ARM_CP_STATE_BOTH,
                  .opc0 = 3, .opc1 = 4, .crn = 0, .crm = 0, .opc2 = 5,
                  .access = PL2_RW, .accessfn = access_el3_aa32ns,
                  .type = ARM_CP_NO_RAW,
                  .writefn = arm_cp_write_ignore, .readfn = mpidr_read },
                REGINFO_SENTINEL
            };
            define_arm_cp_regs(cpu, vpidr_regs);
            define_arm_cp_regs(cpu, el3_no_el2_cp_reginfo);
            if (arm_feature(env, ARM_FEATURE_V8)) {
                define_arm_cp_regs(cpu, el3_no_el2_v8_cp_reginfo);
            }
        }
    }
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        define_arm_cp_regs(cpu, el3_cp_reginfo);
        ARMCPRegInfo el3_regs[] = {
            { .name = "RVBAR_EL3", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 6, .crn = 12, .crm = 0, .opc2 = 1,
              .type = ARM_CP_CONST, .access = PL3_R, .resetvalue = cpu->rvbar },
            { .name = "SCTLR_EL3", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 0, .opc2 = 0,
              .access = PL3_RW,
              .raw_writefn = raw_write, .writefn = sctlr_write,
              .fieldoffset = offsetof(CPUARMState, cp15.sctlr_el[3]),
              .resetvalue = cpu->reset_sctlr },
            REGINFO_SENTINEL
        };

        define_arm_cp_regs(cpu, el3_regs);
    }
    /* The behaviour of NSACR is sufficiently various that we don't
     * try to describe it in a single reginfo:
     *  if EL3 is 64 bit, then trap to EL3 from S EL1,
     *     reads as constant 0xc00 from NS EL1 and NS EL2
     *  if EL3 is 32 bit, then RW at EL3, RO at NS EL1 and NS EL2
     *  if v7 without EL3, register doesn't exist
     *  if v8 without EL3, reads as constant 0xc00 from NS EL1 and NS EL2
     */
    if (arm_feature(env, ARM_FEATURE_EL3)) {
        if (arm_feature(env, ARM_FEATURE_AARCH64)) {
            ARMCPRegInfo nsacr = {
                .name = "NSACR", .type = ARM_CP_CONST,
                .cp = 15, .opc1 = 0, .crn = 1, .crm = 1, .opc2 = 2,
                .access = PL1_RW, .accessfn = nsacr_access,
                .resetvalue = 0xc00
            };
            define_one_arm_cp_reg(cpu, &nsacr);
        } else {
            ARMCPRegInfo nsacr = {
                .name = "NSACR",
                .cp = 15, .opc1 = 0, .crn = 1, .crm = 1, .opc2 = 2,
                .access = PL3_RW | PL1_R,
                .resetvalue = 0,
                .fieldoffset = offsetof(CPUARMState, cp15.nsacr)
            };
            define_one_arm_cp_reg(cpu, &nsacr);
        }
    } else {
        if (arm_feature(env, ARM_FEATURE_V8)) {
            ARMCPRegInfo nsacr = {
                .name = "NSACR", .type = ARM_CP_CONST,
                .cp = 15, .opc1 = 0, .crn = 1, .crm = 1, .opc2 = 2,
                .access = PL1_R,
                .resetvalue = 0xc00
            };
            define_one_arm_cp_reg(cpu, &nsacr);
        }
    }

    if (arm_feature(env, ARM_FEATURE_PMSA)) {
        if (arm_feature(env, ARM_FEATURE_V6)) {
            /* PMSAv6 not implemented */
            assert(arm_feature(env, ARM_FEATURE_V7));
            define_arm_cp_regs(cpu, vmsa_pmsa_cp_reginfo);
            define_arm_cp_regs(cpu, pmsav7_cp_reginfo);
        } else {
            define_arm_cp_regs(cpu, pmsav5_cp_reginfo);
        }
    } else {
        define_arm_cp_regs(cpu, vmsa_pmsa_cp_reginfo);
        define_arm_cp_regs(cpu, vmsa_cp_reginfo);
        /* TTCBR2 is introduced with ARMv8.2-AA32HPD.  */
        if (cpu_isar_feature(aa32_hpd, cpu)) {
            define_one_arm_cp_reg(cpu, &ttbcr2_reginfo);
        }
    }
    if (arm_feature(env, ARM_FEATURE_THUMB2EE)) {
        define_arm_cp_regs(cpu, t2ee_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_GENERIC_TIMER)) {
        define_arm_cp_regs(cpu, generic_timer_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_VAPA)) {
        define_arm_cp_regs(cpu, vapa_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_CACHE_TEST_CLEAN)) {
        define_arm_cp_regs(cpu, cache_test_clean_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_CACHE_DIRTY_REG)) {
        define_arm_cp_regs(cpu, cache_dirty_status_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_CACHE_BLOCK_OPS)) {
        define_arm_cp_regs(cpu, cache_block_ops_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_OMAPCP)) {
        define_arm_cp_regs(cpu, omap_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_STRONGARM)) {
        define_arm_cp_regs(cpu, strongarm_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_XSCALE)) {
        define_arm_cp_regs(cpu, xscale_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_DUMMY_C15_REGS)) {
        define_arm_cp_regs(cpu, dummy_c15_cp_reginfo);
    }
    if (arm_feature(env, ARM_FEATURE_LPAE)) {
        define_arm_cp_regs(cpu, lpae_cp_reginfo);
    }
    if (cpu_isar_feature(aa32_jazelle, cpu)) {
        define_arm_cp_regs(cpu, jazelle_regs);
    }
    /* Slightly awkwardly, the OMAP and StrongARM cores need all of
     * cp15 crn=0 to be writes-ignored, whereas for other cores they should
     * be read-only (ie write causes UNDEF exception).
     */
    {
        ARMCPRegInfo id_pre_v8_midr_cp_reginfo[] = {
            /* Pre-v8 MIDR space.
             * Note that the MIDR isn't a simple constant register because
             * of the TI925 behaviour where writes to another register can
             * cause the MIDR value to change.
             *
             * Unimplemented registers in the c15 0 0 0 space default to
             * MIDR. Define MIDR first as this entire space, then CTR, TCMTR
             * and friends override accordingly.
             */
            { .name = "MIDR",
              .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = CP_ANY,
              .access = PL1_R, .resetvalue = cpu->midr,
              .writefn = arm_cp_write_ignore, .raw_writefn = raw_write,
              .readfn = midr_read,
              .fieldoffset = offsetof(CPUARMState, cp15.c0_cpuid),
              .type = ARM_CP_OVERRIDE },
            /* crn = 0 op1 = 0 crm = 3..7 : currently unassigned; we RAZ. */
            { .name = "DUMMY",
              .cp = 15, .crn = 0, .crm = 3, .opc1 = 0, .opc2 = CP_ANY,
              .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
            { .name = "DUMMY",
              .cp = 15, .crn = 0, .crm = 4, .opc1 = 0, .opc2 = CP_ANY,
              .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
            { .name = "DUMMY",
              .cp = 15, .crn = 0, .crm = 5, .opc1 = 0, .opc2 = CP_ANY,
              .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
            { .name = "DUMMY",
              .cp = 15, .crn = 0, .crm = 6, .opc1 = 0, .opc2 = CP_ANY,
              .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
            { .name = "DUMMY",
              .cp = 15, .crn = 0, .crm = 7, .opc1 = 0, .opc2 = CP_ANY,
              .access = PL1_R, .type = ARM_CP_CONST, .resetvalue = 0 },
            REGINFO_SENTINEL
        };
        ARMCPRegInfo id_v8_midr_cp_reginfo[] = {
            { .name = "MIDR_EL1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 0, .opc2 = 0,
              .access = PL1_R, .type = ARM_CP_NO_RAW, .resetvalue = cpu->midr,
              .fieldoffset = offsetof(CPUARMState, cp15.c0_cpuid),
              .readfn = midr_read },
            /* crn = 0 op1 = 0 crm = 0 op2 = 4,7 : AArch32 aliases of MIDR */
            { .name = "MIDR", .type = ARM_CP_ALIAS | ARM_CP_CONST,
              .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 4,
              .access = PL1_R, .resetvalue = cpu->midr },
            { .name = "MIDR", .type = ARM_CP_ALIAS | ARM_CP_CONST,
              .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 7,
              .access = PL1_R, .resetvalue = cpu->midr },
            { .name = "REVIDR_EL1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 0, .crm = 0, .opc2 = 6,
              .access = PL1_R,
              .accessfn = access_aa64_tid1,
              .type = ARM_CP_CONST, .resetvalue = cpu->revidr },
            REGINFO_SENTINEL
        };
        ARMCPRegInfo id_cp_reginfo[] = {
            /* These are common to v8 and pre-v8 */
            { .name = "CTR",
              .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 1,
              .access = PL1_R, .accessfn = ctr_el0_access,
              .type = ARM_CP_CONST, .resetvalue = cpu->ctr },
            { .name = "CTR_EL0", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 3, .opc2 = 1, .crn = 0, .crm = 0,
              .access = PL0_R, .accessfn = ctr_el0_access,
              .type = ARM_CP_CONST, .resetvalue = cpu->ctr },
            /* TCMTR and TLBTR exist in v8 but have no 64-bit versions */
            { .name = "TCMTR",
              .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 2,
              .access = PL1_R,
              .accessfn = access_aa32_tid1,
              .type = ARM_CP_CONST, .resetvalue = 0 },
            REGINFO_SENTINEL
        };
        /* TLBTR is specific to VMSA */
        ARMCPRegInfo id_tlbtr_reginfo = {
              .name = "TLBTR",
              .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 3,
              .access = PL1_R,
              .accessfn = access_aa32_tid1,
              .type = ARM_CP_CONST, .resetvalue = 0,
        };
        /* MPUIR is specific to PMSA V6+ */
        ARMCPRegInfo id_mpuir_reginfo = {
              .name = "MPUIR",
              .cp = 15, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 4,
              .access = PL1_R, .type = ARM_CP_CONST,
              .resetvalue = cpu->pmsav7_dregion << 8
        };
        ARMCPRegInfo crn0_wi_reginfo = {
            .name = "CRN0_WI", .cp = 15, .crn = 0, .crm = CP_ANY,
            .opc1 = CP_ANY, .opc2 = CP_ANY, .access = PL1_W,
            .type = ARM_CP_NOP | ARM_CP_OVERRIDE
        };
#ifdef CONFIG_USER_ONLY
        ARMCPRegUserSpaceInfo id_v8_user_midr_cp_reginfo[] = {
            { .name = "MIDR_EL1",
              .exported_bits = 0x00000000ffffffff },
            { .name = "REVIDR_EL1"                },
            REGUSERINFO_SENTINEL
        };
        modify_arm_cp_regs(id_v8_midr_cp_reginfo, id_v8_user_midr_cp_reginfo);
#endif
        if (arm_feature(env, ARM_FEATURE_OMAPCP) ||
            arm_feature(env, ARM_FEATURE_STRONGARM)) {
            ARMCPRegInfo *r;
            /* Register the blanket "writes ignored" value first to cover the
             * whole space. Then update the specific ID registers to allow write
             * access, so that they ignore writes rather than causing them to
             * UNDEF.
             */
            define_one_arm_cp_reg(cpu, &crn0_wi_reginfo);
            for (r = id_pre_v8_midr_cp_reginfo;
                 r->type != ARM_CP_SENTINEL; r++) {
                r->access = PL1_RW;
            }
            for (r = id_cp_reginfo; r->type != ARM_CP_SENTINEL; r++) {
                r->access = PL1_RW;
            }
            id_mpuir_reginfo.access = PL1_RW;
            id_tlbtr_reginfo.access = PL1_RW;
        }
        if (arm_feature(env, ARM_FEATURE_V8)) {
            define_arm_cp_regs(cpu, id_v8_midr_cp_reginfo);
        } else {
            define_arm_cp_regs(cpu, id_pre_v8_midr_cp_reginfo);
        }
        define_arm_cp_regs(cpu, id_cp_reginfo);
        if (!arm_feature(env, ARM_FEATURE_PMSA)) {
            define_one_arm_cp_reg(cpu, &id_tlbtr_reginfo);
        } else if (arm_feature(env, ARM_FEATURE_V7)) {
            define_one_arm_cp_reg(cpu, &id_mpuir_reginfo);
        }
    }

    if (arm_feature(env, ARM_FEATURE_MPIDR)) {
        ARMCPRegInfo mpidr_cp_reginfo[] = {
            { .name = "MPIDR_EL1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .crn = 0, .crm = 0, .opc1 = 0, .opc2 = 5,
              .access = PL1_R, .readfn = mpidr_read, .type = ARM_CP_NO_RAW },
            REGINFO_SENTINEL
        };
#ifdef CONFIG_USER_ONLY
        ARMCPRegUserSpaceInfo mpidr_user_cp_reginfo[] = {
            { .name = "MPIDR_EL1",
              .fixed_bits = 0x0000000080000000 },
            REGUSERINFO_SENTINEL
        };
        modify_arm_cp_regs(mpidr_cp_reginfo, mpidr_user_cp_reginfo);
#endif
        define_arm_cp_regs(cpu, mpidr_cp_reginfo);
    }

    if (arm_feature(env, ARM_FEATURE_AUXCR)) {
        ARMCPRegInfo auxcr_reginfo[] = {
            { .name = "ACTLR_EL1", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 1,
              .access = PL1_RW, .accessfn = access_tacr,
              .type = ARM_CP_CONST, .resetvalue = cpu->reset_auxcr },
            { .name = "ACTLR_EL2", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .opc1 = 4, .crn = 1, .crm = 0, .opc2 = 1,
              .access = PL2_RW, .type = ARM_CP_CONST,
              .resetvalue = 0 },
            { .name = "ACTLR_EL3", .state = ARM_CP_STATE_AA64,
              .opc0 = 3, .opc1 = 6, .crn = 1, .crm = 0, .opc2 = 1,
              .access = PL3_RW, .type = ARM_CP_CONST,
              .resetvalue = 0 },
            REGINFO_SENTINEL
        };
        define_arm_cp_regs(cpu, auxcr_reginfo);
        if (cpu_isar_feature(aa32_ac2, cpu)) {
            define_arm_cp_regs(cpu, actlr2_hactlr2_reginfo);
        }
    }

    if (arm_feature(env, ARM_FEATURE_CBAR)) {
        /*
         * CBAR is IMPDEF, but common on Arm Cortex-A implementations.
         * There are two flavours:
         *  (1) older 32-bit only cores have a simple 32-bit CBAR
         *  (2) 64-bit cores have a 64-bit CBAR visible to AArch64, plus a
         *      32-bit register visible to AArch32 at a different encoding
         *      to the "flavour 1" register and with the bits rearranged to
         *      be able to squash a 64-bit address into the 32-bit view.
         * We distinguish the two via the ARM_FEATURE_AARCH64 flag, but
         * in future if we support AArch32-only configs of some of the
         * AArch64 cores we might need to add a specific feature flag
         * to indicate cores with "flavour 2" CBAR.
         */
        if (arm_feature(env, ARM_FEATURE_AARCH64)) {
            /* 32 bit view is [31:18] 0...0 [43:32]. */
            uint32_t cbar32 = (extract64(cpu->reset_cbar, 18, 14) << 18)
                | extract64(cpu->reset_cbar, 32, 12);
            ARMCPRegInfo cbar_reginfo[] = {
                { .name = "CBAR",
                  .type = ARM_CP_CONST,
                  .cp = 15, .crn = 15, .crm = 3, .opc1 = 1, .opc2 = 0,
                  .access = PL1_R, .resetvalue = cbar32 },
                { .name = "CBAR_EL1", .state = ARM_CP_STATE_AA64,
                  .type = ARM_CP_CONST,
                  .opc0 = 3, .opc1 = 1, .crn = 15, .crm = 3, .opc2 = 0,
                  .access = PL1_R, .resetvalue = cpu->reset_cbar },
                REGINFO_SENTINEL
            };
            /* We don't implement a r/w 64 bit CBAR currently */
            assert(arm_feature(env, ARM_FEATURE_CBAR_RO));
            define_arm_cp_regs(cpu, cbar_reginfo);
        } else {
            ARMCPRegInfo cbar = {
                .name = "CBAR",
                .cp = 15, .crn = 15, .crm = 0, .opc1 = 4, .opc2 = 0,
                .access = PL1_R|PL3_W, .resetvalue = cpu->reset_cbar,
                .fieldoffset = offsetof(CPUARMState,
                                        cp15.c15_config_base_address)
            };
            if (arm_feature(env, ARM_FEATURE_CBAR_RO)) {
                cbar.access = PL1_R;
                cbar.fieldoffset = 0;
                cbar.type = ARM_CP_CONST;
            }
            define_one_arm_cp_reg(cpu, &cbar);
        }
    }

    if (arm_feature(env, ARM_FEATURE_VBAR)) {
        ARMCPRegInfo vbar_cp_reginfo[] = {
            { .name = "VBAR", .state = ARM_CP_STATE_BOTH,
              .opc0 = 3, .crn = 12, .crm = 0, .opc1 = 0, .opc2 = 0,
              .access = PL1_RW, .writefn = vbar_write,
              .bank_fieldoffsets = { offsetof(CPUARMState, cp15.vbar_s),
                                     offsetof(CPUARMState, cp15.vbar_ns) },
              .resetvalue = 0 },
            REGINFO_SENTINEL
        };
        define_arm_cp_regs(cpu, vbar_cp_reginfo);
    }

    /* Generic registers whose values depend on the implementation */
    {
        ARMCPRegInfo sctlr = {
            .name = "SCTLR", .state = ARM_CP_STATE_BOTH,
            .opc0 = 3, .opc1 = 0, .crn = 1, .crm = 0, .opc2 = 0,
            .access = PL1_RW, .accessfn = access_tvm_trvm,
            .bank_fieldoffsets = { offsetof(CPUARMState, cp15.sctlr_s),
                                   offsetof(CPUARMState, cp15.sctlr_ns) },
            .writefn = sctlr_write, .resetvalue = cpu->reset_sctlr,
            .raw_writefn = raw_write,
        };
        if (arm_feature(env, ARM_FEATURE_XSCALE)) {
            /* Normally we would always end the TB on an SCTLR write, but Linux
             * arch/arm/mach-pxa/sleep.S expects two instructions following
             * an MMU enable to execute from cache.  Imitate this behaviour.
             */
            sctlr.type |= ARM_CP_SUPPRESS_TB_END;
        }
        define_one_arm_cp_reg(cpu, &sctlr);
    }

    if (cpu_isar_feature(aa64_lor, cpu)) {
        define_arm_cp_regs(cpu, lor_reginfo);
    }
    if (cpu_isar_feature(aa64_pan, cpu)) {
        define_one_arm_cp_reg(cpu, &pan_reginfo);
    }
#ifndef CONFIG_USER_ONLY
    if (cpu_isar_feature(aa64_ats1e1, cpu)) {
        define_arm_cp_regs(cpu, ats1e1_reginfo);
    }
    if (cpu_isar_feature(aa32_ats1e1, cpu)) {
        define_arm_cp_regs(cpu, ats1cp_reginfo);
    }
#endif
    if (cpu_isar_feature(aa64_uao, cpu)) {
        define_one_arm_cp_reg(cpu, &uao_reginfo);
    }

    if (arm_feature(env, ARM_FEATURE_EL2) && cpu_isar_feature(aa64_vh, cpu)) {
        define_arm_cp_regs(cpu, vhe_reginfo);
    }

    if (cpu_isar_feature(aa64_sve, cpu)) {
        define_one_arm_cp_reg(cpu, &zcr_el1_reginfo);
        if (arm_feature(env, ARM_FEATURE_EL2)) {
            define_one_arm_cp_reg(cpu, &zcr_el2_reginfo);
        } else {
            define_one_arm_cp_reg(cpu, &zcr_no_el2_reginfo);
        }
        if (arm_feature(env, ARM_FEATURE_EL3)) {
            define_one_arm_cp_reg(cpu, &zcr_el3_reginfo);
        }
    }

#ifdef TARGET_AARCH64
    if (cpu_isar_feature(aa64_pauth, cpu)) {
        define_arm_cp_regs(cpu, pauth_reginfo);
    }
    if (cpu_isar_feature(aa64_rndr, cpu)) {
        define_arm_cp_regs(cpu, rndr_reginfo);
    }
#ifndef CONFIG_USER_ONLY
    /* Data Cache clean instructions up to PoP */
    if (cpu_isar_feature(aa64_dcpop, cpu)) {
        define_one_arm_cp_reg(cpu, dcpop_reg);

        if (cpu_isar_feature(aa64_dcpodp, cpu)) {
            define_one_arm_cp_reg(cpu, dcpodp_reg);
        }
    }
#endif /*CONFIG_USER_ONLY*/

    /*
     * If full MTE is enabled, add all of the system registers.
     * If only "instructions available at EL0" are enabled,
     * then define only a RAZ/WI version of PSTATE.TCO.
     */
    if (cpu_isar_feature(aa64_mte, cpu)) {
        define_arm_cp_regs(cpu, mte_reginfo);
        define_arm_cp_regs(cpu, mte_el0_cacheop_reginfo);
    } else if (cpu_isar_feature(aa64_mte_insn_reg, cpu)) {
        define_arm_cp_regs(cpu, mte_tco_ro_reginfo);
        define_arm_cp_regs(cpu, mte_el0_cacheop_reginfo);
    }
#endif

    if (cpu_isar_feature(any_predinv, cpu)) {
        define_arm_cp_regs(cpu, predinv_reginfo);
    }

    if (cpu_isar_feature(any_ccidx, cpu)) {
        define_arm_cp_regs(cpu, ccsidr2_reginfo);
    }

#ifndef CONFIG_USER_ONLY
    /*
     * Register redirections and aliases must be done last,
     * after the registers from the other extensions have been defined.
     */
    if (arm_feature(env, ARM_FEATURE_EL2) && cpu_isar_feature(aa64_vh, cpu)) {
        define_arm_vh_e2h_redirects_aliases(cpu);
    }
#endif
}

void arm_cpu_register_gdb_regs_for_features(ARMCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;

    if (arm_feature(env, ARM_FEATURE_AARCH64)) {
        /*
         * The lower part of each SVE register aliases to the FPU
         * registers so we don't need to include both.
         */
#ifdef TARGET_AARCH64
        if (isar_feature_aa64_sve(&cpu->isar)) {
            gdb_register_coprocessor(cs, arm_gdb_get_svereg, arm_gdb_set_svereg,
                                     arm_gen_dynamic_svereg_xml(cs, cs->gdb_num_regs),
                                     "sve-registers.xml", 0);
        } else
#endif
        {
            gdb_register_coprocessor(cs, aarch64_fpu_gdb_get_reg,
                                     aarch64_fpu_gdb_set_reg,
                                     34, "aarch64-fpu.xml", 0);
        }
    } else if (arm_feature(env, ARM_FEATURE_NEON)) {
        gdb_register_coprocessor(cs, vfp_gdb_get_reg, vfp_gdb_set_reg,
                                 51, "arm-neon.xml", 0);
    } else if (cpu_isar_feature(aa32_simd_r32, cpu)) {
        gdb_register_coprocessor(cs, vfp_gdb_get_reg, vfp_gdb_set_reg,
                                 35, "arm-vfp3.xml", 0);
    } else if (cpu_isar_feature(aa32_vfp_simd, cpu)) {
        gdb_register_coprocessor(cs, vfp_gdb_get_reg, vfp_gdb_set_reg,
                                 19, "arm-vfp.xml", 0);
    }
    gdb_register_coprocessor(cs, arm_gdb_get_sysreg, arm_gdb_set_sysreg,
                             arm_gen_dynamic_sysreg_xml(cs, cs->gdb_num_regs),
                             "system-registers.xml", 0);

}

/* Sort alphabetically by type name, except for "any". */
static gint arm_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a, *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    if (strcmp(name_a, "any-" TYPE_ARM_CPU) == 0) {
        return 1;
    } else if (strcmp(name_b, "any-" TYPE_ARM_CPU) == 0) {
        return -1;
    } else {
        return strcmp(name_a, name_b);
    }
}

static void arm_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    const char *typename;
    char *name;

    typename = object_class_get_name(oc);
    name = g_strndup(typename, strlen(typename) - strlen("-" TYPE_ARM_CPU));
    qemu_printf("  %s\n", name);
    g_free(name);
}

void arm_cpu_list(void)
{
    GSList *list;

    list = object_class_get_list(TYPE_ARM_CPU, false);
    list = g_slist_sort(list, arm_cpu_list_compare);
    qemu_printf("Available CPUs:\n");
    g_slist_foreach(list, arm_cpu_list_entry, NULL);
    g_slist_free(list);
}

static void arm_cpu_add_definition(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CpuDefinitionInfoList **cpu_list = user_data;
    CpuDefinitionInfoList *entry;
    CpuDefinitionInfo *info;
    const char *typename;

    typename = object_class_get_name(oc);
    info = g_malloc0(sizeof(*info));
    info->name = g_strndup(typename,
                           strlen(typename) - strlen("-" TYPE_ARM_CPU));
    info->q_typename = g_strdup(typename);

    entry = g_malloc0(sizeof(*entry));
    entry->value = info;
    entry->next = *cpu_list;
    *cpu_list = entry;
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    CpuDefinitionInfoList *cpu_list = NULL;
    GSList *list;

    list = object_class_get_list(TYPE_ARM_CPU, false);
    g_slist_foreach(list, arm_cpu_add_definition, &cpu_list);
    g_slist_free(list);

    return cpu_list;
}

static void add_cpreg_to_hashtable(ARMCPU *cpu, const ARMCPRegInfo *r,
                                   void *opaque, int state, int secstate,
                                   int crm, int opc1, int opc2,
                                   const char *name)
{
    /* Private utility function for define_one_arm_cp_reg_with_opaque():
     * add a single reginfo struct to the hash table.
     */
    uint32_t *key = g_new(uint32_t, 1);
    ARMCPRegInfo *r2 = g_memdup(r, sizeof(ARMCPRegInfo));
    int is64 = (r->type & ARM_CP_64BIT) ? 1 : 0;
    int ns = (secstate & ARM_CP_SECSTATE_NS) ? 1 : 0;

    r2->name = g_strdup(name);
    /* Reset the secure state to the specific incoming state.  This is
     * necessary as the register may have been defined with both states.
     */
    r2->secure = secstate;

    if (r->bank_fieldoffsets[0] && r->bank_fieldoffsets[1]) {
        /* Register is banked (using both entries in array).
         * Overwriting fieldoffset as the array is only used to define
         * banked registers but later only fieldoffset is used.
         */
        r2->fieldoffset = r->bank_fieldoffsets[ns];
    }

    if (state == ARM_CP_STATE_AA32) {
        if (r->bank_fieldoffsets[0] && r->bank_fieldoffsets[1]) {
            /* If the register is banked then we don't need to migrate or
             * reset the 32-bit instance in certain cases:
             *
             * 1) If the register has both 32-bit and 64-bit instances then we
             *    can count on the 64-bit instance taking care of the
             *    non-secure bank.
             * 2) If ARMv8 is enabled then we can count on a 64-bit version
             *    taking care of the secure bank.  This requires that separate
             *    32 and 64-bit definitions are provided.
             */
            if ((r->state == ARM_CP_STATE_BOTH && ns) ||
                (arm_feature(&cpu->env, ARM_FEATURE_V8) && !ns)) {
                r2->type |= ARM_CP_ALIAS;
            }
        } else if ((secstate != r->secure) && !ns) {
            /* The register is not banked so we only want to allow migration of
             * the non-secure instance.
             */
            r2->type |= ARM_CP_ALIAS;
        }

        if (r->state == ARM_CP_STATE_BOTH) {
            /* We assume it is a cp15 register if the .cp field is left unset.
             */
            if (r2->cp == 0) {
                r2->cp = 15;
            }

#ifdef HOST_WORDS_BIGENDIAN
            if (r2->fieldoffset) {
                r2->fieldoffset += sizeof(uint32_t);
            }
#endif
        }
    }
    if (state == ARM_CP_STATE_AA64) {
        /* To allow abbreviation of ARMCPRegInfo
         * definitions, we treat cp == 0 as equivalent to
         * the value for "standard guest-visible sysreg".
         * STATE_BOTH definitions are also always "standard
         * sysreg" in their AArch64 view (the .cp value may
         * be non-zero for the benefit of the AArch32 view).
         */
        if (r->cp == 0 || r->state == ARM_CP_STATE_BOTH) {
            r2->cp = CP_REG_ARM64_SYSREG_CP;
        }
        *key = ENCODE_AA64_CP_REG(r2->cp, r2->crn, crm,
                                  r2->opc0, opc1, opc2);
    } else {
        *key = ENCODE_CP_REG(r2->cp, is64, ns, r2->crn, crm, opc1, opc2);
    }
    if (opaque) {
        r2->opaque = opaque;
    }
    /* reginfo passed to helpers is correct for the actual access,
     * and is never ARM_CP_STATE_BOTH:
     */
    r2->state = state;
    /* Make sure reginfo passed to helpers for wildcarded regs
     * has the correct crm/opc1/opc2 for this reg, not CP_ANY:
     */
    r2->crm = crm;
    r2->opc1 = opc1;
    r2->opc2 = opc2;
    /* By convention, for wildcarded registers only the first
     * entry is used for migration; the others are marked as
     * ALIAS so we don't try to transfer the register
     * multiple times. Special registers (ie NOP/WFI) are
     * never migratable and not even raw-accessible.
     */
    if ((r->type & ARM_CP_SPECIAL)) {
        r2->type |= ARM_CP_NO_RAW;
    }
    if (((r->crm == CP_ANY) && crm != 0) ||
        ((r->opc1 == CP_ANY) && opc1 != 0) ||
        ((r->opc2 == CP_ANY) && opc2 != 0)) {
        r2->type |= ARM_CP_ALIAS | ARM_CP_NO_GDB;
    }

    /* Check that raw accesses are either forbidden or handled. Note that
     * we can't assert this earlier because the setup of fieldoffset for
     * banked registers has to be done first.
     */
    if (!(r2->type & ARM_CP_NO_RAW)) {
        assert(!raw_accessors_invalid(r2));
    }

    /* Overriding of an existing definition must be explicitly
     * requested.
     */
    if (!(r->type & ARM_CP_OVERRIDE)) {
        ARMCPRegInfo *oldreg;
        oldreg = g_hash_table_lookup(cpu->cp_regs, key);
        if (oldreg && !(oldreg->type & ARM_CP_OVERRIDE)) {
            fprintf(stderr, "Register redefined: cp=%d %d bit "
                    "crn=%d crm=%d opc1=%d opc2=%d, "
                    "was %s, now %s\n", r2->cp, 32 + 32 * is64,
                    r2->crn, r2->crm, r2->opc1, r2->opc2,
                    oldreg->name, r2->name);
            g_assert_not_reached();
        }
    }
    g_hash_table_insert(cpu->cp_regs, key, r2);
}


void define_one_arm_cp_reg_with_opaque(ARMCPU *cpu,
                                       const ARMCPRegInfo *r, void *opaque)
{
    /* Define implementations of coprocessor registers.
     * We store these in a hashtable because typically
     * there are less than 150 registers in a space which
     * is 16*16*16*8*8 = 262144 in size.
     * Wildcarding is supported for the crm, opc1 and opc2 fields.
     * If a register is defined twice then the second definition is
     * used, so this can be used to define some generic registers and
     * then override them with implementation specific variations.
     * At least one of the original and the second definition should
     * include ARM_CP_OVERRIDE in its type bits -- this is just a guard
     * against accidental use.
     *
     * The state field defines whether the register is to be
     * visible in the AArch32 or AArch64 execution state. If the
     * state is set to ARM_CP_STATE_BOTH then we synthesise a
     * reginfo structure for the AArch32 view, which sees the lower
     * 32 bits of the 64 bit register.
     *
     * Only registers visible in AArch64 may set r->opc0; opc0 cannot
     * be wildcarded. AArch64 registers are always considered to be 64
     * bits; the ARM_CP_64BIT* flag applies only to the AArch32 view of
     * the register, if any.
     */
    int crm, opc1, opc2, state;
    int crmmin = (r->crm == CP_ANY) ? 0 : r->crm;
    int crmmax = (r->crm == CP_ANY) ? 15 : r->crm;
    int opc1min = (r->opc1 == CP_ANY) ? 0 : r->opc1;
    int opc1max = (r->opc1 == CP_ANY) ? 7 : r->opc1;
    int opc2min = (r->opc2 == CP_ANY) ? 0 : r->opc2;
    int opc2max = (r->opc2 == CP_ANY) ? 7 : r->opc2;
    /* 64 bit registers have only CRm and Opc1 fields */
    assert(!((r->type & ARM_CP_64BIT) && (r->opc2 || r->crn)));
    /* op0 only exists in the AArch64 encodings */
    assert((r->state != ARM_CP_STATE_AA32) || (r->opc0 == 0));
    /* AArch64 regs are all 64 bit so ARM_CP_64BIT is meaningless */
    assert((r->state != ARM_CP_STATE_AA64) || !(r->type & ARM_CP_64BIT));
    /* The AArch64 pseudocode CheckSystemAccess() specifies that op1
     * encodes a minimum access level for the register. We roll this
     * runtime check into our general permission check code, so check
     * here that the reginfo's specified permissions are strict enough
     * to encompass the generic architectural permission check.
     */
    if (r->state != ARM_CP_STATE_AA32) {
        int mask = 0;
        switch (r->opc1) {
        case 0:
            /* min_EL EL1, but some accessible to EL0 via kernel ABI */
            mask = PL0U_R | PL1_RW;
            break;
        case 1: case 2:
            /* min_EL EL1 */
            mask = PL1_RW;
            break;
        case 3:
            /* min_EL EL0 */
            mask = PL0_RW;
            break;
        case 4:
        case 5:
            /* min_EL EL2 */
            mask = PL2_RW;
            break;
        case 6:
            /* min_EL EL3 */
            mask = PL3_RW;
            break;
        case 7:
            /* min_EL EL1, secure mode only (we don't check the latter) */
            mask = PL1_RW;
            break;
        default:
            /* broken reginfo with out-of-range opc1 */
            assert(false);
            break;
        }
        /* assert our permissions are not too lax (stricter is fine) */
        assert((r->access & ~mask) == 0);
    }

    /* Check that the register definition has enough info to handle
     * reads and writes if they are permitted.
     */
    if (!(r->type & (ARM_CP_SPECIAL|ARM_CP_CONST))) {
        if (r->access & PL3_R) {
            assert((r->fieldoffset ||
                   (r->bank_fieldoffsets[0] && r->bank_fieldoffsets[1])) ||
                   r->readfn);
        }
        if (r->access & PL3_W) {
            assert((r->fieldoffset ||
                   (r->bank_fieldoffsets[0] && r->bank_fieldoffsets[1])) ||
                   r->writefn);
        }
    }
    /* Bad type field probably means missing sentinel at end of reg list */
    assert(cptype_valid(r->type));
    for (crm = crmmin; crm <= crmmax; crm++) {
        for (opc1 = opc1min; opc1 <= opc1max; opc1++) {
            for (opc2 = opc2min; opc2 <= opc2max; opc2++) {
                for (state = ARM_CP_STATE_AA32;
                     state <= ARM_CP_STATE_AA64; state++) {
                    if (r->state != state && r->state != ARM_CP_STATE_BOTH) {
                        continue;
                    }
                    if (state == ARM_CP_STATE_AA32) {
                        /* Under AArch32 CP registers can be common
                         * (same for secure and non-secure world) or banked.
                         */
                        char *name;

                        switch (r->secure) {
                        case ARM_CP_SECSTATE_S:
                        case ARM_CP_SECSTATE_NS:
                            add_cpreg_to_hashtable(cpu, r, opaque, state,
                                                   r->secure, crm, opc1, opc2,
                                                   r->name);
                            break;
                        default:
                            name = g_strdup_printf("%s_S", r->name);
                            add_cpreg_to_hashtable(cpu, r, opaque, state,
                                                   ARM_CP_SECSTATE_S,
                                                   crm, opc1, opc2, name);
                            g_free(name);
                            add_cpreg_to_hashtable(cpu, r, opaque, state,
                                                   ARM_CP_SECSTATE_NS,
                                                   crm, opc1, opc2, r->name);
                            break;
                        }
                    } else {
                        /* AArch64 registers get mapped to non-secure instance
                         * of AArch32 */
                        add_cpreg_to_hashtable(cpu, r, opaque, state,
                                               ARM_CP_SECSTATE_NS,
                                               crm, opc1, opc2, r->name);
                    }
                }
            }
        }
    }
}

void define_arm_cp_regs_with_opaque(ARMCPU *cpu,
                                    const ARMCPRegInfo *regs, void *opaque)
{
    /* Define a whole list of registers */
    const ARMCPRegInfo *r;
    for (r = regs; r->type != ARM_CP_SENTINEL; r++) {
        define_one_arm_cp_reg_with_opaque(cpu, r, opaque);
    }
}

/*
 * Modify ARMCPRegInfo for access from userspace.
 *
 * This is a data driven modification directed by
 * ARMCPRegUserSpaceInfo. All registers become ARM_CP_CONST as
 * user-space cannot alter any values and dynamic values pertaining to
 * execution state are hidden from user space view anyway.
 */
void modify_arm_cp_regs(ARMCPRegInfo *regs, const ARMCPRegUserSpaceInfo *mods)
{
    const ARMCPRegUserSpaceInfo *m;
    ARMCPRegInfo *r;

    for (m = mods; m->name; m++) {
        GPatternSpec *pat = NULL;
        if (m->is_glob) {
            pat = g_pattern_spec_new(m->name);
        }
        for (r = regs; r->type != ARM_CP_SENTINEL; r++) {
            if (pat && g_pattern_match_string(pat, r->name)) {
                r->type = ARM_CP_CONST;
                r->access = PL0U_R;
                r->resetvalue = 0;
                /* continue */
            } else if (strcmp(r->name, m->name) == 0) {
                r->type = ARM_CP_CONST;
                r->access = PL0U_R;
                r->resetvalue &= m->exported_bits;
                r->resetvalue |= m->fixed_bits;
                break;
            }
        }
        if (pat) {
            g_pattern_spec_free(pat);
        }
    }
}

const ARMCPRegInfo *get_arm_cp_reginfo(GHashTable *cpregs, uint32_t encoded_cp)
{
    return g_hash_table_lookup(cpregs, &encoded_cp);
}

void arm_cp_write_ignore(CPUARMState *env, const ARMCPRegInfo *ri,
                         uint64_t value)
{
    /* Helper coprocessor write function for write-ignore registers */
}

uint64_t arm_cp_read_zero(CPUARMState *env, const ARMCPRegInfo *ri)
{
    /* Helper coprocessor write function for read-as-zero registers */
    return 0;
}

void arm_cp_reset_ignore(CPUARMState *env, const ARMCPRegInfo *opaque)
{
    /* Helper coprocessor reset function for do-nothing-on-reset registers */
}

static int bad_mode_switch(CPUARMState *env, int mode, CPSRWriteType write_type)
{
    /* Return true if it is not valid for us to switch to
     * this CPU mode (ie all the UNPREDICTABLE cases in
     * the ARM ARM CPSRWriteByInstr pseudocode).
     */

    /* Changes to or from Hyp via MSR and CPS are illegal. */
    if (write_type == CPSRWriteByInstr &&
        ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_HYP ||
         mode == ARM_CPU_MODE_HYP)) {
        return 1;
    }

    switch (mode) {
    case ARM_CPU_MODE_USR:
        return 0;
    case ARM_CPU_MODE_SYS:
    case ARM_CPU_MODE_SVC:
    case ARM_CPU_MODE_ABT:
    case ARM_CPU_MODE_UND:
    case ARM_CPU_MODE_IRQ:
    case ARM_CPU_MODE_FIQ:
        /* Note that we don't implement the IMPDEF NSACR.RFR which in v7
         * allows FIQ mode to be Secure-only. (In v8 this doesn't exist.)
         */
        /* If HCR.TGE is set then changes from Monitor to NS PL1 via MSR
         * and CPS are treated as illegal mode changes.
         */
        if (write_type == CPSRWriteByInstr &&
            (env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_MON &&
            (arm_hcr_el2_eff(env) & HCR_TGE)) {
            return 1;
        }
        return 0;
    case ARM_CPU_MODE_HYP:
        return !arm_feature(env, ARM_FEATURE_EL2)
            || arm_current_el(env) < 2 || arm_is_secure_below_el3(env);
    case ARM_CPU_MODE_MON:
        return arm_current_el(env) < 3;
    default:
        return 1;
    }
}

uint32_t cpsr_read(CPUARMState *env)
{
    int ZF;
    ZF = (env->ZF == 0);
    return env->uncached_cpsr | (env->NF & 0x80000000) | (ZF << 30) |
        (env->CF << 29) | ((env->VF & 0x80000000) >> 3) | (env->QF << 27)
        | (env->thumb << 5) | ((env->condexec_bits & 3) << 25)
        | ((env->condexec_bits & 0xfc) << 8)
        | (env->GE << 16) | (env->daif & CPSR_AIF);
}

void cpsr_write(CPUARMState *env, uint32_t val, uint32_t mask,
                CPSRWriteType write_type)
{
    uint32_t changed_daif;

    if (mask & CPSR_NZCV) {
        env->ZF = (~val) & CPSR_Z;
        env->NF = val;
        env->CF = (val >> 29) & 1;
        env->VF = (val << 3) & 0x80000000;
    }
    if (mask & CPSR_Q)
        env->QF = ((val & CPSR_Q) != 0);
    if (mask & CPSR_T)
        env->thumb = ((val & CPSR_T) != 0);
    if (mask & CPSR_IT_0_1) {
        env->condexec_bits &= ~3;
        env->condexec_bits |= (val >> 25) & 3;
    }
    if (mask & CPSR_IT_2_7) {
        env->condexec_bits &= 3;
        env->condexec_bits |= (val >> 8) & 0xfc;
    }
    if (mask & CPSR_GE) {
        env->GE = (val >> 16) & 0xf;
    }

    /* In a V7 implementation that includes the security extensions but does
     * not include Virtualization Extensions the SCR.FW and SCR.AW bits control
     * whether non-secure software is allowed to change the CPSR_F and CPSR_A
     * bits respectively.
     *
     * In a V8 implementation, it is permitted for privileged software to
     * change the CPSR A/F bits regardless of the SCR.AW/FW bits.
     */
    if (write_type != CPSRWriteRaw && !arm_feature(env, ARM_FEATURE_V8) &&
        arm_feature(env, ARM_FEATURE_EL3) &&
        !arm_feature(env, ARM_FEATURE_EL2) &&
        !arm_is_secure(env)) {

        changed_daif = (env->daif ^ val) & mask;

        if (changed_daif & CPSR_A) {
            /* Check to see if we are allowed to change the masking of async
             * abort exceptions from a non-secure state.
             */
            if (!(env->cp15.scr_el3 & SCR_AW)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Ignoring attempt to switch CPSR_A flag from "
                              "non-secure world with SCR.AW bit clear\n");
                mask &= ~CPSR_A;
            }
        }

        if (changed_daif & CPSR_F) {
            /* Check to see if we are allowed to change the masking of FIQ
             * exceptions from a non-secure state.
             */
            if (!(env->cp15.scr_el3 & SCR_FW)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Ignoring attempt to switch CPSR_F flag from "
                              "non-secure world with SCR.FW bit clear\n");
                mask &= ~CPSR_F;
            }

            /* Check whether non-maskable FIQ (NMFI) support is enabled.
             * If this bit is set software is not allowed to mask
             * FIQs, but is allowed to set CPSR_F to 0.
             */
            if ((A32_BANKED_CURRENT_REG_GET(env, sctlr) & SCTLR_NMFI) &&
                (val & CPSR_F)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Ignoring attempt to enable CPSR_F flag "
                              "(non-maskable FIQ [NMFI] support enabled)\n");
                mask &= ~CPSR_F;
            }
        }
    }

    env->daif &= ~(CPSR_AIF & mask);
    env->daif |= val & CPSR_AIF & mask;

    if (write_type != CPSRWriteRaw &&
        ((env->uncached_cpsr ^ val) & mask & CPSR_M)) {
        if ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_USR) {
            /* Note that we can only get here in USR mode if this is a
             * gdb stub write; for this case we follow the architectural
             * behaviour for guest writes in USR mode of ignoring an attempt
             * to switch mode. (Those are caught by translate.c for writes
             * triggered by guest instructions.)
             */
            mask &= ~CPSR_M;
        } else if (bad_mode_switch(env, val & CPSR_M, write_type)) {
            /* Attempt to switch to an invalid mode: this is UNPREDICTABLE in
             * v7, and has defined behaviour in v8:
             *  + leave CPSR.M untouched
             *  + allow changes to the other CPSR fields
             *  + set PSTATE.IL
             * For user changes via the GDB stub, we don't set PSTATE.IL,
             * as this would be unnecessarily harsh for a user error.
             */
            mask &= ~CPSR_M;
            if (write_type != CPSRWriteByGDBStub &&
                arm_feature(env, ARM_FEATURE_V8)) {
                mask |= CPSR_IL;
                val |= CPSR_IL;
            }
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Illegal AArch32 mode switch attempt from %s to %s\n",
                          aarch32_mode_name(env->uncached_cpsr),
                          aarch32_mode_name(val));
        } else {
            qemu_log_mask(CPU_LOG_INT, "%s %s to %s PC 0x%" PRIx32 "\n",
                          write_type == CPSRWriteExceptionReturn ?
                          "Exception return from AArch32" :
                          "AArch32 mode switch from",
                          aarch32_mode_name(env->uncached_cpsr),
                          aarch32_mode_name(val), env->regs[15]);
            switch_mode(env, val & CPSR_M);
        }
    }
    mask &= ~CACHED_CPSR_BITS;
    env->uncached_cpsr = (env->uncached_cpsr & ~mask) | (val & mask);
}

/* Sign/zero extend */
uint32_t HELPER(sxtb16)(uint32_t x)
{
    uint32_t res;
    res = (uint16_t)(int8_t)x;
    res |= (uint32_t)(int8_t)(x >> 16) << 16;
    return res;
}

uint32_t HELPER(uxtb16)(uint32_t x)
{
    uint32_t res;
    res = (uint16_t)(uint8_t)x;
    res |= (uint32_t)(uint8_t)(x >> 16) << 16;
    return res;
}

int32_t HELPER(sdiv)(int32_t num, int32_t den)
{
    if (den == 0)
      return 0;
    if (num == INT_MIN && den == -1)
      return INT_MIN;
    return num / den;
}

uint32_t HELPER(udiv)(uint32_t num, uint32_t den)
{
    if (den == 0)
      return 0;
    return num / den;
}

uint32_t HELPER(rbit)(uint32_t x)
{
    return revbit32(x);
}

#ifdef CONFIG_USER_ONLY

static void switch_mode(CPUARMState *env, int mode)
{
    ARMCPU *cpu = env_archcpu(env);

    if (mode != ARM_CPU_MODE_USR) {
        cpu_abort(CPU(cpu), "Tried to switch out of user mode\n");
    }
}

uint32_t arm_phys_excp_target_el(CPUState *cs, uint32_t excp_idx,
                                 uint32_t cur_el, bool secure)
{
    return 1;
}

void aarch64_sync_64_to_32(CPUARMState *env)
{
    g_assert_not_reached();
}

#else

static void switch_mode(CPUARMState *env, int mode)
{
    int old_mode;
    int i;

    old_mode = env->uncached_cpsr & CPSR_M;
    if (mode == old_mode)
        return;

    if (old_mode == ARM_CPU_MODE_FIQ) {
        memcpy (env->fiq_regs, env->regs + 8, 5 * sizeof(uint32_t));
        memcpy (env->regs + 8, env->usr_regs, 5 * sizeof(uint32_t));
    } else if (mode == ARM_CPU_MODE_FIQ) {
        memcpy (env->usr_regs, env->regs + 8, 5 * sizeof(uint32_t));
        memcpy (env->regs + 8, env->fiq_regs, 5 * sizeof(uint32_t));
    }

    i = bank_number(old_mode);
    env->banked_r13[i] = env->regs[13];
    env->banked_spsr[i] = env->spsr;

    i = bank_number(mode);
    env->regs[13] = env->banked_r13[i];
    env->spsr = env->banked_spsr[i];

    env->banked_r14[r14_bank_number(old_mode)] = env->regs[14];
    env->regs[14] = env->banked_r14[r14_bank_number(mode)];
}

/* Physical Interrupt Target EL Lookup Table
 *
 * [ From ARM ARM section G1.13.4 (Table G1-15) ]
 *
 * The below multi-dimensional table is used for looking up the target
 * exception level given numerous condition criteria.  Specifically, the
 * target EL is based on SCR and HCR routing controls as well as the
 * currently executing EL and secure state.
 *
 *    Dimensions:
 *    target_el_table[2][2][2][2][2][4]
 *                    |  |  |  |  |  +--- Current EL
 *                    |  |  |  |  +------ Non-secure(0)/Secure(1)
 *                    |  |  |  +--------- HCR mask override
 *                    |  |  +------------ SCR exec state control
 *                    |  +--------------- SCR mask override
 *                    +------------------ 32-bit(0)/64-bit(1) EL3
 *
 *    The table values are as such:
 *    0-3 = EL0-EL3
 *     -1 = Cannot occur
 *
 * The ARM ARM target EL table includes entries indicating that an "exception
 * is not taken".  The two cases where this is applicable are:
 *    1) An exception is taken from EL3 but the SCR does not have the exception
 *    routed to EL3.
 *    2) An exception is taken from EL2 but the HCR does not have the exception
 *    routed to EL2.
 * In these two cases, the below table contain a target of EL1.  This value is
 * returned as it is expected that the consumer of the table data will check
 * for "target EL >= current EL" to ensure the exception is not taken.
 *
 *            SCR     HCR
 *         64  EA     AMO                 From
 *        BIT IRQ     IMO      Non-secure         Secure
 *        EL3 FIQ  RW FMO   EL0 EL1 EL2 EL3   EL0 EL1 EL2 EL3
 */
static const int8_t target_el_table[2][2][2][2][2][4] = {
    {{{{/* 0   0   0   0 */{ 1,  1,  2, -1 },{ 3, -1, -1,  3 },},
       {/* 0   0   0   1 */{ 2,  2,  2, -1 },{ 3, -1, -1,  3 },},},
      {{/* 0   0   1   0 */{ 1,  1,  2, -1 },{ 3, -1, -1,  3 },},
       {/* 0   0   1   1 */{ 2,  2,  2, -1 },{ 3, -1, -1,  3 },},},},
     {{{/* 0   1   0   0 */{ 3,  3,  3, -1 },{ 3, -1, -1,  3 },},
       {/* 0   1   0   1 */{ 3,  3,  3, -1 },{ 3, -1, -1,  3 },},},
      {{/* 0   1   1   0 */{ 3,  3,  3, -1 },{ 3, -1, -1,  3 },},
       {/* 0   1   1   1 */{ 3,  3,  3, -1 },{ 3, -1, -1,  3 },},},},},
    {{{{/* 1   0   0   0 */{ 1,  1,  2, -1 },{ 1,  1, -1,  1 },},
       {/* 1   0   0   1 */{ 2,  2,  2, -1 },{ 1,  1, -1,  1 },},},
      {{/* 1   0   1   0 */{ 1,  1,  1, -1 },{ 1,  1, -1,  1 },},
       {/* 1   0   1   1 */{ 2,  2,  2, -1 },{ 1,  1, -1,  1 },},},},
     {{{/* 1   1   0   0 */{ 3,  3,  3, -1 },{ 3,  3, -1,  3 },},
       {/* 1   1   0   1 */{ 3,  3,  3, -1 },{ 3,  3, -1,  3 },},},
      {{/* 1   1   1   0 */{ 3,  3,  3, -1 },{ 3,  3, -1,  3 },},
       {/* 1   1   1   1 */{ 3,  3,  3, -1 },{ 3,  3, -1,  3 },},},},},
};

/*
 * Determine the target EL for physical exceptions
 */
uint32_t arm_phys_excp_target_el(CPUState *cs, uint32_t excp_idx,
                                 uint32_t cur_el, bool secure)
{
    CPUARMState *env = cs->env_ptr;
    bool rw;
    bool scr;
    bool hcr;
    int target_el;
    /* Is the highest EL AArch64? */
    bool is64 = arm_feature(env, ARM_FEATURE_AARCH64);
    uint64_t hcr_el2;

    if (arm_feature(env, ARM_FEATURE_EL3)) {
        rw = ((env->cp15.scr_el3 & SCR_RW) == SCR_RW);
    } else {
        /* Either EL2 is the highest EL (and so the EL2 register width
         * is given by is64); or there is no EL2 or EL3, in which case
         * the value of 'rw' does not affect the table lookup anyway.
         */
        rw = is64;
    }

    hcr_el2 = arm_hcr_el2_eff(env);
    switch (excp_idx) {
    case EXCP_IRQ:
        scr = ((env->cp15.scr_el3 & SCR_IRQ) == SCR_IRQ);
        hcr = hcr_el2 & HCR_IMO;
        break;
    case EXCP_FIQ:
        scr = ((env->cp15.scr_el3 & SCR_FIQ) == SCR_FIQ);
        hcr = hcr_el2 & HCR_FMO;
        break;
    default:
        scr = ((env->cp15.scr_el3 & SCR_EA) == SCR_EA);
        hcr = hcr_el2 & HCR_AMO;
        break;
    };

    /*
     * For these purposes, TGE and AMO/IMO/FMO both force the
     * interrupt to EL2.  Fold TGE into the bit extracted above.
     */
    hcr |= (hcr_el2 & HCR_TGE) != 0;

    /* Perform a table-lookup for the target EL given the current state */
    target_el = target_el_table[is64][scr][rw][hcr][secure][cur_el];

    assert(target_el > 0);

    return target_el;
}

void arm_log_exception(int idx)
{
    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        const char *exc = NULL;
        static const char * const excnames[] = {
            [EXCP_UDEF] = "Undefined Instruction",
            [EXCP_SWI] = "SVC",
            [EXCP_PREFETCH_ABORT] = "Prefetch Abort",
            [EXCP_DATA_ABORT] = "Data Abort",
            [EXCP_IRQ] = "IRQ",
            [EXCP_FIQ] = "FIQ",
            [EXCP_BKPT] = "Breakpoint",
            [EXCP_EXCEPTION_EXIT] = "QEMU v7M exception exit",
            [EXCP_KERNEL_TRAP] = "QEMU intercept of kernel commpage",
            [EXCP_HVC] = "Hypervisor Call",
            [EXCP_HYP_TRAP] = "Hypervisor Trap",
            [EXCP_SMC] = "Secure Monitor Call",
            [EXCP_VIRQ] = "Virtual IRQ",
            [EXCP_VFIQ] = "Virtual FIQ",
            [EXCP_SEMIHOST] = "Semihosting call",
            [EXCP_NOCP] = "v7M NOCP UsageFault",
            [EXCP_INVSTATE] = "v7M INVSTATE UsageFault",
            [EXCP_STKOF] = "v8M STKOF UsageFault",
            [EXCP_LAZYFP] = "v7M exception during lazy FP stacking",
            [EXCP_LSERR] = "v8M LSERR UsageFault",
            [EXCP_UNALIGNED] = "v7M UNALIGNED UsageFault",
        };

        if (idx >= 0 && idx < ARRAY_SIZE(excnames)) {
            exc = excnames[idx];
        }
        if (!exc) {
            exc = "unknown";
        }
        qemu_log_mask(CPU_LOG_INT, "Taking exception %d [%s]\n", idx, exc);
    }
}

/*
 * Function used to synchronize QEMU's AArch64 register set with AArch32
 * register set.  This is necessary when switching between AArch32 and AArch64
 * execution state.
 */
void aarch64_sync_32_to_64(CPUARMState *env)
{
    int i;
    uint32_t mode = env->uncached_cpsr & CPSR_M;

    /* We can blanket copy R[0:7] to X[0:7] */
    for (i = 0; i < 8; i++) {
        env->xregs[i] = env->regs[i];
    }

    /*
     * Unless we are in FIQ mode, x8-x12 come from the user registers r8-r12.
     * Otherwise, they come from the banked user regs.
     */
    if (mode == ARM_CPU_MODE_FIQ) {
        for (i = 8; i < 13; i++) {
            env->xregs[i] = env->usr_regs[i - 8];
        }
    } else {
        for (i = 8; i < 13; i++) {
            env->xregs[i] = env->regs[i];
        }
    }

    /*
     * Registers x13-x23 are the various mode SP and FP registers. Registers
     * r13 and r14 are only copied if we are in that mode, otherwise we copy
     * from the mode banked register.
     */
    if (mode == ARM_CPU_MODE_USR || mode == ARM_CPU_MODE_SYS) {
        env->xregs[13] = env->regs[13];
        env->xregs[14] = env->regs[14];
    } else {
        env->xregs[13] = env->banked_r13[bank_number(ARM_CPU_MODE_USR)];
        /* HYP is an exception in that it is copied from r14 */
        if (mode == ARM_CPU_MODE_HYP) {
            env->xregs[14] = env->regs[14];
        } else {
            env->xregs[14] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_USR)];
        }
    }

    if (mode == ARM_CPU_MODE_HYP) {
        env->xregs[15] = env->regs[13];
    } else {
        env->xregs[15] = env->banked_r13[bank_number(ARM_CPU_MODE_HYP)];
    }

    if (mode == ARM_CPU_MODE_IRQ) {
        env->xregs[16] = env->regs[14];
        env->xregs[17] = env->regs[13];
    } else {
        env->xregs[16] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_IRQ)];
        env->xregs[17] = env->banked_r13[bank_number(ARM_CPU_MODE_IRQ)];
    }

    if (mode == ARM_CPU_MODE_SVC) {
        env->xregs[18] = env->regs[14];
        env->xregs[19] = env->regs[13];
    } else {
        env->xregs[18] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_SVC)];
        env->xregs[19] = env->banked_r13[bank_number(ARM_CPU_MODE_SVC)];
    }

    if (mode == ARM_CPU_MODE_ABT) {
        env->xregs[20] = env->regs[14];
        env->xregs[21] = env->regs[13];
    } else {
        env->xregs[20] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_ABT)];
        env->xregs[21] = env->banked_r13[bank_number(ARM_CPU_MODE_ABT)];
    }

    if (mode == ARM_CPU_MODE_UND) {
        env->xregs[22] = env->regs[14];
        env->xregs[23] = env->regs[13];
    } else {
        env->xregs[22] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_UND)];
        env->xregs[23] = env->banked_r13[bank_number(ARM_CPU_MODE_UND)];
    }

    /*
     * Registers x24-x30 are mapped to r8-r14 in FIQ mode.  If we are in FIQ
     * mode, then we can copy from r8-r14.  Otherwise, we copy from the
     * FIQ bank for r8-r14.
     */
    if (mode == ARM_CPU_MODE_FIQ) {
        for (i = 24; i < 31; i++) {
            env->xregs[i] = env->regs[i - 16];   /* X[24:30] <- R[8:14] */
        }
    } else {
        for (i = 24; i < 29; i++) {
            env->xregs[i] = env->fiq_regs[i - 24];
        }
        env->xregs[29] = env->banked_r13[bank_number(ARM_CPU_MODE_FIQ)];
        env->xregs[30] = env->banked_r14[r14_bank_number(ARM_CPU_MODE_FIQ)];
    }

    env->pc = env->regs[15];
}

/*
 * Function used to synchronize QEMU's AArch32 register set with AArch64
 * register set.  This is necessary when switching between AArch32 and AArch64
 * execution state.
 */
void aarch64_sync_64_to_32(CPUARMState *env)
{
    int i;
    uint32_t mode = env->uncached_cpsr & CPSR_M;

    /* We can blanket copy X[0:7] to R[0:7] */
    for (i = 0; i < 8; i++) {
        env->regs[i] = env->xregs[i];
    }

    /*
     * Unless we are in FIQ mode, r8-r12 come from the user registers x8-x12.
     * Otherwise, we copy x8-x12 into the banked user regs.
     */
    if (mode == ARM_CPU_MODE_FIQ) {
        for (i = 8; i < 13; i++) {
            env->usr_regs[i - 8] = env->xregs[i];
        }
    } else {
        for (i = 8; i < 13; i++) {
            env->regs[i] = env->xregs[i];
        }
    }

    /*
     * Registers r13 & r14 depend on the current mode.
     * If we are in a given mode, we copy the corresponding x registers to r13
     * and r14.  Otherwise, we copy the x register to the banked r13 and r14
     * for the mode.
     */
    if (mode == ARM_CPU_MODE_USR || mode == ARM_CPU_MODE_SYS) {
        env->regs[13] = env->xregs[13];
        env->regs[14] = env->xregs[14];
    } else {
        env->banked_r13[bank_number(ARM_CPU_MODE_USR)] = env->xregs[13];

        /*
         * HYP is an exception in that it does not have its own banked r14 but
         * shares the USR r14
         */
        if (mode == ARM_CPU_MODE_HYP) {
            env->regs[14] = env->xregs[14];
        } else {
            env->banked_r14[r14_bank_number(ARM_CPU_MODE_USR)] = env->xregs[14];
        }
    }

    if (mode == ARM_CPU_MODE_HYP) {
        env->regs[13] = env->xregs[15];
    } else {
        env->banked_r13[bank_number(ARM_CPU_MODE_HYP)] = env->xregs[15];
    }

    if (mode == ARM_CPU_MODE_IRQ) {
        env->regs[14] = env->xregs[16];
        env->regs[13] = env->xregs[17];
    } else {
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_IRQ)] = env->xregs[16];
        env->banked_r13[bank_number(ARM_CPU_MODE_IRQ)] = env->xregs[17];
    }

    if (mode == ARM_CPU_MODE_SVC) {
        env->regs[14] = env->xregs[18];
        env->regs[13] = env->xregs[19];
    } else {
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_SVC)] = env->xregs[18];
        env->banked_r13[bank_number(ARM_CPU_MODE_SVC)] = env->xregs[19];
    }

    if (mode == ARM_CPU_MODE_ABT) {
        env->regs[14] = env->xregs[20];
        env->regs[13] = env->xregs[21];
    } else {
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_ABT)] = env->xregs[20];
        env->banked_r13[bank_number(ARM_CPU_MODE_ABT)] = env->xregs[21];
    }

    if (mode == ARM_CPU_MODE_UND) {
        env->regs[14] = env->xregs[22];
        env->regs[13] = env->xregs[23];
    } else {
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_UND)] = env->xregs[22];
        env->banked_r13[bank_number(ARM_CPU_MODE_UND)] = env->xregs[23];
    }

    /* Registers x24-x30 are mapped to r8-r14 in FIQ mode.  If we are in FIQ
     * mode, then we can copy to r8-r14.  Otherwise, we copy to the
     * FIQ bank for r8-r14.
     */
    if (mode == ARM_CPU_MODE_FIQ) {
        for (i = 24; i < 31; i++) {
            env->regs[i - 16] = env->xregs[i];   /* X[24:30] -> R[8:14] */
        }
    } else {
        for (i = 24; i < 29; i++) {
            env->fiq_regs[i - 24] = env->xregs[i];
        }
        env->banked_r13[bank_number(ARM_CPU_MODE_FIQ)] = env->xregs[29];
        env->banked_r14[r14_bank_number(ARM_CPU_MODE_FIQ)] = env->xregs[30];
    }

    env->regs[15] = env->pc;
}

static void take_aarch32_exception(CPUARMState *env, int new_mode,
                                   uint32_t mask, uint32_t offset,
                                   uint32_t newpc)
{
    int new_el;

    /* Change the CPU state so as to actually take the exception. */
    switch_mode(env, new_mode);

    /*
     * For exceptions taken to AArch32 we must clear the SS bit in both
     * PSTATE and in the old-state value we save to SPSR_<mode>, so zero it now.
     */
    env->uncached_cpsr &= ~PSTATE_SS;
    env->spsr = cpsr_read(env);
    /* Clear IT bits.  */
    env->condexec_bits = 0;
    /* Switch to the new mode, and to the correct instruction set.  */
    env->uncached_cpsr = (env->uncached_cpsr & ~CPSR_M) | new_mode;

    /* This must be after mode switching. */
    new_el = arm_current_el(env);

    /* Set new mode endianness */
    env->uncached_cpsr &= ~CPSR_E;
    if (env->cp15.sctlr_el[new_el] & SCTLR_EE) {
        env->uncached_cpsr |= CPSR_E;
    }
    /* J and IL must always be cleared for exception entry */
    env->uncached_cpsr &= ~(CPSR_IL | CPSR_J);
    env->daif |= mask;

    if (new_mode == ARM_CPU_MODE_HYP) {
        env->thumb = (env->cp15.sctlr_el[2] & SCTLR_TE) != 0;
        env->elr_el[2] = env->regs[15];
    } else {
        /* CPSR.PAN is normally preserved preserved unless...  */
        if (cpu_isar_feature(aa32_pan, env_archcpu(env))) {
            switch (new_el) {
            case 3:
                if (!arm_is_secure_below_el3(env)) {
                    /* ... the target is EL3, from non-secure state.  */
                    env->uncached_cpsr &= ~CPSR_PAN;
                    break;
                }
                /* ... the target is EL3, from secure state ... */
                /* fall through */
            case 1:
                /* ... the target is EL1 and SCTLR.SPAN is 0.  */
                if (!(env->cp15.sctlr_el[new_el] & SCTLR_SPAN)) {
                    env->uncached_cpsr |= CPSR_PAN;
                }
                break;
            }
        }
        /*
         * this is a lie, as there was no c1_sys on V4T/V5, but who cares
         * and we should just guard the thumb mode on V4
         */
        if (arm_feature(env, ARM_FEATURE_V4T)) {
            env->thumb =
                (A32_BANKED_CURRENT_REG_GET(env, sctlr) & SCTLR_TE) != 0;
        }
        env->regs[14] = env->regs[15] + offset;
    }
    env->regs[15] = newpc;
    arm_rebuild_hflags(env);
}

static void arm_cpu_do_interrupt_aarch32_hyp(CPUState *cs)
{
    /*
     * Handle exception entry to Hyp mode; this is sufficiently
     * different to entry to other AArch32 modes that we handle it
     * separately here.
     *
     * The vector table entry used is always the 0x14 Hyp mode entry point,
     * unless this is an UNDEF/HVC/abort taken from Hyp to Hyp.
     * The offset applied to the preferred return address is always zero
     * (see DDI0487C.a section G1.12.3).
     * PSTATE A/I/F masks are set based only on the SCR.EA/IRQ/FIQ values.
     */
    uint32_t addr, mask;
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    switch (cs->exception_index) {
    case EXCP_UDEF:
        addr = 0x04;
        break;
    case EXCP_SWI:
        addr = 0x14;
        break;
    case EXCP_BKPT:
        /* Fall through to prefetch abort.  */
    case EXCP_PREFETCH_ABORT:
        env->cp15.ifar_s = env->exception.vaddress;
        qemu_log_mask(CPU_LOG_INT, "...with HIFAR 0x%x\n",
                      (uint32_t)env->exception.vaddress);
        addr = 0x0c;
        break;
    case EXCP_DATA_ABORT:
        env->cp15.dfar_s = env->exception.vaddress;
        qemu_log_mask(CPU_LOG_INT, "...with HDFAR 0x%x\n",
                      (uint32_t)env->exception.vaddress);
        addr = 0x10;
        break;
    case EXCP_IRQ:
        addr = 0x18;
        break;
    case EXCP_FIQ:
        addr = 0x1c;
        break;
    case EXCP_HVC:
        addr = 0x08;
        break;
    case EXCP_HYP_TRAP:
        addr = 0x14;
        break;
    default:
        cpu_abort(cs, "Unhandled exception 0x%x\n", cs->exception_index);
    }

    if (cs->exception_index != EXCP_IRQ && cs->exception_index != EXCP_FIQ) {
        if (!arm_feature(env, ARM_FEATURE_V8)) {
            /*
             * QEMU syndrome values are v8-style. v7 has the IL bit
             * UNK/SBZP for "field not valid" cases, where v8 uses RES1.
             * If this is a v7 CPU, squash the IL bit in those cases.
             */
            if (cs->exception_index == EXCP_PREFETCH_ABORT ||
                (cs->exception_index == EXCP_DATA_ABORT &&
                 !(env->exception.syndrome & ARM_EL_ISV)) ||
                syn_get_ec(env->exception.syndrome) == EC_UNCATEGORIZED) {
                env->exception.syndrome &= ~ARM_EL_IL;
            }
        }
        env->cp15.esr_el[2] = env->exception.syndrome;
    }

    if (arm_current_el(env) != 2 && addr < 0x14) {
        addr = 0x14;
    }

    mask = 0;
    if (!(env->cp15.scr_el3 & SCR_EA)) {
        mask |= CPSR_A;
    }
    if (!(env->cp15.scr_el3 & SCR_IRQ)) {
        mask |= CPSR_I;
    }
    if (!(env->cp15.scr_el3 & SCR_FIQ)) {
        mask |= CPSR_F;
    }

    addr += env->cp15.hvbar;

    take_aarch32_exception(env, ARM_CPU_MODE_HYP, mask, 0, addr);
}

static void arm_cpu_do_interrupt_aarch32(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t addr;
    uint32_t mask;
    int new_mode;
    uint32_t offset;
    uint32_t moe;

    /* If this is a debug exception we must update the DBGDSCR.MOE bits */
    switch (syn_get_ec(env->exception.syndrome)) {
    case EC_BREAKPOINT:
    case EC_BREAKPOINT_SAME_EL:
        moe = 1;
        break;
    case EC_WATCHPOINT:
    case EC_WATCHPOINT_SAME_EL:
        moe = 10;
        break;
    case EC_AA32_BKPT:
        moe = 3;
        break;
    case EC_VECTORCATCH:
        moe = 5;
        break;
    default:
        moe = 0;
        break;
    }

    if (moe) {
        env->cp15.mdscr_el1 = deposit64(env->cp15.mdscr_el1, 2, 4, moe);
    }

    if (env->exception.target_el == 2) {
        arm_cpu_do_interrupt_aarch32_hyp(cs);
        return;
    }

    switch (cs->exception_index) {
    case EXCP_UDEF:
        new_mode = ARM_CPU_MODE_UND;
        addr = 0x04;
        mask = CPSR_I;
        if (env->thumb)
            offset = 2;
        else
            offset = 4;
        break;
    case EXCP_SWI:
        new_mode = ARM_CPU_MODE_SVC;
        addr = 0x08;
        mask = CPSR_I;
        /* The PC already points to the next instruction.  */
        offset = 0;
        break;
    case EXCP_BKPT:
        /* Fall through to prefetch abort.  */
    case EXCP_PREFETCH_ABORT:
        A32_BANKED_CURRENT_REG_SET(env, ifsr, env->exception.fsr);
        A32_BANKED_CURRENT_REG_SET(env, ifar, env->exception.vaddress);
        qemu_log_mask(CPU_LOG_INT, "...with IFSR 0x%x IFAR 0x%x\n",
                      env->exception.fsr, (uint32_t)env->exception.vaddress);
        new_mode = ARM_CPU_MODE_ABT;
        addr = 0x0c;
        mask = CPSR_A | CPSR_I;
        offset = 4;
        break;
    case EXCP_DATA_ABORT:
        A32_BANKED_CURRENT_REG_SET(env, dfsr, env->exception.fsr);
        A32_BANKED_CURRENT_REG_SET(env, dfar, env->exception.vaddress);
        qemu_log_mask(CPU_LOG_INT, "...with DFSR 0x%x DFAR 0x%x\n",
                      env->exception.fsr,
                      (uint32_t)env->exception.vaddress);
        new_mode = ARM_CPU_MODE_ABT;
        addr = 0x10;
        mask = CPSR_A | CPSR_I;
        offset = 8;
        break;
    case EXCP_IRQ:
        new_mode = ARM_CPU_MODE_IRQ;
        addr = 0x18;
        /* Disable IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I;
        offset = 4;
        if (env->cp15.scr_el3 & SCR_IRQ) {
            /* IRQ routed to monitor mode */
            new_mode = ARM_CPU_MODE_MON;
            mask |= CPSR_F;
        }
        break;
    case EXCP_FIQ:
        new_mode = ARM_CPU_MODE_FIQ;
        addr = 0x1c;
        /* Disable FIQ, IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I | CPSR_F;
        if (env->cp15.scr_el3 & SCR_FIQ) {
            /* FIQ routed to monitor mode */
            new_mode = ARM_CPU_MODE_MON;
        }
        offset = 4;
        break;
    case EXCP_VIRQ:
        new_mode = ARM_CPU_MODE_IRQ;
        addr = 0x18;
        /* Disable IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I;
        offset = 4;
        break;
    case EXCP_VFIQ:
        new_mode = ARM_CPU_MODE_FIQ;
        addr = 0x1c;
        /* Disable FIQ, IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I | CPSR_F;
        offset = 4;
        break;
    case EXCP_SMC:
        new_mode = ARM_CPU_MODE_MON;
        addr = 0x08;
        mask = CPSR_A | CPSR_I | CPSR_F;
        offset = 0;
        break;
    default:
        cpu_abort(cs, "Unhandled exception 0x%x\n", cs->exception_index);
        return; /* Never happens.  Keep compiler happy.  */
    }

    if (new_mode == ARM_CPU_MODE_MON) {
        addr += env->cp15.mvbar;
    } else if (A32_BANKED_CURRENT_REG_GET(env, sctlr) & SCTLR_V) {
        /* High vectors. When enabled, base address cannot be remapped. */
        addr += 0xffff0000;
    } else {
        /* ARM v7 architectures provide a vector base address register to remap
         * the interrupt vector table.
         * This register is only followed in non-monitor mode, and is banked.
         * Note: only bits 31:5 are valid.
         */
        addr += A32_BANKED_CURRENT_REG_GET(env, vbar);
    }

    if ((env->uncached_cpsr & CPSR_M) == ARM_CPU_MODE_MON) {
        env->cp15.scr_el3 &= ~SCR_NS;
    }

    take_aarch32_exception(env, new_mode, mask, offset, addr);
}

static int aarch64_regnum(CPUARMState *env, int aarch32_reg)
{
    /*
     * Return the register number of the AArch64 view of the AArch32
     * register @aarch32_reg. The CPUARMState CPSR is assumed to still
     * be that of the AArch32 mode the exception came from.
     */
    int mode = env->uncached_cpsr & CPSR_M;

    switch (aarch32_reg) {
    case 0 ... 7:
        return aarch32_reg;
    case 8 ... 12:
        return mode == ARM_CPU_MODE_FIQ ? aarch32_reg + 16 : aarch32_reg;
    case 13:
        switch (mode) {
        case ARM_CPU_MODE_USR:
        case ARM_CPU_MODE_SYS:
            return 13;
        case ARM_CPU_MODE_HYP:
            return 15;
        case ARM_CPU_MODE_IRQ:
            return 17;
        case ARM_CPU_MODE_SVC:
            return 19;
        case ARM_CPU_MODE_ABT:
            return 21;
        case ARM_CPU_MODE_UND:
            return 23;
        case ARM_CPU_MODE_FIQ:
            return 29;
        default:
            g_assert_not_reached();
        }
    case 14:
        switch (mode) {
        case ARM_CPU_MODE_USR:
        case ARM_CPU_MODE_SYS:
        case ARM_CPU_MODE_HYP:
            return 14;
        case ARM_CPU_MODE_IRQ:
            return 16;
        case ARM_CPU_MODE_SVC:
            return 18;
        case ARM_CPU_MODE_ABT:
            return 20;
        case ARM_CPU_MODE_UND:
            return 22;
        case ARM_CPU_MODE_FIQ:
            return 30;
        default:
            g_assert_not_reached();
        }
    case 15:
        return 31;
    default:
        g_assert_not_reached();
    }
}

/* Handle exception entry to a target EL which is using AArch64 */
static void arm_cpu_do_interrupt_aarch64(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    unsigned int new_el = env->exception.target_el;
    target_ulong addr = env->cp15.vbar_el[new_el];
    unsigned int new_mode = aarch64_pstate_mode(new_el, true);
    unsigned int old_mode;
    unsigned int cur_el = arm_current_el(env);
    int rt;

    /*
     * Note that new_el can never be 0.  If cur_el is 0, then
     * el0_a64 is is_a64(), else el0_a64 is ignored.
     */
    aarch64_sve_change_el(env, cur_el, new_el, is_a64(env));

    if (cur_el < new_el) {
        /* Entry vector offset depends on whether the implemented EL
         * immediately lower than the target level is using AArch32 or AArch64
         */
        bool is_aa64;
        uint64_t hcr;

        switch (new_el) {
        case 3:
            is_aa64 = (env->cp15.scr_el3 & SCR_RW) != 0;
            break;
        case 2:
            hcr = arm_hcr_el2_eff(env);
            if ((hcr & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
                is_aa64 = (hcr & HCR_RW) != 0;
                break;
            }
            /* fall through */
        case 1:
            is_aa64 = is_a64(env);
            break;
        default:
            g_assert_not_reached();
        }

        if (is_aa64) {
            addr += 0x400;
        } else {
            addr += 0x600;
        }
    } else if (pstate_read(env) & PSTATE_SP) {
        addr += 0x200;
    }

    switch (cs->exception_index) {
    case EXCP_PREFETCH_ABORT:
    case EXCP_DATA_ABORT:
        env->cp15.far_el[new_el] = env->exception.vaddress;
        qemu_log_mask(CPU_LOG_INT, "...with FAR 0x%" PRIx64 "\n",
                      env->cp15.far_el[new_el]);
        /* fall through */
    case EXCP_BKPT:
    case EXCP_UDEF:
    case EXCP_SWI:
    case EXCP_HVC:
    case EXCP_HYP_TRAP:
    case EXCP_SMC:
        switch (syn_get_ec(env->exception.syndrome)) {
        case EC_ADVSIMDFPACCESSTRAP:
            /*
             * QEMU internal FP/SIMD syndromes from AArch32 include the
             * TA and coproc fields which are only exposed if the exception
             * is taken to AArch32 Hyp mode. Mask them out to get a valid
             * AArch64 format syndrome.
             */
            env->exception.syndrome &= ~MAKE_64BIT_MASK(0, 20);
            break;
        case EC_CP14RTTRAP:
        case EC_CP15RTTRAP:
        case EC_CP14DTTRAP:
            /*
             * For a trap on AArch32 MRC/MCR/LDC/STC the Rt field is currently
             * the raw register field from the insn; when taking this to
             * AArch64 we must convert it to the AArch64 view of the register
             * number. Notice that we read a 4-bit AArch32 register number and
             * write back a 5-bit AArch64 one.
             */
            rt = extract32(env->exception.syndrome, 5, 4);
            rt = aarch64_regnum(env, rt);
            env->exception.syndrome = deposit32(env->exception.syndrome,
                                                5, 5, rt);
            break;
        case EC_CP15RRTTRAP:
        case EC_CP14RRTTRAP:
            /* Similarly for MRRC/MCRR traps for Rt and Rt2 fields */
            rt = extract32(env->exception.syndrome, 5, 4);
            rt = aarch64_regnum(env, rt);
            env->exception.syndrome = deposit32(env->exception.syndrome,
                                                5, 5, rt);
            rt = extract32(env->exception.syndrome, 10, 4);
            rt = aarch64_regnum(env, rt);
            env->exception.syndrome = deposit32(env->exception.syndrome,
                                                10, 5, rt);
            break;
        }
        env->cp15.esr_el[new_el] = env->exception.syndrome;
        break;
    case EXCP_IRQ:
    case EXCP_VIRQ:
        addr += 0x80;
        break;
    case EXCP_FIQ:
    case EXCP_VFIQ:
        addr += 0x100;
        break;
    default:
        cpu_abort(cs, "Unhandled exception 0x%x\n", cs->exception_index);
    }

    if (is_a64(env)) {
        old_mode = pstate_read(env);
        aarch64_save_sp(env, arm_current_el(env));
        env->elr_el[new_el] = env->pc;
    } else {
        old_mode = cpsr_read(env);
        env->elr_el[new_el] = env->regs[15];

        aarch64_sync_32_to_64(env);

        env->condexec_bits = 0;
    }
    env->banked_spsr[aarch64_banked_spsr_index(new_el)] = old_mode;

    qemu_log_mask(CPU_LOG_INT, "...with ELR 0x%" PRIx64 "\n",
                  env->elr_el[new_el]);

    if (cpu_isar_feature(aa64_pan, cpu)) {
        /* The value of PSTATE.PAN is normally preserved, except when ... */
        new_mode |= old_mode & PSTATE_PAN;
        switch (new_el) {
        case 2:
            /* ... the target is EL2 with HCR_EL2.{E2H,TGE} == '11' ...  */
            if ((arm_hcr_el2_eff(env) & (HCR_E2H | HCR_TGE))
                != (HCR_E2H | HCR_TGE)) {
                break;
            }
            /* fall through */
        case 1:
            /* ... the target is EL1 ... */
            /* ... and SCTLR_ELx.SPAN == 0, then set to 1.  */
            if ((env->cp15.sctlr_el[new_el] & SCTLR_SPAN) == 0) {
                new_mode |= PSTATE_PAN;
            }
            break;
        }
    }
    if (cpu_isar_feature(aa64_mte, cpu)) {
        new_mode |= PSTATE_TCO;
    }

    pstate_write(env, PSTATE_DAIF | new_mode);
    env->aarch64 = 1;
    aarch64_restore_sp(env, new_el);
    helper_rebuild_hflags_a64(env, new_el);

    env->pc = addr;

    qemu_log_mask(CPU_LOG_INT, "...to EL%d PC 0x%" PRIx64 " PSTATE 0x%x\n",
                  new_el, env->pc, pstate_read(env));
}

/*
 * Do semihosting call and set the appropriate return value. All the
 * permission and validity checks have been done at translate time.
 *
 * We only see semihosting exceptions in TCG only as they are not
 * trapped to the hypervisor in KVM.
 */
#ifdef CONFIG_TCG
static void handle_semihosting(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;

    if (is_a64(env)) {
        qemu_log_mask(CPU_LOG_INT,
                      "...handling as semihosting call 0x%" PRIx64 "\n",
                      env->xregs[0]);
        env->xregs[0] = do_arm_semihosting(env);
        env->pc += 4;
    } else {
        qemu_log_mask(CPU_LOG_INT,
                      "...handling as semihosting call 0x%x\n",
                      env->regs[0]);
        env->regs[0] = do_arm_semihosting(env);
        env->regs[15] += env->thumb ? 2 : 4;
    }
}
#endif

/* Handle a CPU exception for A and R profile CPUs.
 * Do any appropriate logging, handle PSCI calls, and then hand off
 * to the AArch64-entry or AArch32-entry function depending on the
 * target exception level's register width.
 */
void arm_cpu_do_interrupt(CPUState *cs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    unsigned int new_el = env->exception.target_el;

    assert(!arm_feature(env, ARM_FEATURE_M));

    arm_log_exception(cs->exception_index);
    qemu_log_mask(CPU_LOG_INT, "...from EL%d to EL%d\n", arm_current_el(env),
                  new_el);
    if (qemu_loglevel_mask(CPU_LOG_INT)
        && !excp_is_internal(cs->exception_index)) {
        qemu_log_mask(CPU_LOG_INT, "...with ESR 0x%x/0x%" PRIx32 "\n",
                      syn_get_ec(env->exception.syndrome),
                      env->exception.syndrome);
    }

    if (arm_is_psci_call(cpu, cs->exception_index)) {
        arm_handle_psci_call(cpu);
        qemu_log_mask(CPU_LOG_INT, "...handled as PSCI call\n");
        return;
    }

    /*
     * Semihosting semantics depend on the register width of the code
     * that caused the exception, not the target exception level, so
     * must be handled here.
     */
#ifdef CONFIG_TCG
    if (cs->exception_index == EXCP_SEMIHOST) {
        handle_semihosting(cs);
        return;
    }
#endif

    /* Hooks may change global state so BQL should be held, also the
     * BQL needs to be held for any modification of
     * cs->interrupt_request.
     */
    g_assert(qemu_mutex_iothread_locked());

    arm_call_pre_el_change_hook(cpu);

    assert(!excp_is_internal(cs->exception_index));
    if (arm_el_is_aa64(env, new_el)) {
        arm_cpu_do_interrupt_aarch64(cs);
    } else {
        arm_cpu_do_interrupt_aarch32(cs);
    }

    arm_call_el_change_hook(cpu);

    if (!kvm_enabled()) {
        cs->interrupt_request |= CPU_INTERRUPT_EXITTB;
    }
}
#endif /* !CONFIG_USER_ONLY */

uint64_t arm_sctlr(CPUARMState *env, int el)
{
    /* Only EL0 needs to be adjusted for EL1&0 or EL2&0. */
    if (el == 0) {
        ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, 0);
        el = (mmu_idx == ARMMMUIdx_E20_0 ? 2 : 1);
    }
    return env->cp15.sctlr_el[el];
}

/* Return the SCTLR value which controls this address translation regime */
static inline uint64_t regime_sctlr(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    return env->cp15.sctlr_el[regime_el(env, mmu_idx)];
}

#ifndef CONFIG_USER_ONLY

/* Return true if the specified stage of address translation is disabled */
static inline bool regime_translation_disabled(CPUARMState *env,
                                               ARMMMUIdx mmu_idx)
{
    if (arm_feature(env, ARM_FEATURE_M)) {
        switch (env->v7m.mpu_ctrl[regime_is_secure(env, mmu_idx)] &
                (R_V7M_MPU_CTRL_ENABLE_MASK | R_V7M_MPU_CTRL_HFNMIENA_MASK)) {
        case R_V7M_MPU_CTRL_ENABLE_MASK:
            /* Enabled, but not for HardFault and NMI */
            return mmu_idx & ARM_MMU_IDX_M_NEGPRI;
        case R_V7M_MPU_CTRL_ENABLE_MASK | R_V7M_MPU_CTRL_HFNMIENA_MASK:
            /* Enabled for all cases */
            return false;
        case 0:
        default:
            /* HFNMIENA set and ENABLE clear is UNPREDICTABLE, but
             * we warned about that in armv7m_nvic.c when the guest set it.
             */
            return true;
        }
    }

    if (mmu_idx == ARMMMUIdx_Stage2) {
        /* HCR.DC means HCR.VM behaves as 1 */
        return (env->cp15.hcr_el2 & (HCR_DC | HCR_VM)) == 0;
    }

    if (env->cp15.hcr_el2 & HCR_TGE) {
        /* TGE means that NS EL0/1 act as if SCTLR_EL1.M is zero */
        if (!regime_is_secure(env, mmu_idx) && regime_el(env, mmu_idx) == 1) {
            return true;
        }
    }

    if ((env->cp15.hcr_el2 & HCR_DC) && arm_mmu_idx_is_stage1_of_2(mmu_idx)) {
        /* HCR.DC means SCTLR_EL1.M behaves as 0 */
        return true;
    }

    return (regime_sctlr(env, mmu_idx) & SCTLR_M) == 0;
}

static inline bool regime_translation_big_endian(CPUARMState *env,
                                                 ARMMMUIdx mmu_idx)
{
    return (regime_sctlr(env, mmu_idx) & SCTLR_EE) != 0;
}

/* Return the TTBR associated with this translation regime */
static inline uint64_t regime_ttbr(CPUARMState *env, ARMMMUIdx mmu_idx,
                                   int ttbrn)
{
    if (mmu_idx == ARMMMUIdx_Stage2) {
        return env->cp15.vttbr_el2;
    }
    if (ttbrn == 0) {
        return env->cp15.ttbr0_el[regime_el(env, mmu_idx)];
    } else {
        return env->cp15.ttbr1_el[regime_el(env, mmu_idx)];
    }
}

#endif /* !CONFIG_USER_ONLY */

/* Convert a possible stage1+2 MMU index into the appropriate
 * stage 1 MMU index
 */
static inline ARMMMUIdx stage_1_mmu_idx(ARMMMUIdx mmu_idx)
{
    switch (mmu_idx) {
    case ARMMMUIdx_E10_0:
        return ARMMMUIdx_Stage1_E0;
    case ARMMMUIdx_E10_1:
        return ARMMMUIdx_Stage1_E1;
    case ARMMMUIdx_E10_1_PAN:
        return ARMMMUIdx_Stage1_E1_PAN;
    default:
        return mmu_idx;
    }
}

/* Return true if the translation regime is using LPAE format page tables */
static inline bool regime_using_lpae_format(CPUARMState *env,
                                            ARMMMUIdx mmu_idx)
{
    int el = regime_el(env, mmu_idx);
    if (el == 2 || arm_el_is_aa64(env, el)) {
        return true;
    }
    if (arm_feature(env, ARM_FEATURE_LPAE)
        && (regime_tcr(env, mmu_idx)->raw_tcr & TTBCR_EAE)) {
        return true;
    }
    return false;
}

/* Returns true if the stage 1 translation regime is using LPAE format page
 * tables. Used when raising alignment exceptions, whose FSR changes depending
 * on whether the long or short descriptor format is in use. */
bool arm_s1_regime_using_lpae_format(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    mmu_idx = stage_1_mmu_idx(mmu_idx);

    return regime_using_lpae_format(env, mmu_idx);
}

#ifndef CONFIG_USER_ONLY
static inline bool regime_is_user(CPUARMState *env, ARMMMUIdx mmu_idx)
{
    switch (mmu_idx) {
    case ARMMMUIdx_SE10_0:
    case ARMMMUIdx_E20_0:
    case ARMMMUIdx_Stage1_E0:
    case ARMMMUIdx_MUser:
    case ARMMMUIdx_MSUser:
    case ARMMMUIdx_MUserNegPri:
    case ARMMMUIdx_MSUserNegPri:
        return true;
    default:
        return false;
    case ARMMMUIdx_E10_0:
    case ARMMMUIdx_E10_1:
    case ARMMMUIdx_E10_1_PAN:
        g_assert_not_reached();
    }
}

/* Translate section/page access permissions to page
 * R/W protection flags
 *
 * @env:         CPUARMState
 * @mmu_idx:     MMU index indicating required translation regime
 * @ap:          The 3-bit access permissions (AP[2:0])
 * @domain_prot: The 2-bit domain access permissions
 */
static inline int ap_to_rw_prot(CPUARMState *env, ARMMMUIdx mmu_idx,
                                int ap, int domain_prot)
{
    bool is_user = regime_is_user(env, mmu_idx);

    if (domain_prot == 3) {
        return PAGE_READ | PAGE_WRITE;
    }

    switch (ap) {
    case 0:
        if (arm_feature(env, ARM_FEATURE_V7)) {
            return 0;
        }
        switch (regime_sctlr(env, mmu_idx) & (SCTLR_S | SCTLR_R)) {
        case SCTLR_S:
            return is_user ? 0 : PAGE_READ;
        case SCTLR_R:
            return PAGE_READ;
        default:
            return 0;
        }
    case 1:
        return is_user ? 0 : PAGE_READ | PAGE_WRITE;
    case 2:
        if (is_user) {
            return PAGE_READ;
        } else {
            return PAGE_READ | PAGE_WRITE;
        }
    case 3:
        return PAGE_READ | PAGE_WRITE;
    case 4: /* Reserved.  */
        return 0;
    case 5:
        return is_user ? 0 : PAGE_READ;
    case 6:
        return PAGE_READ;
    case 7:
        if (!arm_feature(env, ARM_FEATURE_V6K)) {
            return 0;
        }
        return PAGE_READ;
    default:
        g_assert_not_reached();
    }
}

/* Translate section/page access permissions to page
 * R/W protection flags.
 *
 * @ap:      The 2-bit simple AP (AP[2:1])
 * @is_user: TRUE if accessing from PL0
 */
static inline int simple_ap_to_rw_prot_is_user(int ap, bool is_user)
{
    switch (ap) {
    case 0:
        return is_user ? 0 : PAGE_READ | PAGE_WRITE;
    case 1:
        return PAGE_READ | PAGE_WRITE;
    case 2:
        return is_user ? 0 : PAGE_READ;
    case 3:
        return PAGE_READ;
    default:
        g_assert_not_reached();
    }
}

static inline int
simple_ap_to_rw_prot(CPUARMState *env, ARMMMUIdx mmu_idx, int ap)
{
    return simple_ap_to_rw_prot_is_user(ap, regime_is_user(env, mmu_idx));
}

/* Translate S2 section/page access permissions to protection flags
 *
 * @env:     CPUARMState
 * @s2ap:    The 2-bit stage2 access permissions (S2AP)
 * @xn:      XN (execute-never) bits
 * @s1_is_el0: true if this is S2 of an S1+2 walk for EL0
 */
static int get_S2prot(CPUARMState *env, int s2ap, int xn, bool s1_is_el0)
{
    int prot = 0;

    if (s2ap & 1) {
        prot |= PAGE_READ;
    }
    if (s2ap & 2) {
        prot |= PAGE_WRITE;
    }

    if (cpu_isar_feature(any_tts2uxn, env_archcpu(env))) {
        switch (xn) {
        case 0:
            prot |= PAGE_EXEC;
            break;
        case 1:
            if (s1_is_el0) {
                prot |= PAGE_EXEC;
            }
            break;
        case 2:
            break;
        case 3:
            if (!s1_is_el0) {
                prot |= PAGE_EXEC;
            }
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        if (!extract32(xn, 1, 1)) {
            if (arm_el_is_aa64(env, 2) || prot & PAGE_READ) {
                prot |= PAGE_EXEC;
            }
        }
    }
    return prot;
}

/* Translate section/page access permissions to protection flags
 *
 * @env:     CPUARMState
 * @mmu_idx: MMU index indicating required translation regime
 * @is_aa64: TRUE if AArch64
 * @ap:      The 2-bit simple AP (AP[2:1])
 * @ns:      NS (non-secure) bit
 * @xn:      XN (execute-never) bit
 * @pxn:     PXN (privileged execute-never) bit
 */
static int get_S1prot(CPUARMState *env, ARMMMUIdx mmu_idx, bool is_aa64,
                      int ap, int ns, int xn, int pxn)
{
    bool is_user = regime_is_user(env, mmu_idx);
    int prot_rw, user_rw;
    bool have_wxn;
    int wxn = 0;

    assert(mmu_idx != ARMMMUIdx_Stage2);

    user_rw = simple_ap_to_rw_prot_is_user(ap, true);
    if (is_user) {
        prot_rw = user_rw;
    } else {
        if (user_rw && regime_is_pan(env, mmu_idx)) {
            /* PAN forbids data accesses but doesn't affect insn fetch */
            prot_rw = 0;
        } else {
            prot_rw = simple_ap_to_rw_prot_is_user(ap, false);
        }
    }

    if (ns && arm_is_secure(env) && (env->cp15.scr_el3 & SCR_SIF)) {
        return prot_rw;
    }

    /* TODO have_wxn should be replaced with
     *   ARM_FEATURE_V8 || (ARM_FEATURE_V7 && ARM_FEATURE_EL2)
     * when ARM_FEATURE_EL2 starts getting set. For now we assume all LPAE
     * compatible processors have EL2, which is required for [U]WXN.
     */
    have_wxn = arm_feature(env, ARM_FEATURE_LPAE);

    if (have_wxn) {
        wxn = regime_sctlr(env, mmu_idx) & SCTLR_WXN;
    }

    if (is_aa64) {
        if (regime_has_2_ranges(mmu_idx) && !is_user) {
            xn = pxn || (user_rw & PAGE_WRITE);
        }
    } else if (arm_feature(env, ARM_FEATURE_V7)) {
        switch (regime_el(env, mmu_idx)) {
        case 1:
        case 3:
            if (is_user) {
                xn = xn || !(user_rw & PAGE_READ);
            } else {
                int uwxn = 0;
                if (have_wxn) {
                    uwxn = regime_sctlr(env, mmu_idx) & SCTLR_UWXN;
                }
                xn = xn || !(prot_rw & PAGE_READ) || pxn ||
                     (uwxn && (user_rw & PAGE_WRITE));
            }
            break;
        case 2:
            break;
        }
    } else {
        xn = wxn = 0;
    }

    if (xn || (wxn && (prot_rw & PAGE_WRITE))) {
        return prot_rw;
    }
    return prot_rw | PAGE_EXEC;
}

static bool get_level1_table_address(CPUARMState *env, ARMMMUIdx mmu_idx,
                                     uint32_t *table, uint32_t address)
{
    /* Note that we can only get here for an AArch32 PL0/PL1 lookup */
    TCR *tcr = regime_tcr(env, mmu_idx);

    if (address & tcr->mask) {
        if (tcr->raw_tcr & TTBCR_PD1) {
            /* Translation table walk disabled for TTBR1 */
            return false;
        }
        *table = regime_ttbr(env, mmu_idx, 1) & 0xffffc000;
    } else {
        if (tcr->raw_tcr & TTBCR_PD0) {
            /* Translation table walk disabled for TTBR0 */
            return false;
        }
        *table = regime_ttbr(env, mmu_idx, 0) & tcr->base_mask;
    }
    *table |= (address >> 18) & 0x3ffc;
    return true;
}

/* Translate a S1 pagetable walk through S2 if needed.  */
static hwaddr S1_ptw_translate(CPUARMState *env, ARMMMUIdx mmu_idx,
                               hwaddr addr, MemTxAttrs txattrs,
                               ARMMMUFaultInfo *fi)
{
    if (arm_mmu_idx_is_stage1_of_2(mmu_idx) &&
        !regime_translation_disabled(env, ARMMMUIdx_Stage2)) {
        target_ulong s2size;
        hwaddr s2pa;
        int s2prot;
        int ret;
        ARMCacheAttrs cacheattrs = {};

        ret = get_phys_addr_lpae(env, addr, MMU_DATA_LOAD, ARMMMUIdx_Stage2,
                                 false,
                                 &s2pa, &txattrs, &s2prot, &s2size, fi,
                                 &cacheattrs);
        if (ret) {
            assert(fi->type != ARMFault_None);
            fi->s2addr = addr;
            fi->stage2 = true;
            fi->s1ptw = true;
            return ~0;
        }
        if ((env->cp15.hcr_el2 & HCR_PTW) && (cacheattrs.attrs & 0xf0) == 0) {
            /*
             * PTW set and S1 walk touched S2 Device memory:
             * generate Permission fault.
             */
            fi->type = ARMFault_Permission;
            fi->s2addr = addr;
            fi->stage2 = true;
            fi->s1ptw = true;
            return ~0;
        }
        addr = s2pa;
    }
    return addr;
}

/* All loads done in the course of a page table walk go through here. */
static uint32_t arm_ldl_ptw(CPUState *cs, hwaddr addr, bool is_secure,
                            ARMMMUIdx mmu_idx, ARMMMUFaultInfo *fi)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    MemTxAttrs attrs = {};
    MemTxResult result = MEMTX_OK;
    AddressSpace *as;
    uint32_t data;

    attrs.secure = is_secure;
    as = arm_addressspace(cs, attrs);
    addr = S1_ptw_translate(env, mmu_idx, addr, attrs, fi);
    if (fi->s1ptw) {
        return 0;
    }
    if (regime_translation_big_endian(env, mmu_idx)) {
        data = address_space_ldl_be(as, addr, attrs, &result);
    } else {
        data = address_space_ldl_le(as, addr, attrs, &result);
    }
    if (result == MEMTX_OK) {
        return data;
    }
    fi->type = ARMFault_SyncExternalOnWalk;
    fi->ea = arm_extabort_type(result);
    return 0;
}

static uint64_t arm_ldq_ptw(CPUState *cs, hwaddr addr, bool is_secure,
                            ARMMMUIdx mmu_idx, ARMMMUFaultInfo *fi)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    MemTxAttrs attrs = {};
    MemTxResult result = MEMTX_OK;
    AddressSpace *as;
    uint64_t data;

    attrs.secure = is_secure;
    as = arm_addressspace(cs, attrs);
    addr = S1_ptw_translate(env, mmu_idx, addr, attrs, fi);
    if (fi->s1ptw) {
        return 0;
    }
    if (regime_translation_big_endian(env, mmu_idx)) {
        data = address_space_ldq_be(as, addr, attrs, &result);
    } else {
        data = address_space_ldq_le(as, addr, attrs, &result);
    }
    if (result == MEMTX_OK) {
        return data;
    }
    fi->type = ARMFault_SyncExternalOnWalk;
    fi->ea = arm_extabort_type(result);
    return 0;
}

static bool get_phys_addr_v5(CPUARMState *env, uint32_t address,
                             MMUAccessType access_type, ARMMMUIdx mmu_idx,
                             hwaddr *phys_ptr, int *prot,
                             target_ulong *page_size,
                             ARMMMUFaultInfo *fi)
{
    CPUState *cs = env_cpu(env);
    int level = 1;
    uint32_t table;
    uint32_t desc;
    int type;
    int ap;
    int domain = 0;
    int domain_prot;
    hwaddr phys_addr;
    uint32_t dacr;

    /* Pagetable walk.  */
    /* Lookup l1 descriptor.  */
    if (!get_level1_table_address(env, mmu_idx, &table, address)) {
        /* Section translation fault if page walk is disabled by PD0 or PD1 */
        fi->type = ARMFault_Translation;
        goto do_fault;
    }
    desc = arm_ldl_ptw(cs, table, regime_is_secure(env, mmu_idx),
                       mmu_idx, fi);
    if (fi->type != ARMFault_None) {
        goto do_fault;
    }
    type = (desc & 3);
    domain = (desc >> 5) & 0x0f;
    if (regime_el(env, mmu_idx) == 1) {
        dacr = env->cp15.dacr_ns;
    } else {
        dacr = env->cp15.dacr_s;
    }
    domain_prot = (dacr >> (domain * 2)) & 3;
    if (type == 0) {
        /* Section translation fault.  */
        fi->type = ARMFault_Translation;
        goto do_fault;
    }
    if (type != 2) {
        level = 2;
    }
    if (domain_prot == 0 || domain_prot == 2) {
        fi->type = ARMFault_Domain;
        goto do_fault;
    }
    if (type == 2) {
        /* 1Mb section.  */
        phys_addr = (desc & 0xfff00000) | (address & 0x000fffff);
        ap = (desc >> 10) & 3;
        *page_size = 1024 * 1024;
    } else {
        /* Lookup l2 entry.  */
        if (type == 1) {
            /* Coarse pagetable.  */
            table = (desc & 0xfffffc00) | ((address >> 10) & 0x3fc);
        } else {
            /* Fine pagetable.  */
            table = (desc & 0xfffff000) | ((address >> 8) & 0xffc);
        }
        desc = arm_ldl_ptw(cs, table, regime_is_secure(env, mmu_idx),
                           mmu_idx, fi);
        if (fi->type != ARMFault_None) {
            goto do_fault;
        }
        switch (desc & 3) {
        case 0: /* Page translation fault.  */
            fi->type = ARMFault_Translation;
            goto do_fault;
        case 1: /* 64k page.  */
            phys_addr = (desc & 0xffff0000) | (address & 0xffff);
            ap = (desc >> (4 + ((address >> 13) & 6))) & 3;
            *page_size = 0x10000;
            break;
        case 2: /* 4k page.  */
            phys_addr = (desc & 0xfffff000) | (address & 0xfff);
            ap = (desc >> (4 + ((address >> 9) & 6))) & 3;
            *page_size = 0x1000;
            break;
        case 3: /* 1k page, or ARMv6/XScale "extended small (4k) page" */
            if (type == 1) {
                /* ARMv6/XScale extended small page format */
                if (arm_feature(env, ARM_FEATURE_XSCALE)
                    || arm_feature(env, ARM_FEATURE_V6)) {
                    phys_addr = (desc & 0xfffff000) | (address & 0xfff);
                    *page_size = 0x1000;
                } else {
                    /* UNPREDICTABLE in ARMv5; we choose to take a
                     * page translation fault.
                     */
                    fi->type = ARMFault_Translation;
                    goto do_fault;
                }
            } else {
                phys_addr = (desc & 0xfffffc00) | (address & 0x3ff);
                *page_size = 0x400;
            }
            ap = (desc >> 4) & 3;
            break;
        default:
            /* Never happens, but compiler isn't smart enough to tell.  */
            abort();
        }
    }
    *prot = ap_to_rw_prot(env, mmu_idx, ap, domain_prot);
    *prot |= *prot ? PAGE_EXEC : 0;
    if (!(*prot & (1 << access_type))) {
        /* Access permission fault.  */
        fi->type = ARMFault_Permission;
        goto do_fault;
    }
    *phys_ptr = phys_addr;
    return false;
do_fault:
    fi->domain = domain;
    fi->level = level;
    return true;
}

static bool get_phys_addr_v6(CPUARMState *env, uint32_t address,
                             MMUAccessType access_type, ARMMMUIdx mmu_idx,
                             hwaddr *phys_ptr, MemTxAttrs *attrs, int *prot,
                             target_ulong *page_size, ARMMMUFaultInfo *fi)
{
    CPUState *cs = env_cpu(env);
    int level = 1;
    uint32_t table;
    uint32_t desc;
    uint32_t xn;
    uint32_t pxn = 0;
    int type;
    int ap;
    int domain = 0;
    int domain_prot;
    hwaddr phys_addr;
    uint32_t dacr;
    bool ns;

    /* Pagetable walk.  */
    /* Lookup l1 descriptor.  */
    if (!get_level1_table_address(env, mmu_idx, &table, address)) {
        /* Section translation fault if page walk is disabled by PD0 or PD1 */
        fi->type = ARMFault_Translation;
        goto do_fault;
    }
    desc = arm_ldl_ptw(cs, table, regime_is_secure(env, mmu_idx),
                       mmu_idx, fi);
    if (fi->type != ARMFault_None) {
        goto do_fault;
    }
    type = (desc & 3);
    if (type == 0 || (type == 3 && !arm_feature(env, ARM_FEATURE_PXN))) {
        /* Section translation fault, or attempt to use the encoding
         * which is Reserved on implementations without PXN.
         */
        fi->type = ARMFault_Translation;
        goto do_fault;
    }
    if ((type == 1) || !(desc & (1 << 18))) {
        /* Page or Section.  */
        domain = (desc >> 5) & 0x0f;
    }
    if (regime_el(env, mmu_idx) == 1) {
        dacr = env->cp15.dacr_ns;
    } else {
        dacr = env->cp15.dacr_s;
    }
    if (type == 1) {
        level = 2;
    }
    domain_prot = (dacr >> (domain * 2)) & 3;
    if (domain_prot == 0 || domain_prot == 2) {
        /* Section or Page domain fault */
        fi->type = ARMFault_Domain;
        goto do_fault;
    }
    if (type != 1) {
        if (desc & (1 << 18)) {
            /* Supersection.  */
            phys_addr = (desc & 0xff000000) | (address & 0x00ffffff);
            phys_addr |= (uint64_t)extract32(desc, 20, 4) << 32;
            phys_addr |= (uint64_t)extract32(desc, 5, 4) << 36;
            *page_size = 0x1000000;
        } else {
            /* Section.  */
            phys_addr = (desc & 0xfff00000) | (address & 0x000fffff);
            *page_size = 0x100000;
        }
        ap = ((desc >> 10) & 3) | ((desc >> 13) & 4);
        xn = desc & (1 << 4);
        pxn = desc & 1;
        ns = extract32(desc, 19, 1);
    } else {
        if (arm_feature(env, ARM_FEATURE_PXN)) {
            pxn = (desc >> 2) & 1;
        }
        ns = extract32(desc, 3, 1);
        /* Lookup l2 entry.  */
        table = (desc & 0xfffffc00) | ((address >> 10) & 0x3fc);
        desc = arm_ldl_ptw(cs, table, regime_is_secure(env, mmu_idx),
                           mmu_idx, fi);
        if (fi->type != ARMFault_None) {
            goto do_fault;
        }
        ap = ((desc >> 4) & 3) | ((desc >> 7) & 4);
        switch (desc & 3) {
        case 0: /* Page translation fault.  */
            fi->type = ARMFault_Translation;
            goto do_fault;
        case 1: /* 64k page.  */
            phys_addr = (desc & 0xffff0000) | (address & 0xffff);
            xn = desc & (1 << 15);
            *page_size = 0x10000;
            break;
        case 2: case 3: /* 4k page.  */
            phys_addr = (desc & 0xfffff000) | (address & 0xfff);
            xn = desc & 1;
            *page_size = 0x1000;
            break;
        default:
            /* Never happens, but compiler isn't smart enough to tell.  */
            abort();
        }
    }
    if (domain_prot == 3) {
        *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    } else {
        if (pxn && !regime_is_user(env, mmu_idx)) {
            xn = 1;
        }
        if (xn && access_type == MMU_INST_FETCH) {
            fi->type = ARMFault_Permission;
            goto do_fault;
        }

        if (arm_feature(env, ARM_FEATURE_V6K) &&
                (regime_sctlr(env, mmu_idx) & SCTLR_AFE)) {
            /* The simplified model uses AP[0] as an access control bit.  */
            if ((ap & 1) == 0) {
                /* Access flag fault.  */
                fi->type = ARMFault_AccessFlag;
                goto do_fault;
            }
            *prot = simple_ap_to_rw_prot(env, mmu_idx, ap >> 1);
        } else {
            *prot = ap_to_rw_prot(env, mmu_idx, ap, domain_prot);
        }
        if (*prot && !xn) {
            *prot |= PAGE_EXEC;
        }
        if (!(*prot & (1 << access_type))) {
            /* Access permission fault.  */
            fi->type = ARMFault_Permission;
            goto do_fault;
        }
    }
    if (ns) {
        /* The NS bit will (as required by the architecture) have no effect if
         * the CPU doesn't support TZ or this is a non-secure translation
         * regime, because the attribute will already be non-secure.
         */
        attrs->secure = false;
    }
    *phys_ptr = phys_addr;
    return false;
do_fault:
    fi->domain = domain;
    fi->level = level;
    return true;
}

/*
 * check_s2_mmu_setup
 * @cpu:        ARMCPU
 * @is_aa64:    True if the translation regime is in AArch64 state
 * @startlevel: Suggested starting level
 * @inputsize:  Bitsize of IPAs
 * @stride:     Page-table stride (See the ARM ARM)
 *
 * Returns true if the suggested S2 translation parameters are OK and
 * false otherwise.
 */
static bool check_s2_mmu_setup(ARMCPU *cpu, bool is_aa64, int level,
                               int inputsize, int stride)
{
    const int grainsize = stride + 3;
    int startsizecheck;

    /* Negative levels are never allowed.  */
    if (level < 0) {
        return false;
    }

    startsizecheck = inputsize - ((3 - level) * stride + grainsize);
    if (startsizecheck < 1 || startsizecheck > stride + 4) {
        return false;
    }

    if (is_aa64) {
        CPUARMState *env = &cpu->env;
        unsigned int pamax = arm_pamax(cpu);

        switch (stride) {
        case 13: /* 64KB Pages.  */
            if (level == 0 || (level == 1 && pamax <= 42)) {
                return false;
            }
            break;
        case 11: /* 16KB Pages.  */
            if (level == 0 || (level == 1 && pamax <= 40)) {
                return false;
            }
            break;
        case 9: /* 4KB Pages.  */
            if (level == 0 && pamax <= 42) {
                return false;
            }
            break;
        default:
            g_assert_not_reached();
        }

        /* Inputsize checks.  */
        if (inputsize > pamax &&
            (arm_el_is_aa64(env, 1) || inputsize > 40)) {
            /* This is CONSTRAINED UNPREDICTABLE and we choose to fault.  */
            return false;
        }
    } else {
        /* AArch32 only supports 4KB pages. Assert on that.  */
        assert(stride == 9);

        if (level == 0) {
            return false;
        }
    }
    return true;
}

/* Translate from the 4-bit stage 2 representation of
 * memory attributes (without cache-allocation hints) to
 * the 8-bit representation of the stage 1 MAIR registers
 * (which includes allocation hints).
 *
 * ref: shared/translation/attrs/S2AttrDecode()
 *      .../S2ConvertAttrsHints()
 */
static uint8_t convert_stage2_attrs(CPUARMState *env, uint8_t s2attrs)
{
    uint8_t hiattr = extract32(s2attrs, 2, 2);
    uint8_t loattr = extract32(s2attrs, 0, 2);
    uint8_t hihint = 0, lohint = 0;

    if (hiattr != 0) { /* normal memory */
        if ((env->cp15.hcr_el2 & HCR_CD) != 0) { /* cache disabled */
            hiattr = loattr = 1; /* non-cacheable */
        } else {
            if (hiattr != 1) { /* Write-through or write-back */
                hihint = 3; /* RW allocate */
            }
            if (loattr != 1) { /* Write-through or write-back */
                lohint = 3; /* RW allocate */
            }
        }
    }

    return (hiattr << 6) | (hihint << 4) | (loattr << 2) | lohint;
}
#endif /* !CONFIG_USER_ONLY */

static int aa64_va_parameter_tbi(uint64_t tcr, ARMMMUIdx mmu_idx)
{
    if (regime_has_2_ranges(mmu_idx)) {
        return extract64(tcr, 37, 2);
    } else if (mmu_idx == ARMMMUIdx_Stage2) {
        return 0; /* VTCR_EL2 */
    } else {
        /* Replicate the single TBI bit so we always have 2 bits.  */
        return extract32(tcr, 20, 1) * 3;
    }
}

static int aa64_va_parameter_tbid(uint64_t tcr, ARMMMUIdx mmu_idx)
{
    if (regime_has_2_ranges(mmu_idx)) {
        return extract64(tcr, 51, 2);
    } else if (mmu_idx == ARMMMUIdx_Stage2) {
        return 0; /* VTCR_EL2 */
    } else {
        /* Replicate the single TBID bit so we always have 2 bits.  */
        return extract32(tcr, 29, 1) * 3;
    }
}

static int aa64_va_parameter_tcma(uint64_t tcr, ARMMMUIdx mmu_idx)
{
    if (regime_has_2_ranges(mmu_idx)) {
        return extract64(tcr, 57, 2);
    } else {
        /* Replicate the single TCMA bit so we always have 2 bits.  */
        return extract32(tcr, 30, 1) * 3;
    }
}

ARMVAParameters aa64_va_parameters(CPUARMState *env, uint64_t va,
                                   ARMMMUIdx mmu_idx, bool data)
{
    uint64_t tcr = regime_tcr(env, mmu_idx)->raw_tcr;
    bool epd, hpd, using16k, using64k;
    int select, tsz, tbi;

    if (!regime_has_2_ranges(mmu_idx)) {
        select = 0;
        tsz = extract32(tcr, 0, 6);
        using64k = extract32(tcr, 14, 1);
        using16k = extract32(tcr, 15, 1);
        if (mmu_idx == ARMMMUIdx_Stage2) {
            /* VTCR_EL2 */
            hpd = false;
        } else {
            hpd = extract32(tcr, 24, 1);
        }
        epd = false;
    } else {
        /*
         * Bit 55 is always between the two regions, and is canonical for
         * determining if address tagging is enabled.
         */
        select = extract64(va, 55, 1);
        if (!select) {
            tsz = extract32(tcr, 0, 6);
            epd = extract32(tcr, 7, 1);
            using64k = extract32(tcr, 14, 1);
            using16k = extract32(tcr, 15, 1);
            hpd = extract64(tcr, 41, 1);
        } else {
            int tg = extract32(tcr, 30, 2);
            using16k = tg == 1;
            using64k = tg == 3;
            tsz = extract32(tcr, 16, 6);
            epd = extract32(tcr, 23, 1);
            hpd = extract64(tcr, 42, 1);
        }
    }
    tsz = MIN(tsz, 39);  /* TODO: ARMv8.4-TTST */
    tsz = MAX(tsz, 16);  /* TODO: ARMv8.2-LVA  */

    /* Present TBI as a composite with TBID.  */
    tbi = aa64_va_parameter_tbi(tcr, mmu_idx);
    if (!data) {
        tbi &= ~aa64_va_parameter_tbid(tcr, mmu_idx);
    }
    tbi = (tbi >> select) & 1;

    return (ARMVAParameters) {
        .tsz = tsz,
        .select = select,
        .tbi = tbi,
        .epd = epd,
        .hpd = hpd,
        .using16k = using16k,
        .using64k = using64k,
    };
}

#ifndef CONFIG_USER_ONLY
static ARMVAParameters aa32_va_parameters(CPUARMState *env, uint32_t va,
                                          ARMMMUIdx mmu_idx)
{
    uint64_t tcr = regime_tcr(env, mmu_idx)->raw_tcr;
    uint32_t el = regime_el(env, mmu_idx);
    int select, tsz;
    bool epd, hpd;

    if (mmu_idx == ARMMMUIdx_Stage2) {
        /* VTCR */
        bool sext = extract32(tcr, 4, 1);
        bool sign = extract32(tcr, 3, 1);

        /*
         * If the sign-extend bit is not the same as t0sz[3], the result
         * is unpredictable. Flag this as a guest error.
         */
        if (sign != sext) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "AArch32: VTCR.S / VTCR.T0SZ[3] mismatch\n");
        }
        tsz = sextract32(tcr, 0, 4) + 8;
        select = 0;
        hpd = false;
        epd = false;
    } else if (el == 2) {
        /* HTCR */
        tsz = extract32(tcr, 0, 3);
        select = 0;
        hpd = extract64(tcr, 24, 1);
        epd = false;
    } else {
        int t0sz = extract32(tcr, 0, 3);
        int t1sz = extract32(tcr, 16, 3);

        if (t1sz == 0) {
            select = va > (0xffffffffu >> t0sz);
        } else {
            /* Note that we will detect errors later.  */
            select = va >= ~(0xffffffffu >> t1sz);
        }
        if (!select) {
            tsz = t0sz;
            epd = extract32(tcr, 7, 1);
            hpd = extract64(tcr, 41, 1);
        } else {
            tsz = t1sz;
            epd = extract32(tcr, 23, 1);
            hpd = extract64(tcr, 42, 1);
        }
        /* For aarch32, hpd0 is not enabled without t2e as well.  */
        hpd &= extract32(tcr, 6, 1);
    }

    return (ARMVAParameters) {
        .tsz = tsz,
        .select = select,
        .epd = epd,
        .hpd = hpd,
    };
}

/**
 * get_phys_addr_lpae: perform one stage of page table walk, LPAE format
 *
 * Returns false if the translation was successful. Otherwise, phys_ptr, attrs,
 * prot and page_size may not be filled in, and the populated fsr value provides
 * information on why the translation aborted, in the format of a long-format
 * DFSR/IFSR fault register, with the following caveats:
 *  * the WnR bit is never set (the caller must do this).
 *
 * @env: CPUARMState
 * @address: virtual address to get physical address for
 * @access_type: MMU_DATA_LOAD, MMU_DATA_STORE or MMU_INST_FETCH
 * @mmu_idx: MMU index indicating required translation regime
 * @s1_is_el0: if @mmu_idx is ARMMMUIdx_Stage2 (so this is a stage 2 page table
 *             walk), must be true if this is stage 2 of a stage 1+2 walk for an
 *             EL0 access). If @mmu_idx is anything else, @s1_is_el0 is ignored.
 * @phys_ptr: set to the physical address corresponding to the virtual address
 * @attrs: set to the memory transaction attributes to use
 * @prot: set to the permissions for the page containing phys_ptr
 * @page_size_ptr: set to the size of the page containing phys_ptr
 * @fi: set to fault info if the translation fails
 * @cacheattrs: (if non-NULL) set to the cacheability/shareability attributes
 */
static bool get_phys_addr_lpae(CPUARMState *env, target_ulong address,
                               MMUAccessType access_type, ARMMMUIdx mmu_idx,
                               bool s1_is_el0,
                               hwaddr *phys_ptr, MemTxAttrs *txattrs, int *prot,
                               target_ulong *page_size_ptr,
                               ARMMMUFaultInfo *fi, ARMCacheAttrs *cacheattrs)
{
    ARMCPU *cpu = env_archcpu(env);
    CPUState *cs = CPU(cpu);
    /* Read an LPAE long-descriptor translation table. */
    ARMFaultType fault_type = ARMFault_Translation;
    uint32_t level;
    ARMVAParameters param;
    uint64_t ttbr;
    hwaddr descaddr, indexmask, indexmask_grainsize;
    uint32_t tableattrs;
    target_ulong page_size;
    uint32_t attrs;
    int32_t stride;
    int addrsize, inputsize;
    TCR *tcr = regime_tcr(env, mmu_idx);
    int ap, ns, xn, pxn;
    uint32_t el = regime_el(env, mmu_idx);
    uint64_t descaddrmask;
    bool aarch64 = arm_el_is_aa64(env, el);
    bool guarded = false;

    /* TODO: This code does not support shareability levels. */
    if (aarch64) {
        param = aa64_va_parameters(env, address, mmu_idx,
                                   access_type != MMU_INST_FETCH);
        level = 0;
        addrsize = 64 - 8 * param.tbi;
        inputsize = 64 - param.tsz;
    } else {
        param = aa32_va_parameters(env, address, mmu_idx);
        level = 1;
        addrsize = (mmu_idx == ARMMMUIdx_Stage2 ? 40 : 32);
        inputsize = addrsize - param.tsz;
    }

    /*
     * We determined the region when collecting the parameters, but we
     * have not yet validated that the address is valid for the region.
     * Extract the top bits and verify that they all match select.
     *
     * For aa32, if inputsize == addrsize, then we have selected the
     * region by exclusion in aa32_va_parameters and there is no more
     * validation to do here.
     */
    if (inputsize < addrsize) {
        target_ulong top_bits = sextract64(address, inputsize,
                                           addrsize - inputsize);
        if (-top_bits != param.select) {
            /* The gap between the two regions is a Translation fault */
            fault_type = ARMFault_Translation;
            goto do_fault;
        }
    }

    if (param.using64k) {
        stride = 13;
    } else if (param.using16k) {
        stride = 11;
    } else {
        stride = 9;
    }

    /* Note that QEMU ignores shareability and cacheability attributes,
     * so we don't need to do anything with the SH, ORGN, IRGN fields
     * in the TTBCR.  Similarly, TTBCR:A1 selects whether we get the
     * ASID from TTBR0 or TTBR1, but QEMU's TLB doesn't currently
     * implement any ASID-like capability so we can ignore it (instead
     * we will always flush the TLB any time the ASID is changed).
     */
    ttbr = regime_ttbr(env, mmu_idx, param.select);

    /* Here we should have set up all the parameters for the translation:
     * inputsize, ttbr, epd, stride, tbi
     */

    if (param.epd) {
        /* Translation table walk disabled => Translation fault on TLB miss
         * Note: This is always 0 on 64-bit EL2 and EL3.
         */
        goto do_fault;
    }

    if (mmu_idx != ARMMMUIdx_Stage2) {
        /* The starting level depends on the virtual address size (which can
         * be up to 48 bits) and the translation granule size. It indicates
         * the number of strides (stride bits at a time) needed to
         * consume the bits of the input address. In the pseudocode this is:
         *  level = 4 - RoundUp((inputsize - grainsize) / stride)
         * where their 'inputsize' is our 'inputsize', 'grainsize' is
         * our 'stride + 3' and 'stride' is our 'stride'.
         * Applying the usual "rounded up m/n is (m+n-1)/n" and simplifying:
         * = 4 - (inputsize - stride - 3 + stride - 1) / stride
         * = 4 - (inputsize - 4) / stride;
         */
        level = 4 - (inputsize - 4) / stride;
    } else {
        /* For stage 2 translations the starting level is specified by the
         * VTCR_EL2.SL0 field (whose interpretation depends on the page size)
         */
        uint32_t sl0 = extract32(tcr->raw_tcr, 6, 2);
        uint32_t startlevel;
        bool ok;

        if (!aarch64 || stride == 9) {
            /* AArch32 or 4KB pages */
            startlevel = 2 - sl0;
        } else {
            /* 16KB or 64KB pages */
            startlevel = 3 - sl0;
        }

        /* Check that the starting level is valid. */
        ok = check_s2_mmu_setup(cpu, aarch64, startlevel,
                                inputsize, stride);
        if (!ok) {
            fault_type = ARMFault_Translation;
            goto do_fault;
        }
        level = startlevel;
    }

    indexmask_grainsize = (1ULL << (stride + 3)) - 1;
    indexmask = (1ULL << (inputsize - (stride * (4 - level)))) - 1;

    /* Now we can extract the actual base address from the TTBR */
    descaddr = extract64(ttbr, 0, 48);
    /*
     * We rely on this masking to clear the RES0 bits at the bottom of the TTBR
     * and also to mask out CnP (bit 0) which could validly be non-zero.
     */
    descaddr &= ~indexmask;

    /* The address field in the descriptor goes up to bit 39 for ARMv7
     * but up to bit 47 for ARMv8, but we use the descaddrmask
     * up to bit 39 for AArch32, because we don't need other bits in that case
     * to construct next descriptor address (anyway they should be all zeroes).
     */
    descaddrmask = ((1ull << (aarch64 ? 48 : 40)) - 1) &
                   ~indexmask_grainsize;

    /* Secure accesses start with the page table in secure memory and
     * can be downgraded to non-secure at any step. Non-secure accesses
     * remain non-secure. We implement this by just ORing in the NSTable/NS
     * bits at each step.
     */
    tableattrs = regime_is_secure(env, mmu_idx) ? 0 : (1 << 4);
    for (;;) {
        uint64_t descriptor;
        bool nstable;

        descaddr |= (address >> (stride * (4 - level))) & indexmask;
        descaddr &= ~7ULL;
        nstable = extract32(tableattrs, 4, 1);
        descriptor = arm_ldq_ptw(cs, descaddr, !nstable, mmu_idx, fi);
        if (fi->type != ARMFault_None) {
            goto do_fault;
        }

        if (!(descriptor & 1) ||
            (!(descriptor & 2) && (level == 3))) {
            /* Invalid, or the Reserved level 3 encoding */
            goto do_fault;
        }
        descaddr = descriptor & descaddrmask;

        if ((descriptor & 2) && (level < 3)) {
            /* Table entry. The top five bits are attributes which may
             * propagate down through lower levels of the table (and
             * which are all arranged so that 0 means "no effect", so
             * we can gather them up by ORing in the bits at each level).
             */
            tableattrs |= extract64(descriptor, 59, 5);
            level++;
            indexmask = indexmask_grainsize;
            continue;
        }
        /* Block entry at level 1 or 2, or page entry at level 3.
         * These are basically the same thing, although the number
         * of bits we pull in from the vaddr varies.
         */
        page_size = (1ULL << ((stride * (4 - level)) + 3));
        descaddr |= (address & (page_size - 1));
        /* Extract attributes from the descriptor */
        attrs = extract64(descriptor, 2, 10)
            | (extract64(descriptor, 52, 12) << 10);

        if (mmu_idx == ARMMMUIdx_Stage2) {
            /* Stage 2 table descriptors do not include any attribute fields */
            break;
        }
        /* Merge in attributes from table descriptors */
        attrs |= nstable << 3; /* NS */
        guarded = extract64(descriptor, 50, 1);  /* GP */
        if (param.hpd) {
            /* HPD disables all the table attributes except NSTable.  */
            break;
        }
        attrs |= extract32(tableattrs, 0, 2) << 11;     /* XN, PXN */
        /* The sense of AP[1] vs APTable[0] is reversed, as APTable[0] == 1
         * means "force PL1 access only", which means forcing AP[1] to 0.
         */
        attrs &= ~(extract32(tableattrs, 2, 1) << 4);   /* !APT[0] => AP[1] */
        attrs |= extract32(tableattrs, 3, 1) << 5;      /* APT[1] => AP[2] */
        break;
    }
    /* Here descaddr is the final physical address, and attributes
     * are all in attrs.
     */
    fault_type = ARMFault_AccessFlag;
    if ((attrs & (1 << 8)) == 0) {
        /* Access flag */
        goto do_fault;
    }

    ap = extract32(attrs, 4, 2);

    if (mmu_idx == ARMMMUIdx_Stage2) {
        ns = true;
        xn = extract32(attrs, 11, 2);
        *prot = get_S2prot(env, ap, xn, s1_is_el0);
    } else {
        ns = extract32(attrs, 3, 1);
        xn = extract32(attrs, 12, 1);
        pxn = extract32(attrs, 11, 1);
        *prot = get_S1prot(env, mmu_idx, aarch64, ap, ns, xn, pxn);
    }

    fault_type = ARMFault_Permission;
    if (!(*prot & (1 << access_type))) {
        goto do_fault;
    }

    if (ns) {
        /* The NS bit will (as required by the architecture) have no effect if
         * the CPU doesn't support TZ or this is a non-secure translation
         * regime, because the attribute will already be non-secure.
         */
        txattrs->secure = false;
    }
    /* When in aarch64 mode, and BTI is enabled, remember GP in the IOTLB.  */
    if (aarch64 && guarded && cpu_isar_feature(aa64_bti, cpu)) {
        arm_tlb_bti_gp(txattrs) = true;
    }

    if (mmu_idx == ARMMMUIdx_Stage2) {
        cacheattrs->attrs = convert_stage2_attrs(env, extract32(attrs, 0, 4));
    } else {
        /* Index into MAIR registers for cache attributes */
        uint8_t attrindx = extract32(attrs, 0, 3);
        uint64_t mair = env->cp15.mair_el[regime_el(env, mmu_idx)];
        assert(attrindx <= 7);
        cacheattrs->attrs = extract64(mair, attrindx * 8, 8);
    }
    cacheattrs->shareability = extract32(attrs, 6, 2);

    *phys_ptr = descaddr;
    *page_size_ptr = page_size;
    return false;

do_fault:
    fi->type = fault_type;
    fi->level = level;
    /* Tag the error as S2 for failed S1 PTW at S2 or ordinary S2.  */
    fi->stage2 = fi->s1ptw || (mmu_idx == ARMMMUIdx_Stage2);
    return true;
}

static inline void get_phys_addr_pmsav7_default(CPUARMState *env,
                                                ARMMMUIdx mmu_idx,
                                                int32_t address, int *prot)
{
    if (!arm_feature(env, ARM_FEATURE_M)) {
        *prot = PAGE_READ | PAGE_WRITE;
        switch (address) {
        case 0xF0000000 ... 0xFFFFFFFF:
            if (regime_sctlr(env, mmu_idx) & SCTLR_V) {
                /* hivecs execing is ok */
                *prot |= PAGE_EXEC;
            }
            break;
        case 0x00000000 ... 0x7FFFFFFF:
            *prot |= PAGE_EXEC;
            break;
        }
    } else {
        /* Default system address map for M profile cores.
         * The architecture specifies which regions are execute-never;
         * at the MPU level no other checks are defined.
         */
        switch (address) {
        case 0x00000000 ... 0x1fffffff: /* ROM */
        case 0x20000000 ... 0x3fffffff: /* SRAM */
        case 0x60000000 ... 0x7fffffff: /* RAM */
        case 0x80000000 ... 0x9fffffff: /* RAM */
            *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
            break;
        case 0x40000000 ... 0x5fffffff: /* Peripheral */
        case 0xa0000000 ... 0xbfffffff: /* Device */
        case 0xc0000000 ... 0xdfffffff: /* Device */
        case 0xe0000000 ... 0xffffffff: /* System */
            *prot = PAGE_READ | PAGE_WRITE;
            break;
        default:
            g_assert_not_reached();
        }
    }
}

static bool pmsav7_use_background_region(ARMCPU *cpu,
                                         ARMMMUIdx mmu_idx, bool is_user)
{
    /* Return true if we should use the default memory map as a
     * "background" region if there are no hits against any MPU regions.
     */
    CPUARMState *env = &cpu->env;

    if (is_user) {
        return false;
    }

    if (arm_feature(env, ARM_FEATURE_M)) {
        return env->v7m.mpu_ctrl[regime_is_secure(env, mmu_idx)]
            & R_V7M_MPU_CTRL_PRIVDEFENA_MASK;
    } else {
        return regime_sctlr(env, mmu_idx) & SCTLR_BR;
    }
}

static inline bool m_is_ppb_region(CPUARMState *env, uint32_t address)
{
    /* True if address is in the M profile PPB region 0xe0000000 - 0xe00fffff */
    return arm_feature(env, ARM_FEATURE_M) &&
        extract32(address, 20, 12) == 0xe00;
}

static inline bool m_is_system_region(CPUARMState *env, uint32_t address)
{
    /* True if address is in the M profile system region
     * 0xe0000000 - 0xffffffff
     */
    return arm_feature(env, ARM_FEATURE_M) && extract32(address, 29, 3) == 0x7;
}

static bool get_phys_addr_pmsav7(CPUARMState *env, uint32_t address,
                                 MMUAccessType access_type, ARMMMUIdx mmu_idx,
                                 hwaddr *phys_ptr, int *prot,
                                 target_ulong *page_size,
                                 ARMMMUFaultInfo *fi)
{
    ARMCPU *cpu = env_archcpu(env);
    int n;
    bool is_user = regime_is_user(env, mmu_idx);

    *phys_ptr = address;
    *page_size = TARGET_PAGE_SIZE;
    *prot = 0;

    if (regime_translation_disabled(env, mmu_idx) ||
        m_is_ppb_region(env, address)) {
        /* MPU disabled or M profile PPB access: use default memory map.
         * The other case which uses the default memory map in the
         * v7M ARM ARM pseudocode is exception vector reads from the vector
         * table. In QEMU those accesses are done in arm_v7m_load_vector(),
         * which always does a direct read using address_space_ldl(), rather
         * than going via this function, so we don't need to check that here.
         */
        get_phys_addr_pmsav7_default(env, mmu_idx, address, prot);
    } else { /* MPU enabled */
        for (n = (int)cpu->pmsav7_dregion - 1; n >= 0; n--) {
            /* region search */
            uint32_t base = env->pmsav7.drbar[n];
            uint32_t rsize = extract32(env->pmsav7.drsr[n], 1, 5);
            uint32_t rmask;
            bool srdis = false;

            if (!(env->pmsav7.drsr[n] & 0x1)) {
                continue;
            }

            if (!rsize) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "DRSR[%d]: Rsize field cannot be 0\n", n);
                continue;
            }
            rsize++;
            rmask = (1ull << rsize) - 1;

            if (base & rmask) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "DRBAR[%d]: 0x%" PRIx32 " misaligned "
                              "to DRSR region size, mask = 0x%" PRIx32 "\n",
                              n, base, rmask);
                continue;
            }

            if (address < base || address > base + rmask) {
                /*
                 * Address not in this region. We must check whether the
                 * region covers addresses in the same page as our address.
                 * In that case we must not report a size that covers the
                 * whole page for a subsequent hit against a different MPU
                 * region or the background region, because it would result in
                 * incorrect TLB hits for subsequent accesses to addresses that
                 * are in this MPU region.
                 */
                if (ranges_overlap(base, rmask,
                                   address & TARGET_PAGE_MASK,
                                   TARGET_PAGE_SIZE)) {
                    *page_size = 1;
                }
                continue;
            }

            /* Region matched */

            if (rsize >= 8) { /* no subregions for regions < 256 bytes */
                int i, snd;
                uint32_t srdis_mask;

                rsize -= 3; /* sub region size (power of 2) */
                snd = ((address - base) >> rsize) & 0x7;
                srdis = extract32(env->pmsav7.drsr[n], snd + 8, 1);

                srdis_mask = srdis ? 0x3 : 0x0;
                for (i = 2; i <= 8 && rsize < TARGET_PAGE_BITS; i *= 2) {
                    /* This will check in groups of 2, 4 and then 8, whether
                     * the subregion bits are consistent. rsize is incremented
                     * back up to give the region size, considering consistent
                     * adjacent subregions as one region. Stop testing if rsize
                     * is already big enough for an entire QEMU page.
                     */
                    int snd_rounded = snd & ~(i - 1);
                    uint32_t srdis_multi = extract32(env->pmsav7.drsr[n],
                                                     snd_rounded + 8, i);
                    if (srdis_mask ^ srdis_multi) {
                        break;
                    }
                    srdis_mask = (srdis_mask << i) | srdis_mask;
                    rsize++;
                }
            }
            if (srdis) {
                continue;
            }
            if (rsize < TARGET_PAGE_BITS) {
                *page_size = 1 << rsize;
            }
            break;
        }

        if (n == -1) { /* no hits */
            if (!pmsav7_use_background_region(cpu, mmu_idx, is_user)) {
                /* background fault */
                fi->type = ARMFault_Background;
                return true;
            }
            get_phys_addr_pmsav7_default(env, mmu_idx, address, prot);
        } else { /* a MPU hit! */
            uint32_t ap = extract32(env->pmsav7.dracr[n], 8, 3);
            uint32_t xn = extract32(env->pmsav7.dracr[n], 12, 1);

            if (m_is_system_region(env, address)) {
                /* System space is always execute never */
                xn = 1;
            }

            if (is_user) { /* User mode AP bit decoding */
                switch (ap) {
                case 0:
                case 1:
                case 5:
                    break; /* no access */
                case 3:
                    *prot |= PAGE_WRITE;
                    /* fall through */
                case 2:
                case 6:
                    *prot |= PAGE_READ | PAGE_EXEC;
                    break;
                case 7:
                    /* for v7M, same as 6; for R profile a reserved value */
                    if (arm_feature(env, ARM_FEATURE_M)) {
                        *prot |= PAGE_READ | PAGE_EXEC;
                        break;
                    }
                    /* fall through */
                default:
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "DRACR[%d]: Bad value for AP bits: 0x%"
                                  PRIx32 "\n", n, ap);
                }
            } else { /* Priv. mode AP bits decoding */
                switch (ap) {
                case 0:
                    break; /* no access */
                case 1:
                case 2:
                case 3:
                    *prot |= PAGE_WRITE;
                    /* fall through */
                case 5:
                case 6:
                    *prot |= PAGE_READ | PAGE_EXEC;
                    break;
                case 7:
                    /* for v7M, same as 6; for R profile a reserved value */
                    if (arm_feature(env, ARM_FEATURE_M)) {
                        *prot |= PAGE_READ | PAGE_EXEC;
                        break;
                    }
                    /* fall through */
                default:
                    qemu_log_mask(LOG_GUEST_ERROR,
                                  "DRACR[%d]: Bad value for AP bits: 0x%"
                                  PRIx32 "\n", n, ap);
                }
            }

            /* execute never */
            if (xn) {
                *prot &= ~PAGE_EXEC;
            }
        }
    }

    fi->type = ARMFault_Permission;
    fi->level = 1;
    return !(*prot & (1 << access_type));
}

static bool v8m_is_sau_exempt(CPUARMState *env,
                              uint32_t address, MMUAccessType access_type)
{
    /* The architecture specifies that certain address ranges are
     * exempt from v8M SAU/IDAU checks.
     */
    return
        (access_type == MMU_INST_FETCH && m_is_system_region(env, address)) ||
        (address >= 0xe0000000 && address <= 0xe0002fff) ||
        (address >= 0xe000e000 && address <= 0xe000efff) ||
        (address >= 0xe002e000 && address <= 0xe002efff) ||
        (address >= 0xe0040000 && address <= 0xe0041fff) ||
        (address >= 0xe00ff000 && address <= 0xe00fffff);
}

void v8m_security_lookup(CPUARMState *env, uint32_t address,
                                MMUAccessType access_type, ARMMMUIdx mmu_idx,
                                V8M_SAttributes *sattrs)
{
    /* Look up the security attributes for this address. Compare the
     * pseudocode SecurityCheck() function.
     * We assume the caller has zero-initialized *sattrs.
     */
    ARMCPU *cpu = env_archcpu(env);
    int r;
    bool idau_exempt = false, idau_ns = true, idau_nsc = true;
    int idau_region = IREGION_NOTVALID;
    uint32_t addr_page_base = address & TARGET_PAGE_MASK;
    uint32_t addr_page_limit = addr_page_base + (TARGET_PAGE_SIZE - 1);

    if (cpu->idau) {
        IDAUInterfaceClass *iic = IDAU_INTERFACE_GET_CLASS(cpu->idau);
        IDAUInterface *ii = IDAU_INTERFACE(cpu->idau);

        iic->check(ii, address, &idau_region, &idau_exempt, &idau_ns,
                   &idau_nsc);
    }

    if (access_type == MMU_INST_FETCH && extract32(address, 28, 4) == 0xf) {
        /* 0xf0000000..0xffffffff is always S for insn fetches */
        return;
    }

    if (idau_exempt || v8m_is_sau_exempt(env, address, access_type)) {
        sattrs->ns = !regime_is_secure(env, mmu_idx);
        return;
    }

    if (idau_region != IREGION_NOTVALID) {
        sattrs->irvalid = true;
        sattrs->iregion = idau_region;
    }

    switch (env->sau.ctrl & 3) {
    case 0: /* SAU.ENABLE == 0, SAU.ALLNS == 0 */
        break;
    case 2: /* SAU.ENABLE == 0, SAU.ALLNS == 1 */
        sattrs->ns = true;
        break;
    default: /* SAU.ENABLE == 1 */
        for (r = 0; r < cpu->sau_sregion; r++) {
            if (env->sau.rlar[r] & 1) {
                uint32_t base = env->sau.rbar[r] & ~0x1f;
                uint32_t limit = env->sau.rlar[r] | 0x1f;

                if (base <= address && limit >= address) {
                    if (base > addr_page_base || limit < addr_page_limit) {
                        sattrs->subpage = true;
                    }
                    if (sattrs->srvalid) {
                        /* If we hit in more than one region then we must report
                         * as Secure, not NS-Callable, with no valid region
                         * number info.
                         */
                        sattrs->ns = false;
                        sattrs->nsc = false;
                        sattrs->sregion = 0;
                        sattrs->srvalid = false;
                        break;
                    } else {
                        if (env->sau.rlar[r] & 2) {
                            sattrs->nsc = true;
                        } else {
                            sattrs->ns = true;
                        }
                        sattrs->srvalid = true;
                        sattrs->sregion = r;
                    }
                } else {
                    /*
                     * Address not in this region. We must check whether the
                     * region covers addresses in the same page as our address.
                     * In that case we must not report a size that covers the
                     * whole page for a subsequent hit against a different MPU
                     * region or the background region, because it would result
                     * in incorrect TLB hits for subsequent accesses to
                     * addresses that are in this MPU region.
                     */
                    if (limit >= base &&
                        ranges_overlap(base, limit - base + 1,
                                       addr_page_base,
                                       TARGET_PAGE_SIZE)) {
                        sattrs->subpage = true;
                    }
                }
            }
        }
        break;
    }

    /*
     * The IDAU will override the SAU lookup results if it specifies
     * higher security than the SAU does.
     */
    if (!idau_ns) {
        if (sattrs->ns || (!idau_nsc && sattrs->nsc)) {
            sattrs->ns = false;
            sattrs->nsc = idau_nsc;
        }
    }
}

bool pmsav8_mpu_lookup(CPUARMState *env, uint32_t address,
                              MMUAccessType access_type, ARMMMUIdx mmu_idx,
                              hwaddr *phys_ptr, MemTxAttrs *txattrs,
                              int *prot, bool *is_subpage,
                              ARMMMUFaultInfo *fi, uint32_t *mregion)
{
    /* Perform a PMSAv8 MPU lookup (without also doing the SAU check
     * that a full phys-to-virt translation does).
     * mregion is (if not NULL) set to the region number which matched,
     * or -1 if no region number is returned (MPU off, address did not
     * hit a region, address hit in multiple regions).
     * We set is_subpage to true if the region hit doesn't cover the
     * entire TARGET_PAGE the address is within.
     */
    ARMCPU *cpu = env_archcpu(env);
    bool is_user = regime_is_user(env, mmu_idx);
    uint32_t secure = regime_is_secure(env, mmu_idx);
    int n;
    int matchregion = -1;
    bool hit = false;
    uint32_t addr_page_base = address & TARGET_PAGE_MASK;
    uint32_t addr_page_limit = addr_page_base + (TARGET_PAGE_SIZE - 1);

    *is_subpage = false;
    *phys_ptr = address;
    *prot = 0;
    if (mregion) {
        *mregion = -1;
    }

    /* Unlike the ARM ARM pseudocode, we don't need to check whether this
     * was an exception vector read from the vector table (which is always
     * done using the default system address map), because those accesses
     * are done in arm_v7m_load_vector(), which always does a direct
     * read using address_space_ldl(), rather than going via this function.
     */
    if (regime_translation_disabled(env, mmu_idx)) { /* MPU disabled */
        hit = true;
    } else if (m_is_ppb_region(env, address)) {
        hit = true;
    } else {
        if (pmsav7_use_background_region(cpu, mmu_idx, is_user)) {
            hit = true;
        }

        for (n = (int)cpu->pmsav7_dregion - 1; n >= 0; n--) {
            /* region search */
            /* Note that the base address is bits [31:5] from the register
             * with bits [4:0] all zeroes, but the limit address is bits
             * [31:5] from the register with bits [4:0] all ones.
             */
            uint32_t base = env->pmsav8.rbar[secure][n] & ~0x1f;
            uint32_t limit = env->pmsav8.rlar[secure][n] | 0x1f;

            if (!(env->pmsav8.rlar[secure][n] & 0x1)) {
                /* Region disabled */
                continue;
            }

            if (address < base || address > limit) {
                /*
                 * Address not in this region. We must check whether the
                 * region covers addresses in the same page as our address.
                 * In that case we must not report a size that covers the
                 * whole page for a subsequent hit against a different MPU
                 * region or the background region, because it would result in
                 * incorrect TLB hits for subsequent accesses to addresses that
                 * are in this MPU region.
                 */
                if (limit >= base &&
                    ranges_overlap(base, limit - base + 1,
                                   addr_page_base,
                                   TARGET_PAGE_SIZE)) {
                    *is_subpage = true;
                }
                continue;
            }

            if (base > addr_page_base || limit < addr_page_limit) {
                *is_subpage = true;
            }

            if (matchregion != -1) {
                /* Multiple regions match -- always a failure (unlike
                 * PMSAv7 where highest-numbered-region wins)
                 */
                fi->type = ARMFault_Permission;
                fi->level = 1;
                return true;
            }

            matchregion = n;
            hit = true;
        }
    }

    if (!hit) {
        /* background fault */
        fi->type = ARMFault_Background;
        return true;
    }

    if (matchregion == -1) {
        /* hit using the background region */
        get_phys_addr_pmsav7_default(env, mmu_idx, address, prot);
    } else {
        uint32_t ap = extract32(env->pmsav8.rbar[secure][matchregion], 1, 2);
        uint32_t xn = extract32(env->pmsav8.rbar[secure][matchregion], 0, 1);

        if (m_is_system_region(env, address)) {
            /* System space is always execute never */
            xn = 1;
        }

        *prot = simple_ap_to_rw_prot(env, mmu_idx, ap);
        if (*prot && !xn) {
            *prot |= PAGE_EXEC;
        }
        /* We don't need to look the attribute up in the MAIR0/MAIR1
         * registers because that only tells us about cacheability.
         */
        if (mregion) {
            *mregion = matchregion;
        }
    }

    fi->type = ARMFault_Permission;
    fi->level = 1;
    return !(*prot & (1 << access_type));
}


static bool get_phys_addr_pmsav8(CPUARMState *env, uint32_t address,
                                 MMUAccessType access_type, ARMMMUIdx mmu_idx,
                                 hwaddr *phys_ptr, MemTxAttrs *txattrs,
                                 int *prot, target_ulong *page_size,
                                 ARMMMUFaultInfo *fi)
{
    uint32_t secure = regime_is_secure(env, mmu_idx);
    V8M_SAttributes sattrs = {};
    bool ret;
    bool mpu_is_subpage;

    if (arm_feature(env, ARM_FEATURE_M_SECURITY)) {
        v8m_security_lookup(env, address, access_type, mmu_idx, &sattrs);
        if (access_type == MMU_INST_FETCH) {
            /* Instruction fetches always use the MMU bank and the
             * transaction attribute determined by the fetch address,
             * regardless of CPU state. This is painful for QEMU
             * to handle, because it would mean we need to encode
             * into the mmu_idx not just the (user, negpri) information
             * for the current security state but also that for the
             * other security state, which would balloon the number
             * of mmu_idx values needed alarmingly.
             * Fortunately we can avoid this because it's not actually
             * possible to arbitrarily execute code from memory with
             * the wrong security attribute: it will always generate
             * an exception of some kind or another, apart from the
             * special case of an NS CPU executing an SG instruction
             * in S&NSC memory. So we always just fail the translation
             * here and sort things out in the exception handler
             * (including possibly emulating an SG instruction).
             */
            if (sattrs.ns != !secure) {
                if (sattrs.nsc) {
                    fi->type = ARMFault_QEMU_NSCExec;
                } else {
                    fi->type = ARMFault_QEMU_SFault;
                }
                *page_size = sattrs.subpage ? 1 : TARGET_PAGE_SIZE;
                *phys_ptr = address;
                *prot = 0;
                return true;
            }
        } else {
            /* For data accesses we always use the MMU bank indicated
             * by the current CPU state, but the security attributes
             * might downgrade a secure access to nonsecure.
             */
            if (sattrs.ns) {
                txattrs->secure = false;
            } else if (!secure) {
                /* NS access to S memory must fault.
                 * Architecturally we should first check whether the
                 * MPU information for this address indicates that we
                 * are doing an unaligned access to Device memory, which
                 * should generate a UsageFault instead. QEMU does not
                 * currently check for that kind of unaligned access though.
                 * If we added it we would need to do so as a special case
                 * for M_FAKE_FSR_SFAULT in arm_v7m_cpu_do_interrupt().
                 */
                fi->type = ARMFault_QEMU_SFault;
                *page_size = sattrs.subpage ? 1 : TARGET_PAGE_SIZE;
                *phys_ptr = address;
                *prot = 0;
                return true;
            }
        }
    }

    ret = pmsav8_mpu_lookup(env, address, access_type, mmu_idx, phys_ptr,
                            txattrs, prot, &mpu_is_subpage, fi, NULL);
    *page_size = sattrs.subpage || mpu_is_subpage ? 1 : TARGET_PAGE_SIZE;
    return ret;
}

static bool get_phys_addr_pmsav5(CPUARMState *env, uint32_t address,
                                 MMUAccessType access_type, ARMMMUIdx mmu_idx,
                                 hwaddr *phys_ptr, int *prot,
                                 ARMMMUFaultInfo *fi)
{
    int n;
    uint32_t mask;
    uint32_t base;
    bool is_user = regime_is_user(env, mmu_idx);

    if (regime_translation_disabled(env, mmu_idx)) {
        /* MPU disabled.  */
        *phys_ptr = address;
        *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return false;
    }

    *phys_ptr = address;
    for (n = 7; n >= 0; n--) {
        base = env->cp15.c6_region[n];
        if ((base & 1) == 0) {
            continue;
        }
        mask = 1 << ((base >> 1) & 0x1f);
        /* Keep this shift separate from the above to avoid an
           (undefined) << 32.  */
        mask = (mask << 1) - 1;
        if (((base ^ address) & ~mask) == 0) {
            break;
        }
    }
    if (n < 0) {
        fi->type = ARMFault_Background;
        return true;
    }

    if (access_type == MMU_INST_FETCH) {
        mask = env->cp15.pmsav5_insn_ap;
    } else {
        mask = env->cp15.pmsav5_data_ap;
    }
    mask = (mask >> (n * 4)) & 0xf;
    switch (mask) {
    case 0:
        fi->type = ARMFault_Permission;
        fi->level = 1;
        return true;
    case 1:
        if (is_user) {
            fi->type = ARMFault_Permission;
            fi->level = 1;
            return true;
        }
        *prot = PAGE_READ | PAGE_WRITE;
        break;
    case 2:
        *prot = PAGE_READ;
        if (!is_user) {
            *prot |= PAGE_WRITE;
        }
        break;
    case 3:
        *prot = PAGE_READ | PAGE_WRITE;
        break;
    case 5:
        if (is_user) {
            fi->type = ARMFault_Permission;
            fi->level = 1;
            return true;
        }
        *prot = PAGE_READ;
        break;
    case 6:
        *prot = PAGE_READ;
        break;
    default:
        /* Bad permission.  */
        fi->type = ARMFault_Permission;
        fi->level = 1;
        return true;
    }
    *prot |= PAGE_EXEC;
    return false;
}

/* Combine either inner or outer cacheability attributes for normal
 * memory, according to table D4-42 and pseudocode procedure
 * CombineS1S2AttrHints() of ARM DDI 0487B.b (the ARMv8 ARM).
 *
 * NB: only stage 1 includes allocation hints (RW bits), leading to
 * some asymmetry.
 */
static uint8_t combine_cacheattr_nibble(uint8_t s1, uint8_t s2)
{
    if (s1 == 4 || s2 == 4) {
        /* non-cacheable has precedence */
        return 4;
    } else if (extract32(s1, 2, 2) == 0 || extract32(s1, 2, 2) == 2) {
        /* stage 1 write-through takes precedence */
        return s1;
    } else if (extract32(s2, 2, 2) == 2) {
        /* stage 2 write-through takes precedence, but the allocation hint
         * is still taken from stage 1
         */
        return (2 << 2) | extract32(s1, 0, 2);
    } else { /* write-back */
        return s1;
    }
}

/* Combine S1 and S2 cacheability/shareability attributes, per D4.5.4
 * and CombineS1S2Desc()
 *
 * @s1:      Attributes from stage 1 walk
 * @s2:      Attributes from stage 2 walk
 */
static ARMCacheAttrs combine_cacheattrs(ARMCacheAttrs s1, ARMCacheAttrs s2)
{
    uint8_t s1lo, s2lo, s1hi, s2hi;
    ARMCacheAttrs ret;
    bool tagged = false;

    if (s1.attrs == 0xf0) {
        tagged = true;
        s1.attrs = 0xff;
    }

    s1lo = extract32(s1.attrs, 0, 4);
    s2lo = extract32(s2.attrs, 0, 4);
    s1hi = extract32(s1.attrs, 4, 4);
    s2hi = extract32(s2.attrs, 4, 4);

    /* Combine shareability attributes (table D4-43) */
    if (s1.shareability == 2 || s2.shareability == 2) {
        /* if either are outer-shareable, the result is outer-shareable */
        ret.shareability = 2;
    } else if (s1.shareability == 3 || s2.shareability == 3) {
        /* if either are inner-shareable, the result is inner-shareable */
        ret.shareability = 3;
    } else {
        /* both non-shareable */
        ret.shareability = 0;
    }

    /* Combine memory type and cacheability attributes */
    if (s1hi == 0 || s2hi == 0) {
        /* Device has precedence over normal */
        if (s1lo == 0 || s2lo == 0) {
            /* nGnRnE has precedence over anything */
            ret.attrs = 0;
        } else if (s1lo == 4 || s2lo == 4) {
            /* non-Reordering has precedence over Reordering */
            ret.attrs = 4;  /* nGnRE */
        } else if (s1lo == 8 || s2lo == 8) {
            /* non-Gathering has precedence over Gathering */
            ret.attrs = 8;  /* nGRE */
        } else {
            ret.attrs = 0xc; /* GRE */
        }

        /* Any location for which the resultant memory type is any
         * type of Device memory is always treated as Outer Shareable.
         */
        ret.shareability = 2;
    } else { /* Normal memory */
        /* Outer/inner cacheability combine independently */
        ret.attrs = combine_cacheattr_nibble(s1hi, s2hi) << 4
                  | combine_cacheattr_nibble(s1lo, s2lo);

        if (ret.attrs == 0x44) {
            /* Any location for which the resultant memory type is Normal
             * Inner Non-cacheable, Outer Non-cacheable is always treated
             * as Outer Shareable.
             */
            ret.shareability = 2;
        }
    }

    /* TODO: CombineS1S2Desc does not consider transient, only WB, RWA. */
    if (tagged && ret.attrs == 0xff) {
        ret.attrs = 0xf0;
    }

    return ret;
}


/* get_phys_addr - get the physical address for this virtual address
 *
 * Find the physical address corresponding to the given virtual address,
 * by doing a translation table walk on MMU based systems or using the
 * MPU state on MPU based systems.
 *
 * Returns false if the translation was successful. Otherwise, phys_ptr, attrs,
 * prot and page_size may not be filled in, and the populated fsr value provides
 * information on why the translation aborted, in the format of a
 * DFSR/IFSR fault register, with the following caveats:
 *  * we honour the short vs long DFSR format differences.
 *  * the WnR bit is never set (the caller must do this).
 *  * for PSMAv5 based systems we don't bother to return a full FSR format
 *    value.
 *
 * @env: CPUARMState
 * @address: virtual address to get physical address for
 * @access_type: 0 for read, 1 for write, 2 for execute
 * @mmu_idx: MMU index indicating required translation regime
 * @phys_ptr: set to the physical address corresponding to the virtual address
 * @attrs: set to the memory transaction attributes to use
 * @prot: set to the permissions for the page containing phys_ptr
 * @page_size: set to the size of the page containing phys_ptr
 * @fi: set to fault info if the translation fails
 * @cacheattrs: (if non-NULL) set to the cacheability/shareability attributes
 */
bool get_phys_addr(CPUARMState *env, target_ulong address,
                   MMUAccessType access_type, ARMMMUIdx mmu_idx,
                   hwaddr *phys_ptr, MemTxAttrs *attrs, int *prot,
                   target_ulong *page_size,
                   ARMMMUFaultInfo *fi, ARMCacheAttrs *cacheattrs)
{
    if (mmu_idx == ARMMMUIdx_E10_0 ||
        mmu_idx == ARMMMUIdx_E10_1 ||
        mmu_idx == ARMMMUIdx_E10_1_PAN) {
        /* Call ourselves recursively to do the stage 1 and then stage 2
         * translations.
         */
        if (arm_feature(env, ARM_FEATURE_EL2)) {
            hwaddr ipa;
            int s2_prot;
            int ret;
            ARMCacheAttrs cacheattrs2 = {};

            ret = get_phys_addr(env, address, access_type,
                                stage_1_mmu_idx(mmu_idx), &ipa, attrs,
                                prot, page_size, fi, cacheattrs);

            /* If S1 fails or S2 is disabled, return early.  */
            if (ret || regime_translation_disabled(env, ARMMMUIdx_Stage2)) {
                *phys_ptr = ipa;
                return ret;
            }

            /* S1 is done. Now do S2 translation.  */
            ret = get_phys_addr_lpae(env, ipa, access_type, ARMMMUIdx_Stage2,
                                     mmu_idx == ARMMMUIdx_E10_0,
                                     phys_ptr, attrs, &s2_prot,
                                     page_size, fi, &cacheattrs2);
            fi->s2addr = ipa;
            /* Combine the S1 and S2 perms.  */
            *prot &= s2_prot;

            /* If S2 fails, return early.  */
            if (ret) {
                return ret;
            }

            /* Combine the S1 and S2 cache attributes. */
            if (env->cp15.hcr_el2 & HCR_DC) {
                /*
                 * HCR.DC forces the first stage attributes to
                 *  Normal Non-Shareable,
                 *  Inner Write-Back Read-Allocate Write-Allocate,
                 *  Outer Write-Back Read-Allocate Write-Allocate.
                 * Do not overwrite Tagged within attrs.
                 */
                if (cacheattrs->attrs != 0xf0) {
                    cacheattrs->attrs = 0xff;
                }
                cacheattrs->shareability = 0;
            }
            *cacheattrs = combine_cacheattrs(*cacheattrs, cacheattrs2);
            return 0;
        } else {
            /*
             * For non-EL2 CPUs a stage1+stage2 translation is just stage 1.
             */
            mmu_idx = stage_1_mmu_idx(mmu_idx);
        }
    }

    /* The page table entries may downgrade secure to non-secure, but
     * cannot upgrade an non-secure translation regime's attributes
     * to secure.
     */
    attrs->secure = regime_is_secure(env, mmu_idx);
    attrs->user = regime_is_user(env, mmu_idx);

    /* Fast Context Switch Extension. This doesn't exist at all in v8.
     * In v7 and earlier it affects all stage 1 translations.
     */
    if (address < 0x02000000 && mmu_idx != ARMMMUIdx_Stage2
        && !arm_feature(env, ARM_FEATURE_V8)) {
        if (regime_el(env, mmu_idx) == 3) {
            address += env->cp15.fcseidr_s;
        } else {
            address += env->cp15.fcseidr_ns;
        }
    }

    if (arm_feature(env, ARM_FEATURE_PMSA)) {
        bool ret;
        *page_size = TARGET_PAGE_SIZE;

        if (arm_feature(env, ARM_FEATURE_V8)) {
            /* PMSAv8 */
            ret = get_phys_addr_pmsav8(env, address, access_type, mmu_idx,
                                       phys_ptr, attrs, prot, page_size, fi);
        } else if (arm_feature(env, ARM_FEATURE_V7)) {
            /* PMSAv7 */
            ret = get_phys_addr_pmsav7(env, address, access_type, mmu_idx,
                                       phys_ptr, prot, page_size, fi);
        } else {
            /* Pre-v7 MPU */
            ret = get_phys_addr_pmsav5(env, address, access_type, mmu_idx,
                                       phys_ptr, prot, fi);
        }
        qemu_log_mask(CPU_LOG_MMU, "PMSA MPU lookup for %s at 0x%08" PRIx32
                      " mmu_idx %u -> %s (prot %c%c%c)\n",
                      access_type == MMU_DATA_LOAD ? "reading" :
                      (access_type == MMU_DATA_STORE ? "writing" : "execute"),
                      (uint32_t)address, mmu_idx,
                      ret ? "Miss" : "Hit",
                      *prot & PAGE_READ ? 'r' : '-',
                      *prot & PAGE_WRITE ? 'w' : '-',
                      *prot & PAGE_EXEC ? 'x' : '-');

        return ret;
    }

    /* Definitely a real MMU, not an MPU */

    if (regime_translation_disabled(env, mmu_idx)) {
        uint64_t hcr;
        uint8_t memattr;

        /*
         * MMU disabled.  S1 addresses within aa64 translation regimes are
         * still checked for bounds -- see AArch64.TranslateAddressS1Off.
         */
        if (mmu_idx != ARMMMUIdx_Stage2) {
            int r_el = regime_el(env, mmu_idx);
            if (arm_el_is_aa64(env, r_el)) {
                int pamax = arm_pamax(env_archcpu(env));
                uint64_t tcr = env->cp15.tcr_el[r_el].raw_tcr;
                int addrtop, tbi;

                tbi = aa64_va_parameter_tbi(tcr, mmu_idx);
                if (access_type == MMU_INST_FETCH) {
                    tbi &= ~aa64_va_parameter_tbid(tcr, mmu_idx);
                }
                tbi = (tbi >> extract64(address, 55, 1)) & 1;
                addrtop = (tbi ? 55 : 63);

                if (extract64(address, pamax, addrtop - pamax + 1) != 0) {
                    fi->type = ARMFault_AddressSize;
                    fi->level = 0;
                    fi->stage2 = false;
                    return 1;
                }

                /*
                 * When TBI is disabled, we've just validated that all of the
                 * bits above PAMax are zero, so logically we only need to
                 * clear the top byte for TBI.  But it's clearer to follow
                 * the pseudocode set of addrdesc.paddress.
                 */
                address = extract64(address, 0, 52);
            }
        }
        *phys_ptr = address;
        *prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        *page_size = TARGET_PAGE_SIZE;

        /* Fill in cacheattr a-la AArch64.TranslateAddressS1Off. */
        hcr = arm_hcr_el2_eff(env);
        cacheattrs->shareability = 0;
        if (hcr & HCR_DC) {
            if (hcr & HCR_DCT) {
                memattr = 0xf0;  /* Tagged, Normal, WB, RWA */
            } else {
                memattr = 0xff;  /* Normal, WB, RWA */
            }
        } else if (access_type == MMU_INST_FETCH) {
            if (regime_sctlr(env, mmu_idx) & SCTLR_I) {
                memattr = 0xee;  /* Normal, WT, RA, NT */
            } else {
                memattr = 0x44;  /* Normal, NC, No */
            }
            cacheattrs->shareability = 2; /* outer sharable */
        } else {
            memattr = 0x00;      /* Device, nGnRnE */
        }
        cacheattrs->attrs = memattr;
        return 0;
    }

    if (regime_using_lpae_format(env, mmu_idx)) {
        return get_phys_addr_lpae(env, address, access_type, mmu_idx, false,
                                  phys_ptr, attrs, prot, page_size,
                                  fi, cacheattrs);
    } else if (regime_sctlr(env, mmu_idx) & SCTLR_XP) {
        return get_phys_addr_v6(env, address, access_type, mmu_idx,
                                phys_ptr, attrs, prot, page_size, fi);
    } else {
        return get_phys_addr_v5(env, address, access_type, mmu_idx,
                                    phys_ptr, prot, page_size, fi);
    }
}

hwaddr arm_cpu_get_phys_page_attrs_debug(CPUState *cs, vaddr addr,
                                         MemTxAttrs *attrs)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    hwaddr phys_addr;
    target_ulong page_size;
    int prot;
    bool ret;
    ARMMMUFaultInfo fi = {};
    ARMMMUIdx mmu_idx = arm_mmu_idx(env);
    ARMCacheAttrs cacheattrs = {};

    *attrs = (MemTxAttrs) {};

    ret = get_phys_addr(env, addr, 0, mmu_idx, &phys_addr,
                        attrs, &prot, &page_size, &fi, &cacheattrs);

    if (ret) {
        return -1;
    }
    return phys_addr;
}

#endif

/* Note that signed overflow is undefined in C.  The following routines are
   careful to use unsigned types where modulo arithmetic is required.
   Failure to do so _will_ break on newer gcc.  */

/* Signed saturating arithmetic.  */

/* Perform 16-bit signed saturating addition.  */
static inline uint16_t add16_sat(uint16_t a, uint16_t b)
{
    uint16_t res;

    res = a + b;
    if (((res ^ a) & 0x8000) && !((a ^ b) & 0x8000)) {
        if (a & 0x8000)
            res = 0x8000;
        else
            res = 0x7fff;
    }
    return res;
}

/* Perform 8-bit signed saturating addition.  */
static inline uint8_t add8_sat(uint8_t a, uint8_t b)
{
    uint8_t res;

    res = a + b;
    if (((res ^ a) & 0x80) && !((a ^ b) & 0x80)) {
        if (a & 0x80)
            res = 0x80;
        else
            res = 0x7f;
    }
    return res;
}

/* Perform 16-bit signed saturating subtraction.  */
static inline uint16_t sub16_sat(uint16_t a, uint16_t b)
{
    uint16_t res;

    res = a - b;
    if (((res ^ a) & 0x8000) && ((a ^ b) & 0x8000)) {
        if (a & 0x8000)
            res = 0x8000;
        else
            res = 0x7fff;
    }
    return res;
}

/* Perform 8-bit signed saturating subtraction.  */
static inline uint8_t sub8_sat(uint8_t a, uint8_t b)
{
    uint8_t res;

    res = a - b;
    if (((res ^ a) & 0x80) && ((a ^ b) & 0x80)) {
        if (a & 0x80)
            res = 0x80;
        else
            res = 0x7f;
    }
    return res;
}

#define ADD16(a, b, n) RESULT(add16_sat(a, b), n, 16);
#define SUB16(a, b, n) RESULT(sub16_sat(a, b), n, 16);
#define ADD8(a, b, n)  RESULT(add8_sat(a, b), n, 8);
#define SUB8(a, b, n)  RESULT(sub8_sat(a, b), n, 8);
#define PFX q

#include "op_addsub.h"

/* Unsigned saturating arithmetic.  */
static inline uint16_t add16_usat(uint16_t a, uint16_t b)
{
    uint16_t res;
    res = a + b;
    if (res < a)
        res = 0xffff;
    return res;
}

static inline uint16_t sub16_usat(uint16_t a, uint16_t b)
{
    if (a > b)
        return a - b;
    else
        return 0;
}

static inline uint8_t add8_usat(uint8_t a, uint8_t b)
{
    uint8_t res;
    res = a + b;
    if (res < a)
        res = 0xff;
    return res;
}

static inline uint8_t sub8_usat(uint8_t a, uint8_t b)
{
    if (a > b)
        return a - b;
    else
        return 0;
}

#define ADD16(a, b, n) RESULT(add16_usat(a, b), n, 16);
#define SUB16(a, b, n) RESULT(sub16_usat(a, b), n, 16);
#define ADD8(a, b, n)  RESULT(add8_usat(a, b), n, 8);
#define SUB8(a, b, n)  RESULT(sub8_usat(a, b), n, 8);
#define PFX uq

#include "op_addsub.h"

/* Signed modulo arithmetic.  */
#define SARITH16(a, b, n, op) do { \
    int32_t sum; \
    sum = (int32_t)(int16_t)(a) op (int32_t)(int16_t)(b); \
    RESULT(sum, n, 16); \
    if (sum >= 0) \
        ge |= 3 << (n * 2); \
    } while(0)

#define SARITH8(a, b, n, op) do { \
    int32_t sum; \
    sum = (int32_t)(int8_t)(a) op (int32_t)(int8_t)(b); \
    RESULT(sum, n, 8); \
    if (sum >= 0) \
        ge |= 1 << n; \
    } while(0)


#define ADD16(a, b, n) SARITH16(a, b, n, +)
#define SUB16(a, b, n) SARITH16(a, b, n, -)
#define ADD8(a, b, n)  SARITH8(a, b, n, +)
#define SUB8(a, b, n)  SARITH8(a, b, n, -)
#define PFX s
#define ARITH_GE

#include "op_addsub.h"

/* Unsigned modulo arithmetic.  */
#define ADD16(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint16_t)(a) + (uint32_t)(uint16_t)(b); \
    RESULT(sum, n, 16); \
    if ((sum >> 16) == 1) \
        ge |= 3 << (n * 2); \
    } while(0)

#define ADD8(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint8_t)(a) + (uint32_t)(uint8_t)(b); \
    RESULT(sum, n, 8); \
    if ((sum >> 8) == 1) \
        ge |= 1 << n; \
    } while(0)

#define SUB16(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint16_t)(a) - (uint32_t)(uint16_t)(b); \
    RESULT(sum, n, 16); \
    if ((sum >> 16) == 0) \
        ge |= 3 << (n * 2); \
    } while(0)

#define SUB8(a, b, n) do { \
    uint32_t sum; \
    sum = (uint32_t)(uint8_t)(a) - (uint32_t)(uint8_t)(b); \
    RESULT(sum, n, 8); \
    if ((sum >> 8) == 0) \
        ge |= 1 << n; \
    } while(0)

#define PFX u
#define ARITH_GE

#include "op_addsub.h"

/* Halved signed arithmetic.  */
#define ADD16(a, b, n) \
  RESULT(((int32_t)(int16_t)(a) + (int32_t)(int16_t)(b)) >> 1, n, 16)
#define SUB16(a, b, n) \
  RESULT(((int32_t)(int16_t)(a) - (int32_t)(int16_t)(b)) >> 1, n, 16)
#define ADD8(a, b, n) \
  RESULT(((int32_t)(int8_t)(a) + (int32_t)(int8_t)(b)) >> 1, n, 8)
#define SUB8(a, b, n) \
  RESULT(((int32_t)(int8_t)(a) - (int32_t)(int8_t)(b)) >> 1, n, 8)
#define PFX sh

#include "op_addsub.h"

/* Halved unsigned arithmetic.  */
#define ADD16(a, b, n) \
  RESULT(((uint32_t)(uint16_t)(a) + (uint32_t)(uint16_t)(b)) >> 1, n, 16)
#define SUB16(a, b, n) \
  RESULT(((uint32_t)(uint16_t)(a) - (uint32_t)(uint16_t)(b)) >> 1, n, 16)
#define ADD8(a, b, n) \
  RESULT(((uint32_t)(uint8_t)(a) + (uint32_t)(uint8_t)(b)) >> 1, n, 8)
#define SUB8(a, b, n) \
  RESULT(((uint32_t)(uint8_t)(a) - (uint32_t)(uint8_t)(b)) >> 1, n, 8)
#define PFX uh

#include "op_addsub.h"

static inline uint8_t do_usad(uint8_t a, uint8_t b)
{
    if (a > b)
        return a - b;
    else
        return b - a;
}

/* Unsigned sum of absolute byte differences.  */
uint32_t HELPER(usad8)(uint32_t a, uint32_t b)
{
    uint32_t sum;
    sum = do_usad(a, b);
    sum += do_usad(a >> 8, b >> 8);
    sum += do_usad(a >> 16, b >>16);
    sum += do_usad(a >> 24, b >> 24);
    return sum;
}

/* For ARMv6 SEL instruction.  */
uint32_t HELPER(sel_flags)(uint32_t flags, uint32_t a, uint32_t b)
{
    uint32_t mask;

    mask = 0;
    if (flags & 1)
        mask |= 0xff;
    if (flags & 2)
        mask |= 0xff00;
    if (flags & 4)
        mask |= 0xff0000;
    if (flags & 8)
        mask |= 0xff000000;
    return (a & mask) | (b & ~mask);
}

/* CRC helpers.
 * The upper bytes of val (above the number specified by 'bytes') must have
 * been zeroed out by the caller.
 */
uint32_t HELPER(crc32)(uint32_t acc, uint32_t val, uint32_t bytes)
{
    uint8_t buf[4];

    stl_le_p(buf, val);

    /* zlib crc32 converts the accumulator and output to one's complement.  */
    return crc32(acc ^ 0xffffffff, buf, bytes) ^ 0xffffffff;
}

uint32_t HELPER(crc32c)(uint32_t acc, uint32_t val, uint32_t bytes)
{
    uint8_t buf[4];

    stl_le_p(buf, val);

    /* Linux crc32c converts the output to one's complement.  */
    return crc32c(acc, buf, bytes) ^ 0xffffffff;
}

/* Return the exception level to which FP-disabled exceptions should
 * be taken, or 0 if FP is enabled.
 */
int fp_exception_el(CPUARMState *env, int cur_el)
{
#ifndef CONFIG_USER_ONLY
    /* CPACR and the CPTR registers don't exist before v6, so FP is
     * always accessible
     */
    if (!arm_feature(env, ARM_FEATURE_V6)) {
        return 0;
    }

    if (arm_feature(env, ARM_FEATURE_M)) {
        /* CPACR can cause a NOCP UsageFault taken to current security state */
        if (!v7m_cpacr_pass(env, env->v7m.secure, cur_el != 0)) {
            return 1;
        }

        if (arm_feature(env, ARM_FEATURE_M_SECURITY) && !env->v7m.secure) {
            if (!extract32(env->v7m.nsacr, 10, 1)) {
                /* FP insns cause a NOCP UsageFault taken to Secure */
                return 3;
            }
        }

        return 0;
    }

    /* The CPACR controls traps to EL1, or PL1 if we're 32 bit:
     * 0, 2 : trap EL0 and EL1/PL1 accesses
     * 1    : trap only EL0 accesses
     * 3    : trap no accesses
     * This register is ignored if E2H+TGE are both set.
     */
    if ((arm_hcr_el2_eff(env) & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
        int fpen = extract32(env->cp15.cpacr_el1, 20, 2);

        switch (fpen) {
        case 0:
        case 2:
            if (cur_el == 0 || cur_el == 1) {
                /* Trap to PL1, which might be EL1 or EL3 */
                if (arm_is_secure(env) && !arm_el_is_aa64(env, 3)) {
                    return 3;
                }
                return 1;
            }
            if (cur_el == 3 && !is_a64(env)) {
                /* Secure PL1 running at EL3 */
                return 3;
            }
            break;
        case 1:
            if (cur_el == 0) {
                return 1;
            }
            break;
        case 3:
            break;
        }
    }

    /*
     * The NSACR allows A-profile AArch32 EL3 and M-profile secure mode
     * to control non-secure access to the FPU. It doesn't have any
     * effect if EL3 is AArch64 or if EL3 doesn't exist at all.
     */
    if ((arm_feature(env, ARM_FEATURE_EL3) && !arm_el_is_aa64(env, 3) &&
         cur_el <= 2 && !arm_is_secure_below_el3(env))) {
        if (!extract32(env->cp15.nsacr, 10, 1)) {
            /* FP insns act as UNDEF */
            return cur_el == 2 ? 2 : 1;
        }
    }

    /* For the CPTR registers we don't need to guard with an ARM_FEATURE
     * check because zero bits in the registers mean "don't trap".
     */

    /* CPTR_EL2 : present in v7VE or v8 */
    if (cur_el <= 2 && extract32(env->cp15.cptr_el[2], 10, 1)
        && !arm_is_secure_below_el3(env)) {
        /* Trap FP ops at EL2, NS-EL1 or NS-EL0 to EL2 */
        return 2;
    }

    /* CPTR_EL3 : present in v8 */
    if (extract32(env->cp15.cptr_el[3], 10, 1)) {
        /* Trap all FP ops to EL3 */
        return 3;
    }
#endif
    return 0;
}

/* Return the exception level we're running at if this is our mmu_idx */
int arm_mmu_idx_to_el(ARMMMUIdx mmu_idx)
{
    if (mmu_idx & ARM_MMU_IDX_M) {
        return mmu_idx & ARM_MMU_IDX_M_PRIV;
    }

    switch (mmu_idx) {
    case ARMMMUIdx_E10_0:
    case ARMMMUIdx_E20_0:
    case ARMMMUIdx_SE10_0:
        return 0;
    case ARMMMUIdx_E10_1:
    case ARMMMUIdx_E10_1_PAN:
    case ARMMMUIdx_SE10_1:
    case ARMMMUIdx_SE10_1_PAN:
        return 1;
    case ARMMMUIdx_E2:
    case ARMMMUIdx_E20_2:
    case ARMMMUIdx_E20_2_PAN:
        return 2;
    case ARMMMUIdx_SE3:
        return 3;
    default:
        g_assert_not_reached();
    }
}

#ifndef CONFIG_TCG
ARMMMUIdx arm_v7m_mmu_idx_for_secstate(CPUARMState *env, bool secstate)
{
    g_assert_not_reached();
}
#endif

ARMMMUIdx arm_mmu_idx_el(CPUARMState *env, int el)
{
    if (arm_feature(env, ARM_FEATURE_M)) {
        return arm_v7m_mmu_idx_for_secstate(env, env->v7m.secure);
    }

    /* See ARM pseudo-function ELIsInHost.  */
    switch (el) {
    case 0:
        if (arm_is_secure_below_el3(env)) {
            return ARMMMUIdx_SE10_0;
        }
        if ((env->cp15.hcr_el2 & (HCR_E2H | HCR_TGE)) == (HCR_E2H | HCR_TGE)
            && arm_el_is_aa64(env, 2)) {
            return ARMMMUIdx_E20_0;
        }
        return ARMMMUIdx_E10_0;
    case 1:
        if (arm_is_secure_below_el3(env)) {
            if (env->pstate & PSTATE_PAN) {
                return ARMMMUIdx_SE10_1_PAN;
            }
            return ARMMMUIdx_SE10_1;
        }
        if (env->pstate & PSTATE_PAN) {
            return ARMMMUIdx_E10_1_PAN;
        }
        return ARMMMUIdx_E10_1;
    case 2:
        /* TODO: ARMv8.4-SecEL2 */
        /* Note that TGE does not apply at EL2.  */
        if ((env->cp15.hcr_el2 & HCR_E2H) && arm_el_is_aa64(env, 2)) {
            if (env->pstate & PSTATE_PAN) {
                return ARMMMUIdx_E20_2_PAN;
            }
            return ARMMMUIdx_E20_2;
        }
        return ARMMMUIdx_E2;
    case 3:
        return ARMMMUIdx_SE3;
    default:
        g_assert_not_reached();
    }
}

ARMMMUIdx arm_mmu_idx(CPUARMState *env)
{
    return arm_mmu_idx_el(env, arm_current_el(env));
}

#ifndef CONFIG_USER_ONLY
ARMMMUIdx arm_stage1_mmu_idx(CPUARMState *env)
{
    return stage_1_mmu_idx(arm_mmu_idx(env));
}
#endif

static uint32_t rebuild_hflags_common(CPUARMState *env, int fp_el,
                                      ARMMMUIdx mmu_idx, uint32_t flags)
{
    flags = FIELD_DP32(flags, TBFLAG_ANY, FPEXC_EL, fp_el);
    flags = FIELD_DP32(flags, TBFLAG_ANY, MMUIDX,
                       arm_to_core_mmu_idx(mmu_idx));

    if (arm_singlestep_active(env)) {
        flags = FIELD_DP32(flags, TBFLAG_ANY, SS_ACTIVE, 1);
    }
    return flags;
}

static uint32_t rebuild_hflags_common_32(CPUARMState *env, int fp_el,
                                         ARMMMUIdx mmu_idx, uint32_t flags)
{
    bool sctlr_b = arm_sctlr_b(env);

    if (sctlr_b) {
        flags = FIELD_DP32(flags, TBFLAG_A32, SCTLR_B, 1);
    }
    if (arm_cpu_data_is_big_endian_a32(env, sctlr_b)) {
        flags = FIELD_DP32(flags, TBFLAG_ANY, BE_DATA, 1);
    }
    flags = FIELD_DP32(flags, TBFLAG_A32, NS, !access_secure_reg(env));

    return rebuild_hflags_common(env, fp_el, mmu_idx, flags);
}

static uint32_t rebuild_hflags_m32(CPUARMState *env, int fp_el,
                                   ARMMMUIdx mmu_idx)
{
    uint32_t flags = 0;

    if (arm_v7m_is_handler_mode(env)) {
        flags = FIELD_DP32(flags, TBFLAG_M32, HANDLER, 1);
    }

    /*
     * v8M always applies stack limit checks unless CCR.STKOFHFNMIGN
     * is suppressing them because the requested execution priority
     * is less than 0.
     */
    if (arm_feature(env, ARM_FEATURE_V8) &&
        !((mmu_idx & ARM_MMU_IDX_M_NEGPRI) &&
          (env->v7m.ccr[env->v7m.secure] & R_V7M_CCR_STKOFHFNMIGN_MASK))) {
        flags = FIELD_DP32(flags, TBFLAG_M32, STACKCHECK, 1);
    }

    return rebuild_hflags_common_32(env, fp_el, mmu_idx, flags);
}

static uint32_t rebuild_hflags_aprofile(CPUARMState *env)
{
    int flags = 0;

    flags = FIELD_DP32(flags, TBFLAG_ANY, DEBUG_TARGET_EL,
                       arm_debug_target_el(env));
    return flags;
}

static uint32_t rebuild_hflags_a32(CPUARMState *env, int fp_el,
                                   ARMMMUIdx mmu_idx)
{
    uint32_t flags = rebuild_hflags_aprofile(env);

    if (arm_el_is_aa64(env, 1)) {
        flags = FIELD_DP32(flags, TBFLAG_A32, VFPEN, 1);
    }

    if (arm_current_el(env) < 2 && env->cp15.hstr_el2 &&
        (arm_hcr_el2_eff(env) & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE)) {
        flags = FIELD_DP32(flags, TBFLAG_A32, HSTR_ACTIVE, 1);
    }

    return rebuild_hflags_common_32(env, fp_el, mmu_idx, flags);
}

static uint32_t rebuild_hflags_a64(CPUARMState *env, int el, int fp_el,
                                   ARMMMUIdx mmu_idx)
{
    uint32_t flags = rebuild_hflags_aprofile(env);
    ARMMMUIdx stage1 = stage_1_mmu_idx(mmu_idx);
    uint64_t tcr = regime_tcr(env, mmu_idx)->raw_tcr;
    uint64_t sctlr;
    int tbii, tbid;

    flags = FIELD_DP32(flags, TBFLAG_ANY, AARCH64_STATE, 1);

    /* Get control bits for tagged addresses.  */
    tbid = aa64_va_parameter_tbi(tcr, mmu_idx);
    tbii = tbid & ~aa64_va_parameter_tbid(tcr, mmu_idx);

    flags = FIELD_DP32(flags, TBFLAG_A64, TBII, tbii);
    flags = FIELD_DP32(flags, TBFLAG_A64, TBID, tbid);

    if (cpu_isar_feature(aa64_sve, env_archcpu(env))) {
        int sve_el = sve_exception_el(env, el);
        uint32_t zcr_len;

        /*
         * If SVE is disabled, but FP is enabled,
         * then the effective len is 0.
         */
        if (sve_el != 0 && fp_el == 0) {
            zcr_len = 0;
        } else {
            zcr_len = sve_zcr_len_for_el(env, el);
        }
        flags = FIELD_DP32(flags, TBFLAG_A64, SVEEXC_EL, sve_el);
        flags = FIELD_DP32(flags, TBFLAG_A64, ZCR_LEN, zcr_len);
    }

    sctlr = regime_sctlr(env, stage1);

    if (arm_cpu_data_is_big_endian_a64(el, sctlr)) {
        flags = FIELD_DP32(flags, TBFLAG_ANY, BE_DATA, 1);
    }

    if (cpu_isar_feature(aa64_pauth, env_archcpu(env))) {
        /*
         * In order to save space in flags, we record only whether
         * pauth is "inactive", meaning all insns are implemented as
         * a nop, or "active" when some action must be performed.
         * The decision of which action to take is left to a helper.
         */
        if (sctlr & (SCTLR_EnIA | SCTLR_EnIB | SCTLR_EnDA | SCTLR_EnDB)) {
            flags = FIELD_DP32(flags, TBFLAG_A64, PAUTH_ACTIVE, 1);
        }
    }

    if (cpu_isar_feature(aa64_bti, env_archcpu(env))) {
        /* Note that SCTLR_EL[23].BT == SCTLR_BT1.  */
        if (sctlr & (el == 0 ? SCTLR_BT0 : SCTLR_BT1)) {
            flags = FIELD_DP32(flags, TBFLAG_A64, BT, 1);
        }
    }

    /* Compute the condition for using AccType_UNPRIV for LDTR et al. */
    if (!(env->pstate & PSTATE_UAO)) {
        switch (mmu_idx) {
        case ARMMMUIdx_E10_1:
        case ARMMMUIdx_E10_1_PAN:
        case ARMMMUIdx_SE10_1:
        case ARMMMUIdx_SE10_1_PAN:
            /* TODO: ARMv8.3-NV */
            flags = FIELD_DP32(flags, TBFLAG_A64, UNPRIV, 1);
            break;
        case ARMMMUIdx_E20_2:
        case ARMMMUIdx_E20_2_PAN:
            /* TODO: ARMv8.4-SecEL2 */
            /*
             * Note that EL20_2 is gated by HCR_EL2.E2H == 1, but EL20_0 is
             * gated by HCR_EL2.<E2H,TGE> == '11', and so is LDTR.
             */
            if (env->cp15.hcr_el2 & HCR_TGE) {
                flags = FIELD_DP32(flags, TBFLAG_A64, UNPRIV, 1);
            }
            break;
        default:
            break;
        }
    }

    if (cpu_isar_feature(aa64_mte, env_archcpu(env))) {
        /*
         * Set MTE_ACTIVE if any access may be Checked, and leave clear
         * if all accesses must be Unchecked:
         * 1) If no TBI, then there are no tags in the address to check,
         * 2) If Tag Check Override, then all accesses are Unchecked,
         * 3) If Tag Check Fail == 0, then Checked access have no effect,
         * 4) If no Allocation Tag Access, then all accesses are Unchecked.
         */
        if (allocation_tag_access_enabled(env, el, sctlr)) {
            flags = FIELD_DP32(flags, TBFLAG_A64, ATA, 1);
            if (tbid
                && !(env->pstate & PSTATE_TCO)
                && (sctlr & (el == 0 ? SCTLR_TCF0 : SCTLR_TCF))) {
                flags = FIELD_DP32(flags, TBFLAG_A64, MTE_ACTIVE, 1);
            }
        }
        /* And again for unprivileged accesses, if required.  */
        if (FIELD_EX32(flags, TBFLAG_A64, UNPRIV)
            && tbid
            && !(env->pstate & PSTATE_TCO)
            && (sctlr & SCTLR_TCF0)
            && allocation_tag_access_enabled(env, 0, sctlr)) {
            flags = FIELD_DP32(flags, TBFLAG_A64, MTE0_ACTIVE, 1);
        }
        /* Cache TCMA as well as TBI. */
        flags = FIELD_DP32(flags, TBFLAG_A64, TCMA,
                           aa64_va_parameter_tcma(tcr, mmu_idx));
    }

    return rebuild_hflags_common(env, fp_el, mmu_idx, flags);
}

static uint32_t rebuild_hflags_internal(CPUARMState *env)
{
    int el = arm_current_el(env);
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    if (is_a64(env)) {
        return rebuild_hflags_a64(env, el, fp_el, mmu_idx);
    } else if (arm_feature(env, ARM_FEATURE_M)) {
        return rebuild_hflags_m32(env, fp_el, mmu_idx);
    } else {
        return rebuild_hflags_a32(env, fp_el, mmu_idx);
    }
}

void arm_rebuild_hflags(CPUARMState *env)
{
    env->hflags = rebuild_hflags_internal(env);
}

/*
 * If we have triggered a EL state change we can't rely on the
 * translator having passed it to us, we need to recompute.
 */
void HELPER(rebuild_hflags_m32_newel)(CPUARMState *env)
{
    int el = arm_current_el(env);
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);
    env->hflags = rebuild_hflags_m32(env, fp_el, mmu_idx);
}

void HELPER(rebuild_hflags_m32)(CPUARMState *env, int el)
{
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    env->hflags = rebuild_hflags_m32(env, fp_el, mmu_idx);
}

/*
 * If we have triggered a EL state change we can't rely on the
 * translator having passed it to us, we need to recompute.
 */
void HELPER(rebuild_hflags_a32_newel)(CPUARMState *env)
{
    int el = arm_current_el(env);
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);
    env->hflags = rebuild_hflags_a32(env, fp_el, mmu_idx);
}

void HELPER(rebuild_hflags_a32)(CPUARMState *env, int el)
{
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    env->hflags = rebuild_hflags_a32(env, fp_el, mmu_idx);
}

void HELPER(rebuild_hflags_a64)(CPUARMState *env, int el)
{
    int fp_el = fp_exception_el(env, el);
    ARMMMUIdx mmu_idx = arm_mmu_idx_el(env, el);

    env->hflags = rebuild_hflags_a64(env, el, fp_el, mmu_idx);
}

static inline void assert_hflags_rebuild_correctly(CPUARMState *env)
{
#ifdef CONFIG_DEBUG_TCG
    uint32_t env_flags_current = env->hflags;
    uint32_t env_flags_rebuilt = rebuild_hflags_internal(env);

    if (unlikely(env_flags_current != env_flags_rebuilt)) {
        fprintf(stderr, "TCG hflags mismatch (current:0x%08x rebuilt:0x%08x)\n",
                env_flags_current, env_flags_rebuilt);
        abort();
    }
#endif
}

void cpu_get_tb_cpu_state(CPUARMState *env, target_ulong *pc,
                          target_ulong *cs_base, uint32_t *pflags)
{
    uint32_t flags = env->hflags;
    uint32_t pstate_for_ss;

    *cs_base = 0;
    assert_hflags_rebuild_correctly(env);

    if (FIELD_EX32(flags, TBFLAG_ANY, AARCH64_STATE)) {
        *pc = env->pc;
        if (cpu_isar_feature(aa64_bti, env_archcpu(env))) {
            flags = FIELD_DP32(flags, TBFLAG_A64, BTYPE, env->btype);
        }
        pstate_for_ss = env->pstate;
    } else {
        *pc = env->regs[15];

        if (arm_feature(env, ARM_FEATURE_M)) {
            if (arm_feature(env, ARM_FEATURE_M_SECURITY) &&
                FIELD_EX32(env->v7m.fpccr[M_REG_S], V7M_FPCCR, S)
                != env->v7m.secure) {
                flags = FIELD_DP32(flags, TBFLAG_M32, FPCCR_S_WRONG, 1);
            }

            if ((env->v7m.fpccr[env->v7m.secure] & R_V7M_FPCCR_ASPEN_MASK) &&
                (!(env->v7m.control[M_REG_S] & R_V7M_CONTROL_FPCA_MASK) ||
                 (env->v7m.secure &&
                  !(env->v7m.control[M_REG_S] & R_V7M_CONTROL_SFPA_MASK)))) {
                /*
                 * ASPEN is set, but FPCA/SFPA indicate that there is no
                 * active FP context; we must create a new FP context before
                 * executing any FP insn.
                 */
                flags = FIELD_DP32(flags, TBFLAG_M32, NEW_FP_CTXT_NEEDED, 1);
            }

            bool is_secure = env->v7m.fpccr[M_REG_S] & R_V7M_FPCCR_S_MASK;
            if (env->v7m.fpccr[is_secure] & R_V7M_FPCCR_LSPACT_MASK) {
                flags = FIELD_DP32(flags, TBFLAG_M32, LSPACT, 1);
            }
        } else {
            /*
             * Note that XSCALE_CPAR shares bits with VECSTRIDE.
             * Note that VECLEN+VECSTRIDE are RES0 for M-profile.
             */
            if (arm_feature(env, ARM_FEATURE_XSCALE)) {
                flags = FIELD_DP32(flags, TBFLAG_A32,
                                   XSCALE_CPAR, env->cp15.c15_cpar);
            } else {
                flags = FIELD_DP32(flags, TBFLAG_A32, VECLEN,
                                   env->vfp.vec_len);
                flags = FIELD_DP32(flags, TBFLAG_A32, VECSTRIDE,
                                   env->vfp.vec_stride);
            }
            if (env->vfp.xregs[ARM_VFP_FPEXC] & (1 << 30)) {
                flags = FIELD_DP32(flags, TBFLAG_A32, VFPEN, 1);
            }
        }

        flags = FIELD_DP32(flags, TBFLAG_AM32, THUMB, env->thumb);
        flags = FIELD_DP32(flags, TBFLAG_AM32, CONDEXEC, env->condexec_bits);
        pstate_for_ss = env->uncached_cpsr;
    }

    /*
     * The SS_ACTIVE and PSTATE_SS bits correspond to the state machine
     * states defined in the ARM ARM for software singlestep:
     *  SS_ACTIVE   PSTATE.SS   State
     *     0            x       Inactive (the TB flag for SS is always 0)
     *     1            0       Active-pending
     *     1            1       Active-not-pending
     * SS_ACTIVE is set in hflags; PSTATE_SS is computed every TB.
     */
    if (FIELD_EX32(flags, TBFLAG_ANY, SS_ACTIVE) &&
        (pstate_for_ss & PSTATE_SS)) {
        flags = FIELD_DP32(flags, TBFLAG_ANY, PSTATE_SS, 1);
    }

    *pflags = flags;
}

#ifdef TARGET_AARCH64
/*
 * The manual says that when SVE is enabled and VQ is widened the
 * implementation is allowed to zero the previously inaccessible
 * portion of the registers.  The corollary to that is that when
 * SVE is enabled and VQ is narrowed we are also allowed to zero
 * the now inaccessible portion of the registers.
 *
 * The intent of this is that no predicate bit beyond VQ is ever set.
 * Which means that some operations on predicate registers themselves
 * may operate on full uint64_t or even unrolled across the maximum
 * uint64_t[4].  Performing 4 bits of host arithmetic unconditionally
 * may well be cheaper than conditionals to restrict the operation
 * to the relevant portion of a uint16_t[16].
 */
void aarch64_sve_narrow_vq(CPUARMState *env, unsigned vq)
{
    int i, j;
    uint64_t pmask;

    assert(vq >= 1 && vq <= ARM_MAX_VQ);
    assert(vq <= env_archcpu(env)->sve_max_vq);

    /* Zap the high bits of the zregs.  */
    for (i = 0; i < 32; i++) {
        memset(&env->vfp.zregs[i].d[2 * vq], 0, 16 * (ARM_MAX_VQ - vq));
    }

    /* Zap the high bits of the pregs and ffr.  */
    pmask = 0;
    if (vq & 3) {
        pmask = ~(-1ULL << (16 * (vq & 3)));
    }
    for (j = vq / 4; j < ARM_MAX_VQ / 4; j++) {
        for (i = 0; i < 17; ++i) {
            env->vfp.pregs[i].p[j] &= pmask;
        }
        pmask = 0;
    }
}

/*
 * Notice a change in SVE vector size when changing EL.
 */
void aarch64_sve_change_el(CPUARMState *env, int old_el,
                           int new_el, bool el0_a64)
{
    ARMCPU *cpu = env_archcpu(env);
    int old_len, new_len;
    bool old_a64, new_a64;

    /* Nothing to do if no SVE.  */
    if (!cpu_isar_feature(aa64_sve, cpu)) {
        return;
    }

    /* Nothing to do if FP is disabled in either EL.  */
    if (fp_exception_el(env, old_el) || fp_exception_el(env, new_el)) {
        return;
    }

    /*
     * DDI0584A.d sec 3.2: "If SVE instructions are disabled or trapped
     * at ELx, or not available because the EL is in AArch32 state, then
     * for all purposes other than a direct read, the ZCR_ELx.LEN field
     * has an effective value of 0".
     *
     * Consider EL2 (aa64, vq=4) -> EL0 (aa32) -> EL1 (aa64, vq=0).
     * If we ignore aa32 state, we would fail to see the vq4->vq0 transition
     * from EL2->EL1.  Thus we go ahead and narrow when entering aa32 so that
     * we already have the correct register contents when encountering the
     * vq0->vq0 transition between EL0->EL1.
     */
    old_a64 = old_el ? arm_el_is_aa64(env, old_el) : el0_a64;
    old_len = (old_a64 && !sve_exception_el(env, old_el)
               ? sve_zcr_len_for_el(env, old_el) : 0);
    new_a64 = new_el ? arm_el_is_aa64(env, new_el) : el0_a64;
    new_len = (new_a64 && !sve_exception_el(env, new_el)
               ? sve_zcr_len_for_el(env, new_el) : 0);

    /* When changing vector length, clear inaccessible state.  */
    if (new_len < old_len) {
        aarch64_sve_narrow_vq(env, new_len + 1);
    }
}
#endif
