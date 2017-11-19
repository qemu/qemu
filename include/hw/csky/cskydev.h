/*
 * CSKY cpudev header.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HW_CSKY_CPUDEV_H
#define HW_CSKY_CPUDEV_H 1

#include "hw/dma/csky_dma.h"

/* CSKY device prototypes */

/* csky-mac.c */
void csky_mac_create(NICInfo *nd, uint32_t base, qemu_irq irq);

/* csky-mac.c */
void csky_mac_v2_create(NICInfo *nd, uint32_t base, qemu_irq irq);

/* csky_iis.c */
void csky_iis_create(const char *name, hwaddr addr, qemu_irq irq,
                     csky_dma_state *dma);


/* csky_intc.c*/
qemu_irq *csky_intc_init_cpu(CPUCSKYState *env);

/* csky_tcip_v1.c*/
qemu_irq *csky_vic_v1_init_cpu(CPUCSKYState *env, int coret_irq_num);

/* csky_tcip_v1.c*/
void csky_tcip_v1_set_freq(uint32_t freq);

/* csky_timer.c*/
void csky_timer_set_freq(uint32_t freq);

#endif
