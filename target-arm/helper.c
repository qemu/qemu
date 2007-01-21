#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "exec-all.h"

void cpu_reset(CPUARMState *env)
{
#if defined (CONFIG_USER_ONLY)
    env->uncached_cpsr = ARM_CPU_MODE_USR;
    env->vfp.xregs[ARM_VFP_FPEXC] = 1 << 30;
#else
    /* SVC mode with interrupts disabled.  */
    env->uncached_cpsr = ARM_CPU_MODE_SVC | CPSR_A | CPSR_F | CPSR_I;
    env->vfp.xregs[ARM_VFP_FPEXC] = 0;
#endif
    env->regs[15] = 0;
}

CPUARMState *cpu_arm_init(void)
{
    CPUARMState *env;

    env = qemu_mallocz(sizeof(CPUARMState));
    if (!env)
        return NULL;
    cpu_exec_init(env);
    cpu_reset(env);
    tlb_flush(env, 1);
    return env;
}

static inline void set_feature(CPUARMState *env, int feature)
{
    env->features |= 1u << feature;
}

void cpu_arm_set_model(CPUARMState *env, uint32_t id)
{
    env->cp15.c0_cpuid = id;
    switch (id) {
    case ARM_CPUID_ARM926:
        set_feature(env, ARM_FEATURE_VFP);
        env->vfp.xregs[ARM_VFP_FPSID] = 0x41011090;
        break;
    case ARM_CPUID_ARM1026:
        set_feature(env, ARM_FEATURE_VFP);
        set_feature(env, ARM_FEATURE_AUXCR);
        env->vfp.xregs[ARM_VFP_FPSID] = 0x410110a0;
        break;
    default:
        cpu_abort(env, "Bad CPU ID: %x\n", id);
        break;
    }
}

void cpu_arm_close(CPUARMState *env)
{
    free(env);
}

#if defined(CONFIG_USER_ONLY) 

void do_interrupt (CPUState *env)
{
    env->exception_index = -1;
}

int cpu_arm_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                              int is_user, int is_softmmu)
{
    if (rw == 2) {
        env->exception_index = EXCP_PREFETCH_ABORT;
        env->cp15.c6_insn = address;
    } else {
        env->exception_index = EXCP_DATA_ABORT;
        env->cp15.c6_data = address;
    }
    return 1;
}

target_ulong cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    return addr;
}

/* These should probably raise undefined insn exceptions.  */
void helper_set_cp15(CPUState *env, uint32_t insn, uint32_t val)
{
    cpu_abort(env, "cp15 insn %08x\n", insn);
}

uint32_t helper_get_cp15(CPUState *env, uint32_t insn)
{
    cpu_abort(env, "cp15 insn %08x\n", insn);
    return 0;
}

void switch_mode(CPUState *env, int mode)
{
    if (mode != ARM_CPU_MODE_USR)
        cpu_abort(env, "Tried to switch out of user mode\n");
}

#else

extern int semihosting_enabled;

/* Map CPU modes onto saved register banks.  */
static inline int bank_number (int mode)
{
    switch (mode) {
    case ARM_CPU_MODE_USR:
    case ARM_CPU_MODE_SYS:
        return 0;
    case ARM_CPU_MODE_SVC:
        return 1;
    case ARM_CPU_MODE_ABT:
        return 2;
    case ARM_CPU_MODE_UND:
        return 3;
    case ARM_CPU_MODE_IRQ:
        return 4;
    case ARM_CPU_MODE_FIQ:
        return 5;
    }
    cpu_abort(cpu_single_env, "Bad mode %x\n", mode);
    return -1;
}

void switch_mode(CPUState *env, int mode)
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
    env->banked_r14[i] = env->regs[14];
    env->banked_spsr[i] = env->spsr;

    i = bank_number(mode);
    env->regs[13] = env->banked_r13[i];
    env->regs[14] = env->banked_r14[i];
    env->spsr = env->banked_spsr[i];
}

