/*
 * STM32L4X5 USART (Universal Synchronous Asynchronous Receiver Transmitter)
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * The STM32L4X5 USART is heavily inspired by the stm32f2xx_usart
 * by Alistair Francis.
 * The reference used is the STMicroElectronics RM0351 Reference manual
 * for STM32L4x5 and STM32L4x6 advanced Arm ® -based 32-bit MCUs.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "migration/vmstate.h"
#include "hw/char/stm32l4x5_usart.h"
#include "hw/clock.h"
#include "hw/irq.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/registerfields.h"
#include "trace.h"


REG32(CR1, 0x00)
    FIELD(CR1, M1, 28, 1)    /* Word length (part 2, see M0) */
    FIELD(CR1, EOBIE, 27, 1) /* End of Block interrupt enable */
    FIELD(CR1, RTOIE, 26, 1) /* Receiver timeout interrupt enable */
    FIELD(CR1, DEAT, 21, 5)  /* Driver Enable assertion time */
    FIELD(CR1, DEDT, 16, 5)  /* Driver Enable de-assertion time */
    FIELD(CR1, OVER8, 15, 1) /* Oversampling mode */
    FIELD(CR1, CMIE, 14, 1)  /* Character match interrupt enable */
    FIELD(CR1, MME, 13, 1)   /* Mute mode enable */
    FIELD(CR1, M0, 12, 1)    /* Word length (part 1, see M1) */
    FIELD(CR1, WAKE, 11, 1)  /* Receiver wakeup method */
    FIELD(CR1, PCE, 10, 1)   /* Parity control enable */
    FIELD(CR1, PS, 9, 1)     /* Parity selection */
    FIELD(CR1, PEIE, 8, 1)   /* PE interrupt enable */
    FIELD(CR1, TXEIE, 7, 1)  /* TXE interrupt enable */
    FIELD(CR1, TCIE, 6, 1)   /* Transmission complete interrupt enable */
    FIELD(CR1, RXNEIE, 5, 1) /* RXNE interrupt enable */
    FIELD(CR1, IDLEIE, 4, 1) /* IDLE interrupt enable */
    FIELD(CR1, TE, 3, 1)     /* Transmitter enable */
    FIELD(CR1, RE, 2, 1)     /* Receiver enable */
    FIELD(CR1, UESM, 1, 1)   /* USART enable in Stop mode */
    FIELD(CR1, UE, 0, 1)     /* USART enable */
REG32(CR2, 0x04)
    FIELD(CR2, ADD_1, 28, 4)    /* ADD[7:4] */
    FIELD(CR2, ADD_0, 24, 4)    /* ADD[3:0] */
    FIELD(CR2, RTOEN, 23, 1)    /* Receiver timeout enable */
    FIELD(CR2, ABRMOD, 21, 2)   /* Auto baud rate mode */
    FIELD(CR2, ABREN, 20, 1)    /* Auto baud rate enable */
    FIELD(CR2, MSBFIRST, 19, 1) /* Most significant bit first */
    FIELD(CR2, DATAINV, 18, 1)  /* Binary data inversion */
    FIELD(CR2, TXINV, 17, 1)    /* TX pin active level inversion */
    FIELD(CR2, RXINV, 16, 1)    /* RX pin active level inversion */
    FIELD(CR2, SWAP, 15, 1)     /* Swap RX/TX pins */
    FIELD(CR2, LINEN, 14, 1)    /* LIN mode enable */
    FIELD(CR2, STOP, 12, 2)     /* STOP bits */
    FIELD(CR2, CLKEN, 11, 1)    /* Clock enable */
    FIELD(CR2, CPOL, 10, 1)     /* Clock polarity */
    FIELD(CR2, CPHA, 9, 1)      /* Clock phase */
    FIELD(CR2, LBCL, 8, 1)      /* Last bit clock pulse */
    FIELD(CR2, LBDIE, 6, 1)     /* LIN break detection interrupt enable */
    FIELD(CR2, LBDL, 5, 1)      /* LIN break detection length */
    FIELD(CR2, ADDM7, 4, 1)     /* 7-bit / 4-bit Address Detection */

