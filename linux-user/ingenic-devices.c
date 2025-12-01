/*
 * Ingenic T41/XBurst2 Device Emulation for QEMU Linux User-mode
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu.h"
#include "user-internals.h"
#include "ingenic-devices.h"
#include "qemu/log.h"

/* Global state */
static GHashTable *nna_contexts = NULL;  /* fd -> IngenicNNAContext* */
static QemuMutex nna_lock;
static int next_fake_fd = 1000;  /* Start fake FDs high to avoid conflicts */

/* Forward declarations */
static abi_long nna_ioctl_malloc(IngenicNNAContext *ctx, abi_ulong arg);
static abi_long nna_ioctl_free(IngenicNNAContext *ctx, abi_ulong arg);
static abi_long nna_ioctl_flushcache(IngenicNNAContext *ctx, abi_ulong arg);
static abi_long nna_ioctl_setup_des(IngenicNNAContext *ctx, abi_ulong arg);
static abi_long nna_ioctl_rdch_start(IngenicNNAContext *ctx, abi_ulong arg);
static abi_long nna_ioctl_wrch_start(IngenicNNAContext *ctx, abi_ulong arg);
static abi_long nna_ioctl_version(IngenicNNAContext *ctx, abi_ulong arg);

void ingenic_devices_init(void)
{
    qemu_mutex_init(&nna_lock);
    nna_contexts = g_hash_table_new(g_direct_hash, g_direct_equal);
    qemu_log_mask(LOG_GUEST_ERROR, 
                  "Ingenic T41 device emulation initialized\n");
}

void ingenic_devices_cleanup(void)
{
    if (nna_contexts) {
        /* TODO: Free all contexts */
        g_hash_table_destroy(nna_contexts);
        nna_contexts = NULL;
    }
    qemu_mutex_destroy(&nna_lock);
}

bool ingenic_is_emulated_device(const char *pathname)
{
    if (!pathname) {
        return false;
    }
    return strcmp(pathname, INGENIC_SOC_NNA_PATH) == 0 ||
           strcmp(pathname, INGENIC_MXUV3_PATH) == 0;
}

int ingenic_device_open(const char *pathname, int flags, mode_t mode)
{
    IngenicNNAContext *ctx;
    int fd;
    
    if (strcmp(pathname, INGENIC_SOC_NNA_PATH) == 0) {
        ctx = g_new0(IngenicNNAContext, 1);
        
        qemu_mutex_lock(&nna_lock);
        fd = next_fake_fd++;
        ctx->fd = fd;
        ctx->initialized = true;
        ctx->version = 0x00010000;  /* v1.0.0 */
        
        /* Allocate emulated ORAM */
        ctx->oram_size = NNA_ORAM_SIZE;
        ctx->oram_ptr = g_malloc0(ctx->oram_size);
        ctx->ddr_allocs = NULL;
        
        g_hash_table_insert(nna_contexts, GINT_TO_POINTER(fd), ctx);
        qemu_mutex_unlock(&nna_lock);
        
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ingenic: opened /dev/soc-nna as fd=%d\n", fd);
        return fd;
    }
    
    if (strcmp(pathname, INGENIC_MXUV3_PATH) == 0) {
        /* TODO: Implement mxuv3 emulation */
        qemu_log_mask(LOG_UNIMP,
                      "ingenic: /dev/mxuv3 not yet implemented\n");
        errno = ENODEV;
        return -1;
    }
    
    errno = ENOENT;
    return -1;
}

int ingenic_device_close(int fd)
{
    IngenicNNAContext *ctx;
    
    qemu_mutex_lock(&nna_lock);
    ctx = g_hash_table_lookup(nna_contexts, GINT_TO_POINTER(fd));
    if (ctx) {
        g_hash_table_remove(nna_contexts, GINT_TO_POINTER(fd));
        qemu_mutex_unlock(&nna_lock);
        
        /* Free resources */
        if (ctx->oram_ptr) {
            g_free(ctx->oram_ptr);
        }
        /* TODO: Free DDR allocations */
        g_free(ctx);
        
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ingenic: closed /dev/soc-nna fd=%d\n", fd);
        return 0;
    }
    qemu_mutex_unlock(&nna_lock);
    
    return -1;  /* Not our fd */
}

