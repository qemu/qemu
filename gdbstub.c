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
#include "qemu-common.h"
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
#include "qemu-char.h"
#include "sysemu.h"
#include "gdbstub.h"
#endif

#define MAX_PACKET_LENGTH 4096

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

typedef struct GDBRegisterState {
    int base_reg;
    int num_regs;
    gdb_reg_cb get_reg;
    gdb_reg_cb set_reg;
    const char *xml;
    struct GDBRegisterState *next;
} GDBRegisterState;

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
    char line_buf[MAX_PACKET_LENGTH];
    int line_buf_index;
    int line_csum;
    uint8_t last_packet[MAX_PACKET_LENGTH + 4];
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

/* This is an ugly hack to cope with both new and old gdb.
   If gdb sends qXfer:features:read then assume we're talking to a newish
   gdb that understands target descriptions.  */
static int gdb_has_xml;

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
static int put_packet_binary(GDBState *s, const char *buf, int len)
{
    int csum, i;
    uint8_t *p;

    for(;;) {
        p = s->last_packet;
        *(p++) = '$';
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

/* return -1 if error, 0 if OK */
static int put_packet(GDBState *s, const char *buf)
{
#ifdef DEBUG_GDB
    printf("reply='%s'\n", buf);
#endif

    return put_packet_binary(s, buf, strlen(buf));
}

/* The GDB remote protocol transfers values in target byte order.  This means
   we can use the raw memory access routines to access the value buffer.
   Conveniently, these also handle the case where the buffer is mis-aligned.
 */
#define GET_REG8(val) do { \
    stb_p(mem_buf, val); \
    return 1; \
    } while(0)
#define GET_REG16(val) do { \
    stw_p(mem_buf, val); \
    return 2; \
    } while(0)
#define GET_REG32(val) do { \
    stl_p(mem_buf, val); \
    return 4; \
    } while(0)
#define GET_REG64(val) do { \
    stq_p(mem_buf, val); \
    return 8; \
    } while(0)

#if TARGET_LONG_BITS == 64
#define GET_REGL(val) GET_REG64(val)
#define ldtul_p(addr) ldq_p(addr)
#else
#define GET_REGL(val) GET_REG32(val)
#define ldtul_p(addr) ldl_p(addr)
#endif

#if defined(TARGET_I386)

#ifdef TARGET_X86_64
static const int gpr_map[16] = {
    R_EAX, R_EBX, R_ECX, R_EDX, R_ESI, R_EDI, R_EBP, R_ESP,
    8, 9, 10, 11, 12, 13, 14, 15
};
#else
static const int gpr_map[8] = {0, 1, 2, 3, 4, 5, 6, 7};
#endif

#define NUM_CORE_REGS (CPU_NB_REGS * 2 + 25)

static int cpu_gdb_read_register(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < CPU_NB_REGS) {
        GET_REGL(env->regs[gpr_map[n]]);
    } else if (n >= CPU_NB_REGS + 8 && n < CPU_NB_REGS + 16) {
        /* FIXME: byteswap float values.  */
#ifdef USE_X86LDOUBLE
        memcpy(mem_buf, &env->fpregs[n - (CPU_NB_REGS + 8)], 10);
#else
        memset(mem_buf, 0, 10);
#endif
        return 10;
    } else if (n >= CPU_NB_REGS + 24) {
        n -= CPU_NB_REGS + 24;
        if (n < CPU_NB_REGS) {
            stq_p(mem_buf, env->xmm_regs[n].XMM_Q(0));
            stq_p(mem_buf + 8, env->xmm_regs[n].XMM_Q(1));
            return 16;
        } else if (n == CPU_NB_REGS) {
            GET_REG32(env->mxcsr);
        } 
    } else {
        n -= CPU_NB_REGS;
        switch (n) {
        case 0: GET_REGL(env->eip);
        case 1: GET_REG32(env->eflags);
        case 2: GET_REG32(env->segs[R_CS].selector);
        case 3: GET_REG32(env->segs[R_SS].selector);
        case 4: GET_REG32(env->segs[R_DS].selector);
        case 5: GET_REG32(env->segs[R_ES].selector);
        case 6: GET_REG32(env->segs[R_FS].selector);
        case 7: GET_REG32(env->segs[R_GS].selector);
        /* 8...15 x87 regs.  */
        case 16: GET_REG32(env->fpuc);
        case 17: GET_REG32((env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11);
        case 18: GET_REG32(0); /* ftag */
        case 19: GET_REG32(0); /* fiseg */
        case 20: GET_REG32(0); /* fioff */
        case 21: GET_REG32(0); /* foseg */
        case 22: GET_REG32(0); /* fooff */
        case 23: GET_REG32(0); /* fop */
        /* 24+ xmm regs.  */
        }
    }
    return 0;
}

