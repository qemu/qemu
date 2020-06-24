/*
 * QEMU s390-ccw firmware - jump to IPL code
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or (at
 * your option) any later version. See the COPYING file in the top-level
 * directory.
 */

#include "libc.h"
#include "s390-ccw.h"
#include "s390-arch.h"

#define KERN_IMAGE_START 0x010000UL
#define RESET_PSW_MASK (PSW_MASK_SHORTPSW | PSW_MASK_64)

typedef struct ResetInfo {
    uint64_t ipl_psw;
    uint32_t ipl_continue;
} ResetInfo;

static ResetInfo save;

static void jump_to_IPL_2(void)
{
    ResetInfo *current = 0;

    void (*ipl)(void) = (void *) (uint64_t) current->ipl_continue;
    *current = save;
    ipl(); /* should not return */
}

void jump_to_IPL_code(uint64_t address)
{
    /* store the subsystem information _after_ the bootmap was loaded */
    write_subsystem_identification();
    write_iplb_location();

    /* prevent unknown IPL types in the guest */
    if (iplb.pbt == S390_IPL_TYPE_QEMU_SCSI) {
        iplb.pbt = S390_IPL_TYPE_CCW;
        set_iplb(&iplb);
    }

    /*
     * The IPL PSW is at address 0. We also must not overwrite the
     * content of non-BIOS memory after we loaded the guest, so we
     * save the original content and restore it in jump_to_IPL_2.
     */
    ResetInfo *current = 0;

    save = *current;

    current->ipl_psw = (uint64_t) &jump_to_IPL_2;
    current->ipl_psw |= RESET_PSW_MASK;
    current->ipl_continue = address & PSW_MASK_SHORT_ADDR;

    debug_print_int("set IPL addr to", current->ipl_continue);

    /* Ensure the guest output starts fresh */
    sclp_print("\n");

    /*
     * HACK ALERT.
     * We use the load normal reset to keep r15 unchanged. jump_to_IPL_2
     * can then use r15 as its stack pointer.
     */
    asm volatile("lghi 1,1\n\t"
                 "diag 1,1,0x308\n\t"
                 : : : "1", "memory");
    panic("\n! IPL returns !\n");
}

void jump_to_low_kernel(void)
{
    /*
     * If it looks like a Linux binary, i.e. there is the "S390EP" magic from
     * arch/s390/kernel/head.S here, then let's jump to the well-known Linux
     * kernel start address (when jumping to the PSW-at-zero address instead,
     * the kernel startup code fails when we booted from a network device).
     */
    if (!memcmp((char *)0x10008, "S390EP", 6)) {
        jump_to_IPL_code(KERN_IMAGE_START);
    }

    /* Trying to get PSW at zero address */
    if (*((uint64_t *)0) & RESET_PSW_MASK) {
        jump_to_IPL_code((*((uint64_t *)0)) & PSW_MASK_SHORT_ADDR);
    }

    /* No other option left, so use the Linux kernel start address */
    jump_to_IPL_code(KERN_IMAGE_START);
}