/* Handle a CPU exception.  */
void do_interrupt(CPUARMState *env)
{
    uint32_t addr;
    uint32_t mask;
    int new_mode;
    uint32_t offset;

    /* TODO: Vectored interrupt controller.  */
    switch (env->exception_index) {
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
        if (semihosting_enabled) {
            /* Check for semihosting interrupt.  */
            if (env->thumb) {
                mask = lduw_code(env->regs[15] - 2) & 0xff;
            } else {
                mask = ldl_code(env->regs[15] - 4) & 0xffffff;
            }
            /* Only intercept calls from privileged modes, to provide some
               semblance of security.  */
            if (((mask == 0x123456 && !env->thumb)
                    || (mask == 0xab && env->thumb))
                  && (env->uncached_cpsr & CPSR_M) != ARM_CPU_MODE_USR) {
                env->regs[0] = do_arm_semihosting(env);
                return;
            }
        }
        new_mode = ARM_CPU_MODE_SVC;
        addr = 0x08;
        mask = CPSR_I;
        /* The PC already points to the next instructon.  */
        offset = 0;
        break;
    case EXCP_PREFETCH_ABORT:
    case EXCP_BKPT:
        new_mode = ARM_CPU_MODE_ABT;
        addr = 0x0c;
        mask = CPSR_A | CPSR_I;
        offset = 4;
        break;
    case EXCP_DATA_ABORT:
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
        break;
    case EXCP_FIQ:
        new_mode = ARM_CPU_MODE_FIQ;
        addr = 0x1c;
        /* Disable FIQ, IRQ and imprecise data aborts.  */
        mask = CPSR_A | CPSR_I | CPSR_F;
        offset = 4;
        break;
    default:
        cpu_abort(env, "Unhandled exception 0x%x\n", env->exception_index);
        return; /* Never happens.  Keep compiler happy.  */
    }
    /* High vectors.  */
    if (env->cp15.c1_sys & (1 << 13)) {
        addr += 0xffff0000;
    }
    switch_mode (env, new_mode);
    env->spsr = cpsr_read(env);
    /* Switch to the new mode, and switch to Arm mode.  */
    /* ??? Thumb interrupt handlers not implemented.  */
    env->uncached_cpsr = (env->uncached_cpsr & ~CPSR_M) | new_mode;
    env->uncached_cpsr |= mask;
    env->thumb = 0;
    env->regs[14] = env->regs[15] + offset;
    env->regs[15] = addr;
    env->interrupt_request |= CPU_INTERRUPT_EXITTB;
}

/* Check section/page access permissions.
   Returns the page protection flags, or zero if the access is not
   permitted.  */
static inline int check_ap(CPUState *env, int ap, int domain, int access_type,
                           int is_user)
{
  if (domain == 3)
    return PAGE_READ | PAGE_WRITE;

  switch (ap) {
  case 0:
      if (access_type == 1)
          return 0;
      switch ((env->cp15.c1_sys >> 8) & 3) {
      case 1:
          return is_user ? 0 : PAGE_READ;
      case 2:
          return PAGE_READ;
      default:
          return 0;
      }
  case 1:
      return is_user ? 0 : PAGE_READ | PAGE_WRITE;
  case 2:
      if (is_user)
          return (access_type == 1) ? 0 : PAGE_READ;
      else
          return PAGE_READ | PAGE_WRITE;
  case 3:
      return PAGE_READ | PAGE_WRITE;
  default:
      abort();
  }
}

static int get_phys_addr(CPUState *env, uint32_t address, int access_type,
                         int is_user, uint32_t *phys_ptr, int *prot)
{
    int code;
    uint32_t table;
    uint32_t desc;
    int type;
    int ap;
    int domain;
    uint32_t phys_addr;

    /* Fast Context Switch Extension.  */
    if (address < 0x02000000)
        address += env->cp15.c13_fcse;

    if ((env->cp15.c1_sys & 1) == 0) {
        /* MMU diusabled.  */
        *phys_ptr = address;
        *prot = PAGE_READ | PAGE_WRITE;
    } else {
        /* Pagetable walk.  */
        /* Lookup l1 descriptor.  */
        table = (env->cp15.c2 & 0xffffc000) | ((address >> 18) & 0x3ffc);
        desc = ldl_phys(table);
        type = (desc & 3);
        domain = (env->cp15.c3 >> ((desc >> 4) & 0x1e)) & 3;
        if (type == 0) {
            /* Secton translation fault.  */
            code = 5;
            goto do_fault;
        }
        if (domain == 0 || domain == 2) {
            if (type == 2)
                code = 9; /* Section domain fault.  */
            else
                code = 11; /* Page domain fault.  */
            goto do_fault;
        }
        if (type == 2) {
            /* 1Mb section.  */
            phys_addr = (desc & 0xfff00000) | (address & 0x000fffff);
            ap = (desc >> 10) & 3;
            code = 13;
        } else {
            /* Lookup l2 entry.  */
            table = (desc & 0xfffffc00) | ((address >> 10) & 0x3fc);
            desc = ldl_phys(table);
            switch (desc & 3) {
            case 0: /* Page translation fault.  */
                code = 7;
                goto do_fault;
            case 1: /* 64k page.  */
                phys_addr = (desc & 0xffff0000) | (address & 0xffff);
                ap = (desc >> (4 + ((address >> 13) & 6))) & 3;
                break;
            case 2: /* 4k page.  */
                phys_addr = (desc & 0xfffff000) | (address & 0xfff);
                ap = (desc >> (4 + ((address >> 13) & 6))) & 3;
                break;
            case 3: /* 1k page.  */
                if (type == 1) {
                    /* Page translation fault.  */
                    code = 7;
                    goto do_fault;
                }
                phys_addr = (desc & 0xfffffc00) | (address & 0x3ff);
                ap = (desc >> 4) & 3;
                break;
            default:
                /* Never happens, but compiler isn't smart enough to tell.  */
                abort();
            }
            code = 15;
        }
        *prot = check_ap(env, ap, domain, access_type, is_user);
        if (!*prot) {
            /* Access permission fault.  */
            goto do_fault;
        }
        *phys_ptr = phys_addr;
    }
    return 0;
do_fault:
    return code | (domain << 4);
}