static int cpu_gdb_write_register(CPUState *env, uint8_t *mem_buf, int i)
{
    uint32_t tmp;

    if (i < CPU_NB_REGS) {
        env->regs[gpr_map[i]] = ldtul_p(mem_buf);
        return sizeof(target_ulong);
    } else if (i >= CPU_NB_REGS + 8 && i < CPU_NB_REGS + 16) {
        i -= CPU_NB_REGS + 8;
#ifdef USE_X86LDOUBLE
        memcpy(&env->fpregs[i], mem_buf, 10);
#endif
        return 10;
    } else if (i >= CPU_NB_REGS + 24) {
        i -= CPU_NB_REGS + 24;
        if (i < CPU_NB_REGS) {
            env->xmm_regs[i].XMM_Q(0) = ldq_p(mem_buf);
            env->xmm_regs[i].XMM_Q(1) = ldq_p(mem_buf + 8);
            return 16;
        } else if (i == CPU_NB_REGS) {
            env->mxcsr = ldl_p(mem_buf);
            return 4;
        }
    } else {
        i -= CPU_NB_REGS;
        switch (i) {
        case 0: env->eip = ldtul_p(mem_buf); return sizeof(target_ulong);
        case 1: env->eflags = ldl_p(mem_buf); return 4;
#if defined(CONFIG_USER_ONLY)
#define LOAD_SEG(index, sreg)\
            tmp = ldl_p(mem_buf);\
            if (tmp != env->segs[sreg].selector)\
                cpu_x86_load_seg(env, sreg, tmp);
#else
/* FIXME: Honor segment registers.  Needs to avoid raising an exception
   when the selector is invalid.  */
#define LOAD_SEG(index, sreg) do {} while(0)
#endif
        case 2: LOAD_SEG(10, R_CS); return 4;
        case 3: LOAD_SEG(11, R_SS); return 4;
        case 4: LOAD_SEG(12, R_DS); return 4;
        case 5: LOAD_SEG(13, R_ES); return 4;
        case 6: LOAD_SEG(14, R_FS); return 4;
        case 7: LOAD_SEG(15, R_GS); return 4;
        /* 8...15 x87 regs.  */
        case 16: env->fpuc = ldl_p(mem_buf); return 4;
        case 17:
                 tmp = ldl_p(mem_buf);
                 env->fpstt = (tmp >> 11) & 7;
                 env->fpus = tmp & ~0x3800;
                 return 4;
        case 18: /* ftag */ return 4;
        case 19: /* fiseg */ return 4;
        case 20: /* fioff */ return 4;
        case 21: /* foseg */ return 4;
        case 22: /* fooff */ return 4;
        case 23: /* fop */ return 4;
        /* 24+ xmm regs.  */
        }
    }
    /* Unrecognised register.  */
    return 0;
}

#elif defined (TARGET_PPC)

#define NUM_CORE_REGS 71

static int cpu_gdb_read_register(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
        /* gprs */
        GET_REGL(env->gpr[n]);
    } else if (n < 64) {
        /* fprs */
        stfq_p(mem_buf, env->fpr[n]);
        return 8;
    } else {
        switch (n) {
        case 64: GET_REGL(env->nip);
        case 65: GET_REGL(env->msr);
        case 66:
            {
                uint32_t cr = 0;
                int i;
                for (i = 0; i < 8; i++)
                    cr |= env->crf[i] << (32 - ((i + 1) * 4));
                GET_REG32(cr);
            }
        case 67: GET_REGL(env->lr);
        case 68: GET_REGL(env->ctr);
        case 69: GET_REGL(env->xer);
        case 70: GET_REG32(0); /* fpscr */
        }
    }
    return 0;
}

static int cpu_gdb_write_register(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
        /* gprs */
        env->gpr[n] = ldtul_p(mem_buf);
        return sizeof(target_ulong);
    } else if (n < 64) {
        /* fprs */
        env->fpr[n] = ldfq_p(mem_buf);
        return 8;
    } else {
        switch (n) {
        case 64:
            env->nip = ldtul_p(mem_buf);
            return sizeof(target_ulong);
        case 65:
            ppc_store_msr(env, ldtul_p(mem_buf));
            return sizeof(target_ulong);
        case 66:
            {
                uint32_t cr = ldl_p(mem_buf);
                int i;
                for (i = 0; i < 8; i++)
                    env->crf[i] = (cr >> (32 - ((i + 1) * 4))) & 0xF;
                return 4;
            }
        case 67:
            env->lr = ldtul_p(mem_buf);
            return sizeof(target_ulong);
        case 68:
            env->ctr = ldtul_p(mem_buf);
            return sizeof(target_ulong);
        case 69:
            env->xer = ldtul_p(mem_buf);
            return sizeof(target_ulong);
        case 70:
            /* fpscr */
            return 4;
        }
    }
    return 0;
}

