/*
 * Texas Instruments TUSB6010 emulation.
 * Based on reverse-engineering of a linux driver.
 *
 * Copyright (C) 2008 Nokia Corporation
 * Written by Andrzej Zaborowski <andrew@openedhand.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu-common.h"
#include "qemu-timer.h"
#include "usb.h"
#include "omap.h"
#include "irq.h"
#include "tusb6010.h"

struct TUSBState {
    int iomemtype[2];
    qemu_irq irq;
    MUSBState *musb;
    QEMUTimer *otg_timer;
    QEMUTimer *pwr_timer;

    int power;
    uint32_t scratch;
    uint16_t test_reset;
    uint32_t prcm_config;
    uint32_t prcm_mngmt;
    uint16_t otg_status;
    uint32_t dev_config;
    int host_mode;
    uint32_t intr;
    uint32_t intr_ok;
    uint32_t mask;
    uint32_t usbip_intr;
    uint32_t usbip_mask;
    uint32_t gpio_intr;
    uint32_t gpio_mask;
    uint32_t gpio_config;
    uint32_t dma_intr;
    uint32_t dma_mask;
    uint32_t dma_map;
    uint32_t dma_config;
    uint32_t ep0_config;
    uint32_t rx_config[15];
    uint32_t tx_config[15];
    uint32_t wkup_mask;
    uint32_t pullup[2];
    uint32_t control_config;
    uint32_t otg_timer_val;
};

#define TUSB_DEVCLOCK			60000000	/* 60 MHz */

#define TUSB_VLYNQ_CTRL			0x004

/* Mentor Graphics OTG core registers.  */
#define TUSB_BASE_OFFSET		0x400

/* FIFO registers, 32-bit.  */
#define TUSB_FIFO_BASE			0x600

/* Device System & Control registers, 32-bit.  */
#define TUSB_SYS_REG_BASE		0x800

#define TUSB_DEV_CONF			(TUSB_SYS_REG_BASE + 0x000)
#define	TUSB_DEV_CONF_USB_HOST_MODE	(1 << 16)
#define	TUSB_DEV_CONF_PROD_TEST_MODE	(1 << 15)
#define	TUSB_DEV_CONF_SOFT_ID		(1 << 1)
#define	TUSB_DEV_CONF_ID_SEL		(1 << 0)

#define TUSB_PHY_OTG_CTRL_ENABLE	(TUSB_SYS_REG_BASE + 0x004)
#define TUSB_PHY_OTG_CTRL		(TUSB_SYS_REG_BASE + 0x008)
#define	TUSB_PHY_OTG_CTRL_WRPROTECT	(0xa5 << 24)
#define	TUSB_PHY_OTG_CTRL_O_ID_PULLUP	(1 << 23)
#define	TUSB_PHY_OTG_CTRL_O_VBUS_DET_EN	(1 << 19)
#define	TUSB_PHY_OTG_CTRL_O_SESS_END_EN	(1 << 18)
#define	TUSB_PHY_OTG_CTRL_TESTM2	(1 << 17)
#define	TUSB_PHY_OTG_CTRL_TESTM1	(1 << 16)
#define	TUSB_PHY_OTG_CTRL_TESTM0	(1 << 15)
#define	TUSB_PHY_OTG_CTRL_TX_DATA2	(1 << 14)
#define	TUSB_PHY_OTG_CTRL_TX_GZ2	(1 << 13)
#define	TUSB_PHY_OTG_CTRL_TX_ENABLE2	(1 << 12)
#define	TUSB_PHY_OTG_CTRL_DM_PULLDOWN	(1 << 11)
#define	TUSB_PHY_OTG_CTRL_DP_PULLDOWN	(1 << 10)
#define	TUSB_PHY_OTG_CTRL_OSC_EN	(1 << 9)
#define	TUSB_PHY_OTG_CTRL_PHYREF_CLK(v)	(((v) & 3) << 7)
#define	TUSB_PHY_OTG_CTRL_PD		(1 << 6)
#define	TUSB_PHY_OTG_CTRL_PLL_ON	(1 << 5)
#define	TUSB_PHY_OTG_CTRL_EXT_RPU	(1 << 4)
#define	TUSB_PHY_OTG_CTRL_PWR_GOOD	(1 << 3)
#define	TUSB_PHY_OTG_CTRL_RESET		(1 << 2)
#define	TUSB_PHY_OTG_CTRL_SUSPENDM	(1 << 1)
#define	TUSB_PHY_OTG_CTRL_CLK_MODE	(1 << 0)