abi_long ingenic_device_ioctl(int fd, unsigned int cmd, abi_ulong arg)
{
    IngenicNNAContext *ctx;
    abi_long ret = -2;  /* Not our fd */
    
    qemu_mutex_lock(&nna_lock);
    ctx = g_hash_table_lookup(nna_contexts, GINT_TO_POINTER(fd));
    qemu_mutex_unlock(&nna_lock);
    
    if (!ctx) {
        return -2;  /* Not our fd, let normal ioctl handle it */
    }
    
    switch (cmd) {
    case IOCTL_SOC_NNA_MALLOC:
        ret = nna_ioctl_malloc(ctx, arg);
        break;
    case IOCTL_SOC_NNA_FREE:
        ret = nna_ioctl_free(ctx, arg);
        break;
    case IOCTL_SOC_NNA_FLUSHCACHE:
        ret = nna_ioctl_flushcache(ctx, arg);
        break;
    case IOCTL_SOC_NNA_SETUP_DES:
        ret = nna_ioctl_setup_des(ctx, arg);
        break;
    case IOCTL_SOC_NNA_RDCH_START:
        ret = nna_ioctl_rdch_start(ctx, arg);
        break;
    case IOCTL_SOC_NNA_WRCH_START:
        ret = nna_ioctl_wrch_start(ctx, arg);
        break;
    case IOCTL_SOC_NNA_VERSION:
        ret = nna_ioctl_version(ctx, arg);
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "ingenic: unknown NNA ioctl cmd=0x%x\n", cmd);
        ret = -TARGET_ENOTTY;
    }
    
    return ret;
}

/*
 * IOCTL Handlers
 */

/* DDR memory allocation tracking */
typedef struct NNADDRAlloc {
    uint32_t vaddr;
    uint32_t paddr;
    size_t size;
    void *host_ptr;
} NNADDRAlloc;

static abi_long nna_ioctl_malloc(IngenicNNAContext *ctx, abi_ulong arg)
{
    struct soc_nna_buf buf;
    struct soc_nna_buf *target_buf;
    NNADDRAlloc *alloc;
    static uint32_t next_paddr = 0x10000000;  /* Fake physical address */

    target_buf = lock_user(VERIFY_WRITE, arg, sizeof(buf), 1);
    if (!target_buf) {
        return -TARGET_EFAULT;
    }

    /* Read size from guest */
    buf.size = tswap32(target_buf->size);

    if (buf.size <= 0 || buf.size > 64 * 1024 * 1024) {
        unlock_user(target_buf, arg, 0);
        return -TARGET_EINVAL;
    }

    /* Allocate host memory */
    alloc = g_new0(NNADDRAlloc, 1);
    alloc->size = buf.size;
    alloc->host_ptr = g_malloc0(buf.size);
    alloc->paddr = next_paddr;
    next_paddr += (buf.size + 0xFFF) & ~0xFFF;  /* Page aligned */

    /* For now, vaddr = paddr (simplified mapping) */
    alloc->vaddr = alloc->paddr;

    ctx->ddr_allocs = g_list_append(ctx->ddr_allocs, alloc);

    /* Write back to guest */
    target_buf->vaddr = tswap32(alloc->vaddr);
    target_buf->paddr = tswap32(alloc->paddr);
    target_buf->size = tswap32(alloc->size);

    unlock_user(target_buf, arg, sizeof(buf));

    qemu_log_mask(LOG_GUEST_ERROR,
                  "ingenic: NNA malloc size=%d vaddr=0x%x paddr=0x%x\n",
                  (int)alloc->size, alloc->vaddr, alloc->paddr);

    return 0;
}

static abi_long nna_ioctl_free(IngenicNNAContext *ctx, abi_ulong arg)
{
    struct soc_nna_buf buf;
    struct soc_nna_buf *target_buf;
    GList *l;

    target_buf = lock_user(VERIFY_READ, arg, sizeof(buf), 1);
    if (!target_buf) {
        return -TARGET_EFAULT;
    }

    buf.paddr = tswap32(target_buf->paddr);
    unlock_user(target_buf, arg, 0);

    /* Find and free the allocation */
    for (l = ctx->ddr_allocs; l; l = l->next) {
        NNADDRAlloc *alloc = l->data;
        if (alloc->paddr == buf.paddr) {
            ctx->ddr_allocs = g_list_remove(ctx->ddr_allocs, alloc);
            g_free(alloc->host_ptr);
            g_free(alloc);
            qemu_log_mask(LOG_GUEST_ERROR,
                          "ingenic: NNA free paddr=0x%x\n", buf.paddr);
            return 0;
        }
    }

    return -TARGET_EINVAL;
}