#elif defined (TARGET_SPARC)

#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
#define NUM_CORE_REGS 86
#else
#define NUM_CORE_REGS 73
#endif

#ifdef TARGET_ABI32
#define GET_REGA(val) GET_REG32(val)
#else
#define GET_REGA(val) GET_REGL(val)
#endif

static int cpu_gdb_read_register(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 8) {
        /* g0..g7 */
        GET_REGA(env->gregs[n]);
    }
    if (n < 32) {
        /* register window */
        GET_REGA(env->regwptr[n - 8]);
    }
#if defined(TARGET_ABI32) || !defined(TARGET_SPARC64)
    if (n < 64) {
        /* fprs */
        GET_REG32(*((uint32_t *)&env->fpr[n - 32]));
    }
    /* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
    switch (n) {
    case 64: GET_REGA(env->y);
    case 65: GET_REGA(GET_PSR(env));
    case 66: GET_REGA(env->wim);
    case 67: GET_REGA(env->tbr);
    case 68: GET_REGA(env->pc);
    case 69: GET_REGA(env->npc);
    case 70: GET_REGA(env->fsr);
    case 71: GET_REGA(0); /* csr */
    case 72: GET_REGA(0);
    }
#else
    if (n < 64) {
        /* f0-f31 */
        GET_REG32(*((uint32_t *)&env->fpr[n - 32]));
    }
    if (n < 80) {
        /* f32-f62 (double width, even numbers only) */
        uint64_t val;

        val = (uint64_t)*((uint32_t *)&env->fpr[(n - 64) * 2 + 32]) << 32;
        val |= *((uint32_t *)&env->fpr[(n - 64) * 2 + 33]);
        GET_REG64(val);
    }
    switch (n) {
    case 80: GET_REGL(env->pc);
    case 81: GET_REGL(env->npc);
    case 82: GET_REGL(((uint64_t)GET_CCR(env) << 32) |
                           ((env->asi & 0xff) << 24) |
                           ((env->pstate & 0xfff) << 8) |
                           GET_CWP64(env));
    case 83: GET_REGL(env->fsr);
    case 84: GET_REGL(env->fprs);
    case 85: GET_REGL(env->y);
    }
#endif
    return 0;
}

static int cpu_gdb_write_register(CPUState *env, uint8_t *mem_buf, int n)
{
#if defined(TARGET_ABI32)
    abi_ulong tmp;

    tmp = ldl_p(mem_buf);
#else
    target_ulong tmp;

    tmp = ldtul_p(mem_buf);
#endif

    if (n < 8) {
        /* g0..g7 */
        env->gregs[n] = tmp;
    } else if (n < 32) {
        /* register window */
        env->regwptr[n - 8] = tmp;
    }
#if defined(TARGET_ABI32) || !defined(TARGET_SPARC64)
    else if (n < 64) {
        /* fprs */
        *((uint32_t *)&env->fpr[n - 32]) = tmp;
    } else {
        /* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
        switch (n) {
        case 64: env->y = tmp; break;
        case 65: PUT_PSR(env, tmp); break;
        case 66: env->wim = tmp; break;
        case 67: env->tbr = tmp; break;
        case 68: env->pc = tmp; break;
        case 69: env->npc = tmp; break;
        case 70: env->fsr = tmp; break;
        default: return 0;
        }
    }
    return 4;
#else
    else if (n < 64) {
        /* f0-f31 */
        env->fpr[n] = ldfl_p(mem_buf);
        return 4;
    } else if (n < 80) {
        /* f32-f62 (double width, even numbers only) */
        *((uint32_t *)&env->fpr[(n - 64) * 2 + 32]) = tmp >> 32;
        *((uint32_t *)&env->fpr[(n - 64) * 2 + 33]) = tmp;
    } else {
        switch (n) {
        case 80: env->pc = tmp; break;
        case 81: env->npc = tmp; break;
        case 82:
	    PUT_CCR(env, tmp >> 32);
	    env->asi = (tmp >> 24) & 0xff;
	    env->pstate = (tmp >> 8) & 0xfff;
	    PUT_CWP64(env, tmp & 0xff);
	    break;
        case 83: env->fsr = tmp; break;
        case 84: env->fprs = tmp; break;
        case 85: env->y = tmp; break;
        default: return 0;
        }
    }
    return 8;
#endif
}
#elif defined (TARGET_ARM)