REG32(CR3, 0x08)
    /* TCBGTIE only on STM32L496xx/4A6xx devices */
    FIELD(CR3, UCESM, 23, 1)   /* USART Clock Enable in Stop Mode */
    FIELD(CR3, WUFIE, 22, 1)   /* Wakeup from Stop mode interrupt enable */
    FIELD(CR3, WUS, 20, 2)     /* Wakeup from Stop mode interrupt flag selection */
    FIELD(CR3, SCARCNT, 17, 3) /* Smartcard auto-retry count */
    FIELD(CR3, DEP, 15, 1)     /* Driver enable polarity selection */
    FIELD(CR3, DEM, 14, 1)     /* Driver enable mode */
    FIELD(CR3, DDRE, 13, 1)    /* DMA Disable on Reception Error */
    FIELD(CR3, OVRDIS, 12, 1)  /* Overrun Disable */
    FIELD(CR3, ONEBIT, 11, 1)  /* One sample bit method enable */
    FIELD(CR3, CTSIE, 10, 1)   /* CTS interrupt enable */
    FIELD(CR3, CTSE, 9, 1)     /* CTS enable */
    FIELD(CR3, RTSE, 8, 1)     /* RTS enable */
    FIELD(CR3, DMAT, 7, 1)     /* DMA enable transmitter */
    FIELD(CR3, DMAR, 6, 1)     /* DMA enable receiver */
    FIELD(CR3, SCEN, 5, 1)     /* Smartcard mode enable */
    FIELD(CR3, NACK, 4, 1)     /* Smartcard NACK enable */
    FIELD(CR3, HDSEL, 3, 1)    /* Half-duplex selection */
    FIELD(CR3, IRLP, 2, 1)     /* IrDA low-power */
    FIELD(CR3, IREN, 1, 1)     /* IrDA mode enable */
    FIELD(CR3, EIE, 0, 1)      /* Error interrupt enable */
REG32(BRR, 0x0C)
    FIELD(BRR, BRR, 0, 16)
REG32(GTPR, 0x10)
    FIELD(GTPR, GT, 8, 8)  /* Guard time value */
    FIELD(GTPR, PSC, 0, 8) /* Prescaler value */
REG32(RTOR, 0x14)
    FIELD(RTOR, BLEN, 24, 8) /* Block Length */
    FIELD(RTOR, RTO, 0, 24)  /* Receiver timeout value */
REG32(RQR, 0x18)
    FIELD(RQR, TXFRQ, 4, 1)  /* Transmit data flush request */
    FIELD(RQR, RXFRQ, 3, 1)  /* Receive data flush request */
    FIELD(RQR, MMRQ, 2, 1)   /* Mute mode request */
    FIELD(RQR, SBKRQ, 1, 1)  /* Send break request */
    FIELD(RQR, ABBRRQ, 0, 1) /* Auto baud rate request */
REG32(ISR, 0x1C)
    /* TCBGT only for STM32L475xx/476xx/486xx devices */
    FIELD(ISR, REACK, 22, 1) /* Receive enable acknowledge flag */
    FIELD(ISR, TEACK, 21, 1) /* Transmit enable acknowledge flag */
    FIELD(ISR, WUF, 20, 1)   /* Wakeup from Stop mode flag */
    FIELD(ISR, RWU, 19, 1)   /* Receiver wakeup from Mute mode */
    FIELD(ISR, SBKF, 18, 1)  /* Send break flag */
    FIELD(ISR, CMF, 17, 1)   /* Character match flag */
    FIELD(ISR, BUSY, 16, 1)  /* Busy flag */
    FIELD(ISR, ABRF, 15, 1)  /* Auto Baud rate flag */
    FIELD(ISR, ABRE, 14, 1)  /* Auto Baud rate error */
    FIELD(ISR, EOBF, 12, 1)  /* End of block flag */
    FIELD(ISR, RTOF, 11, 1)  /* Receiver timeout */
    FIELD(ISR, CTS, 10, 1)   /* CTS flag */
    FIELD(ISR, CTSIF, 9, 1)  /* CTS interrupt flag */
    FIELD(ISR, LBDF, 8, 1)   /* LIN break detection flag */
    FIELD(ISR, TXE, 7, 1)    /* Transmit data register empty */
    FIELD(ISR, TC, 6, 1)     /* Transmission complete */
    FIELD(ISR, RXNE, 5, 1)   /* Read data register not empty */
    FIELD(ISR, IDLE, 4, 1)   /* Idle line detected */
    FIELD(ISR, ORE, 3, 1)    /* Overrun error */
    FIELD(ISR, NF, 2, 1)     /* START bit Noise detection flag */
    FIELD(ISR, FE, 1, 1)     /* Framing Error */
    FIELD(ISR, PE, 0, 1)     /* Parity Error */
