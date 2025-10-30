/*
 * QEMU AVR CPU helpers
 *
 * Copyright (c) 2016-2020 Michael Rolnik
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "cpu.h"
#include "accel/tcg/cpu-ops.h"
#include "exec/cputlb.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "accel/tcg/cpu-ldst.h"
#include "exec/helper-proto.h"
#include "qemu/plugin.h"

bool avr_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUAVRState *env = cpu_env(cs);

    /*
     * We cannot separate a skip from the next instruction,
     * as the skip would not be preserved across the interrupt.
     * Separating the two insn normally only happens at page boundaries.
     */
    if (env->skip) {
        return false;
    }

    if (interrupt_request & CPU_INTERRUPT_RESET) {
        if (cpu_interrupts_enabled(env)) {
            cs->exception_index = EXCP_RESET;
            avr_cpu_do_interrupt(cs);

            cpu_reset_interrupt(cs, CPU_INTERRUPT_RESET);
            return true;
        }
    }
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        if (cpu_interrupts_enabled(env) && env->intsrc != 0) {
            int index = ctz64(env->intsrc);
            cs->exception_index = EXCP_INT(index);
            avr_cpu_do_interrupt(cs);

            env->intsrc &= env->intsrc - 1; /* clear the interrupt */
            if (!env->intsrc) {
                cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
            }
            return true;
        }
    }
    return false;
}

static void do_stb(CPUAVRState *env, uint32_t addr, uint8_t data, uintptr_t ra)
{
    cpu_stb_mmuidx_ra(env, addr, data, MMU_DATA_IDX, ra);
}

void avr_cpu_do_interrupt(CPUState *cs)
{
    CPUAVRState *env = cpu_env(cs);

    uint32_t ret = env->pc_w;
    int vector = 0;
    int size = avr_feature(env, AVR_FEATURE_JMP_CALL) ? 2 : 1;
    int base = 0;

    if (cs->exception_index == EXCP_RESET) {
        vector = 0;
    } else if (env->intsrc != 0) {
        vector = ctz64(env->intsrc) + 1;
    }

    if (avr_feature(env, AVR_FEATURE_3_BYTE_PC)) {
        do_stb(env, env->sp--, ret, 0);
        do_stb(env, env->sp--, ret >> 8, 0);
        do_stb(env, env->sp--, ret >> 16, 0);
    } else if (avr_feature(env, AVR_FEATURE_2_BYTE_PC)) {
        do_stb(env, env->sp--, ret, 0);
        do_stb(env, env->sp--, ret >> 8, 0);
    } else {
        do_stb(env, env->sp--, ret, 0);
    }

    env->pc_w = base + vector * size;
    env->sregI = 0; /* clear Global Interrupt Flag */

    cs->exception_index = -1;

    qemu_plugin_vcpu_interrupt_cb(cs, ret);
}

hwaddr avr_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    return addr; /* I assume 1:1 address correspondence */
}

bool avr_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                      MMUAccessType access_type, int mmu_idx,
                      bool probe, uintptr_t retaddr)
{
    int prot;
    uint32_t paddr;

    address &= TARGET_PAGE_MASK;

    if (mmu_idx == MMU_CODE_IDX) {
        /* Access to code in flash. */
        paddr = OFFSET_CODE + address;
        prot = PAGE_READ | PAGE_EXEC;
        if (paddr >= OFFSET_DATA) {
            /*
             * This should not be possible via any architectural operations.
             * There is certainly not an exception that we can deliver.
             * Accept probing that might come from generic code.
             */
            if (probe) {
                return false;
            }
            error_report("execution left flash memory");
            abort();
        }
    } else {
        /* Access to memory. */
        paddr = OFFSET_DATA + address;
        prot = PAGE_READ | PAGE_WRITE;
    }

    tlb_set_page(cs, address, paddr, prot, mmu_idx, TARGET_PAGE_SIZE);
    return true;
}

/*
 *  helpers
 */

void helper_sleep(CPUAVRState *env)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = EXCP_HLT;
    cpu_loop_exit(cs);
}

void helper_unsupported(CPUAVRState *env)
{
    CPUState *cs = env_cpu(env);

    /*
     *  I count not find what happens on the real platform, so
     *  it's EXCP_DEBUG for meanwhile
     */
    cs->exception_index = EXCP_DEBUG;
    if (qemu_loglevel_mask(LOG_UNIMP)) {
        qemu_log("UNSUPPORTED\n");
        cpu_dump_state(cs, stderr, 0);
    }
    cpu_loop_exit(cs);
}