/* Old gdb always expect FPA registers.  Newer (xml-aware) gdb only expect
   whatever the target description contains.  Due to a historical mishap
   the FPA registers appear in between core integer regs and the CPSR.
   We hack round this by giving the FPA regs zero size when talking to a
   newer gdb.  */
#define NUM_CORE_REGS 26
#define GDB_CORE_XML "arm-core.xml"

static int cpu_gdb_read_register(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 16) {
        /* Core integer register.  */
        GET_REG32(env->regs[n]);
    }
    if (n < 24) {
        /* FPA registers.  */
        if (gdb_has_xml)
            return 0;
        memset(mem_buf, 0, 12);
        return 12;
    }
    switch (n) {
    case 24:
        /* FPA status register.  */
        if (gdb_has_xml)
            return 0;
        GET_REG32(0);
    case 25:
        /* CPSR */
        GET_REG32(cpsr_read(env));
    }
    /* Unknown register.  */
    return 0;
}

static int cpu_gdb_write_register(CPUState *env, uint8_t *mem_buf, int n)
{
    uint32_t tmp;

    tmp = ldl_p(mem_buf);

    /* Mask out low bit of PC to workaround gdb bugs.  This will probably
       cause problems if we ever implement the Jazelle DBX extensions.  */
    if (n == 15)
        tmp &= ~1;

    if (n < 16) {
        /* Core integer register.  */
        env->regs[n] = tmp;
        return 4;
    }
    if (n < 24) { /* 16-23 */
        /* FPA registers (ignored).  */
        if (gdb_has_xml)
            return 0;
        return 12;
    }
    switch (n) {
    case 24:
        /* FPA status register (ignored).  */
        if (gdb_has_xml)
            return 0;
        return 4;
    case 25:
        /* CPSR */
        cpsr_write (env, tmp, 0xffffffff);
        return 4;
    }
    /* Unknown register.  */
    return 0;
}

#elif defined (TARGET_M68K)

#define NUM_CORE_REGS 18

#define GDB_CORE_XML "cf-core.xml"

static int cpu_gdb_read_register(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 8) {
        /* D0-D7 */
        GET_REG32(env->dregs[n]);
    } else if (n < 16) {
        /* A0-A7 */
        GET_REG32(env->aregs[n - 8]);
    } else {
	switch (n) {
        case 16: GET_REG32(env->sr);
        case 17: GET_REG32(env->pc);
        }
    }
    /* FP registers not included here because they vary between
       ColdFire and m68k.  Use XML bits for these.  */
    return 0;
}

static int cpu_gdb_write_register(CPUState *env, uint8_t *mem_buf, int n)
{
    uint32_t tmp;

    tmp = ldl_p(mem_buf);

    if (n < 8) {
        /* D0-D7 */
        env->dregs[n] = tmp;
    } else if (n < 8) {
        /* A0-A7 */
        env->aregs[n - 8] = tmp;
    } else {
        switch (n) {
        case 16: env->sr = tmp; break;
        case 17: env->pc = tmp; break;
        default: return 0;
        }
    }
    return 4;
}
#elif defined (TARGET_MIPS)

#define NUM_CORE_REGS 73

static int cpu_gdb_read_register(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
        GET_REGL(env->active_tc.gpr[n]);
    }
    if (env->CP0_Config1 & (1 << CP0C1_FP)) {
        if (n >= 38 && n < 70) {
            if (env->CP0_Status & (1 << CP0St_FR))
		GET_REGL(env->active_fpu.fpr[n - 38].d);
            else
		GET_REGL(env->active_fpu.fpr[n - 38].w[FP_ENDIAN_IDX]);
        }
        switch (n) {
        case 70: GET_REGL((int32_t)env->active_fpu.fcr31);
        case 71: GET_REGL((int32_t)env->active_fpu.fcr0);
        }
    }
    switch (n) {
    case 32: GET_REGL((int32_t)env->CP0_Status);
    case 33: GET_REGL(env->active_tc.LO[0]);
    case 34: GET_REGL(env->active_tc.HI[0]);
    case 35: GET_REGL(env->CP0_BadVAddr);
    case 36: GET_REGL((int32_t)env->CP0_Cause);
    case 37: GET_REGL(env->active_tc.PC);
    case 72: GET_REGL(0); /* fp */
    case 89: GET_REGL((int32_t)env->CP0_PRid);
    }
    if (n >= 73 && n <= 88) {
	/* 16 embedded regs.  */
	GET_REGL(0);
    }

    return 0;
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
    set_float_rounding_mode(ieee_rm[env->active_fpu.fcr31 & 3], &env->active_fpu.fp_status)

