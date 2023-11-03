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

typedef struct {
    const void *cache;
    unsigned int cache_size;
    int fd;
} ImageSource;

/**
 * imgsrc_read: Read from ImageSource
 * @dst: destination for read
 * @offset: offset within file for read
 * @len: size of the read
 * @img: ImageSource to read from
 * @errp: Error details.
 *
 * Read into @dst, using the cache when possible.
 */
bool imgsrc_read(void *dst, off_t offset, size_t len,
                 const ImageSource *img, Error **errp);

/**
 * imgsrc_read_alloc: Read from ImageSource
 * @offset: offset within file for read
 * @size: size of the read
 * @img: ImageSource to read from
 * @errp: Error details.
 *
 * Read into newly allocated memory, using the cache when possible.
 */
void *imgsrc_read_alloc(off_t offset, size_t len,
                        const ImageSource *img, Error **errp);

/**
 * imgsrc_mmap: Map from ImageSource
 *
 * If @src has a file descriptor, pass on to target_mmap.  Otherwise,
 * this is "mapping" from a host buffer, which resolves to memcpy.
 * Therefore, flags must be MAP_PRIVATE | MAP_FIXED; the argument is
 * retained for clarity.
 */
abi_long imgsrc_mmap(abi_ulong start, abi_ulong len, int prot,
                     int flags, const ImageSource *src, abi_ulong offset);

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
    ImageSource src;
    abi_ulong p;
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

#if defined(TARGET_S390X) || defined(TARGET_AARCH64) || defined(TARGET_ARM)
uint32_t get_elf_hwcap(void);
const char *elf_hwcap_str(uint32_t bit);
#endif
#if defined(TARGET_AARCH64) || defined(TARGET_ARM)
uint64_t get_elf_hwcap2(void);
const char *elf_hwcap2_str(uint32_t bit);
#endif

#endif /* LINUX_USER_LOADER_H */
