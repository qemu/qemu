/*
 * gdb server stub
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "config.h"
#ifdef CONFIG_USER_ONLY
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "qemu.h"
#else
#include "qemu-common.h"
#include "qemu-char.h"
#include "sysemu.h"
#include "gdbstub.h"
#endif

#include "qemu_socket.h"
#ifdef _WIN32
/* XXX: these constants may be independent of the host ones even for Unix */
#ifndef SIGTRAP
#define SIGTRAP 5
#endif
#ifndef SIGINT
#define SIGINT 2
#endif
#else
#include <signal.h>
#endif

//#define DEBUG_GDB

enum RSState {
    RS_IDLE,
    RS_GETLINE,
    RS_CHKSUM1,
    RS_CHKSUM2,
    RS_SYSCALL,
};
typedef struct GDBState {
    CPUState *env; /* current CPU */
    enum RSState state; /* parsing state */
    char line_buf[4096];
    int line_buf_index;
    int line_csum;
    uint8_t last_packet[4100];
    int last_packet_len;
    int signal;
#ifdef CONFIG_USER_ONLY
    int fd;
    int running_state;
#else
    CharDriverState *chr;
#endif
} GDBState;

/* By default use no IRQs and no timers while single stepping so as to
 * make single stepping like an ICE HW step.
 */
static int sstep_flags = SSTEP_ENABLE|SSTEP_NOIRQ|SSTEP_NOTIMER;

#ifdef CONFIG_USER_ONLY
/* XXX: This is not thread safe.  Do we care?  */
static int gdbserver_fd = -1;

/* XXX: remove this hack.  */
static GDBState gdbserver_state;

static int get_char(GDBState *s)
{
    uint8_t ch;
    int ret;

    for(;;) {
        ret = recv(s->fd, &ch, 1, 0);
        if (ret < 0) {
            if (errno == ECONNRESET)
                s->fd = -1;
            if (errno != EINTR && errno != EAGAIN)
                return -1;
        } else if (ret == 0) {
            close(s->fd);
            s->fd = -1;
            return -1;
        } else {
            break;
        }
    }
    return ch;
}
#endif

/* GDB stub state for use by semihosting syscalls.  */
static GDBState *gdb_syscall_state;
static gdb_syscall_complete_cb gdb_current_syscall_cb;

enum {
    GDB_SYS_UNKNOWN,
    GDB_SYS_ENABLED,
    GDB_SYS_DISABLED,
} gdb_syscall_mode;

/* If gdb is connected when the first semihosting syscall occurs then use
   remote gdb syscalls.  Otherwise use native file IO.  */
int use_gdb_syscalls(void)
{
    if (gdb_syscall_mode == GDB_SYS_UNKNOWN) {
        gdb_syscall_mode = (gdb_syscall_state ? GDB_SYS_ENABLED
                                              : GDB_SYS_DISABLED);
    }
    return gdb_syscall_mode == GDB_SYS_ENABLED;
}

/* Resume execution.  */
static inline void gdb_continue(GDBState *s)
{
#ifdef CONFIG_USER_ONLY
    s->running_state = 1;
#else
    vm_start();
#endif
}

static void put_buffer(GDBState *s, const uint8_t *buf, int len)
{
#ifdef CONFIG_USER_ONLY
    int ret;

    while (len > 0) {
        ret = send(s->fd, buf, len, 0);
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN)
                return;
        } else {
            buf += ret;
            len -= ret;
        }
    }
#else
    qemu_chr_write(s->chr, buf, len);
#endif
}

static inline int fromhex(int v)
{
    if (v >= '0' && v <= '9')
        return v - '0';
    else if (v >= 'A' && v <= 'F')
        return v - 'A' + 10;
    else if (v >= 'a' && v <= 'f')
        return v - 'a' + 10;
    else
        return 0;
}

static inline int tohex(int v)
{
    if (v < 10)
        return v + '0';
    else
        return v - 10 + 'a';
}

static void memtohex(char *buf, const uint8_t *mem, int len)
{
    int i, c;
    char *q;
    q = buf;
    for(i = 0; i < len; i++) {
        c = mem[i];
        *q++ = tohex(c >> 4);
        *q++ = tohex(c & 0xf);
    }
    *q = '\0';
}

static void hextomem(uint8_t *mem, const char *buf, int len)
{
    int i;

    for(i = 0; i < len; i++) {
        mem[i] = (fromhex(buf[0]) << 4) | fromhex(buf[1]);
        buf += 2;
    }
}

/* return -1 if error, 0 if OK */
static int put_packet(GDBState *s, const char *buf)
{
    int len, csum, i;
    uint8_t *p;

#ifdef DEBUG_GDB
    printf("reply='%s'\n", buf);
#endif

    for(;;) {
        p = s->last_packet;
        *(p++) = '$';
        len = strlen(buf);
        memcpy(p, buf, len);
        p += len;
        csum = 0;
        for(i = 0; i < len; i++) {
            csum += buf[i];
        }
        *(p++) = '#';
        *(p++) = tohex((csum >> 4) & 0xf);
        *(p++) = tohex((csum) & 0xf);

        s->last_packet_len = p - s->last_packet;
        put_buffer(s, (uint8_t *)s->last_packet, s->last_packet_len);

#ifdef CONFIG_USER_ONLY
        i = get_char(s);
        if (i < 0)
            return -1;
        if (i == '+')
            break;
#else
        break;
#endif
    }
    return 0;
}

#if defined(TARGET_I386)

#ifdef TARGET_X86_64
static const uint8_t gdb_x86_64_regs[16] = {
    R_EAX, R_EBX, R_ECX, R_EDX, R_ESI, R_EDI, R_EBP, R_ESP,
    8, 9, 10, 11, 12, 13, 14, 15,
};
#endif

static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    int i, fpus, nb_regs;
    uint8_t *p;

    p = mem_buf;
