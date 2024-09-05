/*
 *  Copyright(c) 2019-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "internal.h"
#include "exec/exec-all.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "fpu/softfloat-helpers.h"
#include "tcg/tcg.h"
#include "exec/gdbstub.h"
#include "cpu_helper.h"
#include "max.h"
#include "hex_mmu.h"

#ifndef CONFIG_USER_ONLY
#include "macros.h"
#include "sys_macros.h"
#include "qemu/main-loop.h"
#include "hex_interrupts.h"
#endif

static void hexagon_v66_cpu_init(Object *obj) { }
static void hexagon_v67_cpu_init(Object *obj) { }
static void hexagon_v68_cpu_init(Object *obj) { }
static void hexagon_v69_cpu_init(Object *obj) { }
static void hexagon_v71_cpu_init(Object *obj) { }
static void hexagon_v73_cpu_init(Object *obj) { }

static ObjectClass *hexagon_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;
    char **cpuname;

    cpuname = g_strsplit(cpu_model, ",", 1);
    typename = g_strdup_printf(HEXAGON_CPU_TYPE_NAME("%s"), cpuname[0]);
    oc = object_class_by_name(typename);
    g_strfreev(cpuname);
    g_free(typename);

    return oc;
}

static const Property hexagon_cpu_properties[] = {
#if !defined(CONFIG_USER_ONLY)
    DEFINE_PROP_UINT32("num-tlbs", HexagonCPU, num_tlbs, MAX_TLB_ENTRIES),
    DEFINE_PROP_UINT32("l2vic-base-addr", HexagonCPU, l2vic_base_addr,
        0xffffffffULL),
    DEFINE_PROP_UINT32("hvx-contexts", HexagonCPU, hvx_contexts, 0),
    DEFINE_PROP_UINT32("exec-start-addr", HexagonCPU, boot_addr, 0xffffffffULL),
#endif
    DEFINE_PROP_BOOL("lldb-compat", HexagonCPU, lldb_compat, false),
    DEFINE_PROP_UNSIGNED("lldb-stack-adjust", HexagonCPU, lldb_stack_adjust, 0,
                         qdev_prop_uint32, target_ulong),
    DEFINE_PROP_BOOL("short-circuit", HexagonCPU, short_circuit, true),
    DEFINE_PROP_END_OF_LIST()
};

const char * const hexagon_regnames[TOTAL_PER_THREAD_REGS] = {
   "r0", "r1",  "r2",  "r3",  "r4",   "r5",  "r6",  "r7",
   "r8", "r9",  "r10", "r11", "r12",  "r13", "r14", "r15",
  "r16", "r17", "r18", "r19", "r20",  "r21", "r22", "r23",
  "r24", "r25", "r26", "r27", "r28",  "r29", "r30", "r31",
  "sa0", "lc0", "sa1", "lc1", "p3_0", "c5",  "m0",  "m1",
  "usr", "pc",  "ugp", "gp",  "cs0",  "cs1", "c14", "c15",
  "c16", "c17", "c18", "c19", "pkt_cnt",  "insn_cnt", "hvx_cnt", "c23",
  "c24", "c25", "c26", "c27", "c28",  "c29", "c30", "c31",
};

/*
 * One of the main debugging techniques is to use "-d cpu" and compare against
 * LLDB output when single stepping.  However, the target and qemu put the
 * stacks at different locations.  This is used to compensate so the diff is
 * cleaner.
 */
static target_ulong adjust_stack_ptrs(CPUHexagonState *env, target_ulong addr)
{
    HexagonCPU *cpu = env_archcpu(env);
    target_ulong stack_adjust = cpu->lldb_stack_adjust;
    target_ulong stack_start = env->stack_start;
    target_ulong stack_size = 0x10000;

    if (stack_adjust == 0) {
        return addr;
    }

    if (stack_start + 0x1000 >= addr && addr >= (stack_start - stack_size)) {
        return addr - stack_adjust;
    }
    return addr;
}

/* HEX_REG_P3_0_ALIASED (aka C4) is an alias for the predicate registers */
static target_ulong read_p3_0(CPUHexagonState *env)
{
    int32_t control_reg = 0;
    int i;
    for (i = NUM_PREGS - 1; i >= 0; i--) {
        control_reg <<= 8;
        control_reg |= env->pred[i] & 0xff;
    }
    return control_reg;
}