static int cpu_gdb_write_register(CPUState *env, uint8_t *mem_buf, int n)
{
    target_ulong tmp;

    tmp = ldtul_p(mem_buf);

    if (n < 32) {
        env->active_tc.gpr[n] = tmp;
        return sizeof(target_ulong);
    }
    if (env->CP0_Config1 & (1 << CP0C1_FP)
            && n >= 38 && n < 73) {
        if (n < 70) {
            if (env->CP0_Status & (1 << CP0St_FR))
              env->active_fpu.fpr[n - 38].d = tmp;
            else
              env->active_fpu.fpr[n - 38].w[FP_ENDIAN_IDX] = tmp;
        }
        switch (n) {
        case 70:
            env->active_fpu.fcr31 = tmp & 0xFF83FFFF;
            /* set rounding mode */
            RESTORE_ROUNDING_MODE;
#ifndef CONFIG_SOFTFLOAT
            /* no floating point exception for native float */
            SET_FP_ENABLE(env->active_fpu.fcr31, 0);
#endif
            break;
        case 71: env->active_fpu.fcr0 = tmp; break;
        }
        return sizeof(target_ulong);
    }
    switch (n) {
    case 32: env->CP0_Status = tmp; break;
    case 33: env->active_tc.LO[0] = tmp; break;
    case 34: env->active_tc.HI[0] = tmp; break;
    case 35: env->CP0_BadVAddr = tmp; break;
    case 36: env->CP0_Cause = tmp; break;
    case 37: env->active_tc.PC = tmp; break;
    case 72: /* fp, ignored */ break;
    default: 
	if (n > 89)
	    return 0;
	/* Other registers are readonly.  Ignore writes.  */
	break;
    }

    return sizeof(target_ulong);
}
#elif defined (TARGET_SH4)

/* Hint: Use "set architecture sh4" in GDB to see fpu registers */
/* FIXME: We should use XML for this.  */

#define NUM_CORE_REGS 59

static int cpu_gdb_read_register(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 8) {
        if ((env->sr & (SR_MD | SR_RB)) == (SR_MD | SR_RB)) {
            GET_REGL(env->gregs[n + 16]);
        } else {
            GET_REGL(env->gregs[n]);
        }
    } else if (n < 16) {
        GET_REGL(env->gregs[n - 8]);
    } else if (n >= 25 && n < 41) {
	GET_REGL(env->fregs[(n - 25) + ((env->fpscr & FPSCR_FR) ? 16 : 0)]);
    } else if (n >= 43 && n < 51) {
	GET_REGL(env->gregs[n - 43]);
    } else if (n >= 51 && n < 59) {
	GET_REGL(env->gregs[n - (51 - 16)]);
    }
    switch (n) {
    case 16: GET_REGL(env->pc);
    case 17: GET_REGL(env->pr);
    case 18: GET_REGL(env->gbr);
    case 19: GET_REGL(env->vbr);
    case 20: GET_REGL(env->mach);
    case 21: GET_REGL(env->macl);
    case 22: GET_REGL(env->sr);
    case 23: GET_REGL(env->fpul);
    case 24: GET_REGL(env->fpscr);
    case 41: GET_REGL(env->ssr);
    case 42: GET_REGL(env->spc);
    }

    return 0;
}

static int cpu_gdb_write_register(CPUState *env, uint8_t *mem_buf, int n)
{
    uint32_t tmp;

    tmp = ldl_p(mem_buf);

    if (n < 8) {
        if ((env->sr & (SR_MD | SR_RB)) == (SR_MD | SR_RB)) {
            env->gregs[n + 16] = tmp;
        } else {
            env->gregs[n] = tmp;
        }
	return 4;
    } else if (n < 16) {
        env->gregs[n - 8] = tmp;
	return 4;
    } else if (n >= 25 && n < 41) {
	env->fregs[(n - 25) + ((env->fpscr & FPSCR_FR) ? 16 : 0)] = tmp;
    } else if (n >= 43 && n < 51) {
	env->gregs[n - 43] = tmp;
	return 4;
    } else if (n >= 51 && n < 59) {
	env->gregs[n - (51 - 16)] = tmp;
	return 4;
    }
    switch (n) {
    case 16: env->pc = tmp;
    case 17: env->pr = tmp;
    case 18: env->gbr = tmp;
    case 19: env->vbr = tmp;
    case 20: env->mach = tmp;
    case 21: env->macl = tmp;
    case 22: env->sr = tmp;
    case 23: env->fpul = tmp;
    case 24: env->fpscr = tmp;
    case 41: env->ssr = tmp;
    case 42: env->spc = tmp;
    default: return 0;
    }

    return 4;
}
#elif defined (TARGET_CRIS)

