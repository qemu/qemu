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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
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
#include "monitor.h"
#include "qemu-char.h"
#include "sysemu.h"
#include "gdbstub.h"
#endif

#define MAX_PACKET_LENGTH 4096

#include "qemu_socket.h"
#include "kvm.h"


enum {
    GDB_SIGNAL_0 = 0,
    GDB_SIGNAL_INT = 2,
    GDB_SIGNAL_TRAP = 5,
    GDB_SIGNAL_UNKNOWN = 143
};

#ifdef CONFIG_USER_ONLY

/* Map target signal numbers to GDB protocol signal numbers and vice
 * versa.  For user emulation's currently supported systems, we can
 * assume most signals are defined.
 */

static int gdb_signal_table[] = {
    0,
    TARGET_SIGHUP,
    TARGET_SIGINT,
    TARGET_SIGQUIT,
    TARGET_SIGILL,
    TARGET_SIGTRAP,
    TARGET_SIGABRT,
    -1, /* SIGEMT */
    TARGET_SIGFPE,
    TARGET_SIGKILL,
    TARGET_SIGBUS,
    TARGET_SIGSEGV,
    TARGET_SIGSYS,
    TARGET_SIGPIPE,
    TARGET_SIGALRM,
    TARGET_SIGTERM,
    TARGET_SIGURG,
    TARGET_SIGSTOP,
    TARGET_SIGTSTP,
    TARGET_SIGCONT,
    TARGET_SIGCHLD,
    TARGET_SIGTTIN,
    TARGET_SIGTTOU,
    TARGET_SIGIO,
    TARGET_SIGXCPU,
    TARGET_SIGXFSZ,
    TARGET_SIGVTALRM,
    TARGET_SIGPROF,
    TARGET_SIGWINCH,
    -1, /* SIGLOST */
    TARGET_SIGUSR1,
    TARGET_SIGUSR2,
#ifdef TARGET_SIGPWR
    TARGET_SIGPWR,
#else
    -1,
#endif
    -1, /* SIGPOLL */
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
    -1,
#ifdef __SIGRTMIN
    __SIGRTMIN + 1,
    __SIGRTMIN + 2,
    __SIGRTMIN + 3,
    __SIGRTMIN + 4,
    __SIGRTMIN + 5,
    __SIGRTMIN + 6,
    __SIGRTMIN + 7,
    __SIGRTMIN + 8,
    __SIGRTMIN + 9,
    __SIGRTMIN + 10,
    __SIGRTMIN + 11,
    __SIGRTMIN + 12,
    __SIGRTMIN + 13,
    __SIGRTMIN + 14,
    __SIGRTMIN + 15,
    __SIGRTMIN + 16,
    __SIGRTMIN + 17,
    __SIGRTMIN + 18,
    __SIGRTMIN + 19,
    __SIGRTMIN + 20,
    __SIGRTMIN + 21,
    __SIGRTMIN + 22,
    __SIGRTMIN + 23,
    __SIGRTMIN + 24,
    __SIGRTMIN + 25,
    __SIGRTMIN + 26,
    __SIGRTMIN + 27,
    __SIGRTMIN + 28,
    __SIGRTMIN + 29,
    __SIGRTMIN + 30,
    __SIGRTMIN + 31,
    -1, /* SIGCANCEL */
    __SIGRTMIN,
    __SIGRTMIN + 32,
    __SIGRTMIN + 33,
    __SIGRTMIN + 34,
    __SIGRTMIN + 35,
    __SIGRTMIN + 36,
    __SIGRTMIN + 37,
    __SIGRTMIN + 38,
    __SIGRTMIN + 39,
    __SIGRTMIN + 40,
    __SIGRTMIN + 41,
    __SIGRTMIN + 42,
    __SIGRTMIN + 43,
    __SIGRTMIN + 44,
    __SIGRTMIN + 45,
    __SIGRTMIN + 46,
    __SIGRTMIN + 47,
    __SIGRTMIN + 48,
    __SIGRTMIN + 49,
    __SIGRTMIN + 50,
    __SIGRTMIN + 51,
    __SIGRTMIN + 52,
    __SIGRTMIN + 53,
    __SIGRTMIN + 54,
    __SIGRTMIN + 55,
    __SIGRTMIN + 56,
    __SIGRTMIN + 57,
    __SIGRTMIN + 58,
    __SIGRTMIN + 59,
    __SIGRTMIN + 60,
    __SIGRTMIN + 61,
    __SIGRTMIN + 62,
    __SIGRTMIN + 63,
    __SIGRTMIN + 64,
    __SIGRTMIN + 65,
    __SIGRTMIN + 66,
    __SIGRTMIN + 67,
    __SIGRTMIN + 68,
    __SIGRTMIN + 69,
    __SIGRTMIN + 70,
    __SIGRTMIN + 71,
    __SIGRTMIN + 72,
    __SIGRTMIN + 73,
    __SIGRTMIN + 74,
    __SIGRTMIN + 75,
    __SIGRTMIN + 76,
    __SIGRTMIN + 77,
    __SIGRTMIN + 78,
    __SIGRTMIN + 79,
    __SIGRTMIN + 80,
    __SIGRTMIN + 81,
    __SIGRTMIN + 82,
    __SIGRTMIN + 83,
    __SIGRTMIN + 84,
    __SIGRTMIN + 85,
    __SIGRTMIN + 86,
    __SIGRTMIN + 87,
    __SIGRTMIN + 88,
    __SIGRTMIN + 89,
    __SIGRTMIN + 90,
    __SIGRTMIN + 91,
    __SIGRTMIN + 92,
    __SIGRTMIN + 93,
    __SIGRTMIN + 94,
    __SIGRTMIN + 95,
    -1, /* SIGINFO */
    -1, /* UNKNOWN */
    -1, /* DEFAULT */
    -1,
    -1,
    -1,
    -1,
    -1,
    -1
#endif
};
#else
/* In system mode we only need SIGINT and SIGTRAP; other signals
   are not yet supported.  */

