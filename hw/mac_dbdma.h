/*
 * Copyright (c) 2009 Laurent Vivier
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

typedef struct {
    void *opaque;
    void *channel;
    int len;
    int is_last;
    void *buf;
    int buf_pos;
    int buf_len;
} DBDMA_transfer;

typedef int (*DBDMA_transfer_cb)(DBDMA_transfer *info);
typedef int (*DBDMA_transfer_handler)(DBDMA_transfer *info,
                                      DBDMA_transfer_cb cb);

void DBDMA_register_channel(void *dbdma, int nchan, qemu_irq irq,
                            DBDMA_transfer_handler transfer_handler,
                            void *opaque);
void DBDMA_schedule(void);
void* DBDMA_init (int *dbdma_mem_index);