#define NUM_CORE_REGS 49

static int cpu_gdb_read_register(CPUState *env, uint8_t *mem_buf, int n)
{
    uint8_t srs;

    srs = env->pregs[PR_SRS];
    if (n < 16) {
	GET_REG32(env->regs[n]);
    }

    if (n >= 21 && n < 32) {
	GET_REG32(env->pregs[n - 16]);
    }
    if (n >= 33 && n < 49) {
	GET_REG32(env->sregs[srs][n - 33]);
    }
    switch (n) {
    case 16: GET_REG8(env->pregs[0]);
    case 17: GET_REG8(env->pregs[1]);
    case 18: GET_REG32(env->pregs[2]);
    case 19: GET_REG8(srs);
    case 20: GET_REG16(env->pregs[4]);
    case 32: GET_REG32(env->pc);
    }

    return 0;
}

static int cpu_gdb_write_register(CPUState *env, uint8_t *mem_buf, int n)
{
    uint32_t tmp;

    if (n > 49)
	return 0;

    tmp = ldl_p(mem_buf);

    if (n < 16) {
	env->regs[n] = tmp;
    }

    if (n >= 21 && n < 32) {
	env->pregs[n - 16] = tmp;
    }

    /* FIXME: Should support function regs be writable?  */
    switch (n) {
    case 16: return 1;
    case 17: return 1;
    case 18: env->pregs[PR_PID] = tmp; break;
    case 19: return 1;
    case 20: return 2;
    case 32: env->pc = tmp; break;
    }

    return 4;
}
#else

#define NUM_CORE_REGS 0

static int cpu_gdb_read_register(CPUState *env, uint8_t *mem_buf, int n)
{
    return 0;
}

static int cpu_gdb_write_register(CPUState *env, uint8_t *mem_buf, int n)
{
    return 0;
}

#endif

static int num_g_regs = NUM_CORE_REGS;

#ifdef GDB_CORE_XML
/* Encode data using the encoding for 'x' packets.  */
static int memtox(char *buf, const char *mem, int len)
{
    char *p = buf;
    char c;

    while (len--) {
        c = *(mem++);
        switch (c) {
        case '#': case '$': case '*': case '}':
            *(p++) = '}';
            *(p++) = c ^ 0x20;
            break;
        default:
            *(p++) = c;
            break;
        }
    }
    return p - buf;
}

const char *get_feature_xml(CPUState *env, const char *p, const char **newp)
{
    extern const char *const xml_builtin[][2];
    size_t len;
    int i;
    const char *name;
    static char target_xml[1024];

    len = 0;
    while (p[len] && p[len] != ':')
        len++;
    *newp = p + len;

    name = NULL;
    if (strncmp(p, "target.xml", len) == 0) {
        /* Generate the XML description for this CPU.  */
        if (!target_xml[0]) {
            GDBRegisterState *r;

            snprintf(target_xml, sizeof(target_xml),
                     "<?xml version=\"1.0\"?>"
                     "<!DOCTYPE target SYSTEM \"gdb-target.dtd\">"
                     "<target>"
                     "<xi:include href=\"%s\"/>",
                     GDB_CORE_XML);

            for (r = env->gdb_regs; r; r = r->next) {
                strcat(target_xml, "<xi:include href=\"");
                strcat(target_xml, r->xml);
                strcat(target_xml, "\"/>");
            }
            strcat(target_xml, "</target>");
        }
        return target_xml;
    }
    for (i = 0; ; i++) {
        name = xml_builtin[i][0];
        if (!name || (strncmp(name, p, len) == 0 && strlen(name) == len))
            break;
    }
    return name ? xml_builtin[i][1] : NULL;
}
#endif

static int gdb_read_register(CPUState *env, uint8_t *mem_buf, int reg)
{
    GDBRegisterState *r;

    if (reg < NUM_CORE_REGS)
        return cpu_gdb_read_register(env, mem_buf, reg);

    for (r = env->gdb_regs; r; r = r->next) {
        if (r->base_reg <= reg && reg < r->base_reg + r->num_regs) {
            return r->get_reg(env, mem_buf, reg - r->base_reg);
        }
    }
    return 0;
}