/* OTG status register */
#define TUSB_DEV_OTG_STAT		(TUSB_SYS_REG_BASE + 0x00c)
#define	TUSB_DEV_OTG_STAT_PWR_CLK_GOOD	(1 << 8)
#define	TUSB_DEV_OTG_STAT_SESS_END	(1 << 7)
#define	TUSB_DEV_OTG_STAT_SESS_VALID	(1 << 6)
#define	TUSB_DEV_OTG_STAT_VBUS_VALID	(1 << 5)
#define	TUSB_DEV_OTG_STAT_VBUS_SENSE	(1 << 4)
#define	TUSB_DEV_OTG_STAT_ID_STATUS	(1 << 3)
#define	TUSB_DEV_OTG_STAT_HOST_DISCON	(1 << 2)
#define	TUSB_DEV_OTG_STAT_LINE_STATE	(3 << 0)
#define	TUSB_DEV_OTG_STAT_DP_ENABLE	(1 << 1)
#define	TUSB_DEV_OTG_STAT_DM_ENABLE	(1 << 0)

#define TUSB_DEV_OTG_TIMER		(TUSB_SYS_REG_BASE + 0x010)
#define TUSB_DEV_OTG_TIMER_ENABLE	(1 << 31)
#define TUSB_DEV_OTG_TIMER_VAL(v)	((v) & 0x07ffffff)
#define TUSB_PRCM_REV			(TUSB_SYS_REG_BASE + 0x014)

/* PRCM configuration register */
#define TUSB_PRCM_CONF			(TUSB_SYS_REG_BASE + 0x018)
#define	TUSB_PRCM_CONF_SFW_CPEN		(1 << 24)
#define	TUSB_PRCM_CONF_SYS_CLKSEL(v)	(((v) & 3) << 16)

/* PRCM management register */
#define TUSB_PRCM_MNGMT			(TUSB_SYS_REG_BASE + 0x01c)
#define	TUSB_PRCM_MNGMT_SRP_FIX_TMR(v)	(((v) & 0xf) << 25)
#define	TUSB_PRCM_MNGMT_SRP_FIX_EN	(1 << 24)
#define	TUSB_PRCM_MNGMT_VBUS_VAL_TMR(v)	(((v) & 0xf) << 20)
#define	TUSB_PRCM_MNGMT_VBUS_VAL_FLT_EN	(1 << 19)
#define	TUSB_PRCM_MNGMT_DFT_CLK_DIS	(1 << 18)
#define	TUSB_PRCM_MNGMT_VLYNQ_CLK_DIS	(1 << 17)
#define	TUSB_PRCM_MNGMT_OTG_SESS_END_EN	(1 << 10)
#define	TUSB_PRCM_MNGMT_OTG_VBUS_DET_EN	(1 << 9)
#define	TUSB_PRCM_MNGMT_OTG_ID_PULLUP	(1 << 8)
#define	TUSB_PRCM_MNGMT_15_SW_EN	(1 << 4)
#define	TUSB_PRCM_MNGMT_33_SW_EN	(1 << 3)
#define	TUSB_PRCM_MNGMT_5V_CPEN		(1 << 2)
#define	TUSB_PRCM_MNGMT_PM_IDLE		(1 << 1)
#define	TUSB_PRCM_MNGMT_DEV_IDLE	(1 << 0)

/* Wake-up source clear and mask registers */
#define TUSB_PRCM_WAKEUP_SOURCE		(TUSB_SYS_REG_BASE + 0x020)
#define TUSB_PRCM_WAKEUP_CLEAR		(TUSB_SYS_REG_BASE + 0x028)
#define TUSB_PRCM_WAKEUP_MASK		(TUSB_SYS_REG_BASE + 0x02c)
#define	TUSB_PRCM_WAKEUP_RESERVED_BITS	(0xffffe << 13)
#define	TUSB_PRCM_WGPIO_7		(1 << 12)
#define	TUSB_PRCM_WGPIO_6		(1 << 11)
#define	TUSB_PRCM_WGPIO_5		(1 << 10)
#define	TUSB_PRCM_WGPIO_4		(1 << 9)
#define	TUSB_PRCM_WGPIO_3		(1 << 8)
#define	TUSB_PRCM_WGPIO_2		(1 << 7)
#define	TUSB_PRCM_WGPIO_1		(1 << 6)
#define	TUSB_PRCM_WGPIO_0		(1 << 5)
#define	TUSB_PRCM_WHOSTDISCON		(1 << 4)	/* Host disconnect */
#define	TUSB_PRCM_WBUS			(1 << 3)	/* USB bus resume */
#define	TUSB_PRCM_WNORCS		(1 << 2)	/* NOR chip select */
#define	TUSB_PRCM_WVBUS			(1 << 1)	/* OTG PHY VBUS */
#define	TUSB_PRCM_WID			(1 << 0)	/* OTG PHY ID detect */