REG32(ICR, 0x20)
    FIELD(ICR, WUCF, 20, 1)   /* Wakeup from Stop mode clear flag */
    FIELD(ICR, CMCF, 17, 1)   /* Character match clear flag */
    FIELD(ICR, EOBCF, 12, 1)  /* End of block clear flag */
    FIELD(ICR, RTOCF, 11, 1)  /* Receiver timeout clear flag */
    FIELD(ICR, CTSCF, 9, 1)   /* CTS clear flag */
    FIELD(ICR, LBDCF, 8, 1)   /* LIN break detection clear flag */
    /* TCBGTCF only on STM32L496xx/4A6xx devices */
    FIELD(ICR, TCCF, 6, 1)    /* Transmission complete clear flag */
    FIELD(ICR, IDLECF, 4, 1)  /* Idle line detected clear flag */
    FIELD(ICR, ORECF, 3, 1)   /* Overrun error clear flag */
    FIELD(ICR, NCF, 2, 1)     /* Noise detected clear flag */
    FIELD(ICR, FECF, 1, 1)    /* Framing error clear flag */
    FIELD(ICR, PECF, 0, 1)    /* Parity error clear flag */
REG32(RDR, 0x24)
    FIELD(RDR, RDR, 0, 9)
REG32(TDR, 0x28)
    FIELD(TDR, TDR, 0, 9)

static void stm32l4x5_update_isr(Stm32l4x5UsartBaseState *s)
{
    if (s->cr1 & R_CR1_TE_MASK) {
        s->isr |= R_ISR_TEACK_MASK;
    } else {
        s->isr &= ~R_ISR_TEACK_MASK;
    }

    if (s->cr1 & R_CR1_RE_MASK) {
        s->isr |= R_ISR_REACK_MASK;
    } else {
        s->isr &= ~R_ISR_REACK_MASK;
    }
}

static void stm32l4x5_update_irq(Stm32l4x5UsartBaseState *s)
{
    if (((s->isr & R_ISR_WUF_MASK) && (s->cr3 & R_CR3_WUFIE_MASK))        ||
        ((s->isr & R_ISR_CMF_MASK) && (s->cr1 & R_CR1_CMIE_MASK))         ||
        ((s->isr & R_ISR_ABRF_MASK) && (s->cr1 & R_CR1_RXNEIE_MASK))      ||
        ((s->isr & R_ISR_EOBF_MASK) && (s->cr1 & R_CR1_EOBIE_MASK))       ||
        ((s->isr & R_ISR_RTOF_MASK) && (s->cr1 & R_CR1_RTOIE_MASK))       ||
        ((s->isr & R_ISR_CTSIF_MASK) && (s->cr3 & R_CR3_CTSIE_MASK))      ||
        ((s->isr & R_ISR_LBDF_MASK) && (s->cr2 & R_CR2_LBDIE_MASK))       ||
        ((s->isr & R_ISR_TXE_MASK) && (s->cr1 & R_CR1_TXEIE_MASK))        ||
        ((s->isr & R_ISR_TC_MASK) && (s->cr1 & R_CR1_TCIE_MASK))          ||
        ((s->isr & R_ISR_RXNE_MASK) && (s->cr1 & R_CR1_RXNEIE_MASK))      ||
        ((s->isr & R_ISR_IDLE_MASK) && (s->cr1 & R_CR1_IDLEIE_MASK))      ||
        ((s->isr & R_ISR_ORE_MASK) &&
            ((s->cr1 & R_CR1_RXNEIE_MASK) || (s->cr3 & R_CR3_EIE_MASK)))  ||
        /* TODO: Handle NF ? */
        ((s->isr & R_ISR_FE_MASK) && (s->cr3 & R_CR3_EIE_MASK))           ||
        ((s->isr & R_ISR_PE_MASK) && (s->cr1 & R_CR1_PEIE_MASK))) {
        qemu_irq_raise(s->irq);
        trace_stm32l4x5_usart_irq_raised(s->isr);
    } else {
        qemu_irq_lower(s->irq);
        trace_stm32l4x5_usart_irq_lowered();
    }
}

static int stm32l4x5_usart_base_can_receive(void *opaque)
{
    Stm32l4x5UsartBaseState *s = opaque;

    if (!(s->isr & R_ISR_RXNE_MASK)) {
        return 1;
    }

    return 0;
}

