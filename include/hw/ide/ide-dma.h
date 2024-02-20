#ifndef HW_IDE_DMA_H
#define HW_IDE_DMA_H

#include "block/aio.h"
#include "qemu/iov.h"

typedef struct IDEState IDEState;
typedef struct IDEDMAOps IDEDMAOps;
typedef struct IDEDMA IDEDMA;

typedef void DMAStartFunc(const IDEDMA *, IDEState *, BlockCompletionFunc *);
typedef void DMAVoidFunc(const IDEDMA *);
typedef int DMAIntFunc(const IDEDMA *, bool);
typedef int32_t DMAInt32Func(const IDEDMA *, int32_t len);
typedef void DMAu32Func(const IDEDMA *, uint32_t);
typedef void DMAStopFunc(const IDEDMA *, bool);

struct IDEDMAOps {
    DMAStartFunc *start_dma;
    DMAVoidFunc *pio_transfer;
    DMAInt32Func *prepare_buf;
    DMAu32Func *commit_buf;
    DMAIntFunc *rw_buf;
    DMAVoidFunc *restart;
    DMAVoidFunc *restart_dma;
    DMAStopFunc *set_inactive;
    DMAVoidFunc *cmd_done;
    DMAVoidFunc *reset;
};

struct IDEDMA {
    const IDEDMAOps *ops;
    QEMUIOVector qiov;
    BlockAIOCB *aiocb;
};

#endif