#define TUSB_PULLUP_1_CTRL		(TUSB_SYS_REG_BASE + 0x030)
#define TUSB_PULLUP_2_CTRL		(TUSB_SYS_REG_BASE + 0x034)
#define TUSB_INT_CTRL_REV		(TUSB_SYS_REG_BASE + 0x038)
#define TUSB_INT_CTRL_CONF		(TUSB_SYS_REG_BASE + 0x03c)
#define TUSB_USBIP_INT_SRC		(TUSB_SYS_REG_BASE + 0x040)
#define TUSB_USBIP_INT_SET		(TUSB_SYS_REG_BASE + 0x044)
#define TUSB_USBIP_INT_CLEAR		(TUSB_SYS_REG_BASE + 0x048)
#define TUSB_USBIP_INT_MASK		(TUSB_SYS_REG_BASE + 0x04c)
#define TUSB_DMA_INT_SRC		(TUSB_SYS_REG_BASE + 0x050)
#define TUSB_DMA_INT_SET		(TUSB_SYS_REG_BASE + 0x054)
#define TUSB_DMA_INT_CLEAR		(TUSB_SYS_REG_BASE + 0x058)
#define TUSB_DMA_INT_MASK		(TUSB_SYS_REG_BASE + 0x05c)
#define TUSB_GPIO_INT_SRC		(TUSB_SYS_REG_BASE + 0x060)
#define TUSB_GPIO_INT_SET		(TUSB_SYS_REG_BASE + 0x064)
#define TUSB_GPIO_INT_CLEAR		(TUSB_SYS_REG_BASE + 0x068)
#define TUSB_GPIO_INT_MASK		(TUSB_SYS_REG_BASE + 0x06c)

/* NOR flash interrupt source registers */
#define TUSB_INT_SRC			(TUSB_SYS_REG_BASE + 0x070)
#define TUSB_INT_SRC_SET		(TUSB_SYS_REG_BASE + 0x074)
#define TUSB_INT_SRC_CLEAR		(TUSB_SYS_REG_BASE + 0x078)
#define TUSB_INT_MASK			(TUSB_SYS_REG_BASE + 0x07c)
#define	TUSB_INT_SRC_TXRX_DMA_DONE	(1 << 24)
#define	TUSB_INT_SRC_USB_IP_CORE	(1 << 17)
#define	TUSB_INT_SRC_OTG_TIMEOUT	(1 << 16)
#define	TUSB_INT_SRC_VBUS_SENSE_CHNG	(1 << 15)
#define	TUSB_INT_SRC_ID_STATUS_CHNG	(1 << 14)
#define	TUSB_INT_SRC_DEV_WAKEUP		(1 << 13)
#define	TUSB_INT_SRC_DEV_READY		(1 << 12)
#define	TUSB_INT_SRC_USB_IP_TX		(1 << 9)
#define	TUSB_INT_SRC_USB_IP_RX		(1 << 8)
#define	TUSB_INT_SRC_USB_IP_VBUS_ERR	(1 << 7)
#define	TUSB_INT_SRC_USB_IP_VBUS_REQ	(1 << 6)
#define	TUSB_INT_SRC_USB_IP_DISCON	(1 << 5)
#define	TUSB_INT_SRC_USB_IP_CONN	(1 << 4)
#define	TUSB_INT_SRC_USB_IP_SOF		(1 << 3)
#define	TUSB_INT_SRC_USB_IP_RST_BABBLE	(1 << 2)
#define	TUSB_INT_SRC_USB_IP_RESUME	(1 << 1)
#define	TUSB_INT_SRC_USB_IP_SUSPEND	(1 << 0)