static void stm32l4x5_usart_base_receive(void *opaque, const uint8_t *buf,
                                         int size)
{
    Stm32l4x5UsartBaseState *s = opaque;

    if (!((s->cr1 & R_CR1_UE_MASK) && (s->cr1 & R_CR1_RE_MASK))) {
        trace_stm32l4x5_usart_receiver_not_enabled(
            FIELD_EX32(s->cr1, CR1, UE), FIELD_EX32(s->cr1, CR1, RE));
        return;
    }

    /* Check if overrun detection is enabled and if there is an overrun */
    if (!(s->cr3 & R_CR3_OVRDIS_MASK) && (s->isr & R_ISR_RXNE_MASK)) {
        /*
         * A character has been received while
         * the previous has not been read = Overrun.
         */
        s->isr |= R_ISR_ORE_MASK;
        trace_stm32l4x5_usart_overrun_detected(s->rdr, *buf);
    } else {
        /* No overrun */
        s->rdr = *buf;
        s->isr |= R_ISR_RXNE_MASK;
        trace_stm32l4x5_usart_rx(s->rdr);
    }

    stm32l4x5_update_irq(s);
}

/*
 * Try to send tx data, and arrange to be called back later if
 * we can't (ie the char backend is busy/blocking).
 */
static gboolean usart_transmit(void *do_not_use, GIOCondition cond,
                               void *opaque)
{
    Stm32l4x5UsartBaseState *s = STM32L4X5_USART_BASE(opaque);
    int ret;
    /* TODO: Handle 9 bits transmission */
    uint8_t ch = s->tdr;

    s->watch_tag = 0;

    if (!(s->cr1 & R_CR1_TE_MASK) || (s->isr & R_ISR_TXE_MASK)) {
        return G_SOURCE_REMOVE;
    }

    ret = qemu_chr_fe_write(&s->chr, &ch, 1);
    if (ret <= 0) {
        s->watch_tag = qemu_chr_fe_add_watch(&s->chr, G_IO_OUT | G_IO_HUP,
                                             usart_transmit, s);
        if (!s->watch_tag) {
            /*
             * Most common reason to be here is "no chardev backend":
             * just insta-drain the buffer, so the serial output
             * goes into a void, rather than blocking the guest.
             */
            goto buffer_drained;
        }
        /* Transmit pending */
        trace_stm32l4x5_usart_tx_pending();
        return G_SOURCE_REMOVE;
    }

buffer_drained:
    /* Character successfully sent */
    trace_stm32l4x5_usart_tx(ch);
    s->isr |= R_ISR_TC_MASK | R_ISR_TXE_MASK;
    stm32l4x5_update_irq(s);
    return G_SOURCE_REMOVE;
}

static void usart_cancel_transmit(Stm32l4x5UsartBaseState *s)
{
    if (s->watch_tag) {
        g_source_remove(s->watch_tag);
        s->watch_tag = 0;
    }
}

static void stm32l4x5_update_params(Stm32l4x5UsartBaseState *s)
{
    int speed, parity, data_bits, stop_bits;
    uint32_t value, usart_div;
    QEMUSerialSetParams ssp;

    /* Select the parity type */
    if (s->cr1 & R_CR1_PCE_MASK) {
        if (s->cr1 & R_CR1_PS_MASK) {
            parity = 'O';
        } else {
            parity = 'E';
        }
    } else {
        parity = 'N';
    }

    /* Select the number of stop bits */
    switch (FIELD_EX32(s->cr2, CR2, STOP)) {
    case 0:
        stop_bits = 1;
        break;
    case 2:
        stop_bits = 2;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
            "UNIMPLEMENTED: fractionnal stop bits; CR2[13:12] = %u",
            FIELD_EX32(s->cr2, CR2, STOP));
        return;
    }

    /* Select the length of the word */
    switch ((FIELD_EX32(s->cr1, CR1, M1) << 1) | FIELD_EX32(s->cr1, CR1, M0)) {
    case 0:
        data_bits = 8;
        break;
    case 1:
        data_bits = 9;
        break;
    case 2:
        data_bits = 7;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "UNDEFINED: invalid word length, CR1.M = 0b11");
        return;
    }

    /* Select the baud rate */
    value = FIELD_EX32(s->brr, BRR, BRR);
    if (value < 16) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "UNDEFINED: BRR less than 16: %u", value);
        return;
    }

    if (FIELD_EX32(s->cr1, CR1, OVER8) == 0) {
        /*
         * Oversampling by 16
         * BRR = USARTDIV
         */
        usart_div = value;
    } else {
        /*
         * Oversampling by 8
         * - BRR[2:0] = USARTDIV[3:0] shifted 1 bit to the right.
         * - BRR[3] must be kept cleared.
         * - BRR[15:4] = USARTDIV[15:4]
         * - The frequency is multiplied by 2
         */
        usart_div = ((value & 0xFFF0) | ((value & 0x0007) << 1)) / 2;
    }

    speed = clock_get_hz(s->clk) / usart_div;

    ssp.speed     = speed;
    ssp.parity    = parity;
    ssp.data_bits = data_bits;
    ssp.stop_bits = stop_bits;

    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &ssp);

    trace_stm32l4x5_usart_update_params(speed, parity, data_bits, stop_bits);
}