static void print_reg(FILE *f, CPUHexagonState *env, int regnum)
{
    target_ulong value;

    if (regnum == HEX_REG_P3_0_ALIASED) {
        value = read_p3_0(env);
    } else {
        value = regnum < 32 ? adjust_stack_ptrs(env, env->gpr[regnum])
                            : env->gpr[regnum];
    }

    qemu_fprintf(f, "  %s = 0x" TARGET_FMT_lx "\n",
                 hexagon_regnames[regnum], value);
}

static void print_vreg(FILE *f, CPUHexagonState *env, int regnum,
                       bool skip_if_zero)
{
    if (skip_if_zero) {
        bool nonzero_found = false;
        for (int i = 0; i < MAX_VEC_SIZE_BYTES; i++) {
            if (env->VRegs[regnum].ub[i] != 0) {
                nonzero_found = true;
                break;
            }
        }
        if (!nonzero_found) {
            return;
        }
    }

    qemu_fprintf(f, "  v%d = ( ", regnum);
    qemu_fprintf(f, "0x%02x", env->VRegs[regnum].ub[MAX_VEC_SIZE_BYTES - 1]);
    for (int i = MAX_VEC_SIZE_BYTES - 2; i >= 0; i--) {
        qemu_fprintf(f, ", 0x%02x", env->VRegs[regnum].ub[i]);
    }
    qemu_fprintf(f, " )\n");
}

void hexagon_debug_vreg(CPUHexagonState *env, int regnum)
{
    print_vreg(stdout, env, regnum, false);
}

static void print_qreg(FILE *f, CPUHexagonState *env, int regnum,
                       bool skip_if_zero)
{
    if (skip_if_zero) {
        bool nonzero_found = false;
        for (int i = 0; i < MAX_VEC_SIZE_BYTES / 8; i++) {
            if (env->QRegs[regnum].ub[i] != 0) {
                nonzero_found = true;
                break;
            }
        }
        if (!nonzero_found) {
            return;
        }
    }

    qemu_fprintf(f, "  q%d = ( ", regnum);
    qemu_fprintf(f, "0x%02x",
                 env->QRegs[regnum].ub[MAX_VEC_SIZE_BYTES / 8 - 1]);
    for (int i = MAX_VEC_SIZE_BYTES / 8 - 2; i >= 0; i--) {
        qemu_fprintf(f, ", 0x%02x", env->QRegs[regnum].ub[i]);
    }
    qemu_fprintf(f, " )\n");
}

void hexagon_debug_qreg(CPUHexagonState *env, int regnum)
{
    print_qreg(stdout, env, regnum, false);
}

static void hexagon_dump(CPUHexagonState *env, FILE *f, int flags)
{
    HexagonCPU *cpu = env_archcpu(env);

    if (cpu->lldb_compat) {
        /*
         * When comparing with LLDB, it doesn't step through single-cycle
         * hardware loops the same way.  So, we just skip them here
         */
        if (env->gpr[HEX_REG_PC] == env->last_pc_dumped) {
            return;
        }
        env->last_pc_dumped = env->gpr[HEX_REG_PC];
    }

    qemu_fprintf(f, "General Purpose Registers = {\n");
    for (int i = 0; i < 32; i++) {
        print_reg(f, env, i);
    }
    print_reg(f, env, HEX_REG_SA0);
    print_reg(f, env, HEX_REG_LC0);
    print_reg(f, env, HEX_REG_SA1);
    print_reg(f, env, HEX_REG_LC1);
    print_reg(f, env, HEX_REG_M0);
    print_reg(f, env, HEX_REG_M1);
    print_reg(f, env, HEX_REG_USR);
    print_reg(f, env, HEX_REG_P3_0_ALIASED);
    print_reg(f, env, HEX_REG_GP);
    print_reg(f, env, HEX_REG_UGP);
    print_reg(f, env, HEX_REG_PC);
#ifdef CONFIG_USER_ONLY
    /*
     * Not modelled in user mode, print junk to minimize the diff's
     * with LLDB output
     */
    qemu_fprintf(f, "  cause = 0x000000db\n");
    qemu_fprintf(f, "  badva = 0x00000000\n");
    qemu_fprintf(f, "  cs0 = 0x00000000\n");
    qemu_fprintf(f, "  cs1 = 0x00000000\n");
#else
    print_reg(f, env, HEX_SREG_BADVA);
    print_reg(f, env, HEX_REG_CS0);
    print_reg(f, env, HEX_REG_CS1);
#endif
    qemu_fprintf(f, "}\n");

    if (flags & CPU_DUMP_FPU) {
        qemu_fprintf(f, "Vector Registers = {\n");
        for (int i = 0; i < NUM_VREGS; i++) {
            print_vreg(f, env, i, true);
        }
        for (int i = 0; i < NUM_QREGS; i++) {
            print_qreg(f, env, i, true);
        }
        qemu_fprintf(f, "}\n");
    }
}

