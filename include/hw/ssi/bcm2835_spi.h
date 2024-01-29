/*
 * BCM2835 SPI Master Controller
 *
 * Copyright (c) 2024 Rayhan Faizel <rayhan.faizel@gmail.com>
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

#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"
#include "qemu/fifo8.h"

#define TYPE_BCM2835_SPI "bcm2835-spi"
OBJECT_DECLARE_SIMPLE_TYPE(BCM2835SPIState, BCM2835_SPI)

/*
 * Though BCM2835 documentation says FIFOs have a capacity of 16,
 * FIFOs are actually 16 words in size or effectively 64 bytes when operating
 * in non DMA mode.
 */
#define FIFO_SIZE               64
#define FIFO_SIZE_3_4           48

#define RO_MASK                 0x1f0000

#define BCM2835_SPI_CS          0x00
#define BCM2835_SPI_FIFO        0x04
#define BCM2835_SPI_CLK         0x08
#define BCM2835_SPI_DLEN        0x0c
#define BCM2835_SPI_LTOH        0x10
#define BCM2835_SPI_DC          0x14

#define BCM2835_SPI_CS_RXF      BIT(20)
#define BCM2835_SPI_CS_RXR      BIT(19)
#define BCM2835_SPI_CS_TXD      BIT(18)
#define BCM2835_SPI_CS_RXD      BIT(17)
#define BCM2835_SPI_CS_DONE     BIT(16)
#define BCM2835_SPI_CS_LEN      BIT(13)
#define BCM2835_SPI_CS_REN      BIT(12)
#define BCM2835_SPI_CS_INTR     BIT(10)
#define BCM2835_SPI_CS_INTD     BIT(9)
#define BCM2835_SPI_CS_DMAEN    BIT(8)
#define BCM2835_SPI_CS_TA       BIT(7)
#define BCM2835_SPI_CLEAR_RX    BIT(5)
#define BCM2835_SPI_CLEAR_TX    BIT(4)

struct BCM2835SPIState {
    /* <private> */
    SysBusDevice parent_obj;

    /* <public> */
    SSIBus *bus;
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t cs;
    uint32_t clk;
    uint32_t dlen;
    uint32_t ltoh;
    uint32_t dc;

    Fifo8 tx_fifo;
    Fifo8 rx_fifo;
};
