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
#include "vl.h"
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
    char last_packet[4100];
    int last_packet_len;
#ifdef CONFIG_USER_ONLY
    int fd;
    int running_state;
#else
    CharDriverState *chr;
#endif
} GDBState;

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
            if (errno != EINTR && errno != EAGAIN)
                return -1;
        } else if (ret == 0) {
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
static int put_packet(GDBState *s, char *buf)
{
    int len, csum, i;
    char *p;

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
        put_buffer(s, s->last_packet, s->last_packet_len);

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

static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    uint32_t *registers = (uint32_t *)mem_buf;
    int i, fpus;

    for(i = 0; i < 8; i++) {
        registers[i] = env->regs[i];
    }
    registers[8] = env->eip;
    registers[9] = env->eflags;
    registers[10] = env->segs[R_CS].selector;
    registers[11] = env->segs[R_SS].selector;
    registers[12] = env->segs[R_DS].selector;
    registers[13] = env->segs[R_ES].selector;
    registers[14] = env->segs[R_FS].selector;
    registers[15] = env->segs[R_GS].selector;
    /* XXX: convert floats */
    for(i = 0; i < 8; i++) {
        memcpy(mem_buf + 16 * 4 + i * 10, &env->fpregs[i], 10);
    }
    registers[36] = env->fpuc;
    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    registers[37] = fpus;
    registers[38] = 0; /* XXX: convert tags */
    registers[39] = 0; /* fiseg */
    registers[40] = 0; /* fioff */
    registers[41] = 0; /* foseg */
    registers[42] = 0; /* fooff */
    registers[43] = 0; /* fop */
    
    for(i = 0; i < 16; i++)
        tswapls(&registers[i]);
    for(i = 36; i < 44; i++)
        tswapls(&registers[i]);
    return 44 * 4;
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
    uint32_t *registers = (uint32_t *)mem_buf;
    int i;

    for(i = 0; i < 8; i++) {
        env->regs[i] = tswapl(registers[i]);
    }
    env->eip = tswapl(registers[8]);
    env->eflags = tswapl(registers[9]);
#if defined(CONFIG_USER_ONLY)
#define LOAD_SEG(index, sreg)\
            if (tswapl(registers[index]) != env->segs[sreg].selector)\
                cpu_x86_load_seg(env, sreg, tswapl(registers[index]));
            LOAD_SEG(10, R_CS);
            LOAD_SEG(11, R_SS);
            LOAD_SEG(12, R_DS);
            LOAD_SEG(13, R_ES);
            LOAD_SEG(14, R_FS);
            LOAD_SEG(15, R_GS);
#endif
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
    registers[97] = tswapl(do_load_msr(env));
    tmp = 0;
    for (i = 0; i < 8; i++)
        tmp |= env->crf[i] << (32 - ((i + 1) * 4));
    registers[98] = tswapl(tmp);
    registers[99] = tswapl(env->lr);
    registers[100] = tswapl(env->ctr);
    registers[101] = tswapl(do_load_xer(env));
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
    do_store_msr(env, tswapl(registers[97]));
    registers[98] = tswapl(registers[98]);
    for (i = 0; i < 8; i++)
        env->crf[i] = (registers[98] >> (32 - ((i + 1) * 4))) & 0xF;
    env->lr = tswapl(registers[99]);
    env->ctr = tswapl(registers[100]);
    do_store_xer(env, tswapl(registers[101]));
}
#elif defined (TARGET_SPARC)
static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    target_ulong *registers = (target_ulong *)mem_buf;
    int i;

    /* fill in g0..g7 */
    for(i = 0; i < 8; i++) {
        registers[i] = tswapl(env->gregs[i]);
    }
    /* fill in register window */
    for(i = 0; i < 24; i++) {
        registers[i + 8] = tswapl(env->regwptr[i]);
    }
#ifndef TARGET_SPARC64
    /* fill in fprs */
    for (i = 0; i < 32; i++) {
        registers[i + 32] = tswapl(*((uint32_t *)&env->fpr[i]));
    }
    /* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
    registers[64] = tswapl(env->y);
    {
	target_ulong tmp;

	tmp = GET_PSR(env);
	registers[65] = tswapl(tmp);
    }
    registers[66] = tswapl(env->wim);
    registers[67] = tswapl(env->tbr);
    registers[68] = tswapl(env->pc);
    registers[69] = tswapl(env->npc);
    registers[70] = tswapl(env->fsr);
    registers[71] = 0; /* csr */
    registers[72] = 0;
    return 73 * sizeof(target_ulong);
#else
    /* fill in fprs */
    for (i = 0; i < 64; i += 2) {
	uint64_t tmp;

        tmp = (uint64_t)tswap32(*((uint32_t *)&env->fpr[i])) << 32;
        tmp |= tswap32(*((uint32_t *)&env->fpr[i + 1]));
        registers[i/2 + 32] = tmp;
    }
    registers[64] = tswapl(env->pc);
    registers[65] = tswapl(env->npc);
    registers[66] = tswapl(env->tstate[env->tl]);
    registers[67] = tswapl(env->fsr);
    registers[68] = tswapl(env->fprs);
    registers[69] = tswapl(env->y);
    return 70 * sizeof(target_ulong);
#endif
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
    target_ulong *registers = (target_ulong *)mem_buf;
    int i;

    /* fill in g0..g7 */
    for(i = 0; i < 7; i++) {
        env->gregs[i] = tswapl(registers[i]);
    }
    /* fill in register window */
    for(i = 0; i < 24; i++) {
        env->regwptr[i] = tswapl(registers[i + 8]);
    }
#ifndef TARGET_SPARC64
    /* fill in fprs */
    for (i = 0; i < 32; i++) {
        *((uint32_t *)&env->fpr[i]) = tswapl(registers[i + 32]);
    }
    /* Y, PSR, WIM, TBR, PC, NPC, FPSR, CPSR */
    env->y = tswapl(registers[64]);
    PUT_PSR(env, tswapl(registers[65]));
    env->wim = tswapl(registers[66]);
    env->tbr = tswapl(registers[67]);
    env->pc = tswapl(registers[68]);
    env->npc = tswapl(registers[69]);
    env->fsr = tswapl(registers[70]);
#else
    for (i = 0; i < 64; i += 2) {
	*((uint32_t *)&env->fpr[i]) = tswap32(registers[i/2 + 32] >> 32);
	*((uint32_t *)&env->fpr[i + 1]) = tswap32(registers[i/2 + 32] & 0xffffffff);
    }
    env->pc = tswapl(registers[64]);
    env->npc = tswapl(registers[65]);
    env->tstate[env->tl] = tswapl(registers[66]);
    env->fsr = tswapl(registers[67]);
    env->fprs = tswapl(registers[68]);
    env->y = tswapl(registers[69]);
#endif
}
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
        *(uint32_t *)ptr = tswapl(env->gpr[i]);
        ptr += 4;
      }

    *(uint32_t *)ptr = tswapl(env->CP0_Status);
    ptr += 4;

    *(uint32_t *)ptr = tswapl(env->LO);
    ptr += 4;

    *(uint32_t *)ptr = tswapl(env->HI);
    ptr += 4;

    *(uint32_t *)ptr = tswapl(env->CP0_BadVAddr);
    ptr += 4;

    *(uint32_t *)ptr = tswapl(env->CP0_Cause);
    ptr += 4;

    *(uint32_t *)ptr = tswapl(env->PC);
    ptr += 4;

    if (env->CP0_Config1 & (1 << CP0C1_FP))
      {
        for (i = 0; i < 32; i++)
          {
            *(uint32_t *)ptr = tswapl(FPR_W (env, i));
            ptr += 4;
          }

        *(uint32_t *)ptr = tswapl(env->fcr31);
        ptr += 4;

        *(uint32_t *)ptr = tswapl(env->fcr0);
        ptr += 4;
      }

    /* 32 FP registers, fsr, fir, fp.  Not yet implemented.  */
    /* what's 'fp' mean here?  */

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
    set_float_rounding_mode(ieee_rm[env->fcr31 & 3], &env->fp_status)

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
    int i;
    uint8_t *ptr;

    ptr = mem_buf;
    for (i = 0; i < 32; i++)
      {
        env->gpr[i] = tswapl(*(uint32_t *)ptr);
        ptr += 4;
      }

    env->CP0_Status = tswapl(*(uint32_t *)ptr);
    ptr += 4;

    env->LO = tswapl(*(uint32_t *)ptr);
    ptr += 4;

    env->HI = tswapl(*(uint32_t *)ptr);
    ptr += 4;

    env->CP0_BadVAddr = tswapl(*(uint32_t *)ptr);
    ptr += 4;

    env->CP0_Cause = tswapl(*(uint32_t *)ptr);
    ptr += 4;

    env->PC = tswapl(*(uint32_t *)ptr);
    ptr += 4;

    if (env->CP0_Config1 & (1 << CP0C1_FP))
      {
        for (i = 0; i < 32; i++)
          {
            FPR_W (env, i) = tswapl(*(uint32_t *)ptr);
            ptr += 4;
          }

        env->fcr31 = tswapl(*(uint32_t *)ptr) & 0x0183FFFF;
        ptr += 4;

        env->fcr0 = tswapl(*(uint32_t *)ptr);
        ptr += 4;

        /* set rounding mode */
        RESTORE_ROUNDING_MODE;

#ifndef CONFIG_SOFTFLOAT
        /* no floating point exception for native float */
        SET_FP_ENABLE(env->fcr31, 0);
#endif
      }
}
#elif defined (TARGET_SH4)
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
  SAVE (0); /* TICKS */
  SAVE (0); /* STALLS */
  SAVE (0); /* CYCLES */
  SAVE (0); /* INSTS */
  SAVE (0); /* PLR */

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
    uint8_t mem_buf[2000];
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
#endif
        }
