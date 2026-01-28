/*
 * Q1 Shared Memory Implementation
 *
 * Provides shared memory between the Q1 PCIe device model
 * and the RISC-V firmware running in a separate QEMU instance.
 *
 * Copyright (c) 2026 Qernel AI
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "q1_shmem.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/*============================================================================
 * Shared Memory Functions
 *============================================================================*/

int q1_shmem_init(Q1ShmemContext *ctx, const char *shmem_path, bool create)
{
    int fd;
    void *mapped;
    int flags;
    struct stat st;
    
    if (!ctx || !shmem_path) {
        return -EINVAL;
    }
    
    memset(ctx, 0, sizeof(*ctx));
    ctx->shmem_fd = -1;
    
    /* Open or create the shared memory file */
    flags = O_RDWR;
    if (create) {
        flags |= O_CREAT;
    }
    
    fd = open(shmem_path, flags, 0666);
    if (fd < 0) {
        return -errno;
    }
    
    /* If creating, set the file size */
    if (create) {
        if (fstat(fd, &st) < 0) {
            close(fd);
            return -errno;
        }
        
        if (st.st_size < (off_t)Q1_SHMEM_TOTAL_SIZE) {
            if (ftruncate(fd, Q1_SHMEM_TOTAL_SIZE) < 0) {
                close(fd);
                return -errno;
            }
        }
    } else {
        /* Verify file size */
        if (fstat(fd, &st) < 0) {
            close(fd);
            return -errno;
        }
        
        if (st.st_size < (off_t)Q1_SHMEM_TOTAL_SIZE) {
            close(fd);
            return -EINVAL;
        }
    }
    
    /* Map the shared memory */
    mapped = mmap(NULL, Q1_SHMEM_TOTAL_SIZE, PROT_READ | PROT_WRITE,
                  MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        return -errno;
    }
    
    ctx->shmem_fd = fd;
    ctx->shmem_base = mapped;
    ctx->ddr_base = mapped;
    ctx->ctrl_base = (uint8_t *)mapped + Q1_SHMEM_DDR_SIZE;
    ctx->initialized = true;
    
    /* Initialize control region if creating */
    if (create) {
        q1_shmem_ctrl_init(ctx);
    }
    
    return 0;
}

void q1_shmem_cleanup(Q1ShmemContext *ctx)
{
    if (!ctx) {
        return;
    }
    
    if (ctx->shmem_base) {
        munmap(ctx->shmem_base, Q1_SHMEM_TOTAL_SIZE);
        ctx->shmem_base = NULL;
        ctx->ddr_base = NULL;
        ctx->ctrl_base = NULL;
    }
    
    if (ctx->shmem_fd >= 0) {
        close(ctx->shmem_fd);
        ctx->shmem_fd = -1;
    }
    
    ctx->initialized = false;
}

void q1_shmem_ctrl_init(Q1ShmemContext *ctx)
{
    if (!ctx || !ctx->ctrl_base) {
        return;
    }
    
    /* Zero the control region */
    memset(ctx->ctrl_base, 0, Q1_SHMEM_CTRL_SIZE);
    
    /* Set magic value */
    q1_shmem_ctrl_write32(ctx, Q1_SHMEM_CTRL_MAGIC, Q1_SHMEM_MAGIC);
    
    /* Set initial firmware status */
    q1_shmem_ctrl_write32(ctx, Q1_SHMEM_CTRL_FW_STATUS, Q1_SHMEM_FW_STATUS_RESET);
    
    /* Ensure writes are visible */
    __sync_synchronize();
}
