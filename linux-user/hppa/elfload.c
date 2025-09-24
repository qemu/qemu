/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"


const char *get_elf_cpu_model(uint32_t eflags)
{
    return "hppa";
}

const char *get_elf_platform(CPUState *cs)
{
    return "PARISC";
}

bool init_guest_commpage(void)
{
    /* If reserved_va, then we have already mapped 0 page on the host. */
    if (!reserved_va) {
        void *want, *addr;

        want = g2h_untagged(LO_COMMPAGE);
        addr = mmap(want, TARGET_PAGE_SIZE, PROT_NONE,
                    MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE, -1, 0);
        if (addr == MAP_FAILED) {
            perror("Allocating guest commpage");
            exit(EXIT_FAILURE);
        }
        if (addr != want) {
            return false;
        }
    }

    /*
     * On Linux, page zero is normally marked execute only + gateway.
     * Normal read or write is supposed to fail (thus PROT_NONE above),
     * but specific offsets have kernel code mapped to raise permissions
     * and implement syscalls.  Here, simply mark the page executable.
     * Special case the entry points during translation (see do_page_zero).
     */
    page_set_flags(LO_COMMPAGE, LO_COMMPAGE | ~TARGET_PAGE_MASK,
                   PAGE_EXEC | PAGE_VALID, PAGE_VALID);
    return true;
}