#define TUSB_GPIO_REV			(TUSB_SYS_REG_BASE + 0x080)
#define TUSB_GPIO_CONF			(TUSB_SYS_REG_BASE + 0x084)
#define TUSB_DMA_CTRL_REV		(TUSB_SYS_REG_BASE + 0x100)
#define TUSB_DMA_REQ_CONF		(TUSB_SYS_REG_BASE + 0x104)
#define TUSB_EP0_CONF			(TUSB_SYS_REG_BASE + 0x108)
#define TUSB_EP_IN_SIZE			(TUSB_SYS_REG_BASE + 0x10c)
#define TUSB_DMA_EP_MAP			(TUSB_SYS_REG_BASE + 0x148)
#define TUSB_EP_OUT_SIZE		(TUSB_SYS_REG_BASE + 0x14c)
#define TUSB_EP_MAX_PACKET_SIZE_OFFSET	(TUSB_SYS_REG_BASE + 0x188)
#define TUSB_SCRATCH_PAD		(TUSB_SYS_REG_BASE + 0x1c4)
#define TUSB_WAIT_COUNT			(TUSB_SYS_REG_BASE + 0x1c8)
#define TUSB_PROD_TEST_RESET		(TUSB_SYS_REG_BASE + 0x1d8)

#define TUSB_DIDR1_LO			(TUSB_SYS_REG_BASE + 0x1f8)
#define TUSB_DIDR1_HI			(TUSB_SYS_REG_BASE + 0x1fc)

/* Device System & Control register bitfields */
#define TUSB_INT_CTRL_CONF_INT_RLCYC(v)	(((v) & 0x7) << 18)
#define TUSB_INT_CTRL_CONF_INT_POLARITY	(1 << 17)
#define TUSB_INT_CTRL_CONF_INT_MODE	(1 << 16)
#define TUSB_GPIO_CONF_DMAREQ(v)	(((v) & 0x3f) << 24)
#define TUSB_DMA_REQ_CONF_BURST_SIZE(v)	(((v) & 3) << 26)
#define TUSB_DMA_REQ_CONF_DMA_RQ_EN(v)	(((v) & 0x3f) << 20)
#define TUSB_DMA_REQ_CONF_DMA_RQ_ASR(v)	(((v) & 0xf) << 16)
#define TUSB_EP0_CONFIG_SW_EN		(1 << 8)
#define TUSB_EP0_CONFIG_DIR_TX		(1 << 7)
#define TUSB_EP0_CONFIG_XFR_SIZE(v)	((v) & 0x7f)
#define TUSB_EP_CONFIG_SW_EN		(1 << 31)
#define TUSB_EP_CONFIG_XFR_SIZE(v)	((v) & 0x7fffffff)
#define TUSB_PROD_TEST_RESET_VAL	0xa596

int tusb6010_sync_io(TUSBState *s)
{
    return s->iomemtype[0];
}

int tusb6010_async_io(TUSBState *s)
{
    return s->iomemtype[1];
}

static void tusb_intr_update(TUSBState *s)
{
    if (s->control_config & TUSB_INT_CTRL_CONF_INT_POLARITY)
        qemu_set_irq(s->irq, s->intr & ~s->mask & s->intr_ok);
    else
        qemu_set_irq(s->irq, (!(s->intr & ~s->mask)) & s->intr_ok);
}

static void tusb_usbip_intr_update(TUSBState *s)
{
    /* TX interrupt in the MUSB */
    if (s->usbip_intr & 0x0000ffff & ~s->usbip_mask)
        s->intr |= TUSB_INT_SRC_USB_IP_TX;
    else
        s->intr &= ~TUSB_INT_SRC_USB_IP_TX;

    /* RX interrupt in the MUSB */
    if (s->usbip_intr & 0xffff0000 & ~s->usbip_mask)
        s->intr |= TUSB_INT_SRC_USB_IP_RX;
    else
        s->intr &= ~TUSB_INT_SRC_USB_IP_RX;

    /* XXX: What about TUSB_INT_SRC_USB_IP_CORE?  */

    tusb_intr_update(s);
}

static void tusb_dma_intr_update(TUSBState *s)
{
    if (s->dma_intr & ~s->dma_mask)
        s->intr |= TUSB_INT_SRC_TXRX_DMA_DONE;
    else
        s->intr &= ~TUSB_INT_SRC_TXRX_DMA_DONE;

    tusb_intr_update(s);
}

static void tusb_gpio_intr_update(TUSBState *s)
{
    /* TODO: How is this signalled?  */
}

