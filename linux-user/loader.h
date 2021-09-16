/*
 * loader.h: prototypes for linux-user guest binary loader
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

#ifndef LINUX_USER_LOADER_H
#define LINUX_USER_LOADER_H

/*
 * Read a good amount of data initially, to hopefully get all the
 * program headers loaded.
 */
#define BPRM_BUF_SIZE  1024

/*
 * This structure is used to hold the arguments that are
 * used when loading binaries.
 */
struct linux_binprm {
        char buf[BPRM_BUF_SIZE] __attribute__((aligned));
        abi_ulong p;
        int fd;
        int e_uid, e_gid;
        int argc, envc;
        char **argv;
        char **envp;
        char *filename;        /* Name of binary */
        int (*core_dump)(int, const CPUArchState *); /* coredump routine */
};

void do_init_thread(struct target_pt_regs *regs, struct image_info *infop);
abi_ulong loader_build_argptr(int envc, int argc, abi_ulong sp,
                              abi_ulong stringp, int push_ptr);
int loader_exec(int fdexec, const char *filename, char **argv, char **envp,
             struct target_pt_regs *regs, struct image_info *infop,
             struct linux_binprm *);

uint32_t get_elf_eflags(int fd);
int load_elf_binary(struct linux_binprm *bprm, struct image_info *info);
int load_flt_binary(struct linux_binprm *bprm, struct image_info *info);

abi_long memcpy_to_target(abi_ulong dest, const void *src,
                          unsigned long len);

extern unsigned long guest_stack_size;

#endif /* LINUX_USER_LOADER_H */
