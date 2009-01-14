 /*
 *  Commpage syscalls
 *
 *  Copyright (c) 2006 Pierre d'Herbemont
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
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <mach/message.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <libkern/OSAtomic.h>

#include "qemu.h"

//#define DEBUG_COMMPAGE

#ifdef DEBUG_COMMPAGE
# define DPRINTF(...) do { if(loglevel) fprintf(logfile, __VA_ARGS__); printf(__VA_ARGS__); } while(0)
#else
# define DPRINTF(...) do { if(loglevel) fprintf(logfile, __VA_ARGS__); } while(0)
#endif

/********************************************************************
 *   Commpage definitions
 */
#ifdef TARGET_I386
/* Reserve space for the commpage see xnu/osfmk/i386/cpu_capabilities.h */
# define COMMPAGE_START (-16 * 4096) /* base address is -20 * 4096 */
# define COMMPAGE_SIZE  (0x1240) /* _COMM_PAGE_AREA_LENGTH is 19 * 4096 */
#elif defined(TARGET_PPC)
/* Reserve space for the commpage see xnu/osfmk/ppc/cpu_capabilities.h */
# define COMMPAGE_START (-8*4096)
# define COMMPAGE_SIZE  (2*4096) /* its _COMM_PAGE_AREA_USED but _COMM_PAGE_AREA_LENGTH is 7*4096 */
#endif

void do_compare_and_swap32(void *cpu_env, int num);
void do_compare_and_swap64(void *cpu_env, int num);
void do_add_atomic_word32(void *cpu_env, int num);
void do_cgettimeofday(void *cpu_env, int num, uint32_t arg1);
void do_nanotime(void *cpu_env, int num);

void unimpl_commpage(void *cpu_env, int num);

typedef void (*commpage_8args_function_t)(uint32_t arg1, uint32_t arg2, uint32_t arg3,
                uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7,
                uint32_t arg8);
typedef void (*commpage_indirect_function_t)(void *cpu_env, int num, uint32_t arg1,
                uint32_t arg2, uint32_t arg3,  uint32_t arg4, uint32_t arg5,
                uint32_t arg6, uint32_t arg7, uint32_t arg8);

#define HAS_PTR  0x10
#define NO_PTR   0x20
#define CALL_DIRECT   0x1
#define CALL_INDIRECT 0x2