#ifdef TARGET_X86_64
    if (env->hflags & HF_CS64_MASK) {
        nb_regs = 16;
        for(i = 0; i < 16; i++) {
            *(uint64_t *)p = tswap64(env->regs[gdb_x86_64_regs[i]]);
            p += 8;
        }
        *(uint64_t *)p = tswap64(env->eip);
        p += 8;
    } else
#endif
    {
        nb_regs = 8;
        for(i = 0; i < 8; i++) {
            *(uint32_t *)p = tswap32(env->regs[i]);
            p += 4;
        }
        *(uint32_t *)p = tswap32(env->eip);
        p += 4;
    }

    *(uint32_t *)p = tswap32(env->eflags);
    p += 4;
    *(uint32_t *)p = tswap32(env->segs[R_CS].selector);
    p += 4;
    *(uint32_t *)p = tswap32(env->segs[R_SS].selector);
    p += 4;
    *(uint32_t *)p = tswap32(env->segs[R_DS].selector);
    p += 4;
    *(uint32_t *)p = tswap32(env->segs[R_ES].selector);
    p += 4;
    *(uint32_t *)p = tswap32(env->segs[R_FS].selector);
    p += 4;
    *(uint32_t *)p = tswap32(env->segs[R_GS].selector);
    p += 4;
    for(i = 0; i < 8; i++) {
        /* XXX: convert floats */
#ifdef USE_X86LDOUBLE
        memcpy(p, &env->fpregs[i], 10);
#else
        memset(p, 0, 10);
#endif
        p += 10;
    }
    *(uint32_t *)p = tswap32(env->fpuc); /* fctrl */
    p += 4;
    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    *(uint32_t *)p = tswap32(fpus); /* fstat */
    p += 4;
    *(uint32_t *)p = 0; /* ftag */
    p += 4;
    *(uint32_t *)p = 0; /* fiseg */
    p += 4;
    *(uint32_t *)p = 0; /* fioff */
    p += 4;
    *(uint32_t *)p = 0; /* foseg */
    p += 4;
    *(uint32_t *)p = 0; /* fooff */
    p += 4;
    *(uint32_t *)p = 0; /* fop */
    p += 4;
    for(i = 0; i < nb_regs; i++) {
        *(uint64_t *)p = tswap64(env->xmm_regs[i].XMM_Q(0));
        p += 8;
        *(uint64_t *)p = tswap64(env->xmm_regs[i].XMM_Q(1));
        p += 8;
    }
    *(uint32_t *)p = tswap32(env->mxcsr);
    p += 4;
    return p - mem_buf;
}

static inline void cpu_gdb_load_seg(CPUState *env, const uint8_t **pp, 
                                    int sreg)
{
    const uint8_t *p;
    uint32_t sel;
    p = *pp;
    sel = tswap32(*(uint32_t *)p);
    p += 4;
    if (sel != env->segs[sreg].selector) {
#if defined(CONFIG_USER_ONLY)
        cpu_x86_load_seg(env, sreg, sel);
#else
        /* XXX: do it with a debug function which does not raise an
           exception */
#endif
    }
    *pp = p;
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
    const uint8_t *p = mem_buf;
    int i, nb_regs;
    uint16_t fpus;

#ifdef TARGET_X86_64
    if (env->hflags & HF_CS64_MASK) {
        nb_regs = 16;
        for(i = 0; i < 16; i++) {
            env->regs[gdb_x86_64_regs[i]] = tswap64(*(uint64_t *)p);
            p += 8;
        }
        env->eip = tswap64(*(uint64_t *)p);
        p += 8;
    } else
#endif
    {
        nb_regs = 8;
        for(i = 0; i < 8; i++) {
            env->regs[i] = tswap32(*(uint32_t *)p);
            p += 4;
        }
        env->eip = tswap32(*(uint32_t *)p);
        p += 4;
    }
    env->eflags = tswap32(*(uint32_t *)p);
    p += 4;
    cpu_gdb_load_seg(env, &p, R_CS);
    cpu_gdb_load_seg(env, &p, R_SS);
    cpu_gdb_load_seg(env, &p, R_DS);
    cpu_gdb_load_seg(env, &p, R_ES);
    cpu_gdb_load_seg(env, &p, R_FS);
    cpu_gdb_load_seg(env, &p, R_GS);
    
    /* FPU state */
    for(i = 0; i < 8; i++) {
        /* XXX: convert floats */
#ifdef USE_X86LDOUBLE
        memcpy(&env->fpregs[i], p, 10);
#endif
        p += 10;
    }
    env->fpuc = tswap32(*(uint32_t *)p); /* fctrl */
    p += 4;
    fpus = tswap32(*(uint32_t *)p);
    p += 4;
    env->fpstt = (fpus >> 11) & 7;
    env->fpus = fpus & ~0x3800;
    p += 4 * 6;
    
    if (size >= ((p - mem_buf) + 16 * nb_regs + 4)) {
        /* SSE state */
        for(i = 0; i < nb_regs; i++) {
            env->xmm_regs[i].XMM_Q(0) = tswap64(*(uint64_t *)p);
            p += 8;
            env->xmm_regs[i].XMM_Q(1) = tswap64(*(uint64_t *)p);
            p += 8;
        }
        env->mxcsr = tswap32(*(uint32_t *)p);
        p += 4;
    }
}