extern CPUReadMemoryFunc * const musb_read[];
extern CPUWriteMemoryFunc * const musb_write[];

static uint32_t tusb_async_readb(void *opaque, target_phys_addr_t addr)
{
    TUSBState *s = (TUSBState *) opaque;

    switch (addr & 0xfff) {
    case TUSB_BASE_OFFSET ... (TUSB_BASE_OFFSET | 0x1ff):
        return musb_read[0](s->musb, addr & 0x1ff);

    case TUSB_FIFO_BASE ... (TUSB_FIFO_BASE | 0x1ff):
        return musb_read[0](s->musb, 0x20 + ((addr >> 3) & 0x3c));
    }

    printf("%s: unknown register at %03x\n",
                    __FUNCTION__, (int) (addr & 0xfff));
    return 0;
}

static uint32_t tusb_async_readh(void *opaque, target_phys_addr_t addr)
{
    TUSBState *s = (TUSBState *) opaque;

    switch (addr & 0xfff) {
    case TUSB_BASE_OFFSET ... (TUSB_BASE_OFFSET | 0x1ff):
        return musb_read[1](s->musb, addr & 0x1ff);

    case TUSB_FIFO_BASE ... (TUSB_FIFO_BASE | 0x1ff):
        return musb_read[1](s->musb, 0x20 + ((addr >> 3) & 0x3c));
    }

    printf("%s: unknown register at %03x\n",
                    __FUNCTION__, (int) (addr & 0xfff));
    return 0;
}

static uint32_t tusb_async_readw(void *opaque, target_phys_addr_t addr)
{
    TUSBState *s = (TUSBState *) opaque;
    int offset = addr & 0xfff;
    int epnum;
    uint32_t ret;

    switch (offset) {
    case TUSB_DEV_CONF:
        return s->dev_config;

    case TUSB_BASE_OFFSET ... (TUSB_BASE_OFFSET | 0x1ff):
        return musb_read[2](s->musb, offset & 0x1ff);

    case TUSB_FIFO_BASE ... (TUSB_FIFO_BASE | 0x1ff):
        return musb_read[2](s->musb, 0x20 + ((addr >> 3) & 0x3c));

    case TUSB_PHY_OTG_CTRL_ENABLE:
    case TUSB_PHY_OTG_CTRL:
        return 0x00;	/* TODO */

    case TUSB_DEV_OTG_STAT:
        ret = s->otg_status;
#if 0
        if (!(s->prcm_mngmt & TUSB_PRCM_MNGMT_OTG_VBUS_DET_EN))
            ret &= ~TUSB_DEV_OTG_STAT_VBUS_VALID;
#endif
        return ret;
    case TUSB_DEV_OTG_TIMER:
        return s->otg_timer_val;

    case TUSB_PRCM_REV:
        return 0x20;
    case TUSB_PRCM_CONF:
        return s->prcm_config;
    case TUSB_PRCM_MNGMT:
        return s->prcm_mngmt;
    case TUSB_PRCM_WAKEUP_SOURCE:
    case TUSB_PRCM_WAKEUP_CLEAR:	/* TODO: What does this one return?  */
        return 0x00000000;
    case TUSB_PRCM_WAKEUP_MASK:
        return s->wkup_mask;

    case TUSB_PULLUP_1_CTRL:
        return s->pullup[0];
    case TUSB_PULLUP_2_CTRL:
        return s->pullup[1];

    case TUSB_INT_CTRL_REV:
        return 0x20;
    case TUSB_INT_CTRL_CONF:
        return s->control_config;

    case TUSB_USBIP_INT_SRC:
    case TUSB_USBIP_INT_SET:	/* TODO: What do these two return?  */
    case TUSB_USBIP_INT_CLEAR:
        return s->usbip_intr;
    case TUSB_USBIP_INT_MASK:
        return s->usbip_mask;

    case TUSB_DMA_INT_SRC:
    case TUSB_DMA_INT_SET:	/* TODO: What do these two return?  */
    case TUSB_DMA_INT_CLEAR:
        return s->dma_intr;
    case TUSB_DMA_INT_MASK:
        return s->dma_mask;

    case TUSB_GPIO_INT_SRC:	/* TODO: What do these two return?  */
    case TUSB_GPIO_INT_SET:
    case TUSB_GPIO_INT_CLEAR:
        return s->gpio_intr;
    case TUSB_GPIO_INT_MASK:
        return s->gpio_mask;

    case TUSB_INT_SRC:
    case TUSB_INT_SRC_SET:	/* TODO: What do these two return?  */
    case TUSB_INT_SRC_CLEAR:
        return s->intr;
    case TUSB_INT_MASK:
        return s->mask;

    case TUSB_GPIO_REV:
        return 0x30;
    case TUSB_GPIO_CONF:
        return s->gpio_config;

    case TUSB_DMA_CTRL_REV:
        return 0x30;
    case TUSB_DMA_REQ_CONF:
        return s->dma_config;
    case TUSB_EP0_CONF:
        return s->ep0_config;
    case TUSB_EP_IN_SIZE ... (TUSB_EP_IN_SIZE + 0x3b):
        epnum = (offset - TUSB_EP_IN_SIZE) >> 2;
        return s->tx_config[epnum];
    case TUSB_DMA_EP_MAP:
        return s->dma_map;
    case TUSB_EP_OUT_SIZE ... (TUSB_EP_OUT_SIZE + 0x3b):
        epnum = (offset - TUSB_EP_OUT_SIZE) >> 2;
        return s->rx_config[epnum];
    case TUSB_EP_MAX_PACKET_SIZE_OFFSET ...
            (TUSB_EP_MAX_PACKET_SIZE_OFFSET + 0x3b):
        return 0x00000000;	/* TODO */
    case TUSB_WAIT_COUNT:
        return 0x00;		/* TODO */

    case TUSB_SCRATCH_PAD:
        return s->scratch;

    case TUSB_PROD_TEST_RESET:
        return s->test_reset;

    /* DIE IDs */
    case TUSB_DIDR1_LO:
        return 0xa9453c59;
    case TUSB_DIDR1_HI:
        return 0x54059adf;
    }

    printf("%s: unknown register at %03x\n", __FUNCTION__, offset);
    return 0;
}