static void hexagon_dump_state(CPUState *cs, FILE *f, int flags)
{
    hexagon_dump(cpu_env(cs), f, flags);
}

void hexagon_debug(CPUHexagonState *env)
{
    hexagon_dump(env, stdout, CPU_DUMP_FPU);
}

static void hexagon_cpu_set_pc(CPUState *cs, vaddr value)
{
    cpu_env(cs)->gpr[HEX_REG_PC] = value;
}

static vaddr hexagon_cpu_get_pc(CPUState *cs)
{
    return cpu_env(cs)->gpr[HEX_REG_PC];
}

static void hexagon_cpu_synchronize_from_tb(CPUState *cs,
                                            const TranslationBlock *tb)
{
    tcg_debug_assert(!tcg_cflags_has(cs, CF_PCREL));
    cpu_env(cs)->gpr[HEX_REG_PC] = tb->pc;
}

#ifndef CONFIG_USER_ONLY
bool hexagon_thread_is_enabled(CPUHexagonState *env)
{
    target_ulong modectl = ARCH_GET_SYSTEM_REG(env, HEX_SREG_MODECTL);
    uint32_t thread_enabled_mask = GET_FIELD(MODECTL_E, modectl);
    bool E_bit = thread_enabled_mask & (0x1 << env->threadId);

    return E_bit;
}
#endif

static bool hexagon_cpu_has_work(CPUState *cs)
{
#ifndef CONFIG_USER_ONLY
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;

    return hexagon_thread_is_enabled(env) &&
        (cs->interrupt_request & (CPU_INTERRUPT_HARD | CPU_INTERRUPT_SWI
            | CPU_INTERRUPT_K0_UNLOCK | CPU_INTERRUPT_TLB_UNLOCK));
#else
    return true;
#endif
}

static void hexagon_restore_state_to_opc(CPUState *cs,
                                         const TranslationBlock *tb,
                                         const uint64_t *data)
{
    cpu_env(cs)->gpr[HEX_REG_PC] = data[0];
}


#ifndef CONFIG_USER_ONLY
static void mmu_reset(CPUHexagonState *env)
{
    CPUState *cs = env_cpu(env);
    if (cs->cpu_index == 0) {
        memset(env->hex_tlb, 0, sizeof(*env->hex_tlb));
    }
}

void hexagon_cpu_soft_reset(CPUHexagonState *env)
{
    BQL_LOCK_GUARD();
    ARCH_SET_SYSTEM_REG(env, HEX_SREG_SSR, 0);
    hexagon_ssr_set_cause(env, HEX_CAUSE_RESET);

    target_ulong evb = ARCH_GET_SYSTEM_REG(env, HEX_SREG_EVB);
    ARCH_SET_THREAD_REG(env, HEX_REG_PC, evb);
}
#endif

static void hexagon_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    HexagonCPUClass *mcc = HEXAGON_CPU_GET_CLASS(obj);
    CPUHexagonState *env = cpu_env(cs);

    if (mcc->parent_phases.hold) {
        mcc->parent_phases.hold(obj, type);
    }

    set_default_nan_mode(1, &env->fp_status);
    set_float_detect_tininess(float_tininess_before_rounding, &env->fp_status);
    /* Default NaN value: sign bit set, all frac bits set */
    set_float_default_nan_pattern(0b11111111, &env->fp_status);

#ifndef CONFIG_USER_ONLY
    HexagonCPU *cpu = HEXAGON_CPU(cs);

    if (cs->cpu_index == 0) {
        memset(env->g_sreg, 0, sizeof(target_ulong) * NUM_SREGS);
    }
    memset(env->t_sreg, 0, sizeof(target_ulong) * NUM_SREGS);
    memset(env->greg, 0, sizeof(target_ulong) * NUM_GREGS);

    if (cs->cpu_index == 0) {
        ARCH_SET_SYSTEM_REG(env, HEX_SREG_MODECTL, 0x1);
        *(env->g_pcycle_base) = 0;
    }
    mmu_reset(env);
    ARCH_SET_SYSTEM_REG(env, HEX_SREG_HTID, cs->cpu_index);
    hexagon_cpu_soft_reset(env);
    env->threadId = cs->cpu_index;
    env->tlb_lock_state = HEX_LOCK_UNLOCKED;
    env->k0_lock_state = HEX_LOCK_UNLOCKED;
    env->tlb_lock_count = 0;
    env->k0_lock_count = 0;
    env->next_PC = 0;
    env->wait_next_pc = 0;
    env->cause_code = -1;
    ARCH_SET_THREAD_REG(env, HEX_REG_PC, cpu->boot_addr);
