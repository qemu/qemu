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
#include "m48t59.h"

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
    int64_t diff;

    diff = tb_env->decr_next - qemu_get_clock(vm_clock);
    if (diff >= 0)
        decr = muldiv64(diff, tb_env->tb_freq, ticks_per_sec);
    else
        decr = -muldiv64(-diff, tb_env->tb_freq, ticks_per_sec);
#if defined(DEBUG_TB)
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
/* Debug port */
void PPC_debug_write (void *opaque, uint32_t addr, uint32_t val)
{
    addr &= 0xF;
    switch (addr) {
    case 0:
        printf("%c", val);
        break;
    case 1:
        printf("\n");
        fflush(stdout);
        break;
    case 2:
        printf("Set loglevel to %04x\n", val);
        cpu_set_log(val | 0x100);
        break;
    }
}

/*****************************************************************************/
/* NVRAM helpers */
void NVRAM_set_byte (m48t59_t *nvram, uint32_t addr, uint8_t value)
{
    m48t59_write(nvram, addr, value);
}

uint8_t NVRAM_get_byte (m48t59_t *nvram, uint32_t addr)
{
    return m48t59_read(nvram, addr);
}

void NVRAM_set_word (m48t59_t *nvram, uint32_t addr, uint16_t value)
{
    m48t59_write(nvram, addr, value >> 8);
    m48t59_write(nvram, addr + 1, value & 0xFF);
}

uint16_t NVRAM_get_word (m48t59_t *nvram, uint32_t addr)
{
    uint16_t tmp;

    tmp = m48t59_read(nvram, addr) << 8;
    tmp |= m48t59_read(nvram, addr + 1);
    return tmp;
}

void NVRAM_set_lword (m48t59_t *nvram, uint32_t addr, uint32_t value)
{
    m48t59_write(nvram, addr, value >> 24);
    m48t59_write(nvram, addr + 1, (value >> 16) & 0xFF);
    m48t59_write(nvram, addr + 2, (value >> 8) & 0xFF);
    m48t59_write(nvram, addr + 3, value & 0xFF);
}

uint32_t NVRAM_get_lword (m48t59_t *nvram, uint32_t addr)
{
    uint32_t tmp;

    tmp = m48t59_read(nvram, addr) << 24;
    tmp |= m48t59_read(nvram, addr + 1) << 16;
    tmp |= m48t59_read(nvram, addr + 2) << 8;
    tmp |= m48t59_read(nvram, addr + 3);
    return tmp;
}

void NVRAM_set_string (m48t59_t *nvram, uint32_t addr,
                       const unsigned char *str, uint32_t max)
{
    int i;

    for (i = 0; i < max && str[i] != '\0'; i++) {
        m48t59_write(nvram, addr + i, str[i]);
    }
    m48t59_write(nvram, addr + max - 1, '\0');
}

int NVRAM_get_string (m48t59_t *nvram, uint8_t *dst, uint16_t addr, int max)
{
    int i;

    memset(dst, 0, max);
    for (i = 0; i < max; i++) {
        dst[i] = NVRAM_get_byte(nvram, addr + i);
        if (dst[i] == '\0')
            break;
    }

    return i;
}

static uint16_t NVRAM_crc_update (uint16_t prev, uint16_t value)
{
    uint16_t tmp;
    uint16_t pd, pd1, pd2;

    tmp = prev >> 8;
    pd = prev ^ value;
    pd1 = pd & 0x000F;
    pd2 = ((pd >> 4) & 0x000F) ^ pd1;
    tmp ^= (pd1 << 3) | (pd1 << 8);
    tmp ^= pd2 | (pd2 << 7) | (pd2 << 12);

    return tmp;
}

uint16_t NVRAM_compute_crc (m48t59_t *nvram, uint32_t start, uint32_t count)
{
    uint32_t i;
    uint16_t crc = 0xFFFF;
    int odd;

    odd = count & 1;
    count &= ~1;
    for (i = 0; i != count; i++) {
	crc = NVRAM_crc_update(crc, NVRAM_get_word(nvram, start + i));
    }
    if (odd) {
	crc = NVRAM_crc_update(crc, NVRAM_get_byte(nvram, start + i) << 8);
    }

    return crc;
}

#define CMDLINE_ADDR 0x017ff000

int PPC_NVRAM_set_params (m48t59_t *nvram, uint16_t NVRAM_size,
                          const unsigned char *arch,
                          uint32_t RAM_size, int boot_device,
                          uint32_t kernel_image, uint32_t kernel_size,
                          const char *cmdline,
                          uint32_t initrd_image, uint32_t initrd_size,
                          uint32_t NVRAM_image,
                          int width, int height, int depth)
{
    uint16_t crc;

    /* Set parameters for Open Hack'Ware BIOS */
    NVRAM_set_string(nvram, 0x00, "QEMU_BIOS", 16);
    NVRAM_set_lword(nvram,  0x10, 0x00000002); /* structure v2 */
    NVRAM_set_word(nvram,   0x14, NVRAM_size);
    NVRAM_set_string(nvram, 0x20, arch, 16);
    NVRAM_set_lword(nvram,  0x30, RAM_size);
    NVRAM_set_byte(nvram,   0x34, boot_device);
    NVRAM_set_lword(nvram,  0x38, kernel_image);
    NVRAM_set_lword(nvram,  0x3C, kernel_size);
    if (cmdline) {
        /* XXX: put the cmdline in NVRAM too ? */
        strcpy(phys_ram_base + CMDLINE_ADDR, cmdline);
        NVRAM_set_lword(nvram,  0x40, CMDLINE_ADDR);
        NVRAM_set_lword(nvram,  0x44, strlen(cmdline));
    } else {
        NVRAM_set_lword(nvram,  0x40, 0);
        NVRAM_set_lword(nvram,  0x44, 0);
    }
    NVRAM_set_lword(nvram,  0x48, initrd_image);
    NVRAM_set_lword(nvram,  0x4C, initrd_size);
    NVRAM_set_lword(nvram,  0x50, NVRAM_image);

    NVRAM_set_word(nvram,   0x54, width);
    NVRAM_set_word(nvram,   0x56, height);
    NVRAM_set_word(nvram,   0x58, depth);
    crc = NVRAM_compute_crc(nvram, 0x00, 0xF8);
    NVRAM_set_word(nvram,  0xFC, crc);

    return 0;
}
