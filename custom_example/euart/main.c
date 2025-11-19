#include <stdint.h>

#define EUART_BASE       0x0A100000UL

/* Registers */
#define EUART_REG_DATA      (*(volatile uint32_t *)(EUART_BASE + 0x00))
#define EUART_REG_STATUS    (*(volatile uint32_t *)(EUART_BASE + 0x04))
#define EUART_REG_CONTROL   (*(volatile uint32_t *)(EUART_BASE + 0x08))
#define EUART_INT_STATUS    (*(volatile uint32_t *)(EUART_BASE + 0x0C))
#define EUART_INT_ENABLE    (*(volatile uint32_t *)(EUART_BASE + 0x10))
#define EUART_DMA_SRC       (*(volatile uint32_t *)(EUART_BASE + 0x14))
#define EUART_DMA_DST       (*(volatile uint32_t *)(EUART_BASE + 0x18))
#define EUART_DMA_LEN       (*(volatile uint32_t *)(EUART_BASE + 0x1C))
#define EUART_DMA_CTRL      (*(volatile uint32_t *)(EUART_BASE + 0x20))

/* Bits */
#define EUART_STATUS_TX_READY   (1 << 0)
#define EUART_STATUS_RX_READY   (1 << 1)
#define EUART_CTRL_TX_ENABLE    (1 << 0)
#define EUART_CTRL_RX_ENABLE    (1 << 1)

#define DMA_DIR_DEV2MEM   (1 << 0)     /* RX DMA */
#define DMA_START         (1 << 1)
#define DMA_INT_EN        (1 << 2)

static void euart_putc(char c)
{
    while (!(EUART_REG_STATUS & EUART_STATUS_TX_READY));
    EUART_REG_DATA = (uint32_t)c;
}

static void euart_puts(const char* s)
{
    while (*s) {
        if (*s == '\n') euart_putc('\r');
        euart_putc(*s++);
    }
}

static void euart_init(void)
{
    EUART_REG_CONTROL = EUART_CTRL_TX_ENABLE | EUART_CTRL_RX_ENABLE;
}

/************************************************************
 *                 DMA RX TEST (WORKING)
 ************************************************************/
static char dma_buf[64];

static void dma_rx_test(uint32_t bytes)
{
    EUART_DMA_DST = (uint32_t)dma_buf;
    EUART_DMA_LEN = bytes;

    /* DIR = 1 → Device → Memory */
    EUART_DMA_CTRL = DMA_DIR_DEV2MEM | DMA_START;
}

/************************************************************
 *                 DMA TX TEST (WORKING)
 ************************************************************/

static void dma_tx_test(uint32_t bytes)
{
    EUART_DMA_SRC = (uint32_t)dma_buf;
    EUART_DMA_LEN = bytes;
    EUART_DMA_CTRL = DMA_START;    // DIR=0 (TX)
}


/************************************************************
 *                       MAIN
 ************************************************************/
void main(void)
{
    euart_init();

    euart_puts("Welcome\r\n");

    while (1)
    {
        /* Wait until some bytes arrive in RX FIFO */
        if (EUART_REG_STATUS & EUART_STATUS_RX_READY)
        {
            /* Assume user sends <= 16 bytes (FIFO size) */
            uint32_t count = 16;    // read 16 chars via DMA (example)

            dma_rx_test(count);

            /* Wait until DMA finishes (DMA_START cleared by model) */
            while (EUART_DMA_CTRL & DMA_START);

            dma_tx_test(count);

            for (uint32_t i = 0; i < count; i++) {
                dma_buf[i] = 0;
            }
        }
    }
}