#elif defined (TARGET_PPC)
static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    uint32_t *registers = (uint32_t *)mem_buf, tmp;
    int i;

    /* fill in gprs */
    for(i = 0; i < 32; i++) {
        registers[i] = tswapl(env->gpr[i]);
    }
    /* fill in fprs */
    for (i = 0; i < 32; i++) {
        registers[(i * 2) + 32] = tswapl(*((uint32_t *)&env->fpr[i]));
	registers[(i * 2) + 33] = tswapl(*((uint32_t *)&env->fpr[i] + 1));
    }
    /* nip, msr, ccr, lnk, ctr, xer, mq */
    registers[96] = tswapl(env->nip);
    registers[97] = tswapl(env->msr);
    tmp = 0;
    for (i = 0; i < 8; i++)
        tmp |= env->crf[i] << (32 - ((i + 1) * 4));
    registers[98] = tswapl(tmp);
    registers[99] = tswapl(env->lr);
    registers[100] = tswapl(env->ctr);
    registers[101] = tswapl(ppc_load_xer(env));
    registers[102] = 0;

    return 103 * 4;
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
    uint32_t *registers = (uint32_t *)mem_buf;
    int i;

    /* fill in gprs */
    for (i = 0; i < 32; i++) {
        env->gpr[i] = tswapl(registers[i]);
    }
    /* fill in fprs */
    for (i = 0; i < 32; i++) {
        *((uint32_t *)&env->fpr[i]) = tswapl(registers[(i * 2) + 32]);
	*((uint32_t *)&env->fpr[i] + 1) = tswapl(registers[(i * 2) + 33]);
    }
    /* nip, msr, ccr, lnk, ctr, xer, mq */
    env->nip = tswapl(registers[96]);
    ppc_store_msr(env, tswapl(registers[97]));
    registers[98] = tswapl(registers[98]);
    for (i = 0; i < 8; i++)
        env->crf[i] = (registers[98] >> (32 - ((i + 1) * 4))) & 0xF;
    env->lr = tswapl(registers[99]);
    env->ctr = tswapl(registers[100]);
    ppc_store_xer(env, tswapl(registers[101]));
}
#elif defined (TARGET_SPARC)
#ifdef TARGET_ABI32
#define tswap_abi(val) tswap32(val &0xffffffff)
#else
#define tswap_abi(val) tswapl(val)
#endif
static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
#ifdef TARGET_ABI32
    abi_ulong *registers = (abi_ulong *)mem_buf;
#else
    target_ulong *registers = (target_ulong *)mem_buf;
#endif
    int i;

    /* fill in g0..g7 */
    for(i = 0; i < 8; i++) {
        registers[i] = tswap_abi(env->gregs[i]);
    }
    /* fill in register window */
    for(i = 0; i < 24; i++) {
        registers[i + 8] = tswap_abi(env->regwptr[i]);
    }
#if !defined(TARGET_SPARC64) || defined(TARGET_ABI32)
    /* fill in fprs */
    for (i = 0; i < 32; i++) {
        registers[i + 32] = tswap_abi(*((uint32_t *)&env->fpr[i]));
    }
    /* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
    registers[64] = tswap_abi(env->y);
    {
        uint32_t tmp;

        tmp = GET_PSR(env);
        registers[65] = tswap32(tmp);
    }
    registers[66] = tswap_abi(env->wim);
    registers[67] = tswap_abi(env->tbr);
    registers[68] = tswap_abi(env->pc);
    registers[69] = tswap_abi(env->npc);
    registers[70] = tswap_abi(env->fsr);
    registers[71] = 0; /* csr */
    registers[72] = 0;
    return 73 * sizeof(uint32_t);
#else
    /* fill in fprs */
    for (i = 0; i < 64; i += 2) {
	uint64_t tmp;

        tmp = ((uint64_t)*(uint32_t *)&env->fpr[i]) << 32;
        tmp |= *(uint32_t *)&env->fpr[i + 1];
        registers[i / 2 + 32] = tswap64(tmp);
    }
    registers[64] = tswapl(env->pc);
    registers[65] = tswapl(env->npc);
    registers[66] = tswapl(((uint64_t)GET_CCR(env) << 32) |
                           ((env->asi & 0xff) << 24) |
                           ((env->pstate & 0xfff) << 8) |
                           GET_CWP64(env));
    registers[67] = tswapl(env->fsr);
    registers[68] = tswapl(env->fprs);
    registers[69] = tswapl(env->y);
    return 70 * sizeof(target_ulong);
#endif
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
#ifdef TARGET_ABI32
    abi_ulong *registers = (abi_ulong *)mem_buf;
#else
    target_ulong *registers = (target_ulong *)mem_buf;
#endif
    int i;

    /* fill in g0..g7 */
    for(i = 0; i < 7; i++) {
        env->gregs[i] = tswap_abi(registers[i]);
    }
    /* fill in register window */
    for(i = 0; i < 24; i++) {
        env->regwptr[i] = tswap_abi(registers[i + 8]);
    }
