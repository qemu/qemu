#ifndef PRIMECELL_H
#define PRIMECELL_H

/* Declarations for ARM PrimeCell based periperals.  */
/* Also includes some devices that are currently only used by the
   ARM boards.  */

/* pl080.c */
void *pl080_init(uint32_t base, qemu_irq irq, int nchannels);

/* arm_sysctl.c */
void arm_sysctl_init(uint32_t base, uint32_t sys_id);

#endif
