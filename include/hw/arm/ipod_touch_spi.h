#ifndef HW_ARM_IPOD_TOUCH_SPI_H
#define HW_ARM_IPOD_TOUCH_SPI_H

#include "qemu/osdep.h"
#include "qemu/fifo8.h"
#include "qemu/log.h"
#include "hw/platform-bus.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/ssi/ssi.h"
#include "hw/arm/ipod_touch_nor_spi.h"
#include "hw/arm/ipod_touch_multitouch.h"

#define TYPE_IPOD_TOUCH_SPI "ipodtouch.spi"
OBJECT_DECLARE_SIMPLE_TYPE(IPodTouchSPIState, IPOD_TOUCH_SPI)

#define  R_CTRL                  0x000
#define  R_CTRL_RUN              (1 << 0)
#define  R_CTRL_TX_RESET         (1 << 2)
#define  R_CTRL_RX_RESET         (1 << 3)

#define  R_CFG                   0x004
#define  R_CFG_AGD               (1 << 0)
#define  R_CFG_CPHA              (1 << 1)
#define  R_CFG_CPOL              (1 << 2)
#define  R_CFG_IE_RXREADY        (1 << 7)
#define  R_CFG_IE_TXEMPTY        (1 << 8)
#define  R_CFG_WORD_SIZE(_x)     (((_x) >> 15) & 0x3)
#define  R_CFG_WORD_SIZE_8B      0
#define  R_CFG_WORD_SIZE_16B     1
#define  R_CFG_WORD_SIZE_32B     2
#define  R_CFG_IE_COMPLETE       (1 << 21)

#define  R_STATUS                0x008
#define  R_STATUS_RXREADY	     (1 << 0)
#define  R_STATUS_TXEMPTY        (1 << 1)
#define  R_STATUS_RXOVERFLOW     (1 << 3)
#define  R_STATUS_COMPLETE       (1 << 22)
#define  R_STATUS_TXFIFO_SHIFT   (6)
#define  R_STATUS_RXFIFO_SHIFT   (11)

#define  R_PIN                   0x00c
#define  R_PIN_CS                (1 << 1)

#define R_TXDATA                0x010
#define R_RXDATA                0x020
#define R_CLKDIV                0x030
#define R_CLKDIV_MAX            0x7ff
#define R_RXCNT                 0x034
#define R_WORD_DELAY            0x038
#define R_TXCNT                 0x04c

#define R_FIFO_TX_DEPTH         50000
#define R_FIFO_RX_DEPTH         16

#define REG(_s,_v)             ((_s)->regs[(_v)>>2])
#define MMIO_SIZE              (0x4000)


typedef struct IPodTouchSPIState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    SSIBus *spi;
    IPodTouchMultitouchState *mt;

    qemu_irq irq;
    uint32_t last_irq;
    qemu_irq cs_line;

    uint32_t regs[MMIO_SIZE >> 2];
    uint8_t base;
    IPodTouchNORSPIState *nor;
    Fifo8 rx_fifo;
    Fifo8 tx_fifo;
} IPodTouchSPIState;

void set_spi_base(uint32_t base);

#endif