static void tusb_async_writeb(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    TUSBState *s = (TUSBState *) opaque;

    switch (addr & 0xfff) {
    case TUSB_BASE_OFFSET ... (TUSB_BASE_OFFSET | 0x1ff):
        musb_write[0](s->musb, addr & 0x1ff, value);
        break;

    case TUSB_FIFO_BASE ... (TUSB_FIFO_BASE | 0x1ff):
        musb_write[0](s->musb, 0x20 + ((addr >> 3) & 0x3c), value);
        break;

    default:
        printf("%s: unknown register at %03x\n",
                        __FUNCTION__, (int) (addr & 0xfff));
        return;
    }
}

static void tusb_async_writeh(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    TUSBState *s = (TUSBState *) opaque;

    switch (addr & 0xfff) {
    case TUSB_BASE_OFFSET ... (TUSB_BASE_OFFSET | 0x1ff):
        musb_write[1](s->musb, addr & 0x1ff, value);
        break;

    case TUSB_FIFO_BASE ... (TUSB_FIFO_BASE | 0x1ff):
        musb_write[1](s->musb, 0x20 + ((addr >> 3) & 0x3c), value);
        break;

    default:
        printf("%s: unknown register at %03x\n",
                        __FUNCTION__, (int) (addr & 0xfff));
        return;
    }
}

