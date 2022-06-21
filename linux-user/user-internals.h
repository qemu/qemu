/*
 * user-internals.h: prototypes etc internal to the linux-user implementation
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
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LINUX_USER_USER_INTERNALS_H
#define LINUX_USER_USER_INTERNALS_H

#include "exec/user/thunk.h"
#include "exec/exec-all.h"
#include "qemu/log.h"

extern char *exec_path;
void init_task_state(TaskState *ts);
void task_settid(TaskState *);
void stop_all_tasks(void);
extern const char *qemu_uname_release;
extern unsigned long mmap_min_addr;

typedef struct IOCTLEntry IOCTLEntry;

typedef abi_long do_ioctl_fn(const IOCTLEntry *ie, uint8_t *buf_temp,
                             int fd, int cmd, abi_long arg);

struct IOCTLEntry {
    int target_cmd;
    unsigned int host_cmd;
    const char *name;
    int access;
    do_ioctl_fn *do_ioctl;
    const argtype arg_type[5];
};

extern IOCTLEntry ioctl_entries[];

#define IOC_R 0x0001
#define IOC_W 0x0002
#define IOC_RW (IOC_R | IOC_W)

/*
 * Returns true if the image uses the FDPIC ABI. If this is the case,
 * we have to provide some information (loadmap, pt_dynamic_info) such
 * that the program can be relocated adequately. This is also useful
 * when handling signals.
 */
int info_is_fdpic(struct image_info *info);

void target_set_brk(abi_ulong new_brk);
void syscall_init(void);
abi_long do_syscall(CPUArchState *cpu_env, int num, abi_long arg1,
                    abi_long arg2, abi_long arg3, abi_long arg4,
                    abi_long arg5, abi_long arg6, abi_long arg7,
                    abi_long arg8);
extern __thread CPUState *thread_cpu;
G_NORETURN void cpu_loop(CPUArchState *env);
abi_long get_errno(abi_long ret);
const char *target_strerror(int err);
int get_osversion(void);
void init_qemu_uname_release(void);
void fork_start(void);
void fork_end(int child);

/**
 * probe_guest_base:
 * @image_name: the executable being loaded
 * @loaddr: the lowest fixed address in the executable
 * @hiaddr: the highest fixed address in the executable
 *
 * Creates the initial guest address space in the host memory space.
 *
 * If @loaddr == 0, then no address in the executable is fixed,
 * i.e. it is fully relocatable.  In that case @hiaddr is the size
 * of the executable.
 *
 * This function will not return if a valid value for guest_base
 * cannot be chosen.  On return, the executable loader can expect
 *
 *    target_mmap(loaddr, hiaddr - loaddr, ...)
 *
 * to succeed.
 */
void probe_guest_base(const char *image_name,
                      abi_ulong loaddr, abi_ulong hiaddr);

/* syscall.c */
int host_to_target_waitstatus(int status);

#ifdef TARGET_I386
/* vm86.c */
void save_v86_state(CPUX86State *env);
void handle_vm86_trap(CPUX86State *env, int trapno);
void handle_vm86_fault(CPUX86State *env);
int do_vm86(CPUX86State *env, long subfunction, abi_ulong v86_addr);
#elif defined(TARGET_SPARC64)
void sparc64_set_context(CPUSPARCState *env);
void sparc64_get_context(CPUSPARCState *env);
#endif

static inline int is_error(abi_long ret)
{
    return (abi_ulong)ret >= (abi_ulong)(-4096);
}

#if (TARGET_ABI_BITS == 32) && !defined(TARGET_ABI_MIPSN32)
static inline uint64_t target_offset64(uint32_t word0, uint32_t word1)
{
#if TARGET_BIG_ENDIAN
    return ((uint64_t)word0 << 32) | word1;
#else
    return ((uint64_t)word1 << 32) | word0;
#endif
}
#else /* TARGET_ABI_BITS == 32 && !defined(TARGET_ABI_MIPSN32) */
static inline uint64_t target_offset64(uint64_t word0, uint64_t word1)
{
    return word0;
}
#endif /* TARGET_ABI_BITS != 32 */

void print_termios(void *arg);

/* ARM EABI and MIPS expect 64bit types aligned even on pairs or registers */
#ifdef TARGET_ARM
static inline int regpairs_aligned(CPUArchState *cpu_env, int num)
{
    return cpu_env->eabi == 1;
}
#elif defined(TARGET_MIPS) && defined(TARGET_ABI_MIPSO32)
static inline int regpairs_aligned(CPUArchState *cpu_env, int num) { return 1; }
#elif defined(TARGET_PPC) && !defined(TARGET_PPC64)
/*
 * SysV AVI for PPC32 expects 64bit parameters to be passed on odd/even pairs
 * of registers which translates to the same as ARM/MIPS, because we start with
 * r3 as arg1
 */
static inline int regpairs_aligned(CPUArchState *cpu_env, int num) { return 1; }
#elif defined(TARGET_SH4)
/* SH4 doesn't align register pairs, except for p{read,write}64 */
static inline int regpairs_aligned(CPUArchState *cpu_env, int num)
{
    switch (num) {
    case TARGET_NR_pread64:
    case TARGET_NR_pwrite64:
        return 1;

    default:
        return 0;
    }
}
#elif defined(TARGET_XTENSA)
static inline int regpairs_aligned(CPUArchState *cpu_env, int num) { return 1; }
#elif defined(TARGET_HEXAGON)
static inline int regpairs_aligned(CPUArchState *cpu_env, int num) { return 1; }
#else
static inline int regpairs_aligned(CPUArchState *cpu_env, int num) { return 0; }
#endif

/**
 * preexit_cleanup: housekeeping before the guest exits
 *
 * env: the CPU state
 * code: the exit code
 */
void preexit_cleanup(CPUArchState *env, int code);

/*
 * Include target-specific struct and function definitions;
 * they may need access to the target-independent structures
 * above, so include them last.
 */
#include "target_cpu.h"
#include "target_structs.h"

#endif