#if !defined(TARGET_SPARC64) || defined(TARGET_ABI32)
    /* fill in fprs */
    for (i = 0; i < 32; i++) {
        *((uint32_t *)&env->fpr[i]) = tswap_abi(registers[i + 32]);
    }
    /* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
    env->y = tswap_abi(registers[64]);
    PUT_PSR(env, tswap_abi(registers[65]));
    env->wim = tswap_abi(registers[66]);
    env->tbr = tswap_abi(registers[67]);
    env->pc = tswap_abi(registers[68]);
    env->npc = tswap_abi(registers[69]);
    env->fsr = tswap_abi(registers[70]);
#else
    for (i = 0; i < 64; i += 2) {
        uint64_t tmp;

        tmp = tswap64(registers[i / 2 + 32]);
	*((uint32_t *)&env->fpr[i]) = tmp >> 32;
	*((uint32_t *)&env->fpr[i + 1]) = tmp & 0xffffffff;
    }
    env->pc = tswapl(registers[64]);
    env->npc = tswapl(registers[65]);
    {
        uint64_t tmp = tswapl(registers[66]);

        PUT_CCR(env, tmp >> 32);
        env->asi = (tmp >> 24) & 0xff;
        env->pstate = (tmp >> 8) & 0xfff;
        PUT_CWP64(env, tmp & 0xff);
    }
    env->fsr = tswapl(registers[67]);
    env->fprs = tswapl(registers[68]);
    env->y = tswapl(registers[69]);
#endif
}
#undef tswap_abi
#elif defined (TARGET_ARM)
static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    int i;
    uint8_t *ptr;

    ptr = mem_buf;
    /* 16 core integer registers (4 bytes each).  */
    for (i = 0; i < 16; i++)
      {
        *(uint32_t *)ptr = tswapl(env->regs[i]);
        ptr += 4;
      }
    /* 8 FPA registers (12 bytes each), FPS (4 bytes).
       Not yet implemented.  */
    memset (ptr, 0, 8 * 12 + 4);
    ptr += 8 * 12 + 4;
    /* CPSR (4 bytes).  */
    *(uint32_t *)ptr = tswapl (cpsr_read(env));
    ptr += 4;

    return ptr - mem_buf;
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
    int i;
    uint8_t *ptr;

    ptr = mem_buf;
    /* Core integer registers.  */
    for (i = 0; i < 16; i++)
      {
        env->regs[i] = tswapl(*(uint32_t *)ptr);
        ptr += 4;
      }
    /* Ignore FPA regs and scr.  */
    ptr += 8 * 12 + 4;
    cpsr_write (env, tswapl(*(uint32_t *)ptr), 0xffffffff);
}
#elif defined (TARGET_M68K)
static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    int i;
    uint8_t *ptr;
    CPU_DoubleU u;

    ptr = mem_buf;
    /* D0-D7 */
    for (i = 0; i < 8; i++) {
        *(uint32_t *)ptr = tswapl(env->dregs[i]);
        ptr += 4;
    }
    /* A0-A7 */
    for (i = 0; i < 8; i++) {
        *(uint32_t *)ptr = tswapl(env->aregs[i]);
        ptr += 4;
    }
    *(uint32_t *)ptr = tswapl(env->sr);
    ptr += 4;
    *(uint32_t *)ptr = tswapl(env->pc);
    ptr += 4;
    /* F0-F7.  The 68881/68040 have 12-bit extended precision registers.
       ColdFire has 8-bit double precision registers.  */
    for (i = 0; i < 8; i++) {
        u.d = env->fregs[i];
        *(uint32_t *)ptr = tswap32(u.l.upper);
        *(uint32_t *)ptr = tswap32(u.l.lower);
    }
    /* FP control regs (not implemented).  */
    memset (ptr, 0, 3 * 4);
    ptr += 3 * 4;

    return ptr - mem_buf;
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
    int i;
    uint8_t *ptr;
    CPU_DoubleU u;

    ptr = mem_buf;
    /* D0-D7 */
    for (i = 0; i < 8; i++) {
        env->dregs[i] = tswapl(*(uint32_t *)ptr);
        ptr += 4;
    }
    /* A0-A7 */
    for (i = 0; i < 8; i++) {
        env->aregs[i] = tswapl(*(uint32_t *)ptr);
        ptr += 4;
    }
    env->sr = tswapl(*(uint32_t *)ptr);
    ptr += 4;
    env->pc = tswapl(*(uint32_t *)ptr);
    ptr += 4;
    /* F0-F7.  The 68881/68040 have 12-bit extended precision registers.
       ColdFire has 8-bit double precision registers.  */
    for (i = 0; i < 8; i++) {
        u.l.upper = tswap32(*(uint32_t *)ptr);
        u.l.lower = tswap32(*(uint32_t *)ptr);
        env->fregs[i] = u.d;
    }
    /* FP control regs (not implemented).  */
    ptr += 3 * 4;
}
#elif defined (TARGET_MIPS)
static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    int i;
    uint8_t *ptr;

    ptr = mem_buf;
    for (i = 0; i < 32; i++)
      {
        *(target_ulong *)ptr = tswapl(env->active_tc.gpr[i]);
        ptr += sizeof(target_ulong);
      }

    *(target_ulong *)ptr = (int32_t)tswap32(env->CP0_Status);
    ptr += sizeof(target_ulong);

    *(target_ulong *)ptr = tswapl(env->active_tc.LO[0]);
    ptr += sizeof(target_ulong);

    *(target_ulong *)ptr = tswapl(env->active_tc.HI[0]);
    ptr += sizeof(target_ulong);

    *(target_ulong *)ptr = tswapl(env->CP0_BadVAddr);
    ptr += sizeof(target_ulong);

    *(target_ulong *)ptr = (int32_t)tswap32(env->CP0_Cause);
    ptr += sizeof(target_ulong);

    *(target_ulong *)ptr = tswapl(env->active_tc.PC);
    ptr += sizeof(target_ulong);

    if (env->CP0_Config1 & (1 << CP0C1_FP))
      {
        for (i = 0; i < 32; i++)
          {
            if (env->CP0_Status & (1 << CP0St_FR))
              *(target_ulong *)ptr = tswapl(env->fpu->fpr[i].d);
            else
              *(target_ulong *)ptr = tswap32(env->fpu->fpr[i].w[FP_ENDIAN_IDX]);
            ptr += sizeof(target_ulong);
          }

        *(target_ulong *)ptr = (int32_t)tswap32(env->fpu->fcr31);
        ptr += sizeof(target_ulong);

        *(target_ulong *)ptr = (int32_t)tswap32(env->fpu->fcr0);
        ptr += sizeof(target_ulong);
      }

    /* "fp", pseudo frame pointer. Not yet implemented in gdb. */
    *(target_ulong *)ptr = 0;
    ptr += sizeof(target_ulong);

    /* Registers for embedded use, we just pad them. */
    for (i = 0; i < 16; i++)
      {
        *(target_ulong *)ptr = 0;
        ptr += sizeof(target_ulong);
      }

    /* Processor ID. */
    *(target_ulong *)ptr = (int32_t)tswap32(env->CP0_PRid);
    ptr += sizeof(target_ulong);

    return ptr - mem_buf;
}

/* convert MIPS rounding mode in FCR31 to IEEE library */
static unsigned int ieee_rm[] =
  {
    float_round_nearest_even,
    float_round_to_zero,
    float_round_up,
    float_round_down
  };