enum {
    TARGET_SIGINT = 2,
    TARGET_SIGTRAP = 5
};

static int gdb_signal_table[] = {
    -1,
    -1,
    TARGET_SIGINT,
    -1,
    -1,
    TARGET_SIGTRAP
};
#endif

#ifdef CONFIG_USER_ONLY
static int target_signal_to_gdb (int sig)
{
    int i;
    for (i = 0; i < ARRAY_SIZE (gdb_signal_table); i++)
        if (gdb_signal_table[i] == sig)
            return i;
    return GDB_SIGNAL_UNKNOWN;
}
#endif

static int gdb_signal_to_target (int sig)
{
    if (sig < ARRAY_SIZE (gdb_signal_table))
        return gdb_signal_table[sig];
    else
        return -1;
}

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
    RS_INACTIVE,
    RS_IDLE,
    RS_GETLINE,
    RS_CHKSUM1,
    RS_CHKSUM2,
    RS_SYSCALL,
};
typedef struct GDBState {
    CPUState *c_cpu; /* current CPU for step/continue ops */
    CPUState *g_cpu; /* current CPU for other ops */
    CPUState *query_cpu; /* for q{f|s}ThreadInfo */
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
    CharDriverState *mon_chr;
#endif
} GDBState;

/* By default use no IRQs and no timers while single stepping so as to
 * make single stepping like an ICE HW step.
 */
static int sstep_flags = SSTEP_ENABLE|SSTEP_NOIRQ|SSTEP_NOTIMER;

static GDBState *gdbserver_state;

/* This is an ugly hack to cope with both new and old gdb.
   If gdb sends qXfer:features:read then assume we're talking to a newish
   gdb that understands target descriptions.  */
static int gdb_has_xml;

#ifdef CONFIG_USER_ONLY
/* XXX: This is not thread safe.  Do we care?  */
static int gdbserver_fd = -1;

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

static gdb_syscall_complete_cb gdb_current_syscall_cb;

static enum {
    GDB_SYS_UNKNOWN,
    GDB_SYS_ENABLED,
    GDB_SYS_DISABLED,
} gdb_syscall_mode;

/* If gdb is connected when the first semihosting syscall occurs then use
   remote gdb syscalls.  Otherwise use native file IO.  */