static void stm32l4x5_usart_base_reset_hold(Object *obj, ResetType type)
{
    Stm32l4x5UsartBaseState *s = STM32L4X5_USART_BASE(obj);

    s->cr1 = 0x00000000;
    s->cr2 = 0x00000000;
    s->cr3 = 0x00000000;
    s->brr = 0x00000000;
    s->gtpr = 0x00000000;
    s->rtor = 0x00000000;
    s->isr = 0x020000C0;
    s->rdr = 0x00000000;
    s->tdr = 0x00000000;

    usart_cancel_transmit(s);
    stm32l4x5_update_irq(s);
}

static void usart_update_rqr(Stm32l4x5UsartBaseState *s, uint32_t value)
{
    /* TXFRQ */
    /* Reset RXNE flag */
    if (value & R_RQR_RXFRQ_MASK) {
        s->isr &= ~R_ISR_RXNE_MASK;
    }
    /* MMRQ */
    /* SBKRQ */
    /* ABRRQ */
    stm32l4x5_update_irq(s);
}

static uint64_t stm32l4x5_usart_base_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    Stm32l4x5UsartBaseState *s = opaque;
    uint64_t retvalue = 0;

    switch (addr) {
    case A_CR1:
        retvalue = s->cr1;
        break;
    case A_CR2:
        retvalue = s->cr2;
        break;
    case A_CR3:
        retvalue = s->cr3;
        break;
    case A_BRR:
        retvalue = FIELD_EX32(s->brr, BRR, BRR);
        break;
    case A_GTPR:
        retvalue = s->gtpr;
        break;
    case A_RTOR:
        retvalue = s->rtor;
        break;
    case A_RQR:
        /* RQR is a write only register */
        retvalue = 0x00000000;
        break;
    case A_ISR:
        retvalue = s->isr;
        break;
    case A_ICR:
        /* ICR is a clear register */
        retvalue = 0x00000000;
        break;
    case A_RDR:
        retvalue = FIELD_EX32(s->rdr, RDR, RDR);
        /* Reset RXNE flag */
        s->isr &= ~R_ISR_RXNE_MASK;
        stm32l4x5_update_irq(s);
        break;
    case A_TDR:
        retvalue = FIELD_EX32(s->tdr, TDR, TDR);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }

    trace_stm32l4x5_usart_read(addr, retvalue);

    return retvalue;
}

static void stm32l4x5_usart_base_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    Stm32l4x5UsartBaseState *s = opaque;
    const uint32_t value = val64;

    trace_stm32l4x5_usart_write(addr, value);

    switch (addr) {
    case A_CR1:
        s->cr1 = value;
        stm32l4x5_update_params(s);
        stm32l4x5_update_isr(s);
        stm32l4x5_update_irq(s);
        return;
    case A_CR2:
        s->cr2 = value;
        stm32l4x5_update_params(s);
        return;
    case A_CR3:
        s->cr3 = value;
        return;
    case A_BRR:
        s->brr = value;
        stm32l4x5_update_params(s);
        return;
    case A_GTPR:
        s->gtpr = value;
        return;
    case A_RTOR:
        s->rtor = value;
        return;
    case A_RQR:
        usart_update_rqr(s, value);
        return;
    case A_ISR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: ISR is read only !\n", __func__);
        return;
    case A_ICR:
        /* Clear the status flags */
        s->isr &= ~value;
        stm32l4x5_update_irq(s);
        return;
    case A_RDR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: RDR is read only !\n", __func__);
        return;
    case A_TDR:
        s->tdr = value;
        s->isr &= ~R_ISR_TXE_MASK;
        usart_transmit(NULL, G_IO_OUT, s);
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32l4x5_usart_base_ops = {
    .read = stm32l4x5_usart_base_read,
    .write = stm32l4x5_usart_base_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .max_access_size = 4,
        .min_access_size = 4,
        .unaligned = false
    },
    .impl = {
        .max_access_size = 4,
        .min_access_size = 4,
        .unaligned = false
    },
};