static void tusb_async_writew(void *opaque, target_phys_addr_t addr,
                uint32_t value)
{
    TUSBState *s = (TUSBState *) opaque;
    int offset = addr & 0xfff;
    int epnum;

    switch (offset) {
    case TUSB_VLYNQ_CTRL:
        break;

    case TUSB_BASE_OFFSET ... (TUSB_BASE_OFFSET | 0x1ff):
        musb_write[2](s->musb, offset & 0x1ff, value);
        break;

    case TUSB_FIFO_BASE ... (TUSB_FIFO_BASE | 0x1ff):
        musb_write[2](s->musb, 0x20 + ((addr >> 3) & 0x3c), value);
        break;

    case TUSB_DEV_CONF:
        s->dev_config = value;
        s->host_mode = (value & TUSB_DEV_CONF_USB_HOST_MODE);
        if (value & TUSB_DEV_CONF_PROD_TEST_MODE)
            hw_error("%s: Product Test mode not allowed\n", __FUNCTION__);
        break;

    case TUSB_PHY_OTG_CTRL_ENABLE:
    case TUSB_PHY_OTG_CTRL:
        return;		/* TODO */
    case TUSB_DEV_OTG_TIMER:
        s->otg_timer_val = value;
        if (value & TUSB_DEV_OTG_TIMER_ENABLE)
            qemu_mod_timer(s->otg_timer, qemu_get_clock_ns(vm_clock) +
                            muldiv64(TUSB_DEV_OTG_TIMER_VAL(value),
                                     get_ticks_per_sec(), TUSB_DEVCLOCK));
        else
            qemu_del_timer(s->otg_timer);
        break;

    case TUSB_PRCM_CONF:
        s->prcm_config = value;
        break;
    case TUSB_PRCM_MNGMT:
        s->prcm_mngmt = value;
        break;
    case TUSB_PRCM_WAKEUP_CLEAR:
        break;
    case TUSB_PRCM_WAKEUP_MASK:
        s->wkup_mask = value;
        break;

    case TUSB_PULLUP_1_CTRL:
        s->pullup[0] = value;
        break;
    case TUSB_PULLUP_2_CTRL:
        s->pullup[1] = value;
        break;
    case TUSB_INT_CTRL_CONF:
        s->control_config = value;
        tusb_intr_update(s);
        break;

    case TUSB_USBIP_INT_SET:
        s->usbip_intr |= value;
        tusb_usbip_intr_update(s);
        break;
    case TUSB_USBIP_INT_CLEAR:
        s->usbip_intr &= ~value;
        tusb_usbip_intr_update(s);
        musb_core_intr_clear(s->musb, ~value);
        break;
    case TUSB_USBIP_INT_MASK:
        s->usbip_mask = value;
        tusb_usbip_intr_update(s);
        break;

    case TUSB_DMA_INT_SET:
        s->dma_intr |= value;
        tusb_dma_intr_update(s);
        break;
    case TUSB_DMA_INT_CLEAR:
        s->dma_intr &= ~value;
        tusb_dma_intr_update(s);
        break;
    case TUSB_DMA_INT_MASK:
        s->dma_mask = value;
        tusb_dma_intr_update(s);
        break;

    case TUSB_GPIO_INT_SET:
        s->gpio_intr |= value;
        tusb_gpio_intr_update(s);
        break;
    case TUSB_GPIO_INT_CLEAR:
        s->gpio_intr &= ~value;
        tusb_gpio_intr_update(s);
        break;
    case TUSB_GPIO_INT_MASK:
        s->gpio_mask = value;
        tusb_gpio_intr_update(s);
        break;

    case TUSB_INT_SRC_SET:
        s->intr |= value;
        tusb_intr_update(s);
        break;
    case TUSB_INT_SRC_CLEAR:
        s->intr &= ~value;
        tusb_intr_update(s);
        break;
    case TUSB_INT_MASK:
        s->mask = value;
        tusb_intr_update(s);
        break;

    case TUSB_GPIO_CONF:
        s->gpio_config = value;
        break;
    case TUSB_DMA_REQ_CONF:
        s->dma_config = value;
        break;
    case TUSB_EP0_CONF:
        s->ep0_config = value & 0x1ff;
        musb_set_size(s->musb, 0, TUSB_EP0_CONFIG_XFR_SIZE(value),
                        value & TUSB_EP0_CONFIG_DIR_TX);
        break;
    case TUSB_EP_IN_SIZE ... (TUSB_EP_IN_SIZE + 0x3b):
        epnum = (offset - TUSB_EP_IN_SIZE) >> 2;
        s->tx_config[epnum] = value;
        musb_set_size(s->musb, epnum + 1, TUSB_EP_CONFIG_XFR_SIZE(value), 1);
        break;
    case TUSB_DMA_EP_MAP:
        s->dma_map = value;
        break;
    case TUSB_EP_OUT_SIZE ... (TUSB_EP_OUT_SIZE + 0x3b):
        epnum = (offset - TUSB_EP_OUT_SIZE) >> 2;
        s->rx_config[epnum] = value;
        musb_set_size(s->musb, epnum + 1, TUSB_EP_CONFIG_XFR_SIZE(value), 0);
        break;
    case TUSB_EP_MAX_PACKET_SIZE_OFFSET ...
            (TUSB_EP_MAX_PACKET_SIZE_OFFSET + 0x3b):
        return;		/* TODO */
    case TUSB_WAIT_COUNT:
        return;		/* TODO */

    case TUSB_SCRATCH_PAD:
        s->scratch = value;
        break;

    case TUSB_PROD_TEST_RESET:
        s->test_reset = value;
        break;

    default:
        printf("%s: unknown register at %03x\n", __FUNCTION__, offset);
        return;
    }
}

