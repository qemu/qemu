/*
 * gdb server stub
 * 
 * Copyright (c) 2003 Fabrice Bellard
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>

#include "config.h"
#include "cpu.h"
#include "thunk.h"
#include "exec-all.h"

//#define DEBUG_GDB

int gdbstub_fd = -1;

/* return 0 if OK */
static int gdbstub_open(int port)
{
    struct sockaddr_in sockaddr;
    socklen_t len;
    int fd, val, ret;

    fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    /* allow fast reuse */
    val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

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
    
    /* now wait for one connection */
    for(;;) {
        len = sizeof(sockaddr);
        gdbstub_fd = accept(fd, (struct sockaddr *)&sockaddr, &len);
        if (gdbstub_fd < 0 && errno != EINTR) {
            perror("accept");
            return -1;
        } else if (gdbstub_fd >= 0) {
            break;
        }
    }
    
    /* set short latency */
    val = 1;
    setsockopt(gdbstub_fd, SOL_TCP, TCP_NODELAY, &val, sizeof(val));
    return 0;
}

static int get_char(void)
{
    uint8_t ch;
    int ret;

    for(;;) {
        ret = read(gdbstub_fd, &ch, 1);
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

static void put_buffer(const uint8_t *buf, int len)
{
    int ret;

    while (len > 0) {
        ret = write(gdbstub_fd, buf, len);
        if (ret < 0) {
            if (errno != EINTR && errno != EAGAIN)
                return;
        } else {
            buf += ret;
            len -= ret;
        }
    }
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

/* return -1 if error or EOF */
static int get_packet(char *buf, int buf_size)
{
    int ch, len, csum, csum1;
    char reply[1];
    
    for(;;) {
        for(;;) {
            ch = get_char();
            if (ch < 0)
                return -1;
            if (ch == '$')
                break;
        }
        len = 0;
        csum = 0;
        for(;;) {
            ch = get_char();
            if (ch < 0)
                return -1;
            if (ch == '#')
                break;
            if (len > buf_size - 1)
                return -1;
            buf[len++] = ch;
            csum += ch;
        }
        buf[len] = '\0';
        ch = get_char();
        if (ch < 0)
            return -1;
        csum1 = fromhex(ch) << 4;
        ch = get_char();
        if (ch < 0)
            return -1;
        csum1 |= fromhex(ch);
        if ((csum & 0xff) != csum1) {
            reply[0] = '-';
            put_buffer(reply, 1);
        } else {
            reply[0] = '+';
            put_buffer(reply, 1);
            break;
        }
    }
#ifdef DEBUG_GDB
    printf("command='%s'\n", buf);
#endif
    return len;
}

/* return -1 if error, 0 if OK */
static int put_packet(char *buf)
{
    char buf1[3];
    int len, csum, ch, i;

#ifdef DEBUG_GDB
    printf("reply='%s'\n", buf);
#endif

    for(;;) {
        buf1[0] = '$';
        put_buffer(buf1, 1);
        len = strlen(buf);
        put_buffer(buf, len);
        csum = 0;
        for(i = 0; i < len; i++) {
            csum += buf[i];
        }
        buf1[0] = '#';
        buf1[1] = tohex((csum >> 4) & 0xf);
        buf1[2] = tohex((csum) & 0xf);

        put_buffer(buf1, 3);

        ch = get_char();
        if (ch < 0)
            return -1;
        if (ch == '+')
            break;
    }
    return 0;
}

    /* better than nothing for SOFTMMU : we use physical addresses */
#ifdef CONFIG_SOFTMMU
static int memory_rw(uint8_t *buf, uint32_t addr, int len, int is_write)
{
    uint8_t *ptr;

    if (addr >= phys_ram_size ||
        ((int64_t)addr + len > phys_ram_size))
        return -1;
    ptr = phys_ram_base + addr;
    if (is_write)
        memcpy(ptr, buf, len);
    else
        memcpy(buf, ptr, len);
    return 0;
}
#else
static int memory_rw(uint8_t *buf, uint32_t addr, int len, int is_write)
{
    int l, flags;
    uint32_t page;

    while (len > 0) {
        page = addr & TARGET_PAGE_MASK;
        l = (page + TARGET_PAGE_SIZE) - addr;
        if (l > len)
            l = len;
        flags = page_get_flags(page);
        if (!(flags & PAGE_VALID))
            return -1;
        if (is_write) {
            if (!(flags & PAGE_WRITE))
                return -1;
            memcpy((uint8_t *)addr, buf, l);
        } else {
            if (!(flags & PAGE_READ))
                return -1;
            memcpy(buf, (uint8_t *)addr, l);
        }
        len -= l;
        buf += l;
        addr += l;
    }
    return 0;
}
#endif

#if defined(TARGET_I386)

static void to_le32(uint8_t *p, int v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    int i, fpus;

    for(i = 0; i < 8; i++) {
        to_le32(mem_buf + i * 4, env->regs[i]);
    }
    to_le32(mem_buf + 8 * 4, env->eip);
    to_le32(mem_buf + 9 * 4, env->eflags);
    to_le32(mem_buf + 10 * 4, env->segs[R_CS].selector);
    to_le32(mem_buf + 11 * 4, env->segs[R_SS].selector);
    to_le32(mem_buf + 12 * 4, env->segs[R_DS].selector);
    to_le32(mem_buf + 13 * 4, env->segs[R_ES].selector);
    to_le32(mem_buf + 14 * 4, env->segs[R_FS].selector);
    to_le32(mem_buf + 15 * 4, env->segs[R_GS].selector);
    /* XXX: convert floats */
    for(i = 0; i < 8; i++) {
        memcpy(mem_buf + 16 * 4 + i * 10, &env->fpregs[i], 10);
    }
    to_le32(mem_buf + 36 * 4, env->fpuc);
    fpus = (env->fpus & ~0x3800) | (env->fpstt & 0x7) << 11;
    to_le32(mem_buf + 37 * 4, fpus);
    to_le32(mem_buf + 38 * 4, 0); /* XXX: convert tags */
    to_le32(mem_buf + 39 * 4, 0); /* fiseg */
    to_le32(mem_buf + 40 * 4, 0); /* fioff */
    to_le32(mem_buf + 41 * 4, 0); /* foseg */
    to_le32(mem_buf + 42 * 4, 0); /* fooff */
    to_le32(mem_buf + 43 * 4, 0); /* fop */
    return 44 * 4;
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
    uint32_t *registers = (uint32_t *)mem_buf;
    int i;

    for(i = 0; i < 8; i++) {
        env->regs[i] = tswapl(registers[i]);
    }
    env->eip = registers[8];
    env->eflags = registers[9];
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

#else

static int cpu_gdb_read_registers(CPUState *env, uint8_t *mem_buf)
{
    return 0;
}

static void cpu_gdb_write_registers(CPUState *env, uint8_t *mem_buf, int size)
{
}

#endif

/* port = 0 means default port */
int cpu_gdbstub(void *opaque, int (*main_loop)(void *opaque), int port)
{
    CPUState *env;
    const char *p;
    int ret, ch, reg_size, type;
    char buf[4096];
    uint8_t mem_buf[2000];
    uint32_t *registers;
    uint32_t addr, len;
    
    printf("Waiting gdb connection on port %d\n", port);
    if (gdbstub_open(port) < 0)
        return -1;
    printf("Connected\n");
    for(;;) {
        ret = get_packet(buf, sizeof(buf));
        if (ret < 0)
            break;
        p = buf;
        ch = *p++;
        switch(ch) {
        case '?':
            snprintf(buf, sizeof(buf), "S%02x", SIGTRAP);
            put_packet(buf);
            break;
        case 'c':
            if (*p != '\0') {
                addr = strtoul(p, (char **)&p, 16);
                env = cpu_gdbstub_get_env(opaque);
#if defined(TARGET_I386)
                env->eip = addr;
#endif
            }
            ret = main_loop(opaque);
            if (ret == EXCP_DEBUG)
                ret = SIGTRAP;
            else
                ret = 0;
            snprintf(buf, sizeof(buf), "S%02x", ret);
            put_packet(buf);
            break;
        case 's':
            env = cpu_gdbstub_get_env(opaque);
            if (*p != '\0') {
                addr = strtoul(p, (char **)&p, 16);
#if defined(TARGET_I386)
                env->eip = addr;
#endif
            }
            cpu_single_step(env, 1);
            ret = main_loop(opaque);
            cpu_single_step(env, 0);
            if (ret == EXCP_DEBUG)
                ret = SIGTRAP;
            else
                ret = 0;
            snprintf(buf, sizeof(buf), "S%02x", ret);
            put_packet(buf);
            break;
        case 'g':
            env = cpu_gdbstub_get_env(opaque);
            reg_size = cpu_gdb_read_registers(env, mem_buf);
            memtohex(buf, mem_buf, reg_size);
            put_packet(buf);
            break;
        case 'G':
            env = cpu_gdbstub_get_env(opaque);
            registers = (void *)mem_buf;
            len = strlen(p) / 2;
            hextomem((uint8_t *)registers, p, len);
            cpu_gdb_write_registers(env, mem_buf, len);
            put_packet("OK");
            break;
        case 'm':
            addr = strtoul(p, (char **)&p, 16);
            if (*p == ',')
                p++;
            len = strtoul(p, NULL, 16);
            if (memory_rw(mem_buf, addr, len, 0) != 0)
                memset(mem_buf, 0, len);
            memtohex(buf, mem_buf, len);
            put_packet(buf);
            break;
        case 'M':
            addr = strtoul(p, (char **)&p, 16);
            if (*p == ',')
                p++;
            len = strtoul(p, (char **)&p, 16);
            if (*p == ',')
                p++;
            hextomem(mem_buf, p, len);
            if (memory_rw(mem_buf, addr, len, 1) != 0)
                put_packet("ENN");
            else
                put_packet("OK");
            break;
        case 'Z':
            type = strtoul(p, (char **)&p, 16);
            if (*p == ',')
                p++;
            addr = strtoul(p, (char **)&p, 16);
            if (*p == ',')
                p++;
            len = strtoul(p, (char **)&p, 16);
            if (type == 0 || type == 1) {
                env = cpu_gdbstub_get_env(opaque);
                if (cpu_breakpoint_insert(env, addr) < 0)
                    goto breakpoint_error;
                put_packet("OK");
            } else {
            breakpoint_error:
                put_packet("ENN");
            }
            break;
        case 'z':
            type = strtoul(p, (char **)&p, 16);
            if (*p == ',')
                p++;
            addr = strtoul(p, (char **)&p, 16);
            if (*p == ',')
                p++;
            len = strtoul(p, (char **)&p, 16);
            if (type == 0 || type == 1) {
                env = cpu_gdbstub_get_env(opaque);
                cpu_breakpoint_remove(env, addr);
                put_packet("OK");
            } else {
                goto breakpoint_error;
            }
            break;
        case 'Q':
            if (!strncmp(p, "Tinit", 5)) {
                /* init traces */
                put_packet("OK");
            } else if (!strncmp(p, "TStart", 6)) {
                /* start log (gdb 'tstart' command) */
                env = cpu_gdbstub_get_env(opaque);
                tb_flush(env);
                cpu_set_log(CPU_LOG_ALL);
                put_packet("OK");
            } else if (!strncmp(p, "TStop", 5)) {
                /* stop log (gdb 'tstop' command) */
                cpu_set_log(0);
                put_packet("OK");
            } else {
                goto unknown_command;
            }
            break;
        default:
        unknown_command:
            /* put empty packet */
            buf[0] = '\0';
            put_packet(buf);
            break;
        }
    }
    return 0;
}
