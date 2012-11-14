/*
 * QEMU USB emulation, libhw bits.
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
#include "qemu-common.h"
#include "cpu-common.h"
#include "hw/usb.h"
#include "dma.h"

int usb_packet_map(USBPacket *p, QEMUSGList *sgl)
{
    DMADirection dir = (p->pid == USB_TOKEN_IN) ?
        DMA_DIRECTION_FROM_DEVICE : DMA_DIRECTION_TO_DEVICE;
    void *mem;
    int i;

    for (i = 0; i < sgl->nsg; i++) {
        dma_addr_t base = sgl->sg[i].base;
        dma_addr_t len = sgl->sg[i].len;

        while (len) {
            dma_addr_t xlen = len;
            mem = dma_memory_map(sgl->dma, base, &xlen, dir);
            if (!mem) {
                goto err;
            }
            if (xlen > len) {
                xlen = len;
            }
            qemu_iovec_add(&p->iov, mem, xlen);
            len -= xlen;
            base += xlen;
        }
    }
    return 0;

err:
    usb_packet_unmap(p, sgl);
    return -1;
}

void usb_packet_unmap(USBPacket *p, QEMUSGList *sgl)
{
    DMADirection dir = (p->pid == USB_TOKEN_IN) ?
        DMA_DIRECTION_FROM_DEVICE : DMA_DIRECTION_TO_DEVICE;
    int i;

    for (i = 0; i < p->iov.niov; i++) {
        dma_memory_unmap(sgl->dma, p->iov.iov[i].iov_base,
                         p->iov.iov[i].iov_len, dir,
                         p->iov.iov[i].iov_len);
    }
}
