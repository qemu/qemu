/*
 * Device model for i.MX UART
 *
 * Copyright (c) 2008 OKL
 * Originally Written by Hans Jiang
 * Copyright (c) 2011 NICTA Pty Ltd.
 * Updated by Jean-Christophe Dubois <jcd@tribudubois.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef IMX_SERIAL_H
#define IMX_SERIAL_H

#include "hw/sysbus.h"
#include "sysemu/char.h"

#define TYPE_IMX_SERIAL "imx.serial"
#define IMX_SERIAL(obj) OBJECT_CHECK(IMXSerialState, (obj), TYPE_IMX_SERIAL)

#define URXD_CHARRDY    (1<<15)   /* character read is valid */
#define URXD_ERR        (1<<14)   /* Character has error */
#define URXD_BRK        (1<<11)   /* Break received */

#define USR1_PARTYER    (1<<15)   /* Parity Error */
#define USR1_RTSS       (1<<14)   /* RTS pin status */
#define USR1_TRDY       (1<<13)   /* Tx ready */
#define USR1_RTSD       (1<<12)   /* RTS delta: pin changed state */
#define USR1_ESCF       (1<<11)   /* Escape sequence interrupt */
#define USR1_FRAMERR    (1<<10)   /* Framing error  */
#define USR1_RRDY       (1<<9)    /* receiver ready */
#define USR1_AGTIM      (1<<8)    /* Aging timer interrupt */
#define USR1_DTRD       (1<<7)    /* DTR changed */
#define USR1_RXDS       (1<<6)    /* Receiver is idle */
#define USR1_AIRINT     (1<<5)    /* Aysnch IR interrupt */
#define USR1_AWAKE      (1<<4)    /* Falling edge detected on RXd pin */

#define USR2_ADET       (1<<15)   /* Autobaud complete */
#define USR2_TXFE       (1<<14)   /* Transmit FIFO empty */
#define USR2_DTRF       (1<<13)   /* DTR/DSR transition */
#define USR2_IDLE       (1<<12)   /* UART has been idle for too long */
#define USR2_ACST       (1<<11)   /* Autobaud counter stopped */
#define USR2_RIDELT     (1<<10)   /* Ring Indicator delta */
#define USR2_RIIN       (1<<9)    /* Ring Indicator Input */
#define USR2_IRINT      (1<<8)    /* Serial Infrared Interrupt */
#define USR2_WAKE       (1<<7)    /* Start bit detected */
#define USR2_DCDDELT    (1<<6)    /* Data Carrier Detect delta */
#define USR2_DCDIN      (1<<5)    /* Data Carrier Detect Input */
#define USR2_RTSF       (1<<4)    /* RTS transition */
#define USR2_TXDC       (1<<3)    /* Transmission complete */
#define USR2_BRCD       (1<<2)    /* Break condition detected */
#define USR2_ORE        (1<<1)    /* Overrun error */
#define USR2_RDR        (1<<0)    /* Receive data ready */

#define UCR1_TRDYEN     (1<<13)   /* Tx Ready Interrupt Enable */
#define UCR1_RRDYEN     (1<<9)    /* Rx Ready Interrupt Enable */
#define UCR1_TXMPTYEN   (1<<6)    /* Tx Empty Interrupt Enable */
#define UCR1_UARTEN     (1<<0)    /* UART Enable */

#define UCR2_TXEN       (1<<2)    /* Transmitter enable */
#define UCR2_RXEN       (1<<1)    /* Receiver enable */
#define UCR2_SRST       (1<<0)    /* Reset complete */

#define UTS1_TXEMPTY    (1<<6)
#define UTS1_RXEMPTY    (1<<5)
#define UTS1_TXFULL     (1<<4)
#define UTS1_RXFULL     (1<<3)

typedef struct IMXSerialState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    int32_t readbuff;

    uint32_t usr1;
    uint32_t usr2;
    uint32_t ucr1;
    uint32_t ucr2;
    uint32_t uts1;

    /*
     * The registers below are implemented just so that the
     * guest OS sees what it has written
     */
    uint32_t onems;
    uint32_t ufcr;
    uint32_t ubmr;
    uint32_t ubrc;
    uint32_t ucr3;

    qemu_irq irq;
    CharBackend chr;
} IMXSerialState;

#endif