int cpu_arm_handle_mmu_fault (CPUState *env, target_ulong address,
                              int access_type, int is_user, int is_softmmu)
{
    uint32_t phys_addr;
    int prot;
    int ret;

    ret = get_phys_addr(env, address, access_type, is_user, &phys_addr, &prot);
    if (ret == 0) {
        /* Map a single [sub]page.  */
        phys_addr &= ~(uint32_t)0x3ff;
        address &= ~(uint32_t)0x3ff;
        return tlb_set_page (env, address, phys_addr, prot, is_user,
                             is_softmmu);
    }

    if (access_type == 2) {
        env->cp15.c5_insn = ret;
        env->cp15.c6_insn = address;
        env->exception_index = EXCP_PREFETCH_ABORT;
    } else {
        env->cp15.c5_data = ret;
        env->cp15.c6_data = address;
        env->exception_index = EXCP_DATA_ABORT;
    }
    return 1;
}

target_ulong cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    uint32_t phys_addr;
    int prot;
    int ret;

    ret = get_phys_addr(env, addr, 0, 0, &phys_addr, &prot);

    if (ret != 0)
        return -1;

    return phys_addr;
}

void helper_set_cp15(CPUState *env, uint32_t insn, uint32_t val)
{
    uint32_t op2;

    op2 = (insn >> 5) & 7;
    switch ((insn >> 16) & 0xf) {
    case 0: /* ID codes.  */
        goto bad_reg;
    case 1: /* System configuration.  */
        switch (op2) {
        case 0:
            env->cp15.c1_sys = val;
            /* ??? Lots of these bits are not implemented.  */
            /* This may enable/disable the MMU, so do a TLB flush.  */
            tlb_flush(env, 1);
            break;
        case 2:
            env->cp15.c1_coproc = val;
            /* ??? Is this safe when called from within a TB?  */
            tb_flush(env);
        default:
            goto bad_reg;
        }
        break;
    case 2: /* MMU Page table control.  */
        env->cp15.c2 = val;
        break;
    case 3: /* MMU Domain access control.  */
        env->cp15.c3 = val;
        break;
    case 4: /* Reserved.  */
        goto bad_reg;
    case 5: /* MMU Fault status.  */
        switch (op2) {
        case 0:
            env->cp15.c5_data = val;
            break;
        case 1:
            env->cp15.c5_insn = val;
            break;
        default:
            goto bad_reg;
        }
        break;
    case 6: /* MMU Fault address.  */
        switch (op2) {
        case 0:
            env->cp15.c6_data = val;
            break;
        case 1:
            env->cp15.c6_insn = val;
            break;
        default:
            goto bad_reg;
        }
        break;
    case 7: /* Cache control.  */
        /* No cache, so nothing to do.  */
        break;
    case 8: /* MMU TLB control.  */
        switch (op2) {
        case 0: /* Invalidate all.  */
            tlb_flush(env, 0);
            break;
        case 1: /* Invalidate single TLB entry.  */
#if 0
            /* ??? This is wrong for large pages and sections.  */
            /* As an ugly hack to make linux work we always flush a 4K
               pages.  */
            val &= 0xfffff000;
            tlb_flush_page(env, val);
            tlb_flush_page(env, val + 0x400);
            tlb_flush_page(env, val + 0x800);
            tlb_flush_page(env, val + 0xc00);
#else
            tlb_flush(env, 1);
#endif
            break;
        default:
            goto bad_reg;
        }
        break;
    case 9: /* Cache lockdown.  */
        switch (op2) {
        case 0:
            env->cp15.c9_data = val;
            break;
        case 1:
            env->cp15.c9_insn = val;
            break;
        default:
            goto bad_reg;
        }
        break;
    case 10: /* MMU TLB lockdown.  */
        /* ??? TLB lockdown not implemented.  */
        break;
    case 11: /* TCM DMA control.  */
    case 12: /* Reserved.  */
        goto bad_reg;
    case 13: /* Process ID.  */
        switch (op2) {
        case 0:
            /* Unlike real hardware the qemu TLB uses virtual addresses,
               not modified virtual addresses, so this causes a TLB flush.
             */
            if (env->cp15.c13_fcse != val)
              tlb_flush(env, 1);
            env->cp15.c13_fcse = val;
            break;
        case 1:
            /* This changes the ASID, so do a TLB flush.  */
            if (env->cp15.c13_context != val)
              tlb_flush(env, 0);
            env->cp15.c13_context = val;
            break;
        default:
            goto bad_reg;
        }
        break;
    case 14: /* Reserved.  */
        goto bad_reg;
    case 15: /* Implementation specific.  */
        /* ??? Internal registers not implemented.  */
        break;
    }
    return;
bad_reg:
    /* ??? For debugging only.  Should raise illegal instruction exception.  */
    cpu_abort(env, "Unimplemented cp15 register read\n");
}

