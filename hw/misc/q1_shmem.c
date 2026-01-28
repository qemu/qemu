/*
 * Q1 Shared Memory Implementation
 *
 * Provides shared memory and signaling between the Q1 PCIe device model
 * and the RISC-V firmware running in a separate QEMU instance.
 *
 * Copyright (c) 2026 Qernel AI
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "q1_shmem.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

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
    ctx->signal_sock = -1;
    
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
    
    if (ctx->signal_sock >= 0) {
        close(ctx->signal_sock);
        ctx->signal_sock = -1;
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

/*============================================================================
 * Signaling Functions
 *============================================================================*/

int q1_shmem_connect_signal(Q1ShmemContext *ctx, const char *socket_path)
{
    int sock;
    struct sockaddr_un addr;
    
    if (!ctx || !socket_path) {
        return -EINVAL;
    }
    
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return -errno;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int err = errno;
        close(sock);
        return -err;
    }
    
    ctx->signal_sock = sock;
    ctx->is_server = false;
    
    return 0;
}

int q1_shmem_create_signal_server(Q1ShmemContext *ctx, const char *socket_path)
{
    int sock;
    struct sockaddr_un addr;
    
    if (!ctx || !socket_path) {
        return -EINVAL;
    }
    
    /* Remove existing socket file if present */
    unlink(socket_path);
    
    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        return -errno;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int err = errno;
        close(sock);
        return -err;
    }
    
    if (listen(sock, 1) < 0) {
        int err = errno;
        close(sock);
        unlink(socket_path);
        return -err;
    }
    
    ctx->signal_sock = sock;
    ctx->is_server = true;
    
    return 0;
}

int q1_shmem_accept_signal(Q1ShmemContext *ctx)
{
    int client_sock;
    
    if (!ctx || ctx->signal_sock < 0 || !ctx->is_server) {
        return -EINVAL;
    }
    
    client_sock = accept(ctx->signal_sock, NULL, NULL);
    if (client_sock < 0) {
        return -errno;
    }
    
    return client_sock;
}

int q1_shmem_send_signal(Q1ShmemContext *ctx, uint32_t type, uint32_t value)
{
    Q1Signal signal;
    ssize_t written;
    
    if (!ctx || ctx->signal_sock < 0) {
        return -EINVAL;
    }
    
    signal.type = type;
    signal.value = value;
    
    written = write(ctx->signal_sock, &signal, sizeof(signal));
    if (written < 0) {
        return -errno;
    }
    
    if (written != sizeof(signal)) {
        return -EIO;
    }
    
    return 0;
}

int q1_shmem_recv_signal(Q1ShmemContext *ctx, Q1Signal *signal)
{
    ssize_t bytes_read;
    
    if (!ctx || ctx->signal_sock < 0 || !signal) {
        return -EINVAL;
    }
    
    bytes_read = read(ctx->signal_sock, signal, sizeof(*signal));
    if (bytes_read < 0) {
        return -errno;
    }
    
    if (bytes_read == 0) {
        return -ECONNRESET;  /* Connection closed */
    }
    
    if (bytes_read != sizeof(*signal)) {
        return -EIO;
    }
    
    return 0;
}

int q1_shmem_signal_available(Q1ShmemContext *ctx)
{
    struct pollfd pfd;
    int ret;
    
    if (!ctx || ctx->signal_sock < 0) {
        return -EINVAL;
    }
    
    pfd.fd = ctx->signal_sock;
    pfd.events = POLLIN;
    pfd.revents = 0;
    
    ret = poll(&pfd, 1, 0);  /* Non-blocking poll */
    if (ret < 0) {
        return -errno;
    }
    
    if (ret > 0 && (pfd.revents & POLLIN)) {
        return 1;  /* Signal available */
    }
    
    return 0;  /* No signal */
}