static int gdb_write_register(CPUState *env, uint8_t *mem_buf, int reg)
{
    GDBRegisterState *r;

    if (reg < NUM_CORE_REGS)
        return cpu_gdb_write_register(env, mem_buf, reg);

    for (r = env->gdb_regs; r; r = r->next) {
        if (r->base_reg <= reg && reg < r->base_reg + r->num_regs) {
            return r->set_reg(env, mem_buf, reg - r->base_reg);
        }
    }
    return 0;
}

/* Register a supplemental set of CPU registers.  If g_pos is nonzero it
   specifies the first register number and these registers are included in
   a standard "g" packet.  Direction is relative to gdb, i.e. get_reg is
   gdb reading a CPU register, and set_reg is gdb modifying a CPU register.
 */

void gdb_register_coprocessor(CPUState * env,
                             gdb_reg_cb get_reg, gdb_reg_cb set_reg,
                             int num_regs, const char *xml, int g_pos)
{
    GDBRegisterState *s;
    GDBRegisterState **p;
    static int last_reg = NUM_CORE_REGS;

    s = (GDBRegisterState *)qemu_mallocz(sizeof(GDBRegisterState));
    s->base_reg = last_reg;
    s->num_regs = num_regs;
    s->get_reg = get_reg;
    s->set_reg = set_reg;
    s->xml = xml;
    p = &env->gdb_regs;
    while (*p) {
        /* Check for duplicates.  */
        if (strcmp((*p)->xml, xml) == 0)
            return;
        p = &(*p)->next;
    }
    /* Add to end of list.  */
    last_reg += num_regs;
    *p = s;
    if (g_pos) {
        if (g_pos != s->base_reg) {
            fprintf(stderr, "Error: Bad gdb register numbering for '%s'\n"
                    "Expected %d got %d\n", xml, g_pos, s->base_reg);
        } else {
            num_g_regs = last_reg;
        }
    }
}

/* GDB breakpoint/watchpoint types */
#define GDB_BREAKPOINT_SW        0
#define GDB_BREAKPOINT_HW        1
#define GDB_WATCHPOINT_WRITE     2
#define GDB_WATCHPOINT_READ      3
#define GDB_WATCHPOINT_ACCESS    4

#ifndef CONFIG_USER_ONLY
static const int xlat_gdb_type[] = {
    [GDB_WATCHPOINT_WRITE]  = BP_GDB | BP_MEM_WRITE,
    [GDB_WATCHPOINT_READ]   = BP_GDB | BP_MEM_READ,
    [GDB_WATCHPOINT_ACCESS] = BP_GDB | BP_MEM_ACCESS,
};
#endif

static int gdb_breakpoint_insert(CPUState *env, target_ulong addr,
                                 target_ulong len, int type)
{
    switch (type) {
    case GDB_BREAKPOINT_SW:
    case GDB_BREAKPOINT_HW:
        return cpu_breakpoint_insert(env, addr, BP_GDB, NULL);
#ifndef CONFIG_USER_ONLY
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_ACCESS:
        return cpu_watchpoint_insert(env, addr, len, xlat_gdb_type[type],
                                     NULL);
#endif
    default:
        return -ENOSYS;
    }
}

static int gdb_breakpoint_remove(CPUState *env, target_ulong addr,
                                 target_ulong len, int type)
{
    switch (type) {
    case GDB_BREAKPOINT_SW:
    case GDB_BREAKPOINT_HW:
        return cpu_breakpoint_remove(env, addr, BP_GDB);
#ifndef CONFIG_USER_ONLY
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_ACCESS:
        return cpu_watchpoint_remove(env, addr, len, xlat_gdb_type[type]);
#endif
    default:
        return -ENOSYS;
    }
}

static void gdb_breakpoint_remove_all(CPUState *env)
{
    cpu_breakpoint_remove_all(env, BP_GDB);
#ifndef CONFIG_USER_ONLY
    cpu_watchpoint_remove_all(env, BP_GDB);
#endif
}

