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
#define RESET_PSW ((uint64_t)&jump_to_IPL_addr | RESET_PSW_MASK)

static uint64_t *reset_psw = 0, save_psw, ipl_continue;

void write_reset_psw(uint64_t psw)
{
    *reset_psw = psw;
}

static void jump_to_IPL_addr(void)
{
    __attribute__((noreturn)) void (*ipl)(void) = (void *)ipl_continue;

    /* Restore reset PSW */
    write_reset_psw(save_psw);

    ipl();
    /* should not return */
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
    if (address) {
        save_psw = *reset_psw;
        write_reset_psw(RESET_PSW);
        ipl_continue = address;
    }
    debug_print_int("set IPL addr to", address ?: *reset_psw & PSW_MASK_SHORT_ADDR);

    /* Ensure the guest output starts fresh */
    sclp_print("\n");

    /*
     * HACK ALERT.
     * We use the load normal reset to keep r15 unchanged. jump_to_IPL_2
     * can then use r15 as its stack pointer.
     */
    asm volatile("lghi %%r1,1\n\t"
                 "diag %%r1,%%r1,0x308\n\t"
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
    if (!memcmp((char *)S390EP, "S390EP", 6)) {
        jump_to_IPL_code(KERN_IMAGE_START);
    }

    /* Trying to get PSW at zero address (pointed to by reset_psw) */
    if (*reset_psw & RESET_PSW_MASK) {
        /*
         * Surely nobody will try running directly from lowcore, so
         * let's use 0 as an indication that we want to load the reset
         * psw at 0x0 and not jump to the entry.
         */
        jump_to_IPL_code(0);
    }

    /* No other option left, so use the Linux kernel start address */
    jump_to_IPL_code(KERN_IMAGE_START);
}
