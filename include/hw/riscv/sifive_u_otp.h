/*
 * QEMU SiFive U OTP (One-Time Programmable) Memory interface
 *
 * Copyright (c) 2019 Bin Meng <bmeng.cn@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_SIFIVE_U_OTP_H
#define HW_SIFIVE_U_OTP_H

#define SIFIVE_U_OTP_PA         0x00
#define SIFIVE_U_OTP_PAIO       0x04
#define SIFIVE_U_OTP_PAS        0x08
#define SIFIVE_U_OTP_PCE        0x0C
#define SIFIVE_U_OTP_PCLK       0x10
#define SIFIVE_U_OTP_PDIN       0x14
#define SIFIVE_U_OTP_PDOUT      0x18
#define SIFIVE_U_OTP_PDSTB      0x1C
#define SIFIVE_U_OTP_PPROG      0x20
#define SIFIVE_U_OTP_PTC        0x24
#define SIFIVE_U_OTP_PTM        0x28
#define SIFIVE_U_OTP_PTM_REP    0x2C
#define SIFIVE_U_OTP_PTR        0x30
#define SIFIVE_U_OTP_PTRIM      0x34
#define SIFIVE_U_OTP_PWE        0x38

#define SIFIVE_U_OTP_PCE_EN     (1 << 0)

#define SIFIVE_U_OTP_PDSTB_EN   (1 << 0)

#define SIFIVE_U_OTP_PTRIM_EN   (1 << 0)

#define SIFIVE_U_OTP_PA_MASK        0xfff
#define SIFIVE_U_OTP_NUM_FUSES      0x1000
#define SIFIVE_U_OTP_SERIAL_ADDR    0xfc

#define SIFIVE_U_OTP_REG_SIZE       0x1000

#define TYPE_SIFIVE_U_OTP           "riscv.sifive.u.otp"

#define SIFIVE_U_OTP(obj) \
    OBJECT_CHECK(SiFiveUOTPState, (obj), TYPE_SIFIVE_U_OTP)

typedef struct SiFiveUOTPState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    uint32_t pa;
    uint32_t paio;
    uint32_t pas;
    uint32_t pce;
    uint32_t pclk;
    uint32_t pdin;
    uint32_t pdstb;
    uint32_t pprog;
    uint32_t ptc;
    uint32_t ptm;
    uint32_t ptm_rep;
    uint32_t ptr;
    uint32_t ptrim;
    uint32_t pwe;
    uint32_t fuse[SIFIVE_U_OTP_NUM_FUSES];
    /* config */
    uint32_t serial;
} SiFiveUOTPState;

#endif /* HW_SIFIVE_U_OTP_H */