#endif
}

static void hexagon_cpu_disas_set_info(CPUState *s, disassemble_info *info)
{
    info->print_insn = print_insn_hexagon;
}

static void hexagon_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    HexagonCPUClass *mcc = HEXAGON_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

#ifndef CONFIG_USER_ONLY
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    if (cpu->num_tlbs > MAX_TLB_ENTRIES) {
        error_setg(errp, "Number of TLBs selected is invalid");
        return;
    }
#endif

    gdb_register_coprocessor(cs, hexagon_hvx_gdb_read_register,
                             hexagon_hvx_gdb_write_register,
                             gdb_find_static_feature("hexagon-hvx.xml"), 0);

#ifndef CONFIG_USER_ONLY
    gdb_register_coprocessor(cs, hexagon_sys_gdb_read_register,
                             hexagon_sys_gdb_write_register,
                             gdb_find_static_feature("hexagon-sys.xml"), 0);
#endif

    qemu_init_vcpu(cs);
#ifndef CONFIG_USER_ONLY
    CPUHexagonState *env = cpu_env(cs);
    hex_mmu_realize(env);
    if (cs->cpu_index == 0) {
        env->g_sreg = g_new0(target_ulong, NUM_SREGS);
        env->g_pcycle_base = g_malloc0(sizeof(*env->g_pcycle_base));
    } else {
        CPUState *cpu0_s = NULL;
        CPUHexagonState *env0 = NULL;
        CPU_FOREACH(cpu0_s) {
            assert(cpu0_s->cpu_index == 0);
            env0 = &(HEXAGON_CPU(cpu0_s)->env);

            break;
        }
        env->g_sreg = env0->g_sreg;
        env->g_pcycle_base = env0->g_pcycle_base;
    }
#endif

    mcc->parent_realize(dev, errp);
}

#if !defined(CONFIG_USER_ONLY)
static void hexagon_cpu_set_irq(void *opaque, int irq, int level)
{
    HexagonCPU *cpu = opaque;
    CPUHexagonState *env = &cpu->env;

    switch (irq) {
    case HEXAGON_CPU_IRQ_0 ... HEXAGON_CPU_IRQ_7:
        qemu_log_mask(CPU_LOG_INT, "%s: irq %d, level %d\n",
                      __func__, irq, level);
        if (level) {
            hex_raise_interrupts(env, 1 << irq, CPU_INTERRUPT_HARD);
        }
        break;
    default:
        g_assert_not_reached();
    }
}
#endif


static void hexagon_cpu_init(Object *obj)
{
#if !defined(CONFIG_USER_ONLY)
    HexagonCPU *cpu = HEXAGON_CPU(obj);
    qdev_init_gpio_in(DEVICE(cpu), hexagon_cpu_set_irq, 8);
#endif
}

#include "hw/core/tcg-cpu-ops.h"

#if !defined(CONFIG_USER_ONLY)
static bool get_physical_address(CPUHexagonState *env, hwaddr *phys, int *prot,
                                 int *size, int32_t *excp, target_ulong address,
                                 MMUAccessType access_type, int mmu_idx)

{
    if (hexagon_cpu_mmu_enabled(env)) {
        return hex_tlb_find_match(env, address, access_type, phys, prot, size,
                                  excp, mmu_idx);
    } else {
        *phys = address & 0xFFFFFFFF;
        *prot = PAGE_VALID | PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        *size = TARGET_PAGE_SIZE;
        return true;
    }
}

/* qemu seems to only want to know about TARGET_PAGE_SIZE pages */
static void find_qemu_subpage(vaddr *addr, hwaddr *phys, int page_size)
{
    vaddr page_start = *addr & ~((vaddr)(page_size - 1));
    vaddr offset = ((*addr - page_start) / TARGET_PAGE_SIZE) * TARGET_PAGE_SIZE;
    *addr = page_start + offset;
    *phys += offset;
}