#define RESTORE_ROUNDING_MODE \
    set_float_rounding_mode(ieee_rm[env->fpu->fcr31 & 3], &env->fpu->fp_status)

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
    int i;
    uint8_t *ptr;

    ptr = mem_buf;
    for (i = 0; i < 32; i++)
      {
        env->active_tc.gpr[i] = tswapl(*(target_ulong *)ptr);
        ptr += sizeof(target_ulong);
      }

    env->CP0_Status = tswapl(*(target_ulong *)ptr);
    ptr += sizeof(target_ulong);

    env->active_tc.LO[0] = tswapl(*(target_ulong *)ptr);
    ptr += sizeof(target_ulong);

    env->active_tc.HI[0] = tswapl(*(target_ulong *)ptr);
    ptr += sizeof(target_ulong);

    env->CP0_BadVAddr = tswapl(*(target_ulong *)ptr);
    ptr += sizeof(target_ulong);

    env->CP0_Cause = tswapl(*(target_ulong *)ptr);
    ptr += sizeof(target_ulong);

    env->active_tc.PC = tswapl(*(target_ulong *)ptr);
    ptr += sizeof(target_ulong);

    if (env->CP0_Config1 & (1 << CP0C1_FP))
      {
        for (i = 0; i < 32; i++)
          {
            if (env->CP0_Status & (1 << CP0St_FR))
              env->fpu->fpr[i].d = tswapl(*(target_ulong *)ptr);
            else
              env->fpu->fpr[i].w[FP_ENDIAN_IDX] = tswapl(*(target_ulong *)ptr);
            ptr += sizeof(target_ulong);
          }

        env->fpu->fcr31 = tswapl(*(target_ulong *)ptr) & 0xFF83FFFF;
        ptr += sizeof(target_ulong);

        /* The remaining registers are assumed to be read-only. */

        /* set rounding mode */
        RESTORE_ROUNDING_MODE;

#ifndef CONFIG_SOFTFLOAT
        /* no floating point exception for native float */
        SET_FP_ENABLE(env->fcr31, 0);
#endif
      }
}
#elif defined (TARGET_SH4)

/* Hint: Use "set architecture sh4" in GDB to see fpu registers */

static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
  uint32_t *ptr = (uint32_t *)mem_buf;
  int i;

#define SAVE(x) *ptr++=tswapl(x)
  if ((env->sr & (SR_MD | SR_RB)) == (SR_MD | SR_RB)) {
      for (i = 0; i < 8; i++) SAVE(env->gregs[i + 16]);
  } else {
      for (i = 0; i < 8; i++) SAVE(env->gregs[i]);
  }
  for (i = 8; i < 16; i++) SAVE(env->gregs[i]);
  SAVE (env->pc);
  SAVE (env->pr);
  SAVE (env->gbr);
  SAVE (env->vbr);
  SAVE (env->mach);
  SAVE (env->macl);
  SAVE (env->sr);
  SAVE (env->fpul);
  SAVE (env->fpscr);
  for (i = 0; i < 16; i++)
      SAVE(env->fregs[i + ((env->fpscr & FPSCR_FR) ? 16 : 0)]);
  SAVE (env->ssr);
  SAVE (env->spc);
  for (i = 0; i < 8; i++) SAVE(env->gregs[i]);
  for (i = 0; i < 8; i++) SAVE(env->gregs[i + 16]);
  return ((uint8_t *)ptr - mem_buf);
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
  uint32_t *ptr = (uint32_t *)mem_buf;
  int i;

#define LOAD(x) (x)=*ptr++;
  if ((env->sr & (SR_MD | SR_RB)) == (SR_MD | SR_RB)) {
      for (i = 0; i < 8; i++) LOAD(env->gregs[i + 16]);
  } else {
      for (i = 0; i < 8; i++) LOAD(env->gregs[i]);
  }
  for (i = 8; i < 16; i++) LOAD(env->gregs[i]);
  LOAD (env->pc);
  LOAD (env->pr);
  LOAD (env->gbr);
  LOAD (env->vbr);
  LOAD (env->mach);
  LOAD (env->macl);
  LOAD (env->sr);
  LOAD (env->fpul);
  LOAD (env->fpscr);
  for (i = 0; i < 16; i++)
      LOAD(env->fregs[i + ((env->fpscr & FPSCR_FR) ? 16 : 0)]);
  LOAD (env->ssr);
  LOAD (env->spc);
  for (i = 0; i < 8; i++) LOAD(env->gregs[i]);
  for (i = 0; i < 8; i++) LOAD(env->gregs[i + 16]);
}
#elif defined (TARGET_CRIS)

static int cris_save_32 (unsigned char *d, uint32_t value)
{
	*d++ = (value);
	*d++ = (value >>= 8);
	*d++ = (value >>= 8);
	*d++ = (value >>= 8);
	return 4;
}
static int cris_save_16 (unsigned char *d, uint32_t value)
{
	*d++ = (value);
	*d++ = (value >>= 8);
	return 2;
}
static int cris_save_8 (unsigned char *d, uint32_t value)
{
	*d++ = (value);
	return 1;
}

/* FIXME: this will bug on archs not supporting unaligned word accesses.  */
static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
  uint8_t *ptr = mem_buf;
  uint8_t srs;
  int i;

  for (i = 0; i < 16; i++)
	  ptr += cris_save_32 (ptr, env->regs[i]);

  srs = env->pregs[PR_SRS];

  ptr += cris_save_8 (ptr, env->pregs[0]);
  ptr += cris_save_8 (ptr, env->pregs[1]);
  ptr += cris_save_32 (ptr, env->pregs[2]);
  ptr += cris_save_8 (ptr, srs);
  ptr += cris_save_16 (ptr, env->pregs[4]);

  for (i = 5; i < 16; i++)
	  ptr += cris_save_32 (ptr, env->pregs[i]);

  ptr += cris_save_32 (ptr, env->pc);

  for (i = 0; i < 16; i++)
	  ptr += cris_save_32 (ptr, env->sregs[srs][i]);

  return ((uint8_t *)ptr - mem_buf);
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
  uint32_t *ptr = (uint32_t *)mem_buf;
  int i;