#define COMMPAGE_ENTRY(name, nargs, offset, func, options) \
    { #name, offset, nargs, options, (commpage_8args_function_t)func }

struct commpage_entry {
    char * name;
    int offset;
    int nargs;
    char options;
    commpage_8args_function_t function;
};

static inline int commpage_code_num(struct commpage_entry *entry)
{
    if((entry->options & HAS_PTR))
        return entry->offset + 4;
    else
        return entry->offset;
}

static inline int commpage_is_indirect(struct commpage_entry *entry)
{
    return !(entry->options & CALL_DIRECT);
}

/********************************************************************
 *   Commpage entry
 */
static struct commpage_entry commpage_entries[] =
{
    COMMPAGE_ENTRY(compare_and_swap32,    0, 0x080,  do_compare_and_swap32, CALL_INDIRECT | HAS_PTR),
    COMMPAGE_ENTRY(compare_and_swap64,    0, 0x0c0,  do_compare_and_swap64, CALL_INDIRECT | HAS_PTR),
    COMMPAGE_ENTRY(enqueue,               0, 0x100,  unimpl_commpage,       CALL_INDIRECT),
    COMMPAGE_ENTRY(dequeue,               0, 0x140,  unimpl_commpage,       CALL_INDIRECT),
    COMMPAGE_ENTRY(memory_barrier,        0, 0x180,  unimpl_commpage,       CALL_INDIRECT),
    COMMPAGE_ENTRY(add_atomic_word32,     0, 0x1a0,  do_add_atomic_word32,  CALL_INDIRECT | HAS_PTR),
    COMMPAGE_ENTRY(add_atomic_word64,     0, 0x1c0,  unimpl_commpage,       CALL_INDIRECT | HAS_PTR),

    COMMPAGE_ENTRY(mach_absolute_time,    0, 0x200,  unimpl_commpage,       CALL_INDIRECT),
    COMMPAGE_ENTRY(spinlock_try,          1, 0x220,  unimpl_commpage,       CALL_INDIRECT),
    COMMPAGE_ENTRY(spinlock_lock,         1, 0x260,  OSSpinLockLock,        CALL_DIRECT),
    COMMPAGE_ENTRY(spinlock_unlock,       1, 0x2a0,  OSSpinLockUnlock,      CALL_DIRECT),
    COMMPAGE_ENTRY(pthread_getspecific,   0, 0x2c0,  unimpl_commpage,       CALL_INDIRECT),
    COMMPAGE_ENTRY(gettimeofday,          1, 0x2e0,  do_cgettimeofday,      CALL_INDIRECT),
    COMMPAGE_ENTRY(sys_dcache_flush,      0, 0x4e0,  unimpl_commpage,       CALL_INDIRECT),
    COMMPAGE_ENTRY(sys_icache_invalidate, 0, 0x520,  unimpl_commpage,       CALL_INDIRECT),
    COMMPAGE_ENTRY(pthread_self,          0, 0x580,  unimpl_commpage,       CALL_INDIRECT),

    COMMPAGE_ENTRY(relinquish,            0, 0x5c0,  unimpl_commpage,       CALL_INDIRECT),

#ifdef TARGET_I386
    COMMPAGE_ENTRY(bts,                   0, 0x5e0,  unimpl_commpage,       CALL_INDIRECT),
    COMMPAGE_ENTRY(btc,                   0, 0x5f0,  unimpl_commpage,       CALL_INDIRECT),
#endif

    COMMPAGE_ENTRY(bzero,                 2, 0x600,  bzero,                 CALL_DIRECT),
    COMMPAGE_ENTRY(bcopy,                 3, 0x780,  bcopy,                 CALL_DIRECT),
    COMMPAGE_ENTRY(memcpy,                3, 0x7a0,  memcpy,                CALL_DIRECT),

#ifdef TARGET_I386
    COMMPAGE_ENTRY(old_nanotime,          0, 0xf80,  do_nanotime,           CALL_INDIRECT),
    COMMPAGE_ENTRY(memset_pattern,        0, 0xf80,  unimpl_commpage,       CALL_INDIRECT),
    COMMPAGE_ENTRY(long_copy,             0, 0x1200, unimpl_commpage,       CALL_INDIRECT),

    COMMPAGE_ENTRY(sysintegrity,          0, 0x1600, unimpl_commpage,       CALL_INDIRECT),

    COMMPAGE_ENTRY(nanotime,              0, 0x1700, do_nanotime,           CALL_INDIRECT),
#elif TARGET_PPC
    COMMPAGE_ENTRY(compare_and_swap32b,   0, 0xf80,  unimpl_commpage,       CALL_INDIRECT),
    COMMPAGE_ENTRY(compare_and_swap64b,   0, 0xfc0,  unimpl_commpage,       CALL_INDIRECT),
    COMMPAGE_ENTRY(memset_pattern,        0, 0x1000, unimpl_commpage,       CALL_INDIRECT),
    COMMPAGE_ENTRY(bigcopy,               0, 0x1140, unimpl_commpage,       CALL_INDIRECT),
#endif
};


/********************************************************************
 *   Commpage backdoor
 */
static inline void print_commpage_entry(struct commpage_entry entry)
{
    printf("@0x%x %s\n", entry.offset, entry.name);
}

static inline void install_commpage_backdoor_for_entry(struct commpage_entry entry)
{
#ifdef TARGET_I386
    char * commpage = (char*)(COMMPAGE_START+entry.offset);
    int c = 0;
    if(entry.options & HAS_PTR)
    {
        commpage[c++] = (COMMPAGE_START+entry.offset+4) & 0xff;
        commpage[c++] = ((COMMPAGE_START+entry.offset+4) >> 8) & 0xff;
        commpage[c++] = ((COMMPAGE_START+entry.offset+4) >> 16) & 0xff;
        commpage[c++] = ((COMMPAGE_START+entry.offset+4) >> 24) & 0xff;
    }
    commpage[c++] = 0xcd;
    commpage[c++] = 0x79; /* int 0x79 */
    commpage[c++] = 0xc3; /* ret */
#else
    qerror("can't install the commpage on this arch\n");
#endif
}

/********************************************************************
 *   Commpage initialization
 */
void commpage_init(void)
{
#if (defined(__i386__) ^ defined(TARGET_I386)) || (defined(_ARCH_PPC) ^ defined(TARGET_PPC))
    int i;
    void * commpage = (void *)target_mmap( COMMPAGE_START, COMMPAGE_SIZE,
                           PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if((int)commpage != COMMPAGE_START)
        qerror("can't allocate the commpage\n");

    bzero(commpage, COMMPAGE_SIZE);

    /* XXX: commpage data not handled */

    for(i = 0; i < ARRAY_SIZE(commpage_entries); i++)
        install_commpage_backdoor_for_entry(commpage_entries[i]);
#else
    /* simply map our pages so they can be executed
       XXX: we don't really want to do that since in the ppc on ppc situation we may
       not able to run commpages host optimized instructions (like G5's on a G5),
       hence this is sometimes a broken fix. */
    page_set_flags(COMMPAGE_START, COMMPAGE_START+COMMPAGE_SIZE, PROT_EXEC | PROT_READ | PAGE_VALID);
#endif
}

/********************************************************************
 *   Commpage implementation
 */
void do_compare_and_swap32(void *cpu_env, int num)
{
#ifdef TARGET_I386
    uint32_t old = ((CPUX86State*)cpu_env)->regs[R_EAX];
    uint32_t *value = (uint32_t*)((CPUX86State*)cpu_env)->regs[R_ECX];
    DPRINTF("commpage: compare_and_swap32(%x,new,%p)\n", old, value);

    if(value && old == tswap32(*value))
    {
        uint32_t new = ((CPUX86State*)cpu_env)->regs[R_EDX];
        *value = tswap32(new);
        /* set zf flag */
        ((CPUX86State*)cpu_env)->eflags |= 0x40;
    }
    else
    {
        ((CPUX86State*)cpu_env)->regs[R_EAX] = tswap32(*value);
        /* unset zf flag */
        ((CPUX86State*)cpu_env)->eflags &= ~0x40;
    }
#else
    qerror("do_compare_and_swap32 unimplemented");
#endif
}

void do_compare_and_swap64(void *cpu_env, int num)
{
#ifdef TARGET_I386
    /* OSAtomicCompareAndSwap64 is not available on non 64 bits ppc, here is a raw implementation */
    uint64_t old, new, swapped_val;
    uint64_t *value = (uint64_t*)((CPUX86State*)cpu_env)->regs[R_ESI];
    old = (uint64_t)((uint64_t)((CPUX86State*)cpu_env)->regs[R_EDX]) << 32 | (uint64_t)((CPUX86State*)cpu_env)->regs[R_EAX];

    DPRINTF("commpage: compare_and_swap64(%llx,new,%p)\n", old, value);
    swapped_val = tswap64(*value);

    if(old == swapped_val)
    {
        new = (uint64_t)((uint64_t)((CPUX86State*)cpu_env)->regs[R_ECX]) << 32 | (uint64_t)((CPUX86State*)cpu_env)->regs[R_EBX];
        *value = tswap64(new);
        /* set zf flag */
        ((CPUX86State*)cpu_env)->eflags |= 0x40;
    }
    else
    {
        ((CPUX86State*)cpu_env)->regs[R_EAX] = (uint32_t)(swapped_val);
        ((CPUX86State*)cpu_env)->regs[R_EDX] = (uint32_t)(swapped_val >> 32);
        /* unset zf flag */
        ((CPUX86State*)cpu_env)->eflags &= ~0x40;
    }
#else
    qerror("do_compare_and_swap64 unimplemented");
#endif
}

void do_add_atomic_word32(void *cpu_env, int num)
{
#ifdef TARGET_I386
    uint32_t amt = ((CPUX86State*)cpu_env)->regs[R_EAX];
    uint32_t *value = (uint32_t*)((CPUX86State*)cpu_env)->regs[R_EDX];
    uint32_t swapped_value = tswap32(*value);

    DPRINTF("commpage: add_atomic_word32(%x,%p)\n", amt, value);

    /* old value in EAX */
    ((CPUX86State*)cpu_env)->regs[R_EAX] = swapped_value;
    *value = tswap32(swapped_value + amt);
#else
    qerror("do_add_atomic_word32 unimplemented");
#endif
}

void do_cgettimeofday(void *cpu_env, int num, uint32_t arg1)
{
#ifdef TARGET_I386
    extern int __commpage_gettimeofday(struct timeval *);
    DPRINTF("commpage: gettimeofday(0x%x)\n", arg1);
    struct timeval *time = (struct timeval *)arg1;
    int ret = __commpage_gettimeofday(time);
    tswap32s((uint32_t*)&time->tv_sec);
    tswap32s((uint32_t*)&time->tv_usec);
    ((CPUX86State*)cpu_env)->regs[R_EAX] = ret; /* Success */
#else
    qerror("do_gettimeofday unimplemented");
#endif
}

void do_nanotime(void *cpu_env, int num)
{
#ifdef TARGET_I386
    uint64_t t = mach_absolute_time();
    ((CPUX86State*)cpu_env)->regs[R_EAX] = (int)(t & 0xffffffff);
    ((CPUX86State*)cpu_env)->regs[R_EDX] = (int)((t >> 32) & 0xffffffff);
#else
    qerror("do_nanotime unimplemented");
#endif
}

void unimpl_commpage(void *cpu_env, int num)
{
    qerror("qemu: commpage function 0x%x not implemented\n", num);
}

/********************************************************************
 *   do_commpage - called by the main cpu loop
 */
void
do_commpage(void *cpu_env, int num, uint32_t arg1, uint32_t arg2, uint32_t arg3,
                uint32_t arg4, uint32_t arg5, uint32_t arg6, uint32_t arg7,
                uint32_t arg8)
{
    int i, found = 0;

    arg1 = tswap32(arg1);
    arg2 = tswap32(arg2);
    arg3 = tswap32(arg3);
    arg4 = tswap32(arg4);
    arg5 = tswap32(arg5);
    arg6 = tswap32(arg6);
    arg7 = tswap32(arg7);
    arg8 = tswap32(arg8);

    num = num-COMMPAGE_START-2;

    for(i = 0; i < ARRAY_SIZE(commpage_entries); i++) {
        if( num == commpage_code_num(&commpage_entries[i]) )
        {
            DPRINTF("commpage: %s %s\n", commpage_entries[i].name, commpage_is_indirect(&commpage_entries[i]) ? "[indirect]" : "[direct]");
            found = 1;
            if(commpage_is_indirect(&commpage_entries[i]))
            {
                commpage_indirect_function_t function = (commpage_indirect_function_t)commpage_entries[i].function;
                function(cpu_env, num, arg1, arg2, arg3,
                    arg4, arg5, arg6, arg7, arg8);
            }
            else
            {
                commpage_entries[i].function(arg1, arg2, arg3,
                    arg4, arg5, arg6, arg7, arg8);
            }
            break;
        }
    }

    if(!found)
    {
        gemu_log("qemu: commpage function 0x%x not defined\n", num);
        gdb_handlesig (cpu_env, SIGTRAP);
        exit(-1);
    }
}
