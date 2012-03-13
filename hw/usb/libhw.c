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
    int is_write = (p->pid == USB_TOKEN_IN);
    target_phys_addr_t len;
    void *mem;
    int i;

    for (i = 0; i < sgl->nsg; i++) {
        len = sgl->sg[i].len;
        mem = cpu_physical_memory_map(sgl->sg[i].base, &len,
                                      is_write);
        if (!mem) {
            goto err;
        }
        qemu_iovec_add(&p->iov, mem, len);
        if (len != sgl->sg[i].len) {
            goto err;
        }
    }
    return 0;

err:
    usb_packet_unmap(p);
    return -1;
}

void usb_packet_unmap(USBPacket *p)
{
    int is_write = (p->pid == USB_TOKEN_IN);
    int i;

    for (i = 0; i < p->iov.niov; i++) {
        cpu_physical_memory_unmap(p->iov.iov[i].iov_base,
                                  p->iov.iov[i].iov_len, is_write,
                                  p->iov.iov[i].iov_len);
    }
}
