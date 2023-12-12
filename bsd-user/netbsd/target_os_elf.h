/*
 *  netbsd ELF definitions
 *
 *  Copyright (c) 2013 Stacey D. Son
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

#ifndef TARGET_OS_ELF_H
#define TARGET_OS_ELF_H

#include "target_arch_elf.h"
#include "elf.h"
#include "user/tswap-target.h"

/* this flag is uneffective under linux too, should be deleted */
#ifndef MAP_DENYWRITE
#define MAP_DENYWRITE 0
#endif

/* should probably go in elf.h */
#ifndef ELIBBAD
#define ELIBBAD 80
#endif

#ifndef ELF_PLATFORM
#define ELF_PLATFORM (NULL)
#endif

#ifndef ELF_HWCAP
#define ELF_HWCAP 0
#endif

#ifdef TARGET_ABI32
#undef ELF_CLASS
#define ELF_CLASS ELFCLASS32
#undef bswaptls
#define bswaptls(ptr) bswap32s(ptr)
#endif

/* max code+data+bss space allocated to elf interpreter */
#define INTERP_MAP_SIZE (32 * 1024 * 1024)

/* max code+data+bss+brk space allocated to ET_DYN executables */
#define ET_DYN_MAP_SIZE (128 * 1024 * 1024)

/* Necessary parameters */
#define TARGET_ELF_EXEC_PAGESIZE TARGET_PAGE_SIZE
#define TARGET_ELF_PAGESTART(_v) ((_v) & \
        ~(unsigned long)(TARGET_ELF_EXEC_PAGESIZE - 1))
#define TARGET_ELF_PAGEOFFSET(_v) ((_v) & (TARGET_ELF_EXEC_PAGESIZE - 1))

#define DLINFO_ITEMS 12

static abi_ulong target_create_elf_tables(abi_ulong p, int argc, int envc,
                                          abi_ulong stringp,
                                          struct elfhdr *exec,
                                          abi_ulong load_addr,
                                          abi_ulong load_bias,
                                          abi_ulong interp_load_addr,
                                          struct image_info *info)
{
        abi_ulong sp;
        int size;
        abi_ulong u_platform;
        const char *k_platform;
        const int n = sizeof(elf_addr_t);

        sp = p;
        u_platform = 0;
        k_platform = ELF_PLATFORM;
        if (k_platform) {
            size_t len = strlen(k_platform) + 1;
            sp -= (len + n - 1) & ~(n - 1);
            u_platform = sp;
            /* FIXME - check return value of memcpy_to_target() for failure */
            memcpy_to_target(sp, k_platform, len);
        }
        /*
         * Force 16 byte _final_ alignment here for generality.
         */
        sp = sp & ~(abi_ulong)15;
        size = (DLINFO_ITEMS + 1) * 2;
        if (k_platform) {
            size += 2;
        }
#ifdef DLINFO_ARCH_ITEMS
        size += DLINFO_ARCH_ITEMS * 2;
#endif
        size += envc + argc + 2;
        size += 1;                         /* argc itself */
        size *= n;
        if (size & 15) {
            sp -= 16 - (size & 15);
        }

        /*
         * NetBSD defines elf_addr_t as Elf32_Off / Elf64_Off
         */
#define NEW_AUX_ENT(id, val) do {               \
            sp -= n; put_user_ual(val, sp);     \
            sp -= n; put_user_ual(id, sp);      \
          } while (0)

        NEW_AUX_ENT(AT_NULL, 0);

        /* There must be exactly DLINFO_ITEMS entries here.  */
        NEW_AUX_ENT(AT_PHDR, (abi_ulong)(load_addr + exec->e_phoff));
        NEW_AUX_ENT(AT_PHENT, (abi_ulong)(sizeof(struct elf_phdr)));
        NEW_AUX_ENT(AT_PHNUM, (abi_ulong)(exec->e_phnum));
        NEW_AUX_ENT(AT_PAGESZ, (abi_ulong)(TARGET_PAGE_SIZE));
        NEW_AUX_ENT(AT_BASE, (abi_ulong)(interp_load_addr));
        NEW_AUX_ENT(AT_FLAGS, (abi_ulong)0);
        NEW_AUX_ENT(AT_ENTRY, load_bias + exec->e_entry);
        NEW_AUX_ENT(AT_UID, (abi_ulong)getuid());
        NEW_AUX_ENT(AT_EUID, (abi_ulong)geteuid());
        NEW_AUX_ENT(AT_GID, (abi_ulong)getgid());
        NEW_AUX_ENT(AT_EGID, (abi_ulong)getegid());
        NEW_AUX_ENT(AT_HWCAP, (abi_ulong)ELF_HWCAP);
        NEW_AUX_ENT(AT_CLKTCK, (abi_ulong)sysconf(_SC_CLK_TCK));
        if (k_platform) {
            NEW_AUX_ENT(AT_PLATFORM, u_platform);
        }
#ifdef ARCH_DLINFO
        /*
         * ARCH_DLINFO must come last so platform specific code can enforce
         * special alignment requirements on the AUXV if necessary (eg. PPC).
         */
        ARCH_DLINFO;
#endif
#undef NEW_AUX_ENT

        sp = loader_build_argptr(envc, argc, sp, stringp);
        return sp;
}

#endif /* TARGET_OS_ELF_H */