uint32_t helper_get_cp15(CPUState *env, uint32_t insn)
{
    uint32_t op2;

    op2 = (insn >> 5) & 7;
    switch ((insn >> 16) & 0xf) {
    case 0: /* ID codes.  */
        switch (op2) {
        default: /* Device ID.  */
            return env->cp15.c0_cpuid;
        case 1: /* Cache Type.  */
            return 0x1dd20d2;
        case 2: /* TCM status.  */
            return 0;
        }
    case 1: /* System configuration.  */
        switch (op2) {
        case 0: /* Control register.  */
            return env->cp15.c1_sys;
        case 1: /* Auxiliary control register.  */
            if (arm_feature(env, ARM_FEATURE_AUXCR))
                return 1;
            goto bad_reg;
        case 2: /* Coprocessor access register.  */
            return env->cp15.c1_coproc;
        default:
            goto bad_reg;
        }
    case 2: /* MMU Page table control.  */
        return env->cp15.c2;
    case 3: /* MMU Domain access control.  */
        return env->cp15.c3;
    case 4: /* Reserved.  */
        goto bad_reg;
    case 5: /* MMU Fault status.  */
        switch (op2) {
        case 0:
            return env->cp15.c5_data;
        case 1:
            return env->cp15.c5_insn;
        default:
            goto bad_reg;
        }
    case 6: /* MMU Fault address.  */
        switch (op2) {
        case 0:
            return env->cp15.c6_data;
        case 1:
            /* Arm9 doesn't have an IFAR, but implementing it anyway shouldn't
               do any harm.  */
            return env->cp15.c6_insn;
        default:
            goto bad_reg;
        }
    case 7: /* Cache control.  */
        /* ??? This is for test, clean and invaidate operations that set the
           Z flag.  We can't represent N = Z = 1, so it also clears clears
           the N flag.  Oh well.  */
        env->NZF = 0;
        return 0;
    case 8: /* MMU TLB control.  */
        goto bad_reg;
    case 9: /* Cache lockdown.  */
        switch (op2) {
        case 0:
            return env->cp15.c9_data;
        case 1:
            return env->cp15.c9_insn;
        default:
            goto bad_reg;
        }
    case 10: /* MMU TLB lockdown.  */
        /* ??? TLB lockdown not implemented.  */
        return 0;
    case 11: /* TCM DMA control.  */
    case 12: /* Reserved.  */
        goto bad_reg;
    case 13: /* Process ID.  */
        switch (op2) {
        case 0:
            return env->cp15.c13_fcse;
        case 1:
            return env->cp15.c13_context;
        default:
            goto bad_reg;
        }
    case 14: /* Reserved.  */
        goto bad_reg;
    case 15: /* Implementation specific.  */
        /* ??? Internal registers not implemented.  */
        return 0;
    }
bad_reg:
    /* ??? For debugging only.  Should raise illegal instruction exception.  */
    cpu_abort(env, "Unimplemented cp15 register read\n");
    return 0;
}

#endif