static hwaddr hexagon_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;
    hwaddr phys_addr;
    int prot;
    int page_size = 0;
    int32_t excp = 0;
    int mmu_idx = MMU_KERNEL_IDX;

    if (get_physical_address(env, &phys_addr, &prot, &page_size, &excp,
                             addr, 0, mmu_idx)) {
        find_qemu_subpage(&addr, &phys_addr, page_size);
        return phys_addr;
    }

    return -1;
}


#define INVALID_BADVA 0xbadabada

static void set_badva_regs(CPUHexagonState *env, target_ulong VA, int slot,
                           MMUAccessType access_type)
{
    ARCH_SET_SYSTEM_REG(env, HEX_SREG_BADVA, VA);

    if (access_type == MMU_INST_FETCH || slot == 0) {
        ARCH_SET_SYSTEM_REG(env, HEX_SREG_BADVA0, VA);
        ARCH_SET_SYSTEM_REG(env, HEX_SREG_BADVA1, INVALID_BADVA);
        SET_SSR_FIELD(env, SSR_V0, 1);
        SET_SSR_FIELD(env, SSR_V1, 0);
        SET_SSR_FIELD(env, SSR_BVS, 0);
    } else if (slot == 1) {
        ARCH_SET_SYSTEM_REG(env, HEX_SREG_BADVA0, INVALID_BADVA);
        ARCH_SET_SYSTEM_REG(env, HEX_SREG_BADVA1, VA);
        SET_SSR_FIELD(env, SSR_V0, 0);
        SET_SSR_FIELD(env, SSR_V1, 1);
        SET_SSR_FIELD(env, SSR_BVS, 1);
    } else {
        g_assert_not_reached();
    }
}

static void raise_tlbmiss_exception(CPUState *cs, target_ulong VA, int slot,
                                    MMUAccessType access_type)
{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;

    set_badva_regs(env, VA, slot, access_type);

    switch (access_type) {
    case MMU_INST_FETCH:
        cs->exception_index = HEX_EVENT_TLB_MISS_X;
        if ((VA & ~TARGET_PAGE_MASK) == 0) {
            env->cause_code = HEX_CAUSE_TLBMISSX_CAUSE_NEXTPAGE;
        } else {
            env->cause_code = HEX_CAUSE_TLBMISSX_CAUSE_NORMAL;
        }
        break;
    case MMU_DATA_LOAD:
        cs->exception_index = HEX_EVENT_TLB_MISS_RW;
        env->cause_code = HEX_CAUSE_TLBMISSRW_CAUSE_READ;
        break;
    case MMU_DATA_STORE:
        cs->exception_index = HEX_EVENT_TLB_MISS_RW;
        env->cause_code = HEX_CAUSE_TLBMISSRW_CAUSE_WRITE;
        break;
    }
}

static void raise_perm_exception(CPUState *cs, target_ulong VA, int slot,
                                 MMUAccessType access_type, int32_t excp);
static void raise_perm_exception(CPUState *cs, target_ulong VA, int slot,
                                 MMUAccessType access_type, int32_t excp)
{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;

    set_badva_regs(env, VA, slot, access_type);
    cs->exception_index = excp;
}

static const char *access_type_names[] = { "MMU_DATA_LOAD ", "MMU_DATA_STORE",
                                           "MMU_INST_FETCH" };

static const char *mmu_idx_names[] = { "MMU_USER_IDX", "MMU_GUEST_IDX",
                                       "MMU_KERNEL_IDX" };

static bool hexagon_tlb_fill(CPUState *cs, vaddr address, int size,
                             MMUAccessType access_type, int mmu_idx, bool probe,
                             uintptr_t retaddr)
{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;
    int slot = 0 /* This is broken FIXME */;
    hwaddr phys;
    int prot = 0;
    int page_size = 0;
    int32_t excp = 0;
    bool ret = 0;

    qemu_log_mask(
        CPU_LOG_MMU,
        "%s: tid = 0x%x, pc = 0x%08" PRIx32 ", vaddr = 0x%08" VADDR_PRIx
        ", size = %d, %s,\tprobe = %d, %s\n",
        __func__, env->threadId, env->gpr[HEX_REG_PC], address, size,
        access_type_names[access_type], probe, mmu_idx_names[mmu_idx]);
    ret = get_physical_address(env, &phys, &prot, &page_size, &excp, address,
                               access_type, mmu_idx);
    if (ret) {
        if (!excp) {
            find_qemu_subpage(&address, &phys, page_size);
            tlb_set_page(cs, address, phys, prot, mmu_idx, TARGET_PAGE_SIZE);
            return ret;
        } else {
            raise_perm_exception(cs, address, slot, access_type, excp);
            do_raise_exception(env, cs->exception_index, env->gpr[HEX_REG_PC],
                               retaddr);
        }
    }
    if (probe) {
        return false;
    }
    raise_tlbmiss_exception(cs, address, slot, access_type);
    do_raise_exception(env, cs->exception_index, env->gpr[HEX_REG_PC], retaddr);
}


