/*
 * NeXT Cube
 *
 * Copyright (c) 2011 Bryce Lanham
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 */

#ifndef NEXT_CUBE_H
#define NEXT_CUBE_H

#define TYPE_NEXTFB "next-fb"

#define TYPE_NEXTKBD "next-kbd"

enum next_dma_chan {
    NEXTDMA_FD,
    NEXTDMA_ENRX,
    NEXTDMA_ENTX,
    NEXTDMA_SCSI,
    NEXTDMA_SCC,
    NEXTDMA_SND
};

#define DMA_ENABLE      0x01000000
#define DMA_SUPDATE     0x02000000
#define DMA_COMPLETE    0x08000000

#define DMA_M2DEV       0x0
#define DMA_SETENABLE   0x00010000
#define DMA_SETSUPDATE  0x00020000
#define DMA_DEV2M       0x00040000
#define DMA_CLRCOMPLETE 0x00080000
#define DMA_RESET       0x00100000

enum next_irqs {
    NEXT_FD_I,
    NEXT_KBD_I,
    NEXT_PWR_I,
    NEXT_ENRX_I,
    NEXT_ENTX_I,
    NEXT_SCSI_I,
    NEXT_CLK_I,
    NEXT_SCC_I,
    NEXT_ENTX_DMA_I,
    NEXT_ENRX_DMA_I,
    NEXT_SCSI_DMA_I,
    NEXT_SCC_DMA_I,
    NEXT_SND_I,
    NEXT_NUM_IRQS
};

#endif /* NEXT_CUBE_H */
