/*
 * Ingenic T41/XBurst2 Device Emulation for QEMU Linux User-mode
 *
 * This module provides emulation for Ingenic SoC devices when running
 * T41 userspace binaries under qemu-mipsel.
 *
 * Supported devices:
 *   - /dev/soc-nna    (Neural Network Accelerator)
 *   - /dev/mxuv3      (MXU v3 SIMD unit - future)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef INGENIC_DEVICES_H
#define INGENIC_DEVICES_H

#include "qemu/osdep.h"

/*
 * Device paths we intercept
 */
#define INGENIC_SOC_NNA_PATH    "/dev/soc-nna"
#define INGENIC_MXUV3_PATH      "/dev/mxuv3"

/*
 * NNA IOCTL definitions (from thingino-accel/soc-nna/soc_nna.h)
 * Magic number 'c' = 0x63
 *
 * Pre-computed values for _IOWR('c', N, int) to avoid macro expansion issues.
 * On MIPS: _IOWR(type,nr,size) = _IOC(_IOC_READ|_IOC_WRITE,type,nr,sizeof(size))
 *        = ((3 << 29) | ('c' << 8) | nr | (4 << 16))
 *        = 0x60006300 | nr | 0x00040000 = 0x60046300 | nr
 *
 * However, the TARGET may use different encoding. We use Linux standard:
 * _IOWR = _IOC(_IOC_READ|_IOC_WRITE, type, nr, size)
 *       = ((_IOC_READ|_IOC_WRITE) << _IOC_DIRSHIFT) | (type << _IOC_TYPESHIFT) |
 *         (nr << _IOC_NRSHIFT) | (size << _IOC_SIZESHIFT)
 * On Linux x86/MIPS: dir=30, type=8, nr=0, size=16
 * _IOC_READ=2, _IOC_WRITE=1, so READ|WRITE=3
 * = (3 << 30) | ('c' << 8) | (nr << 0) | (4 << 16)
 * = 0xc0000000 | 0x6300 | nr | 0x40000
 * = 0xc0046300 | nr
 */
#define SOC_NNA_MAGIC            'c'
#define IOCTL_SOC_NNA_MALLOC     0xc0046300U  /* _IOWR('c', 0, int) */
#define IOCTL_SOC_NNA_FREE       0xc0046301U  /* _IOWR('c', 1, int) */
#define IOCTL_SOC_NNA_FLUSHCACHE 0xc0046302U  /* _IOWR('c', 2, int) */
#define IOCTL_SOC_NNA_SETUP_DES  0xc0046303U  /* _IOWR('c', 3, int) */
#define IOCTL_SOC_NNA_RDCH_START 0xc0046304U  /* _IOWR('c', 4, int) */
#define IOCTL_SOC_NNA_WRCH_START 0xc0046305U  /* _IOWR('c', 5, int) */
#define IOCTL_SOC_NNA_VERSION    0xc0046306U  /* _IOWR('c', 6, int) */

/*
 * NNA Memory regions
 * ORAM is on-chip SRAM at fixed address
 */
#define NNA_ORAM_BASE_ADDR      0x12600000
#define NNA_ORAM_SIZE           0xe0000     /* (1024-128)*1024 = 896KB */

/*
 * Structures for IOCTL data marshaling
 */
struct soc_nna_buf {
    uint32_t vaddr;     /* Virtual address (guest pointer) */
    uint32_t paddr;     /* Physical address */
    int32_t  size;      /* Buffer size */
};

struct flush_cache_info {
    uint32_t addr;
    uint32_t len;
    uint32_t dir;       /* 0=BIDIR, 1=TO_DEVICE, 2=FROM_DEVICE */
};

struct nna_dma_cmd {
    uint32_t d_va_st_addr;
    uint32_t o_va_st_addr;
    uint32_t o_va_mlc_addr;
    uint32_t o_mlc_bytes;
    uint32_t data_bytes;
    uint32_t des_link;
};

struct des_gen_result {
    uint32_t rcmd_st_idx;
    uint32_t wcmd_st_idx;
    uint32_t dma_chn_num;
    uint32_t finish;
};

struct nna_dma_cmd_set {
    uint32_t rd_cmd_cnt;
    uint32_t rd_cmd_st_idx;
    uint32_t wr_cmd_cnt;
    uint32_t wr_cmd_st_idx;
    uint32_t d_va_cmd;      /* Guest pointer to nna_dma_cmd array */
    uint32_t d_va_chn;      /* Guest pointer to channel array */
    struct des_gen_result des_rslt;
};

/*
 * Device context structure (per open fd)
 */
typedef struct IngenicNNAContext {
    int fd;                     /* File descriptor for this context */
    bool initialized;
    
    /* Emulated ORAM memory */
    void *oram_ptr;
    size_t oram_size;
    
    /* DDR memory allocations list */
    GList *ddr_allocs;
    
    /* Version info to report */
    uint32_t version;
} IngenicNNAContext;

/*
 * API for syscall integration
 */

/* Check if pathname is an Ingenic device we emulate */
bool ingenic_is_emulated_device(const char *pathname);

/* Handle open() for emulated devices, returns fd or -1 */
int ingenic_device_open(const char *pathname, int flags, mode_t mode);

/* Handle close() for emulated devices */
int ingenic_device_close(int fd);

/* Handle ioctl() for emulated devices, returns -2 if not our fd */
abi_long ingenic_device_ioctl(int fd, unsigned int cmd, abi_ulong arg);

/* Handle mmap() for device memory regions */
abi_long ingenic_device_mmap(int fd, abi_ulong start, abi_ulong len,
                              int prot, int flags, abi_ulong offset);

/* Initialize device emulation subsystem */
void ingenic_devices_init(void);

/* Cleanup device emulation subsystem */
void ingenic_devices_cleanup(void);

#endif /* INGENIC_DEVICES_H */