#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps hexagon_sysemu_ops = {
    .get_phys_page_debug = hexagon_cpu_get_phys_page_debug,
};

static bool hexagon_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    HexagonCPU *cpu = HEXAGON_CPU(cs);
    CPUHexagonState *env = &cpu->env;
    if (interrupt_request & CPU_INTERRUPT_TLB_UNLOCK) {
        cs->halted = false;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_TLB_UNLOCK);
        return true;
    }
    if (interrupt_request & CPU_INTERRUPT_K0_UNLOCK) {
        cs->halted = false;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_K0_UNLOCK);
        return true;
    }
    if (interrupt_request & (CPU_INTERRUPT_HARD | CPU_INTERRUPT_SWI)) {
        return hex_check_interrupts(env);
    }
    return false;
}

#endif

static const TCGCPUOps hexagon_tcg_ops = {
    .initialize = hexagon_translate_init,
    .synchronize_from_tb = hexagon_cpu_synchronize_from_tb,
    .restore_state_to_opc = hexagon_restore_state_to_opc,
#if !defined(CONFIG_USER_ONLY)
    .cpu_exec_interrupt = hexagon_cpu_exec_interrupt,
    .tlb_fill = hexagon_tlb_fill,
    .cpu_exec_halt = hexagon_cpu_has_work,
    .do_interrupt = hexagon_cpu_do_interrupt,
#endif /* !CONFIG_USER_ONLY */
};


static void hexagon_cpu_class_init(ObjectClass *c, void *data)
{
    HexagonCPUClass *mcc = HEXAGON_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);
    ResettableClass *rc = RESETTABLE_CLASS(c);

    device_class_set_parent_realize(dc, hexagon_cpu_realize,
                                    &mcc->parent_realize);

    device_class_set_props(dc, hexagon_cpu_properties);
    resettable_class_set_parent_phases(rc, NULL, hexagon_cpu_reset_hold, NULL,
                                       &mcc->parent_phases);

    cc->class_by_name = hexagon_cpu_class_by_name;
    cc->has_work = hexagon_cpu_has_work;
    cc->dump_state = hexagon_dump_state;
    cc->set_pc = hexagon_cpu_set_pc;
    cc->get_pc = hexagon_cpu_get_pc;
    cc->gdb_read_register = hexagon_gdb_read_register;
    cc->gdb_write_register = hexagon_gdb_write_register;
    cc->gdb_stop_before_watchpoint = true;
    cc->gdb_core_xml_file = "hexagon-core.xml";
    cc->disas_set_info = hexagon_cpu_disas_set_info;
#ifndef CONFIG_USER_ONLY
    cc->sysemu_ops = &hexagon_sysemu_ops;
    dc->vmsd = &vmstate_hexagon_cpu;
#endif
#ifdef CONFIG_TCG
    cc->tcg_ops = &hexagon_tcg_ops;
#endif
}

#ifndef CONFIG_USER_ONLY
uint32_t hexagon_greg_read(CPUHexagonState *env, uint32_t reg)
{
    g_assert_not_reached();
}
#endif

#define DEFINE_CPU(type_name, initfn)      \
    {                                      \
        .name = type_name,                 \
        .parent = TYPE_HEXAGON_CPU,        \
        .instance_init = initfn            \
    }

static const TypeInfo hexagon_cpu_type_infos[] = {
    {
        .name = TYPE_HEXAGON_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(HexagonCPU),
        .instance_align = __alignof(HexagonCPU),
        .instance_init = hexagon_cpu_init,
        .abstract = true,
        .class_size = sizeof(HexagonCPUClass),
        .class_init = hexagon_cpu_class_init,
    },
    DEFINE_CPU(TYPE_HEXAGON_CPU_V66,              hexagon_v66_cpu_init),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V67,              hexagon_v67_cpu_init),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V68,              hexagon_v68_cpu_init),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V69,              hexagon_v69_cpu_init),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V71,              hexagon_v71_cpu_init),
    DEFINE_CPU(TYPE_HEXAGON_CPU_V73,              hexagon_v73_cpu_init),
};

DEFINE_TYPES(hexagon_cpu_type_infos)