void helper_debug(CPUAVRState *env)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}

void helper_break(CPUAVRState *env)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}

void helper_wdr(CPUAVRState *env)
{
    qemu_log_mask(LOG_UNIMP, "WDG reset (not implemented)\n");
}

/*
 * The first 32 bytes of the data space are mapped to the cpu regs.
 * We cannot write these from normal store operations because TCG
 * does not expect global temps to be modified -- a global may be
 * live in a host cpu register across the store.  We can however
 * read these, as TCG does make sure the global temps are saved
 * in case the load operation traps.
 */

static uint64_t avr_cpu_reg1_read(void *opaque, hwaddr addr, unsigned size)
{
    CPUAVRState *env = opaque;

    assert(addr < 32);
    return env->r[addr];
}

/*
 * The range 0x38-0x3f of the i/o space is mapped to cpu regs.
 * As above, we cannot write these from normal store operations.
 */

static uint64_t avr_cpu_reg2_read(void *opaque, hwaddr addr, unsigned size)
{
    CPUAVRState *env = opaque;

    switch (addr) {
    case REG_38_RAMPD:
        return 0xff & (env->rampD >> 16);
    case REG_38_RAMPX:
        return 0xff & (env->rampX >> 16);
    case REG_38_RAMPY:
        return 0xff & (env->rampY >> 16);
    case REG_38_RAMPZ:
        return 0xff & (env->rampZ >> 16);
    case REG_38_EIDN:
        return 0xff & (env->eind >> 16);
    case REG_38_SPL:
        return env->sp & 0x00ff;
    case REG_38_SPH:
        return 0xff & (env->sp >> 8);
    case REG_38_SREG:
        return cpu_get_sreg(env);
    }
    g_assert_not_reached();
}

static void avr_cpu_trap_write(void *opaque, hwaddr addr,
                               uint64_t data64, unsigned size)
{
    CPUAVRState *env = opaque;
    CPUState *cs = env_cpu(env);

    env->fullacc = true;
    cpu_loop_exit_restore(cs, cs->mem_io_pc);
}

const MemoryRegionOps avr_cpu_reg1 = {
    .read = avr_cpu_reg1_read,
    .write = avr_cpu_trap_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
};

const MemoryRegionOps avr_cpu_reg2 = {
    .read = avr_cpu_reg2_read,
    .write = avr_cpu_trap_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
};

/*
 *  this function implements ST instruction when there is a possibility to write
 *  into a CPU register
 */
void helper_fullwr(CPUAVRState *env, uint32_t data, uint32_t addr)
{
    env->fullacc = false;

    switch (addr) {
    case 0 ... 31:
        /* CPU registers */
        env->r[addr] = data;
        break;

    case REG_38_RAMPD + 0x38 + NUMBER_OF_CPU_REGISTERS:
        if (avr_feature(env, AVR_FEATURE_RAMPD)) {
            env->rampD = data << 16;
        }
        break;
    case REG_38_RAMPX + 0x38 + NUMBER_OF_CPU_REGISTERS:
        if (avr_feature(env, AVR_FEATURE_RAMPX)) {
            env->rampX = data << 16;
        }
        break;
    case REG_38_RAMPY + 0x38 + NUMBER_OF_CPU_REGISTERS:
        if (avr_feature(env, AVR_FEATURE_RAMPY)) {
            env->rampY = data << 16;
        }
        break;
    case REG_38_RAMPZ + 0x38 + NUMBER_OF_CPU_REGISTERS:
        if (avr_feature(env, AVR_FEATURE_RAMPZ)) {
            env->rampZ = data << 16;
        }
        break;
    case REG_38_EIDN + 0x38 + NUMBER_OF_CPU_REGISTERS:
        env->eind = data << 16;
        break;
    case REG_38_SPL + 0x38 + NUMBER_OF_CPU_REGISTERS:
        env->sp = (env->sp & 0xff00) | data;
        break;
    case REG_38_SPH + 0x38 + NUMBER_OF_CPU_REGISTERS:
        if (avr_feature(env, AVR_FEATURE_2_BYTE_SP)) {
            env->sp = (env->sp & 0x00ff) | (data << 8);
        }
        break;
    case REG_38_SREG + 0x38 + NUMBER_OF_CPU_REGISTERS:
        cpu_set_sreg(env, data);
        break;

    default:
        do_stb(env, addr, data, GETPC());
        break;
    }
}