#define LOAD(x) (x)=*ptr++;
  for (i = 0; i < 16; i++) LOAD(env->regs[i]);
  LOAD (env->pc);
}
#else
static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    return 0;
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
}

#endif

static int gdb_handle_packet(GDBState *s, CPUState *env, const char *line_buf)
{
    const char *p;
    int ch, reg_size, type;
    char buf[4096];
    uint8_t mem_buf[4096];
    uint32_t *registers;
    target_ulong addr, len;

#ifdef DEBUG_GDB
    printf("command='%s'\n", line_buf);
#endif
    p = line_buf;
    ch = *p++;
    switch(ch) {
    case '?':
        /* TODO: Make this return the correct value for user-mode.  */
        snprintf(buf, sizeof(buf), "S%02x", SIGTRAP);
        put_packet(s, buf);
        /* Remove all the breakpoints when this query is issued,
         * because gdb is doing and initial connect and the state
         * should be cleaned up.
         */
        cpu_breakpoint_remove_all(env);
        cpu_watchpoint_remove_all(env);
        break;
    case 'c':
        if (*p != '\0') {
            addr = strtoull(p, (char **)&p, 16);
#if defined(TARGET_I386)
            env->eip = addr;
#elif defined (TARGET_PPC)
            env->nip = addr;
#elif defined (TARGET_SPARC)
            env->pc = addr;
            env->npc = addr + 4;
#elif defined (TARGET_ARM)
            env->regs[15] = addr;
#elif defined (TARGET_SH4)
            env->pc = addr;
#elif defined (TARGET_MIPS)
            env->active_tc.PC = addr;
#elif defined (TARGET_CRIS)
            env->pc = addr;
#endif
        }
        gdb_continue(s);
	return RS_IDLE;
    case 'C':
        s->signal = strtoul(p, (char **)&p, 16);
        gdb_continue(s);
        return RS_IDLE;
    case 'k':
        /* Kill the target */
        fprintf(stderr, "\nQEMU: Terminated via GDBstub\n");
        exit(0);
    case 'D':
        /* Detach packet */
        cpu_breakpoint_remove_all(env);
        cpu_watchpoint_remove_all(env);
        gdb_continue(s);
        put_packet(s, "OK");
        break;
    case 's':
        if (*p != '\0') {
            addr = strtoull(p, (char **)&p, 16);
#if defined(TARGET_I386)
            env->eip = addr;
#elif defined (TARGET_PPC)
            env->nip = addr;
#elif defined (TARGET_SPARC)
            env->pc = addr;
            env->npc = addr + 4;
#elif defined (TARGET_ARM)
            env->regs[15] = addr;
#elif defined (TARGET_SH4)
            env->pc = addr;
#elif defined (TARGET_MIPS)
            env->active_tc.PC = addr;
#elif defined (TARGET_CRIS)
            env->pc = addr;
#endif
        }
        cpu_single_step(env, sstep_flags);
        gdb_continue(s);
	return RS_IDLE;
    case 'F':
        {
            target_ulong ret;
            target_ulong err;

            ret = strtoull(p, (char **)&p, 16);
            if (*p == ',') {
                p++;
                err = strtoull(p, (char **)&p, 16);
            } else {
                err = 0;
            }
            if (*p == ',')
                p++;
            type = *p;
            if (gdb_current_syscall_cb)
                gdb_current_syscall_cb(s->env, ret, err);
            if (type == 'C') {
                put_packet(s, "T02");
            } else {
                gdb_continue(s);
            }
        }
        break;
    case 'g':
        reg_size = cpu_gdb_read_registers(env, mem_buf);
        memtohex(buf, mem_buf, reg_size);
        put_packet(s, buf);
        break;
    case 'G':
        registers = (void *)mem_buf;
        len = strlen(p) / 2;
        hextomem((uint8_t *)registers, p, len);
        cpu_gdb_write_registers(env, mem_buf, len);
        put_packet(s, "OK");
        break;
    case 'm':
        addr = strtoull(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        len = strtoull(p, NULL, 16);
        if (cpu_memory_rw_debug(env, addr, mem_buf, len, 0) != 0) {
            put_packet (s, "E14");
        } else {
            memtohex(buf, mem_buf, len);
            put_packet(s, buf);
        }
        break;
    case 'M':
        addr = strtoull(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        len = strtoull(p, (char **)&p, 16);
        if (*p == ':')
            p++;
        hextomem(mem_buf, p, len);
        if (cpu_memory_rw_debug(env, addr, mem_buf, len, 1) != 0)
            put_packet(s, "E14");
        else
            put_packet(s, "OK");
        break;
    case 'Z':
        type = strtoul(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        addr = strtoull(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        len = strtoull(p, (char **)&p, 16);
        switch (type) {
        case 0:
        case 1:
            if (cpu_breakpoint_insert(env, addr) < 0)
                goto breakpoint_error;
            put_packet(s, "OK");
            break;
#ifndef CONFIG_USER_ONLY
        case 2:
            type = PAGE_WRITE;
            goto insert_watchpoint;
        case 3:
            type = PAGE_READ;
            goto insert_watchpoint;
        case 4:
            type = PAGE_READ | PAGE_WRITE;
        insert_watchpoint:
            if (cpu_watchpoint_insert(env, addr, type) < 0)
                goto breakpoint_error;
            put_packet(s, "OK");
            break;
#endif
        default:
            put_packet(s, "");
            break;
        }
        break;
    breakpoint_error:
        put_packet(s, "E22");
        break;

    case 'z':
        type = strtoul(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        addr = strtoull(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        len = strtoull(p, (char **)&p, 16);
        if (type == 0 || type == 1) {
            cpu_breakpoint_remove(env, addr);
            put_packet(s, "OK");
#ifndef CONFIG_USER_ONLY
        } else if (type >= 2 || type <= 4) {
            cpu_watchpoint_remove(env, addr);
            put_packet(s, "OK");
#endif
        } else {
            put_packet(s, "");
        }
        break;
    case 'q':
    case 'Q':
        /* parse any 'q' packets here */
        if (!strcmp(p,"qemu.sstepbits")) {
            /* Query Breakpoint bit definitions */
            snprintf(buf, sizeof(buf), "ENABLE=%x,NOIRQ=%x,NOTIMER=%x",
                     SSTEP_ENABLE,
                     SSTEP_NOIRQ,
                     SSTEP_NOTIMER);
            put_packet(s, buf);
            break;
        } else if (strncmp(p,"qemu.sstep",10) == 0) {
            /* Display or change the sstep_flags */
            p += 10;
            if (*p != '=') {
                /* Display current setting */
                snprintf(buf, sizeof(buf), "0x%x", sstep_flags);
                put_packet(s, buf);
                break;
            }
            p++;
            type = strtoul(p, (char **)&p, 16);
            sstep_flags = type;
            put_packet(s, "OK");
            break;
        }
#ifdef CONFIG_LINUX_USER
        else if (strncmp(p, "Offsets", 7) == 0) {
            TaskState *ts = env->opaque;

            snprintf(buf, sizeof(buf),
                     "Text=" TARGET_ABI_FMT_lx ";Data=" TARGET_ABI_FMT_lx
                     ";Bss=" TARGET_ABI_FMT_lx,
                     ts->info->code_offset,
                     ts->info->data_offset,
                     ts->info->data_offset);
            put_packet(s, buf);
            break;
        }
#endif
        /* Fall through.  */
    default:
        /* put empty packet */
        buf[0] = '\0';
        put_packet(s, buf);
        break;
    }
    return RS_IDLE;
}

extern void tb_flush(CPUState *env);

#ifndef CONFIG_USER_ONLY
static void gdb_vm_stopped(void *opaque, int reason)
{
    GDBState *s = opaque;
    char buf[256];
    int ret;

    if (s->state == RS_SYSCALL)
        return;

    /* disable single step if it was enable */
    cpu_single_step(s->env, 0);

    if (reason == EXCP_DEBUG) {
        if (s->env->watchpoint_hit) {
            snprintf(buf, sizeof(buf), "T%02xwatch:" TARGET_FMT_lx ";",
                     SIGTRAP,
                     s->env->watchpoint[s->env->watchpoint_hit - 1].vaddr);
            put_packet(s, buf);
            s->env->watchpoint_hit = 0;
            return;
        }
	tb_flush(s->env);
        ret = SIGTRAP;
    } else if (reason == EXCP_INTERRUPT) {
        ret = SIGINT;
    } else {
        ret = 0;
    }
    snprintf(buf, sizeof(buf), "S%02x", ret);
    put_packet(s, buf);
}
#endif

/* Send a gdb syscall request.
   This accepts limited printf-style format specifiers, specifically:
    %x  - target_ulong argument printed in hex.
    %lx - 64-bit argument printed in hex.
    %s  - string pointer (target_ulong) and length (int) pair.  */
void gdb_do_syscall(gdb_syscall_complete_cb cb, const char *fmt, ...)
{
    va_list va;
    char buf[256];
    char *p;
    target_ulong addr;
    uint64_t i64;
    GDBState *s;

    s = gdb_syscall_state;
    if (!s)
        return;
    gdb_current_syscall_cb = cb;
    s->state = RS_SYSCALL;
#ifndef CONFIG_USER_ONLY
    vm_stop(EXCP_DEBUG);
#endif
    s->state = RS_IDLE;
    va_start(va, fmt);
    p = buf;
    *(p++) = 'F';
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt++) {
            case 'x':
                addr = va_arg(va, target_ulong);
                p += snprintf(p, &buf[sizeof(buf)] - p, TARGET_FMT_lx, addr);
                break;
            case 'l':
                if (*(fmt++) != 'x')
                    goto bad_format;
                i64 = va_arg(va, uint64_t);
                p += snprintf(p, &buf[sizeof(buf)] - p, "%" PRIx64, i64);
                break;
            case 's':
                addr = va_arg(va, target_ulong);
                p += snprintf(p, &buf[sizeof(buf)] - p, TARGET_FMT_lx "/%x",
                              addr, va_arg(va, int));
                break;
            default:
            bad_format:
                fprintf(stderr, "gdbstub: Bad syscall format string '%s'\n",
                        fmt - 1);
                break;
            }
        } else {
            *(p++) = *(fmt++);
        }
    }
    *p = 0;
    va_end(va);
    put_packet(s, buf);
#ifdef CONFIG_USER_ONLY
    gdb_handlesig(s->env, 0);
#else
    cpu_interrupt(s->env, CPU_INTERRUPT_EXIT);
#endif
}

static void gdb_read_byte(GDBState *s, int ch)
{
    CPUState *env = s->env;
    int i, csum;
    uint8_t reply;

#ifndef CONFIG_USER_ONLY
    if (s->last_packet_len) {
        /* Waiting for a response to the last packet.  If we see the start
           of a new command then abandon the previous response.  */
        if (ch == '-') {
#ifdef DEBUG_GDB
            printf("Got NACK, retransmitting\n");
#endif
            put_buffer(s, (uint8_t *)s->last_packet, s->last_packet_len);
        }
#ifdef DEBUG_GDB
        else if (ch == '+')
            printf("Got ACK\n");
        else
            printf("Got '%c' when expecting ACK/NACK\n", ch);
#endif
        if (ch == '+' || ch == '$')
            s->last_packet_len = 0;
        if (ch != '$')
            return;
    }
    if (vm_running) {
        /* when the CPU is running, we cannot do anything except stop
           it when receiving a char */
        vm_stop(EXCP_INTERRUPT);
    } else
#endif
    {
        switch(s->state) {
        case RS_IDLE:
            if (ch == '$') {
                s->line_buf_index = 0;
                s->state = RS_GETLINE;
            }
            break;
        case RS_GETLINE:
            if (ch == '#') {
            s->state = RS_CHKSUM1;
            } else if (s->line_buf_index >= sizeof(s->line_buf) - 1) {
                s->state = RS_IDLE;
            } else {
            s->line_buf[s->line_buf_index++] = ch;
            }
            break;
        case RS_CHKSUM1:
            s->line_buf[s->line_buf_index] = '\0';
            s->line_csum = fromhex(ch) << 4;
            s->state = RS_CHKSUM2;
            break;
        case RS_CHKSUM2:
            s->line_csum |= fromhex(ch);
            csum = 0;
            for(i = 0; i < s->line_buf_index; i++) {
                csum += s->line_buf[i];
            }
            if (s->line_csum != (csum & 0xff)) {
                reply = '-';
                put_buffer(s, &reply, 1);
                s->state = RS_IDLE;
            } else {
                reply = '+';
                put_buffer(s, &reply, 1);
                s->state = gdb_handle_packet(s, env, s->line_buf);
            }
            break;
        default:
            abort();
        }
    }
}

#ifdef CONFIG_USER_ONLY
int
gdb_handlesig (CPUState *env, int sig)
{
  GDBState *s;
  char buf[256];
  int n;

  s = &gdbserver_state;
  if (gdbserver_fd < 0 || s->fd < 0)
    return sig;

  /* disable single step if it was enabled */
  cpu_single_step(env, 0);
  tb_flush(env);

  if (sig != 0)
    {
      snprintf(buf, sizeof(buf), "S%02x", sig);
      put_packet(s, buf);
    }
  /* put_packet() might have detected that the peer terminated the 
     connection.  */
  if (s->fd < 0)
      return sig;

  sig = 0;
  s->state = RS_IDLE;
  s->running_state = 0;
  while (s->running_state == 0) {
      n = read (s->fd, buf, 256);
      if (n > 0)
        {
          int i;

          for (i = 0; i < n; i++)
            gdb_read_byte (s, buf[i]);
        }
      else if (n == 0 || errno != EAGAIN)
        {
          /* XXX: Connection closed.  Should probably wait for annother
             connection before continuing.  */
          return sig;
        }
  }
  sig = s->signal;
  s->signal = 0;
  return sig;
}

/* Tell the remote gdb that the process has exited.  */
void gdb_exit(CPUState *env, int code)
{
  GDBState *s;
  char buf[4];

  s = &gdbserver_state;
  if (gdbserver_fd < 0 || s->fd < 0)
    return;

  snprintf(buf, sizeof(buf), "W%02x", code);
  put_packet(s, buf);
}


static void gdb_accept(void *opaque)
{
    GDBState *s;
    struct sockaddr_in sockaddr;
    socklen_t len;
    int val, fd;

    for(;;) {
        len = sizeof(sockaddr);
        fd = accept(gdbserver_fd, (struct sockaddr *)&sockaddr, &len);
        if (fd < 0 && errno != EINTR) {
            perror("accept");
            return;
        } else if (fd >= 0) {
            break;
        }
    }

    /* set short latency */
    val = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *)&val, sizeof(val));

    s = &gdbserver_state;
    memset (s, 0, sizeof (GDBState));
    s->env = first_cpu; /* XXX: allow to change CPU */
    s->fd = fd;

    gdb_syscall_state = s;

    fcntl(fd, F_SETFL, O_NONBLOCK);
}

static int gdbserver_open(int port)
{
    struct sockaddr_in sockaddr;
    int fd, val, ret;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    /* allow fast reuse */
    val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&val, sizeof(val));

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_port = htons(port);
    sockaddr.sin_addr.s_addr = 0;
    ret = bind(fd, (struct sockaddr *)&sockaddr, sizeof(sockaddr));
    if (ret < 0) {
        perror("bind");
        return -1;
    }
    ret = listen(fd, 0);
    if (ret < 0) {
        perror("listen");
        return -1;
    }
    return fd;
}

