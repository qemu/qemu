/*
 * QEMU Freescale eTSEC Emulator
 *
 * Copyright (c) 2011-2013 AdaCore
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "etsec.h"
#include "registers.h"

/* #define DEBUG_MIIM */

#define MIIM_CONTROL    0
#define MIIM_STATUS     1
#define MIIM_PHY_ID_1   2
#define MIIM_PHY_ID_2   3
#define MIIM_T2_STATUS  10
#define MIIM_EXT_STATUS 15

static void miim_read_cycle(eTSEC *etsec)
{
    uint8_t  phy;
    uint8_t  addr;
    uint16_t value;

    phy  = (etsec->regs[MIIMADD].value >> 8) & 0x1F;
    (void)phy; /* Unreferenced */
    addr = etsec->regs[MIIMADD].value & 0x1F;

    switch (addr) {
    case MIIM_CONTROL:
        value = etsec->phy_control;
        break;
    case MIIM_STATUS:
        value = etsec->phy_status;
        break;
    case MIIM_T2_STATUS:
        value = 0x1800;           /* Local and remote receivers OK */
        break;
    default:
        value = 0x0;
        break;
    };

#ifdef DEBUG_MIIM
    qemu_log("%s phy:%d addr:0x%x value:0x%x\n", __func__, phy, addr, value);
#endif

    etsec->regs[MIIMSTAT].value = value;
}

static void miim_write_cycle(eTSEC *etsec)
{
    uint8_t  phy;
    uint8_t  addr;
    uint16_t value;

    phy   = (etsec->regs[MIIMADD].value >> 8) & 0x1F;
    (void)phy; /* Unreferenced */
    addr  = etsec->regs[MIIMADD].value & 0x1F;
    value = etsec->regs[MIIMCON].value & 0xffff;

#ifdef DEBUG_MIIM
    qemu_log("%s phy:%d addr:0x%x value:0x%x\n", __func__, phy, addr, value);
#endif

    switch (addr) {
    case MIIM_CONTROL:
        etsec->phy_control = value & ~(0x8100);
        break;
    default:
        break;
    };
}

void etsec_write_miim(eTSEC          *etsec,
                      eTSEC_Register *reg,
                      uint32_t        reg_index,
                      uint32_t        value)
{

    switch (reg_index) {

    case MIIMCOM:
        /* Read and scan cycle */

        if ((!(reg->value & MIIMCOM_READ)) && (value & MIIMCOM_READ)) {
            /* Read */
            miim_read_cycle(etsec);
        }
        reg->value = value;
        break;

    case MIIMCON:
        reg->value = value & 0xffff;
        miim_write_cycle(etsec);
        break;

    default:
        /* Default handling */
        switch (reg->access) {

        case ACC_RW:
        case ACC_WO:
            reg->value = value;
            break;

        case ACC_W1C:
            reg->value &= ~value;
            break;

        case ACC_RO:
        default:
            /* Read Only or Unknown register */
            break;
        }
    }

}

void etsec_miim_link_status(eTSEC *etsec, NetClientState *nc)
{
    /* Set link status */
    if (nc->link_down) {
        etsec->phy_status &= ~MII_SR_LINK_STATUS;
    } else {
        etsec->phy_status |= MII_SR_LINK_STATUS;
    }
}
