/* hw/s3c24xx.h
 *
 * Samsung s3c24xx cpu state and functions.
 *
 * Copyright 2006, 2007, 2008 Daniel Silverstone and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2.
 */

#ifndef S3C24XX_H
#define S3C24XX_H 1

/* This structure type encapsulates the state of a S3C24XX SoC. */
typedef struct S3CState_s {
    CPUState *cpu_env;

    /* Memory controller state */
    struct s3c24xx_memc_state_s *memc;

    /* IRQ controller state */
    struct s3c24xx_irq_state_s *irq;

} S3CState;


/* initialise memory controller peripheral */
struct s3c24xx_memc_state_s *s3c24xx_memc_init(target_phys_addr_t base_addr);

/* initialise the IRQ controller */
struct s3c24xx_irq_state_s *s3c24xx_irq_init(S3CState *soc, target_phys_addr_t base_addr);

/* get the qemu interrupt from an irq number */
qemu_irq s3c24xx_get_irq(struct s3c24xx_irq_state_s *s, int inum);


#endif /* S3C24XX_H */
