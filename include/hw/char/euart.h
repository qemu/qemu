#ifndef HW_CHAR_EUART_H
#define HW_CHAR_EUART_H

#include "hw/sysbus.h"
#include "qemu/typedefs.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_EUART "euart"
OBJECT_DECLARE_SIMPLE_TYPE(EUARTState, EUART)

#define EUART_REG_SIZE 0x1000

/* register offsets */
enum {
    EUART_REG_DATA = 0x00,
    EUART_REG_STATUS = 0x04,
    EUART_REG_CONTROL = 0x08,
    EUART_REG_INT_STATUS = 0x0C,
    EUART_REG_INT_ENABLE = 0x10,
    EUART_REG_DMA_SRC = 0x14,
    EUART_REG_DMA_DST = 0x18,
    EUART_REG_DMA_LEN = 0x1C,
    EUART_REG_DMA_CTRL = 0x20,
    EUART_REG_TIMER_PERIOD = 0x24,
    EUART_REG_TIMER_CTRL = 0x28,
};

/* sizes */
#define EUART_FIFO_SIZE 16
#define EUART_DMA_CHUNK_SIZE 64

/* control bits */
#define EUART_CTRL_TX_ENABLE (1 << 0)
#define EUART_CTRL_RX_ENABLE (1 << 1)
#define EUART_CTRL_RESET     (1 << 2)

/* status bits */
#define EUART_STATUS_TX_READY    (1 << 0)
#define EUART_STATUS_RX_READY    (1 << 1)
#define EUART_STATUS_DMA_BUSY    (1 << 2)
#define EUART_STATUS_TIMER_ACTIVE (1 << 3)

/* interrupts */
#define EUART_INT_TX    (1 << 0)
#define EUART_INT_RX    (1 << 1)
#define EUART_INT_DMA   (1 << 2)
#define EUART_INT_TIMER (1 << 3)

/* DMA ctrl bits */
#define EUART_DMA_DIR     (1 << 0) /* 1 = dev->mem (RX DMA), 0 = mem->dev (TX DMA) */
#define EUART_DMA_START   (1 << 1)
#define EUART_DMA_INT_EN  (1 << 2)

/* Timer ctrl bits */
#define EUART_TIMER_EN       (1 << 0)
#define EUART_TIMER_ONE_SHOT (1 << 1)
#define EUART_TIMER_INT_EN   (1 << 2)

typedef struct EUARTState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    /* char backend */
    CharBackend chr;

    /* FIFOs */
    uint8_t rx_fifo[EUART_FIFO_SIZE];
    uint8_t tx_fifo[EUART_FIFO_SIZE];
    uint32_t rx_fifo_len;
    uint32_t tx_fifo_len;

    /* basic registers */
    uint32_t control;
    uint32_t status;
    uint32_t int_status;
    uint32_t int_enable;

    /* DMA */
    uint64_t dma_src; /* use 64-bit addresses for safety on some guests */
    uint64_t dma_dst;
    uint32_t dma_len;
    uint32_t dma_ctrl;
    uint32_t dma_remaining;
    uint64_t dma_current_addr;

    /* Timers */
    QEMUTimer *dma_timer;
    QEMUTimer *periodic_timer;
    QEMUTimer *tx_timer;
    uint32_t timer_period;
    uint32_t timer_ctrl;

    /* helper */
    uint32_t data;

} EUARTState;

#endif /* HW_CHAR_EUART_H */