int use_gdb_syscalls(void)
{
    if (gdb_syscall_mode == GDB_SYS_UNKNOWN) {
        gdb_syscall_mode = (gdbserver_state ? GDB_SYS_ENABLED
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

/* Old gdb always expects FP registers.  Newer (xml-aware) gdb only
   expects whatever the target description contains.  Due to a
   historical mishap the FP registers appear in between core integer
   regs and PC, MSR, CR, and so forth.  We hack round this by giving the
   FP regs zero size when talking to a newer gdb.  */
#define NUM_CORE_REGS 71
#if defined (TARGET_PPC64)
#define GDB_CORE_XML "power64-core.xml"
#else
#define GDB_CORE_XML "power-core.xml"
#endif

static int cpu_gdb_read_register(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
        /* gprs */
        GET_REGL(env->gpr[n]);
    } else if (n < 64) {
        /* fprs */
        if (gdb_has_xml)
            return 0;
        stfq_p(mem_buf, env->fpr[n-32]);
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
        case 70:
            {
                if (gdb_has_xml)
                    return 0;
                GET_REG32(0); /* fpscr */
            }
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
        if (gdb_has_xml)
            return 0;
        env->fpr[n-32] = ldfq_p(mem_buf);
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
            if (gdb_has_xml)
                return 0;
            return 4;
        }
    }
    return 0;
}

#elif defined (TARGET_SPARC)

#if defined(TARGET_SPARC64) && !defined(TARGET_ABI32)
#define NUM_CORE_REGS 86
#else
#define NUM_CORE_REGS 72
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
    default: GET_REGA(0);
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
#elif defined (TARGET_MICROBLAZE)

#define NUM_CORE_REGS (32 + 5)

static int cpu_gdb_read_register(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 32) {
	GET_REG32(env->regs[n]);
    } else {
	GET_REG32(env->sregs[n - 32]);
    }
    return 0;
}