int gdbserver_start(int port)
{
    gdbserver_fd = gdbserver_open(port);
    if (gdbserver_fd < 0)
        return -1;
    /* accept connections */
    gdb_accept (NULL);
    return 0;
}
#else
static int gdb_chr_can_receive(void *opaque)
{
  return 1;
}

static void gdb_chr_receive(void *opaque, const uint8_t *buf, int size)
{
    GDBState *s = opaque;
    int i;

    for (i = 0; i < size; i++) {
        gdb_read_byte(s, buf[i]);
    }
}

static void gdb_chr_event(void *opaque, int event)
{
    switch (event) {
    case CHR_EVENT_RESET:
        vm_stop(EXCP_INTERRUPT);
        gdb_syscall_state = opaque;
        break;
    default:
        break;
    }
}

int gdbserver_start(const char *port)
{
    GDBState *s;
    char gdbstub_port_name[128];
    int port_num;
    char *p;
    CharDriverState *chr;

    if (!port || !*port)
      return -1;

    port_num = strtol(port, &p, 10);
    if (*p == 0) {
        /* A numeric value is interpreted as a port number.  */
        snprintf(gdbstub_port_name, sizeof(gdbstub_port_name),
                 "tcp::%d,nowait,nodelay,server", port_num);
        port = gdbstub_port_name;
    }

    chr = qemu_chr_open(port);
    if (!chr)
        return -1;

    s = qemu_mallocz(sizeof(GDBState));
    if (!s) {
        return -1;
    }
    s->env = first_cpu; /* XXX: allow to change CPU */
    s->chr = chr;
    qemu_chr_add_handlers(chr, gdb_chr_can_receive, gdb_chr_receive,
                          gdb_chr_event, s);
    qemu_add_vm_stop_handler(gdb_vm_stopped, s);
    return 0;
}
#endif