static CPUReadMemoryFunc * const tusb_async_readfn[] = {
    tusb_async_readb,
    tusb_async_readh,
    tusb_async_readw,
};

static CPUWriteMemoryFunc * const tusb_async_writefn[] = {
    tusb_async_writeb,
    tusb_async_writeh,
    tusb_async_writew,
};

static void tusb_otg_tick(void *opaque)
{
    TUSBState *s = (TUSBState *) opaque;

    s->otg_timer_val = 0;
    s->intr |= TUSB_INT_SRC_OTG_TIMEOUT;
    tusb_intr_update(s);
}

static void tusb_power_tick(void *opaque)
{
    TUSBState *s = (TUSBState *) opaque;

    if (s->power) {
        s->intr_ok = ~0;
        tusb_intr_update(s);
    }
}

static void tusb_musb_core_intr(void *opaque, int source, int level)
{
    TUSBState *s = (TUSBState *) opaque;
    uint16_t otg_status = s->otg_status;

    switch (source) {
    case musb_set_vbus:
        if (level)
            otg_status |= TUSB_DEV_OTG_STAT_VBUS_VALID;
        else
            otg_status &= ~TUSB_DEV_OTG_STAT_VBUS_VALID;

        /* XXX: only if TUSB_PHY_OTG_CTRL_OTG_VBUS_DET_EN set?  */
        /* XXX: only if TUSB_PRCM_MNGMT_OTG_VBUS_DET_EN set?  */
        if (s->otg_status != otg_status) {
            s->otg_status = otg_status;
            s->intr |= TUSB_INT_SRC_VBUS_SENSE_CHNG;
            tusb_intr_update(s);
        }
        break;

    case musb_set_session:
        /* XXX: only if TUSB_PHY_OTG_CTRL_OTG_SESS_END_EN set?  */
        /* XXX: only if TUSB_PRCM_MNGMT_OTG_SESS_END_EN set?  */
        if (level) {
            s->otg_status |= TUSB_DEV_OTG_STAT_SESS_VALID;
            s->otg_status &= ~TUSB_DEV_OTG_STAT_SESS_END;
        } else {
            s->otg_status &= ~TUSB_DEV_OTG_STAT_SESS_VALID;
            s->otg_status |= TUSB_DEV_OTG_STAT_SESS_END;
        }

        /* XXX: some IRQ or anything?  */
        break;

    case musb_irq_tx:
    case musb_irq_rx:
        s->usbip_intr = musb_core_intr_get(s->musb);
        /* Fall through.  */
    default:
        if (level)
            s->intr |= 1 << source;
        else
            s->intr &= ~(1 << source);
        tusb_intr_update(s);
        break;
    }
}

TUSBState *tusb6010_init(qemu_irq intr)
{
    TUSBState *s = g_malloc0(sizeof(*s));

    s->test_reset = TUSB_PROD_TEST_RESET_VAL;
    s->host_mode = 0;
    s->dev_config = 0;
    s->otg_status = 0;	/* !TUSB_DEV_OTG_STAT_ID_STATUS means host mode */
    s->power = 0;
    s->mask = 0xffffffff;
    s->intr = 0x00000000;
    s->otg_timer_val = 0;
    s->iomemtype[1] = cpu_register_io_memory(tusb_async_readfn,
                    tusb_async_writefn, s, DEVICE_NATIVE_ENDIAN);
    s->irq = intr;
    s->otg_timer = qemu_new_timer_ns(vm_clock, tusb_otg_tick, s);
    s->pwr_timer = qemu_new_timer_ns(vm_clock, tusb_power_tick, s);
    s->musb = musb_init(qemu_allocate_irqs(tusb_musb_core_intr, s,
                            __musb_irq_max));

    return s;
}

void tusb6010_power(TUSBState *s, int on)
{
    if (!on)
        s->power = 0;
    else if (!s->power && on) {
        s->power = 1;

        /* Pull the interrupt down after TUSB6010 comes up.  */
        s->intr_ok = 0;
        tusb_intr_update(s);
        qemu_mod_timer(s->pwr_timer,
                       qemu_get_clock_ns(vm_clock) + get_ticks_per_sec() / 2);
    }
}
