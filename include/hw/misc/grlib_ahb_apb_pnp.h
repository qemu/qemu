/*
 * GRLIB AHB APB PNP
 *
 *  Copyright (C) 2019 AdaCore
 *
 *  Developed by :
 *  Frederic Konrad   <frederic.konrad@adacore.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef GRLIB_AHB_APB_PNP_H
#define GRLIB_AHB_APB_PNP_H

#define TYPE_GRLIB_AHB_PNP "grlib,ahbpnp"
#define GRLIB_AHB_PNP(obj) \
    OBJECT_CHECK(AHBPnp, (obj), TYPE_GRLIB_AHB_PNP)
typedef struct AHBPnp AHBPnp;

#define TYPE_GRLIB_APB_PNP "grlib,apbpnp"
#define GRLIB_APB_PNP(obj) \
    OBJECT_CHECK(APBPnp, (obj), TYPE_GRLIB_APB_PNP)
typedef struct APBPnp APBPnp;

void grlib_ahb_pnp_add_entry(AHBPnp *dev, uint32_t address, uint32_t mask,
                             uint8_t vendor, uint16_t device, int slave,
                             int type);
void grlib_apb_pnp_add_entry(APBPnp *dev, uint32_t address, uint32_t mask,
                             uint8_t vendor, uint16_t device, uint8_t version,
                             uint8_t irq, int type);

/* VENDORS */
#define GRLIB_VENDOR_GAISLER (0x01)
/* DEVICES */
#define GRLIB_LEON3_DEV      (0x03)
#define GRLIB_APBMST_DEV     (0x06)
#define GRLIB_APBUART_DEV    (0x0C)
#define GRLIB_IRQMP_DEV      (0x0D)
#define GRLIB_GPTIMER_DEV    (0x11)
/* TYPE */
#define GRLIB_CPU_AREA       (0x00)
#define GRLIB_APBIO_AREA     (0x01)
#define GRLIB_AHBMEM_AREA    (0x02)

#define GRLIB_AHB_MASTER     (0x00)
#define GRLIB_AHB_SLAVE      (0x01)

#endif /* GRLIB_AHB_APB_PNP_H */
