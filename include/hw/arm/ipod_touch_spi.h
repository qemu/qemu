#ifndef HW_ARM_IPOD_TOUCH_SPI_H
#define HW_ARM_IPOD_TOUCH_SPI_H

#include "qemu/osdep.h"
#include "qemu/fifo8.h"
#include "qemu/log.h"
#include "hw/platform-bus.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/ssi/ssi.h"

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
#define  R_CFG_MODE(_x)          (((_x) >> 5) & 0x3)
#define  R_CFG_MODE_POLLED       0
#define  R_CFG_MODE_IRQ          1
#define  R_CFG_MODE_DMA          2
#define  R_CFG_IE_RXREADY        (1 << 7)
#define  R_CFG_IE_TXEMPTY        (1 << 8)
#define  R_CFG_WORD_SIZE(_x)     (((_x) >> 13) & 0x3)
#define  R_CFG_WORD_SIZE_8B      0
#define  R_CFG_WORD_SIZE_16B     1
#define  R_CFG_WORD_SIZE_32B     2
#define  R_CFG_IE_COMPLETE       (1 << 21)

#define  R_STATUS                0x008
#define  R_STATUS_RXREADY	     (1 << 0)
#define  R_STATUS_TXEMPTY        (1 << 1)
#define  R_STATUS_RXOVERFLOW     (1 << 3)
#define  R_STATUS_COMPLETE       (1 << 22)
#define  R_STATUS_TXFIFO_SHIFT   (4)
#define  R_STATUS_TXFIFO_MASK    (31 << R_STATUS_TXFIFO_SHIFT)
#define  R_STATUS_RXFIFO_SHIFT   (8)
#define  R_STATUS_RXFIFO_MASK    (31 << R_STATUS_RXFIFO_SHIFT)

#define  R_PIN                   0x00c
#define  R_PIN_CS                (1 << 1)

#define R_TXDATA                0x010
#define R_RXDATA                0x020
#define R_CLKDIV                0x030
#define R_CLKDIV_MAX            0x7ff
#define R_RXCNT                 0x034
#define R_WORD_DELAY            0x038
#define R_TXCNT                 0x04c

#define R_FIFO_DEPTH            50000

#define REG(_s,_v)             ((_s)->regs[(_v)>>2])
#define MMIO_SIZE              (0x4000)


typedef struct IPodTouchSPIState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    SSIBus *spi;

    qemu_irq irq;
    uint32_t last_irq;
    qemu_irq cs_line;

    Fifo8 rx_fifo;
    Fifo8 tx_fifo;
    uint32_t regs[MMIO_SIZE >> 2];
    uint32_t mmio_size;
    uint8_t base;
} IPodTouchSPIState;

void set_spi_base(uint32_t base);

#endif