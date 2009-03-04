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

typedef struct DBDMA_io DBDMA_io;

typedef void (*DBDMA_flush)(DBDMA_io *io);
typedef void (*DBDMA_rw)(DBDMA_io *io);
typedef void (*DBDMA_end)(DBDMA_io *io);
struct DBDMA_io {
    void *opaque;
    void *channel;
    target_phys_addr_t addr;
    int len;
    int is_last;
    int is_dma_out;
    DBDMA_end dma_end;
};


void DBDMA_register_channel(void *dbdma, int nchan, qemu_irq irq,
                            DBDMA_rw rw, DBDMA_flush flush,
                            void *opaque);
void DBDMA_schedule(void);
void* DBDMA_init (int *dbdma_mem_index);