static abi_long nna_ioctl_flushcache(IngenicNNAContext *ctx, abi_ulong arg)
{
    struct flush_cache_info info;
    struct flush_cache_info *target_info;

    target_info = lock_user(VERIFY_READ, arg, sizeof(info), 1);
    if (!target_info) {
        return -TARGET_EFAULT;
    }

    info.addr = tswap32(target_info->addr);
    info.len = tswap32(target_info->len);
    info.dir = tswap32(target_info->dir);
    unlock_user(target_info, arg, 0);

    /* In emulation, cache flush is a no-op */
    qemu_log_mask(LOG_GUEST_ERROR,
                  "ingenic: NNA flushcache addr=0x%x len=%d dir=%d (no-op)\n",
                  info.addr, info.len, info.dir);

    return 0;
}

static abi_long nna_ioctl_setup_des(IngenicNNAContext *ctx, abi_ulong arg)
{
    /* DMA descriptor setup - stub for now */
    qemu_log_mask(LOG_UNIMP,
                  "ingenic: NNA setup_des called (stub)\n");
    return 0;
}

static abi_long nna_ioctl_rdch_start(IngenicNNAContext *ctx, abi_ulong arg)
{
    /* Read channel DMA start - stub for now */
    qemu_log_mask(LOG_UNIMP,
                  "ingenic: NNA rdch_start called (stub)\n");
    return 0;
}

static abi_long nna_ioctl_wrch_start(IngenicNNAContext *ctx, abi_ulong arg)
{
    /* Write channel DMA start - stub for now */
    qemu_log_mask(LOG_UNIMP,
                  "ingenic: NNA wrch_start called (stub)\n");
    return 0;
}

static abi_long nna_ioctl_version(IngenicNNAContext *ctx, abi_ulong arg)
{
    uint32_t *target_ver;

    target_ver = lock_user(VERIFY_WRITE, arg, sizeof(uint32_t), 0);
    if (!target_ver) {
        return -TARGET_EFAULT;
    }

    *target_ver = tswap32(ctx->version);
    unlock_user(target_ver, arg, sizeof(uint32_t));

    qemu_log_mask(LOG_GUEST_ERROR,
                  "ingenic: NNA version=0x%x\n", ctx->version);
    return 0;
}

/* mmap support for NNA memory regions */
abi_long ingenic_device_mmap(int fd, abi_ulong start, abi_ulong len,
                              int prot, int flags, abi_ulong offset)
{
    IngenicNNAContext *ctx;

    qemu_mutex_lock(&nna_lock);
    ctx = g_hash_table_lookup(nna_contexts, GINT_TO_POINTER(fd));
    qemu_mutex_unlock(&nna_lock);

    if (!ctx) {
        return -2;  /* Not our fd */
    }

    /* Check if mapping ORAM region */
    if (offset == NNA_ORAM_BASE_ADDR && len <= ctx->oram_size) {
        /* TODO: Properly map to guest address space */
        qemu_log_mask(LOG_UNIMP,
                      "ingenic: NNA mmap ORAM offset=0x%lx len=0x%lx (stub)\n",
                      (unsigned long)offset, (unsigned long)len);
        return -TARGET_ENOSYS;
    }

    /* Check if mapping a DDR allocation */
    GList *l;
    for (l = ctx->ddr_allocs; l; l = l->next) {
        NNADDRAlloc *alloc = l->data;
        if (offset == alloc->paddr && len <= alloc->size) {
            qemu_log_mask(LOG_UNIMP,
                          "ingenic: NNA mmap DDR offset=0x%lx len=0x%lx (stub)\n",
                          (unsigned long)offset, (unsigned long)len);
            return -TARGET_ENOSYS;
        }
    }

    return -TARGET_EINVAL;
}