#ifdef CONFIG_USER_ONLY
        s->running_state = 1;
#else
        vm_start();
#endif
	return RS_IDLE;
    case 's':
        if (*p != '\0') {
            addr = strtoul(p, (char **)&p, 16);
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
#endif
        }
        cpu_single_step(env, 1);
#ifdef CONFIG_USER_ONLY
        s->running_state = 1;
#else
        vm_start();
#endif
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
#ifdef CONFIG_USER_ONLY
                s->running_state = 1;
#else
                vm_start();
#endif
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
        if (type == 0 || type == 1) {
            if (cpu_breakpoint_insert(env, addr) < 0)
                goto breakpoint_error;
            put_packet(s, "OK");
        } else {
        breakpoint_error:
            put_packet(s, "E22");
        }
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
        } else {
            goto breakpoint_error;
        }
        break;
#ifdef CONFIG_LINUX_USER
    case 'q':
        if (strncmp(p, "Offsets", 7) == 0) {
            TaskState *ts = env->opaque;

            sprintf(buf, "Text=%x;Data=%x;Bss=%x", ts->info->code_offset,
                ts->info->data_offset, ts->info->data_offset);
            put_packet(s, buf);
            break;
        }
        /* Fall through.  */
#endif
    default:
        //        unknown_command:
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
    %x - target_ulong argument printed in hex.
    %s - string pointer (target_ulong) and length (int) pair.  */