static int gdb_handle_packet(GDBState *s, CPUState *env, const char *line_buf)
{
    const char *p;
    int ch, reg_size, type, res;
    char buf[MAX_PACKET_LENGTH];
    uint8_t mem_buf[MAX_PACKET_LENGTH];
    uint8_t *registers;
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
        gdb_breakpoint_remove_all(env);
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
        gdb_breakpoint_remove_all(env);
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
        len = 0;
        for (addr = 0; addr < num_g_regs; addr++) {
            reg_size = gdb_read_register(env, mem_buf + len, addr);
            len += reg_size;
        }
        memtohex(buf, mem_buf, len);
        put_packet(s, buf);
        break;
    case 'G':
        registers = mem_buf;
        len = strlen(p) / 2;
        hextomem((uint8_t *)registers, p, len);
        for (addr = 0; addr < num_g_regs && len > 0; addr++) {
            reg_size = gdb_write_register(env, registers, addr);
            len -= reg_size;
            registers += reg_size;
        }
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
    case 'p':
        /* Older gdb are really dumb, and don't use 'g' if 'p' is avaialable.
           This works, but can be very slow.  Anything new enough to
           understand XML also knows how to use this properly.  */
        if (!gdb_has_xml)
            goto unknown_command;
        addr = strtoull(p, (char **)&p, 16);
        reg_size = gdb_read_register(env, mem_buf, addr);
        if (reg_size) {
            memtohex(buf, mem_buf, reg_size);
            put_packet(s, buf);
        } else {
            put_packet(s, "E14");
        }
        break;
    case 'P':
        if (!gdb_has_xml)
            goto unknown_command;
        addr = strtoull(p, (char **)&p, 16);
        if (*p == '=')
            p++;
        reg_size = strlen(p) / 2;
        hextomem(mem_buf, p, reg_size);
        gdb_write_register(env, mem_buf, addr);
        put_packet(s, "OK");
        break;
    case 'Z':
    case 'z':
        type = strtoul(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        addr = strtoull(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        len = strtoull(p, (char **)&p, 16);
        if (ch == 'Z')
            res = gdb_breakpoint_insert(env, addr, len, type);
        else
            res = gdb_breakpoint_remove(env, addr, len, type);
        if (res >= 0)
             put_packet(s, "OK");
        else if (res == -ENOSYS)
            put_packet(s, "");
        else
            put_packet(s, "E22");
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
        if (strncmp(p, "Supported", 9) == 0) {
            snprintf(buf, sizeof(buf), "PacketSize=%x", MAX_PACKET_LENGTH);
#ifdef GDB_CORE_XML
            strcat(buf, ";qXfer:features:read+");
#endif
            put_packet(s, buf);
            break;
        }
#ifdef GDB_CORE_XML
        if (strncmp(p, "Xfer:features:read:", 19) == 0) {
            const char *xml;
            target_ulong total_len;

            gdb_has_xml = 1;
            p += 19;
            xml = get_feature_xml(env, p, &p);
            if (!xml) {
                snprintf(buf, sizeof(buf), "E00");
                put_packet(s, buf);
                break;
            }

            if (*p == ':')
                p++;
            addr = strtoul(p, (char **)&p, 16);
            if (*p == ',')
                p++;
            len = strtoul(p, (char **)&p, 16);

            total_len = strlen(xml);
            if (addr > total_len) {
                snprintf(buf, sizeof(buf), "E00");
                put_packet(s, buf);
                break;
            }
            if (len > (MAX_PACKET_LENGTH - 5) / 2)
                len = (MAX_PACKET_LENGTH - 5) / 2;
            if (len < total_len - addr) {
                buf[0] = 'm';
                len = memtox(buf + 1, xml + addr, len);
            } else {
                buf[0] = 'l';
                len = memtox(buf + 1, xml + addr, total_len - addr);
            }
            put_packet_binary(s, buf, len + 1);
            break;
        }
#endif
        /* Unrecognised 'q' command.  */
        goto unknown_command;

    default:
    unknown_command:
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
    const char *type;
    int ret;

    if (s->state == RS_SYSCALL)
        return;

    /* disable single step if it was enable */
    cpu_single_step(s->env, 0);

    if (reason == EXCP_DEBUG) {
        if (s->env->watchpoint_hit) {
            switch (s->env->watchpoint_hit->flags & BP_MEM_ACCESS) {
            case BP_MEM_READ:
                type = "r";
                break;
            case BP_MEM_ACCESS:
                type = "a";
                break;
            default:
                type = "";
                break;
            }
            snprintf(buf, sizeof(buf), "T%02x%swatch:" TARGET_FMT_lx ";",
                     SIGTRAP, type, s->env->watchpoint_hit->vaddr);
            put_packet(s, buf);
            s->env->watchpoint_hit = NULL;
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
    gdb_has_xml = 0;

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
  /* We can handle an arbitrarily large amount of data.
   Pick the maximum packet size, which is as good as anything.  */
  return MAX_PACKET_LENGTH;
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
        gdb_has_xml = 0;
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

    chr = qemu_chr_open("gdb", port);
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
