/*
 * QEMU generic PPC hardware System Emulator
 * 
 * Copyright (c) 2003-2004 Jocelyn Mayer
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "vl.h"

void ppc_prep_init (int ram_size, int vga_ram_size, int boot_device,
		    DisplayState *ds, const char **fd_filename, int snapshot,
		    const char *kernel_filename, const char *kernel_cmdline,
		    const char *initrd_filename);

/*****************************************************************************/
/* PPC time base and decrementer emulation */
//#define DEBUG_TB

struct ppc_tb_t {
    /* Time base management */
    int64_t  tb_offset;    /* Compensation               */
    uint32_t tb_freq;      /* TB frequency               */
    /* Decrementer management */
    uint64_t decr_next;    /* Tick for next decr interrupt  */
    struct QEMUTimer *decr_timer;
};

static inline uint64_t cpu_ppc_get_tb (ppc_tb_t *tb_env)
{
    /* TB time in tb periods */
    return muldiv64(qemu_get_clock(vm_clock) + tb_env->tb_offset,
		    tb_env->tb_freq, ticks_per_sec);
}

uint32_t cpu_ppc_load_tbl (CPUState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env);
#ifdef DEBUG_TB
    {
         static int last_time;
	 int now;
	 now = time(NULL);
	 if (last_time != now) {
	     last_time = now;
	     printf("%s: tb=0x%016lx %d %08lx\n",
                    __func__, tb, now, tb_env->tb_offset);
	 }
    }
#endif

    return tb & 0xFFFFFFFF;
}

uint32_t cpu_ppc_load_tbu (CPUState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t tb;

    tb = cpu_ppc_get_tb(tb_env);
#ifdef DEBUG_TB
    printf("%s: tb=0x%016lx\n", __func__, tb);
#endif
    return tb >> 32;
}

static void cpu_ppc_store_tb (ppc_tb_t *tb_env, uint64_t value)
{
    tb_env->tb_offset = muldiv64(value, ticks_per_sec, tb_env->tb_freq)
        - qemu_get_clock(vm_clock);
#ifdef DEBUG_TB
    printf("%s: tb=0x%016lx offset=%08x\n", __func__, value);
#endif
}

void cpu_ppc_store_tbu (CPUState *env, uint32_t value)
{
    ppc_tb_t *tb_env = env->tb_env;

    cpu_ppc_store_tb(tb_env,
                     ((uint64_t)value << 32) | cpu_ppc_load_tbl(env));
}

void cpu_ppc_store_tbl (CPUState *env, uint32_t value)
{
    ppc_tb_t *tb_env = env->tb_env;

    cpu_ppc_store_tb(tb_env,
                     ((uint64_t)cpu_ppc_load_tbu(env) << 32) | value);
}

uint32_t cpu_ppc_load_decr (CPUState *env)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint32_t decr;

    decr = muldiv64(tb_env->decr_next - qemu_get_clock(vm_clock),
                    tb_env->tb_freq, ticks_per_sec);
#ifdef DEBUG_TB
    printf("%s: 0x%08x\n", __func__, decr);
#endif

    return decr;
}

/* When decrementer expires,
 * all we need to do is generate or queue a CPU exception
 */
static inline void cpu_ppc_decr_excp (CPUState *env)
{
    /* Raise it */
#ifdef DEBUG_TB
    printf("raise decrementer exception\n");
#endif
    cpu_interrupt(env, CPU_INTERRUPT_TIMER);
}

static void _cpu_ppc_store_decr (CPUState *env, uint32_t decr,
                                 uint32_t value, int is_excp)
{
    ppc_tb_t *tb_env = env->tb_env;
    uint64_t now, next;

#ifdef DEBUG_TB
    printf("%s: 0x%08x => 0x%08x\n", __func__, decr, value);
#endif
    now = qemu_get_clock(vm_clock);
    next = now + muldiv64(value, ticks_per_sec, tb_env->tb_freq);
    if (is_excp)
        next += tb_env->decr_next - now;
    if (next == now)
	next++;
    tb_env->decr_next = next;
    /* Adjust timer */
    qemu_mod_timer(tb_env->decr_timer, next);
    /* If we set a negative value and the decrementer was positive,
     * raise an exception.
     */
    if ((value & 0x80000000) && !(decr & 0x80000000))
	cpu_ppc_decr_excp(env);
}

void cpu_ppc_store_decr (CPUState *env, uint32_t value)
{
    _cpu_ppc_store_decr(env, cpu_ppc_load_decr(env), value, 0);
}

static void cpu_ppc_decr_cb (void *opaque)
{
    _cpu_ppc_store_decr(opaque, 0x00000000, 0xFFFFFFFF, 1);
}

/* Set up (once) timebase frequency (in Hz) */
ppc_tb_t *cpu_ppc_tb_init (CPUState *env, uint32_t freq)
{
    ppc_tb_t *tb_env;

    tb_env = qemu_mallocz(sizeof(ppc_tb_t));
    if (tb_env == NULL)
        return NULL;
    env->tb_env = tb_env;
    if (tb_env->tb_freq == 0 || 1) {
	tb_env->tb_freq = freq;
	/* Create new timer */
	tb_env->decr_timer =
            qemu_new_timer(vm_clock, &cpu_ppc_decr_cb, env);
	/* There is a bug in  2.4 kernels:
	 * if a decrementer exception is pending when it enables msr_ee,
	 * it's not ready to handle it...
	 */
	_cpu_ppc_store_decr(env, 0xFFFFFFFF, 0xFFFFFFFF, 0);
    }

    return tb_env;
}

#if 0
/*****************************************************************************/
/* Handle system reset (for now, just stop emulation) */
void cpu_ppc_reset (CPUState *env)
{
    printf("Reset asked... Stop emulation\n");
    abort();
}
#endif

/*****************************************************************************/
void ppc_init (int ram_size, int vga_ram_size, int boot_device,
	       DisplayState *ds, const char **fd_filename, int snapshot,
	       const char *kernel_filename, const char *kernel_cmdline,
	       const char *initrd_filename)
{
    /* For now, only PREP is supported */
    return ppc_prep_init(ram_size, vga_ram_size, boot_device, ds, fd_filename,
			 snapshot, kernel_filename, kernel_cmdline,
			 initrd_filename);
}