void gdb_do_syscall(gdb_syscall_complete_cb cb, char *fmt, ...)
{
    va_list va;
    char buf[256];
    char *p;
    target_ulong addr;
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
                p += sprintf(p, TARGET_FMT_lx, addr);
                break;
            case 's':
                addr = va_arg(va, target_ulong);
                p += sprintf(p, TARGET_FMT_lx "/%x", addr, va_arg(va, int));
                break;
            default:
                fprintf(stderr, "gdbstub: Bad syscall format string '%s'\n",
                        fmt - 1);
                break;
            }
        } else {
            *(p++) = *(fmt++);
        }
    }
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
    char reply[1];

#ifndef CONFIG_USER_ONLY
    if (s->last_packet_len) {
        /* Waiting for a response to the last packet.  If we see the start
           of a new command then abandon the previous response.  */
        if (ch == '-') {
#ifdef DEBUG_GDB
            printf("Got NACK, retransmitting\n");
#endif
            put_buffer(s, s->last_packet, s->last_packet_len);
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
                reply[0] = '-';
                put_buffer(s, reply, 1);
                s->state = RS_IDLE;
            } else {
                reply[0] = '+';
                put_buffer(s, reply, 1);
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

  if (gdbserver_fd < 0)
    return sig;

  s = &gdbserver_state;

  /* disable single step if it was enabled */
  cpu_single_step(env, 0);
  tb_flush(env);

  if (sig != 0)
    {
      snprintf(buf, sizeof(buf), "S%02x", sig);
      put_packet(s, buf);
    }

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
  return sig;
}

/* Tell the remote gdb that the process has exited.  */
void gdb_exit(CPUState *env, int code)
{
  GDBState *s;
  char buf[4];

  if (gdbserver_fd < 0)
    return;

  s = &gdbserver_state;

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
static int gdb_chr_can_recieve(void *opaque)
{
  return 1;
}

static void gdb_chr_recieve(void *opaque, const uint8_t *buf, int size)
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
    qemu_chr_add_handlers(chr, gdb_chr_can_recieve, gdb_chr_recieve,
                          gdb_chr_event, s);
    qemu_add_vm_stop_handler(gdb_vm_stopped, s);
    return 0;
}
#endif