static int cpu_gdb_write_register(CPUState *env, uint8_t *mem_buf, int n)
{
    uint32_t tmp;

    if (n > NUM_CORE_REGS)
	return 0;

    tmp = ldl_p(mem_buf);

    if (n < 32) {
	env->regs[n] = tmp;
    } else {
	env->sregs[n - 32] = tmp;
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
#elif defined (TARGET_ALPHA)

#define NUM_CORE_REGS 65

static int cpu_gdb_read_register(CPUState *env, uint8_t *mem_buf, int n)
{
    if (n < 31) {
       GET_REGL(env->ir[n]);
    }
    else if (n == 31) {
       GET_REGL(0);
    }
    else if (n<63) {
       uint64_t val;

       val=*((uint64_t *)&env->fir[n-32]);
       GET_REGL(val);
    }
    else if (n==63) {
       GET_REGL(env->fpcr);
    }
    else if (n==64) {
       GET_REGL(env->pc);
    }
    else {
       GET_REGL(0);
    }

    return 0;
}

static int cpu_gdb_write_register(CPUState *env, uint8_t *mem_buf, int n)
{
    target_ulong tmp;
    tmp = ldtul_p(mem_buf);

    if (n < 31) {
        env->ir[n] = tmp;
    }

    if (n > 31 && n < 63) {
        env->fir[n - 32] = ldfl_p(mem_buf);
    }

    if (n == 64 ) {
       env->pc=tmp;
    }

    return 8;
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

static const char *get_feature_xml(const char *p, const char **newp)
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

            for (r = first_cpu->gdb_regs; r; r = r->next) {
                pstrcat(target_xml, sizeof(target_xml), "<xi:include href=\"");
                pstrcat(target_xml, sizeof(target_xml), r->xml);
                pstrcat(target_xml, sizeof(target_xml), "\"/>");
            }
            pstrcat(target_xml, sizeof(target_xml), "</target>");
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

#ifndef CONFIG_USER_ONLY
static const int xlat_gdb_type[] = {
    [GDB_WATCHPOINT_WRITE]  = BP_GDB | BP_MEM_WRITE,
    [GDB_WATCHPOINT_READ]   = BP_GDB | BP_MEM_READ,
    [GDB_WATCHPOINT_ACCESS] = BP_GDB | BP_MEM_ACCESS,
};
#endif

static int gdb_breakpoint_insert(target_ulong addr, target_ulong len, int type)
{
    CPUState *env;
    int err = 0;

    if (kvm_enabled())
        return kvm_insert_breakpoint(gdbserver_state->c_cpu, addr, len, type);

    switch (type) {
    case GDB_BREAKPOINT_SW:
    case GDB_BREAKPOINT_HW:
        for (env = first_cpu; env != NULL; env = env->next_cpu) {
            err = cpu_breakpoint_insert(env, addr, BP_GDB, NULL);
            if (err)
                break;
        }
        return err;
#ifndef CONFIG_USER_ONLY
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_ACCESS:
        for (env = first_cpu; env != NULL; env = env->next_cpu) {
            err = cpu_watchpoint_insert(env, addr, len, xlat_gdb_type[type],
                                        NULL);
            if (err)
                break;
        }
        return err;
#endif
    default:
        return -ENOSYS;
    }
}

static int gdb_breakpoint_remove(target_ulong addr, target_ulong len, int type)
{
    CPUState *env;
    int err = 0;

    if (kvm_enabled())
        return kvm_remove_breakpoint(gdbserver_state->c_cpu, addr, len, type);

    switch (type) {
    case GDB_BREAKPOINT_SW:
    case GDB_BREAKPOINT_HW:
        for (env = first_cpu; env != NULL; env = env->next_cpu) {
            err = cpu_breakpoint_remove(env, addr, BP_GDB);
            if (err)
                break;
        }
        return err;
#ifndef CONFIG_USER_ONLY
    case GDB_WATCHPOINT_WRITE:
    case GDB_WATCHPOINT_READ:
    case GDB_WATCHPOINT_ACCESS:
        for (env = first_cpu; env != NULL; env = env->next_cpu) {
            err = cpu_watchpoint_remove(env, addr, len, xlat_gdb_type[type]);
            if (err)
                break;
        }
        return err;
#endif
    default:
        return -ENOSYS;
    }
}

static void gdb_breakpoint_remove_all(void)
{
    CPUState *env;

    if (kvm_enabled()) {
        kvm_remove_all_breakpoints(gdbserver_state->c_cpu);
        return;
    }

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        cpu_breakpoint_remove_all(env, BP_GDB);
#ifndef CONFIG_USER_ONLY
        cpu_watchpoint_remove_all(env, BP_GDB);
#endif
    }
}

static void gdb_set_cpu_pc(GDBState *s, target_ulong pc)
{
#if defined(TARGET_I386)
    s->c_cpu->eip = pc;
    cpu_synchronize_state(s->c_cpu, 1);
#elif defined (TARGET_PPC)
    s->c_cpu->nip = pc;
#elif defined (TARGET_SPARC)
    s->c_cpu->pc = pc;
    s->c_cpu->npc = pc + 4;
#elif defined (TARGET_ARM)
    s->c_cpu->regs[15] = pc;
#elif defined (TARGET_SH4)
    s->c_cpu->pc = pc;
#elif defined (TARGET_MIPS)
    s->c_cpu->active_tc.PC = pc;
#elif defined (TARGET_MICROBLAZE)
    s->c_cpu->sregs[SR_PC] = pc;
#elif defined (TARGET_CRIS)
    s->c_cpu->pc = pc;
#elif defined (TARGET_ALPHA)
    s->c_cpu->pc = pc;
#endif
}

static inline int gdb_id(CPUState *env)
{
#if defined(CONFIG_USER_ONLY) && defined(USE_NPTL)
    return env->host_tid;
#else
    return env->cpu_index + 1;
#endif
}

static CPUState *find_cpu(uint32_t thread_id)
{
    CPUState *env;

    for (env = first_cpu; env != NULL; env = env->next_cpu) {
        if (gdb_id(env) == thread_id) {
            return env;
        }
    }

    return NULL;
}

static int gdb_handle_packet(GDBState *s, const char *line_buf)
{
    CPUState *env;
    const char *p;
    uint32_t thread;
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
        snprintf(buf, sizeof(buf), "T%02xthread:%02x;", GDB_SIGNAL_TRAP,
                 gdb_id(s->c_cpu));
        put_packet(s, buf);
        /* Remove all the breakpoints when this query is issued,
         * because gdb is doing and initial connect and the state
         * should be cleaned up.
         */
        gdb_breakpoint_remove_all();
        break;
    case 'c':
        if (*p != '\0') {
            addr = strtoull(p, (char **)&p, 16);
            gdb_set_cpu_pc(s, addr);
        }
        s->signal = 0;
        gdb_continue(s);
	return RS_IDLE;
    case 'C':
        s->signal = gdb_signal_to_target (strtoul(p, (char **)&p, 16));
        if (s->signal == -1)
            s->signal = 0;
        gdb_continue(s);
        return RS_IDLE;
    case 'k':
        /* Kill the target */
        fprintf(stderr, "\nQEMU: Terminated via GDBstub\n");
        exit(0);
    case 'D':
        /* Detach packet */
        gdb_breakpoint_remove_all();
        gdb_continue(s);
        put_packet(s, "OK");
        break;
    case 's':
        if (*p != '\0') {
            addr = strtoull(p, (char **)&p, 16);
            gdb_set_cpu_pc(s, addr);
        }
        cpu_single_step(s->c_cpu, sstep_flags);
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
                gdb_current_syscall_cb(s->c_cpu, ret, err);
            if (type == 'C') {
                put_packet(s, "T02");
            } else {
                gdb_continue(s);
            }
        }
        break;
    case 'g':
        cpu_synchronize_state(s->g_cpu, 0);
        len = 0;
        for (addr = 0; addr < num_g_regs; addr++) {
            reg_size = gdb_read_register(s->g_cpu, mem_buf + len, addr);
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
            reg_size = gdb_write_register(s->g_cpu, registers, addr);
            len -= reg_size;
            registers += reg_size;
        }
        cpu_synchronize_state(s->g_cpu, 1);
        put_packet(s, "OK");
        break;
    case 'm':
        addr = strtoull(p, (char **)&p, 16);
        if (*p == ',')
            p++;
        len = strtoull(p, NULL, 16);
        if (cpu_memory_rw_debug(s->g_cpu, addr, mem_buf, len, 0) != 0) {
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
        if (cpu_memory_rw_debug(s->g_cpu, addr, mem_buf, len, 1) != 0)
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
        reg_size = gdb_read_register(s->g_cpu, mem_buf, addr);
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
        gdb_write_register(s->g_cpu, mem_buf, addr);
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
            res = gdb_breakpoint_insert(addr, len, type);
        else
            res = gdb_breakpoint_remove(addr, len, type);
        if (res >= 0)
             put_packet(s, "OK");
        else if (res == -ENOSYS)
            put_packet(s, "");
        else
            put_packet(s, "E22");
        break;
    case 'H':
        type = *p++;
        thread = strtoull(p, (char **)&p, 16);
        if (thread == -1 || thread == 0) {
            put_packet(s, "OK");
            break;
        }
        env = find_cpu(thread);
        if (env == NULL) {
            put_packet(s, "E22");
            break;
        }
        switch (type) {
        case 'c':
            s->c_cpu = env;
            put_packet(s, "OK");
            break;
        case 'g':
            s->g_cpu = env;
            put_packet(s, "OK");
            break;
        default:
             put_packet(s, "E22");
             break;
        }
        break;
    case 'T':
        thread = strtoull(p, (char **)&p, 16);
        env = find_cpu(thread);

        if (env != NULL) {
            put_packet(s, "OK");
        } else {
            put_packet(s, "E22");
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
        } else if (strcmp(p,"C") == 0) {
            /* "Current thread" remains vague in the spec, so always return
             *  the first CPU (gdb returns the first thread). */
            put_packet(s, "QC1");
            break;
        } else if (strcmp(p,"fThreadInfo") == 0) {
            s->query_cpu = first_cpu;
            goto report_cpuinfo;
        } else if (strcmp(p,"sThreadInfo") == 0) {
        report_cpuinfo:
            if (s->query_cpu) {
                snprintf(buf, sizeof(buf), "m%x", gdb_id(s->query_cpu));
                put_packet(s, buf);
                s->query_cpu = s->query_cpu->next_cpu;
            } else
                put_packet(s, "l");
            break;
        } else if (strncmp(p,"ThreadExtraInfo,", 16) == 0) {
            thread = strtoull(p+16, (char **)&p, 16);
            env = find_cpu(thread);
            if (env != NULL) {
                cpu_synchronize_state(env, 0);
                len = snprintf((char *)mem_buf, sizeof(mem_buf),
                               "CPU#%d [%s]", env->cpu_index,
                               env->halted ? "halted " : "running");
                memtohex(buf, mem_buf, len);
                put_packet(s, buf);
            }
            break;
        }
#ifdef CONFIG_USER_ONLY
        else if (strncmp(p, "Offsets", 7) == 0) {
            TaskState *ts = s->c_cpu->opaque;

            snprintf(buf, sizeof(buf),
                     "Text=" TARGET_ABI_FMT_lx ";Data=" TARGET_ABI_FMT_lx
                     ";Bss=" TARGET_ABI_FMT_lx,
                     ts->info->code_offset,
                     ts->info->data_offset,
                     ts->info->data_offset);
            put_packet(s, buf);
            break;
        }
#else /* !CONFIG_USER_ONLY */
        else if (strncmp(p, "Rcmd,", 5) == 0) {
            int len = strlen(p + 5);

            if ((len % 2) != 0) {
                put_packet(s, "E01");
                break;
            }
            hextomem(mem_buf, p + 5, len);
            len = len / 2;
            mem_buf[len++] = 0;
            qemu_chr_read(s->mon_chr, mem_buf, len);
            put_packet(s, "OK");
            break;
        }
#endif /* !CONFIG_USER_ONLY */
        if (strncmp(p, "Supported", 9) == 0) {
            snprintf(buf, sizeof(buf), "PacketSize=%x", MAX_PACKET_LENGTH);
#ifdef GDB_CORE_XML
            pstrcat(buf, sizeof(buf), ";qXfer:features:read+");
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
            xml = get_feature_xml(p, &p);
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

void gdb_set_stop_cpu(CPUState *env)
{
    gdbserver_state->c_cpu = env;
    gdbserver_state->g_cpu = env;
}

#ifndef CONFIG_USER_ONLY
static void gdb_vm_state_change(void *opaque, int running, int reason)
{
    GDBState *s = gdbserver_state;
    CPUState *env = s->c_cpu;
    char buf[256];
    const char *type;
    int ret;

    if (running || (reason != EXCP_DEBUG && reason != EXCP_INTERRUPT) ||
        s->state == RS_INACTIVE || s->state == RS_SYSCALL)
        return;

    /* disable single step if it was enable */
    cpu_single_step(env, 0);

    if (reason == EXCP_DEBUG) {
        if (env->watchpoint_hit) {
            switch (env->watchpoint_hit->flags & BP_MEM_ACCESS) {
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
            snprintf(buf, sizeof(buf),
                     "T%02xthread:%02x;%swatch:" TARGET_FMT_lx ";",
                     GDB_SIGNAL_TRAP, gdb_id(env), type,
                     env->watchpoint_hit->vaddr);
            put_packet(s, buf);
            env->watchpoint_hit = NULL;
            return;
        }
	tb_flush(env);
        ret = GDB_SIGNAL_TRAP;
    } else {
        ret = GDB_SIGNAL_INT;
    }
    snprintf(buf, sizeof(buf), "T%02xthread:%02x;", ret, gdb_id(env));
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

    s = gdbserver_state;
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
    gdb_handlesig(s->c_cpu, 0);
#else
    cpu_exit(s->c_cpu);
#endif
}

static void gdb_read_byte(GDBState *s, int ch)
{
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
                s->state = gdb_handle_packet(s, s->line_buf);
            }
            break;
        default:
            abort();
        }
    }
}

#ifdef CONFIG_USER_ONLY
int
gdb_queuesig (void)
{
    GDBState *s;

    s = gdbserver_state;

    if (gdbserver_fd < 0 || s->fd < 0)
        return 0;
    else
        return 1;
}

int
gdb_handlesig (CPUState *env, int sig)
{
  GDBState *s;
  char buf[256];
  int n;

  s = gdbserver_state;
  if (gdbserver_fd < 0 || s->fd < 0)
    return sig;

  /* disable single step if it was enabled */
  cpu_single_step(env, 0);
  tb_flush(env);

  if (sig != 0)
    {
      snprintf(buf, sizeof(buf), "S%02x", target_signal_to_gdb (sig));
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

  s = gdbserver_state;
  if (gdbserver_fd < 0 || s->fd < 0)
    return;

  snprintf(buf, sizeof(buf), "W%02x", code);
  put_packet(s, buf);
}

/* Tell the remote gdb that the process has exited due to SIG.  */
void gdb_signalled(CPUState *env, int sig)
{
  GDBState *s;
  char buf[4];

  s = gdbserver_state;
  if (gdbserver_fd < 0 || s->fd < 0)
    return;

  snprintf(buf, sizeof(buf), "X%02x", target_signal_to_gdb (sig));
  put_packet(s, buf);
}

static void gdb_accept(void)
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

    s = qemu_mallocz(sizeof(GDBState));
    s->c_cpu = first_cpu;
    s->g_cpu = first_cpu;
    s->fd = fd;
    gdb_has_xml = 0;

    gdbserver_state = s;

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
    gdb_accept();
    return 0;
}

/* Disable gdb stub for child processes.  */
void gdbserver_fork(CPUState *env)
{
    GDBState *s = gdbserver_state;
    if (gdbserver_fd < 0 || s->fd < 0)
      return;
    close(s->fd);
    s->fd = -1;
    cpu_breakpoint_remove_all(env, BP_GDB);
    cpu_watchpoint_remove_all(env, BP_GDB);
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
    int i;

    for (i = 0; i < size; i++) {
        gdb_read_byte(gdbserver_state, buf[i]);
    }
}

static void gdb_chr_event(void *opaque, int event)
{
    switch (event) {
    case CHR_EVENT_RESET:
        vm_stop(EXCP_INTERRUPT);
        gdb_has_xml = 0;
        break;
    default:
        break;
    }
}

static void gdb_monitor_output(GDBState *s, const char *msg, int len)
{
    char buf[MAX_PACKET_LENGTH];

    buf[0] = 'O';
    if (len > (MAX_PACKET_LENGTH/2) - 1)
        len = (MAX_PACKET_LENGTH/2) - 1;
    memtohex(buf + 1, (uint8_t *)msg, len);
    put_packet(s, buf);
}

static int gdb_monitor_write(CharDriverState *chr, const uint8_t *buf, int len)
{
    const char *p = (const char *)buf;
    int max_sz;

    max_sz = (sizeof(gdbserver_state->last_packet) - 2) / 2;
    for (;;) {
        if (len <= max_sz) {
            gdb_monitor_output(gdbserver_state, p, len);
            break;
        }
        gdb_monitor_output(gdbserver_state, p, max_sz);
        p += max_sz;
        len -= max_sz;
    }
    return len;
}

#ifndef _WIN32
static void gdb_sigterm_handler(int signal)
{
    if (vm_running)
        vm_stop(EXCP_INTERRUPT);
}
#endif

int gdbserver_start(const char *device)
{
    GDBState *s;
    char gdbstub_device_name[128];
    CharDriverState *chr = NULL;
    CharDriverState *mon_chr;

    if (!device)
        return -1;
    if (strcmp(device, "none") != 0) {
        if (strstart(device, "tcp:", NULL)) {
            /* enforce required TCP attributes */
            snprintf(gdbstub_device_name, sizeof(gdbstub_device_name),
                     "%s,nowait,nodelay,server", device);
            device = gdbstub_device_name;
        }
#ifndef _WIN32
        else if (strcmp(device, "stdio") == 0) {
            struct sigaction act;

            memset(&act, 0, sizeof(act));
            act.sa_handler = gdb_sigterm_handler;
            sigaction(SIGINT, &act, NULL);
        }
#endif
        chr = qemu_chr_open("gdb", device, NULL);
        if (!chr)
            return -1;

        qemu_chr_add_handlers(chr, gdb_chr_can_receive, gdb_chr_receive,
                              gdb_chr_event, NULL);
    }

    s = gdbserver_state;
    if (!s) {
        s = qemu_mallocz(sizeof(GDBState));
        gdbserver_state = s;

        qemu_add_vm_change_state_handler(gdb_vm_state_change, NULL);

        /* Initialize a monitor terminal for gdb */
        mon_chr = qemu_mallocz(sizeof(*mon_chr));
        mon_chr->chr_write = gdb_monitor_write;
        monitor_init(mon_chr, 0);
    } else {
        if (s->chr)
            qemu_chr_close(s->chr);
        mon_chr = s->mon_chr;
        memset(s, 0, sizeof(GDBState));
    }
    s->c_cpu = first_cpu;
    s->g_cpu = first_cpu;
    s->chr = chr;
    s->state = chr ? RS_IDLE : RS_INACTIVE;
    s->mon_chr = mon_chr;

    return 0;
}
#endif