static const Property stm32l4x5_usart_base_properties[] = {
    DEFINE_PROP_CHR("chardev", Stm32l4x5UsartBaseState, chr),
};

static void stm32l4x5_usart_base_init(Object *obj)
{
    Stm32l4x5UsartBaseState *s = STM32L4X5_USART_BASE(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &stm32l4x5_usart_base_ops, s,
                          TYPE_STM32L4X5_USART_BASE, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    s->clk = qdev_init_clock_in(DEVICE(s), "clk", NULL, s, 0);
}

static int stm32l4x5_usart_base_post_load(void *opaque, int version_id)
{
    Stm32l4x5UsartBaseState *s = (Stm32l4x5UsartBaseState *)opaque;

    stm32l4x5_update_params(s);
    return 0;
}

static const VMStateDescription vmstate_stm32l4x5_usart_base = {
    .name = TYPE_STM32L4X5_USART_BASE,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = stm32l4x5_usart_base_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cr1, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(cr2, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(cr3, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(brr, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(gtpr, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(rtor, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(isr, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(rdr, Stm32l4x5UsartBaseState),
        VMSTATE_UINT32(tdr, Stm32l4x5UsartBaseState),
        VMSTATE_CLOCK(clk, Stm32l4x5UsartBaseState),
        VMSTATE_END_OF_LIST()
    }
};


static void stm32l4x5_usart_base_realize(DeviceState *dev, Error **errp)
{
    ERRP_GUARD();
    Stm32l4x5UsartBaseState *s = STM32L4X5_USART_BASE(dev);
    if (!clock_has_source(s->clk)) {
        error_setg(errp, "USART clock must be wired up by SoC code");
        return;
    }

    qemu_chr_fe_set_handlers(&s->chr, stm32l4x5_usart_base_can_receive,
                             stm32l4x5_usart_base_receive, NULL, NULL,
                             s, NULL, true);
}

static void stm32l4x5_usart_base_class_init(ObjectClass *klass,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = stm32l4x5_usart_base_reset_hold;
    device_class_set_props(dc, stm32l4x5_usart_base_properties);
    dc->realize = stm32l4x5_usart_base_realize;
    dc->vmsd = &vmstate_stm32l4x5_usart_base;
}

static void stm32l4x5_usart_class_init(ObjectClass *oc, const void *data)
{
    Stm32l4x5UsartBaseClass *subc = STM32L4X5_USART_BASE_CLASS(oc);

    subc->type = STM32L4x5_USART;
}

static void stm32l4x5_uart_class_init(ObjectClass *oc, const void *data)
{
    Stm32l4x5UsartBaseClass *subc = STM32L4X5_USART_BASE_CLASS(oc);

    subc->type = STM32L4x5_UART;
}

static void stm32l4x5_lpuart_class_init(ObjectClass *oc, const void *data)
{
    Stm32l4x5UsartBaseClass *subc = STM32L4X5_USART_BASE_CLASS(oc);

    subc->type = STM32L4x5_LPUART;
}

static const TypeInfo stm32l4x5_usart_types[] = {
    {
        .name           = TYPE_STM32L4X5_USART_BASE,
        .parent         = TYPE_SYS_BUS_DEVICE,
        .instance_size  = sizeof(Stm32l4x5UsartBaseState),
        .instance_init  = stm32l4x5_usart_base_init,
        .class_size     = sizeof(Stm32l4x5UsartBaseClass),
        .class_init     = stm32l4x5_usart_base_class_init,
        .abstract       = true,
    }, {
        .name           = TYPE_STM32L4X5_USART,
        .parent         = TYPE_STM32L4X5_USART_BASE,
        .class_init     = stm32l4x5_usart_class_init,
    }, {
        .name           = TYPE_STM32L4X5_UART,
        .parent         = TYPE_STM32L4X5_USART_BASE,
        .class_init     = stm32l4x5_uart_class_init,
    }, {
        .name           = TYPE_STM32L4X5_LPUART,
        .parent         = TYPE_STM32L4X5_USART_BASE,
        .class_init     = stm32l4x5_lpuart_class_init,
    }
};

DEFINE_TYPES(stm32l4x5_usart_types)
