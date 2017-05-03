/*
 * QEMU Firmware configuration device emulation
 *
 * Copyright (c) 2008 Gleb Natapov
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
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "sysemu/sysemu.h"
#include "sysemu/dma.h"
#include "hw/boards.h"
#include "hw/isa/isa.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/sysbus.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"
#include "qemu/cutils.h"
#include "qapi/error.h"

#define FW_CFG_FILE_SLOTS_DFLT 0x20

#define FW_CFG_NAME "fw_cfg"
#define FW_CFG_PATH "/machine/" FW_CFG_NAME

#define TYPE_FW_CFG     "fw_cfg"
#define TYPE_FW_CFG_IO  "fw_cfg_io"
#define TYPE_FW_CFG_MEM "fw_cfg_mem"

#define FW_CFG(obj)     OBJECT_CHECK(FWCfgState,    (obj), TYPE_FW_CFG)
#define FW_CFG_IO(obj)  OBJECT_CHECK(FWCfgIoState,  (obj), TYPE_FW_CFG_IO)
#define FW_CFG_MEM(obj) OBJECT_CHECK(FWCfgMemState, (obj), TYPE_FW_CFG_MEM)

/* FW_CFG_VERSION bits */
#define FW_CFG_VERSION      0x01
#define FW_CFG_VERSION_DMA  0x02

/* FW_CFG_DMA_CONTROL bits */
#define FW_CFG_DMA_CTL_ERROR   0x01
#define FW_CFG_DMA_CTL_READ    0x02
#define FW_CFG_DMA_CTL_SKIP    0x04
#define FW_CFG_DMA_CTL_SELECT  0x08
#define FW_CFG_DMA_CTL_WRITE   0x10

#define FW_CFG_DMA_SIGNATURE 0x51454d5520434647ULL /* "QEMU CFG" */

typedef struct FWCfgEntry {
    uint32_t len;
    bool allow_write;
    uint8_t *data;
    void *callback_opaque;
    FWCfgReadCallback read_callback;
} FWCfgEntry;

struct FWCfgState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    uint16_t file_slots;
    FWCfgEntry *entries[2];
    int *entry_order;
    FWCfgFiles *files;
    uint16_t cur_entry;
    uint32_t cur_offset;
    Notifier machine_ready;

    int fw_cfg_order_override;

    bool dma_enabled;
    dma_addr_t dma_addr;
    AddressSpace *dma_as;
    MemoryRegion dma_iomem;
};

struct FWCfgIoState {
    /*< private >*/
    FWCfgState parent_obj;
    /*< public >*/

    MemoryRegion comb_iomem;
    uint32_t iobase, dma_iobase;
};

struct FWCfgMemState {
    /*< private >*/
    FWCfgState parent_obj;
    /*< public >*/

    MemoryRegion ctl_iomem, data_iomem;
    uint32_t data_width;
    MemoryRegionOps wide_data_ops;
};

#define JPG_FILE 0
#define BMP_FILE 1

static char *read_splashfile(char *filename, gsize *file_sizep,
                             int *file_typep)
{
    GError *err = NULL;
    gboolean res;
    gchar *content;
    int file_type;
    unsigned int filehead;
    int bmp_bpp;

    res = g_file_get_contents(filename, &content, file_sizep, &err);
    if (res == FALSE) {
        error_report("failed to read splash file '%s'", filename);
        g_error_free(err);
        return NULL;
    }

    /* check file size */
    if (*file_sizep < 30) {
        goto error;
    }

    /* check magic ID */
    filehead = ((content[0] & 0xff) + (content[1] << 8)) & 0xffff;
    if (filehead == 0xd8ff) {
        file_type = JPG_FILE;
    } else if (filehead == 0x4d42) {
        file_type = BMP_FILE;
    } else {
        goto error;
    }

    /* check BMP bpp */
    if (file_type == BMP_FILE) {
        bmp_bpp = (content[28] + (content[29] << 8)) & 0xffff;
        if (bmp_bpp != 24) {
            goto error;
        }
    }

    /* return values */
    *file_typep = file_type;

    return content;

error:
    error_report("splash file '%s' format not recognized; must be JPEG "
                 "or 24 bit BMP", filename);
    g_free(content);
    return NULL;
}

static void fw_cfg_bootsplash(FWCfgState *s)
{
    int boot_splash_time = -1;
    const char *boot_splash_filename = NULL;
    char *p;
    char *filename, *file_data;
    gsize file_size;
    int file_type;
    const char *temp;

    /* get user configuration */
    QemuOptsList *plist = qemu_find_opts("boot-opts");
    QemuOpts *opts = QTAILQ_FIRST(&plist->head);
    if (opts != NULL) {
        temp = qemu_opt_get(opts, "splash");
        if (temp != NULL) {
            boot_splash_filename = temp;
        }
        temp = qemu_opt_get(opts, "splash-time");
        if (temp != NULL) {
            p = (char *)temp;
            boot_splash_time = strtol(p, &p, 10);
        }
    }

    /* insert splash time if user configurated */
    if (boot_splash_time >= 0) {
        /* validate the input */
        if (boot_splash_time > 0xffff) {
            error_report("splash time is big than 65535, force it to 65535.");
            boot_splash_time = 0xffff;
        }
        /* use little endian format */
        qemu_extra_params_fw[0] = (uint8_t)(boot_splash_time & 0xff);
        qemu_extra_params_fw[1] = (uint8_t)((boot_splash_time >> 8) & 0xff);
        fw_cfg_add_file(s, "etc/boot-menu-wait", qemu_extra_params_fw, 2);
    }

    /* insert splash file if user configurated */
    if (boot_splash_filename != NULL) {
        filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, boot_splash_filename);
        if (filename == NULL) {
            error_report("failed to find file '%s'.", boot_splash_filename);
            return;
        }

        /* loading file data */
        file_data = read_splashfile(filename, &file_size, &file_type);
        if (file_data == NULL) {
            g_free(filename);
            return;
        }
        g_free(boot_splash_filedata);
        boot_splash_filedata = (uint8_t *)file_data;
        boot_splash_filedata_size = file_size;

        /* insert data */
        if (file_type == JPG_FILE) {
            fw_cfg_add_file(s, "bootsplash.jpg",
                    boot_splash_filedata, boot_splash_filedata_size);
        } else {
            fw_cfg_add_file(s, "bootsplash.bmp",
                    boot_splash_filedata, boot_splash_filedata_size);
        }
        g_free(filename);
    }
}

static void fw_cfg_reboot(FWCfgState *s)
{
    int reboot_timeout = -1;
    char *p;
    const char *temp;

    /* get user configuration */
    QemuOptsList *plist = qemu_find_opts("boot-opts");
    QemuOpts *opts = QTAILQ_FIRST(&plist->head);
    if (opts != NULL) {
        temp = qemu_opt_get(opts, "reboot-timeout");
        if (temp != NULL) {
            p = (char *)temp;
            reboot_timeout = strtol(p, &p, 10);
        }
    }
    /* validate the input */
    if (reboot_timeout > 0xffff) {
        error_report("reboot timeout is larger than 65535, force it to 65535.");
        reboot_timeout = 0xffff;
    }
    fw_cfg_add_file(s, "etc/boot-fail-wait", g_memdup(&reboot_timeout, 4), 4);
}

static void fw_cfg_write(FWCfgState *s, uint8_t value)
{
    /* nothing, write support removed in QEMU v2.4+ */
}

static inline uint16_t fw_cfg_file_slots(const FWCfgState *s)
{
    return s->file_slots;
}

/* Note: this function returns an exclusive limit. */
static inline uint32_t fw_cfg_max_entry(const FWCfgState *s)
{
    return FW_CFG_FILE_FIRST + fw_cfg_file_slots(s);
}

static int fw_cfg_select(FWCfgState *s, uint16_t key)
{
    int arch, ret;
    FWCfgEntry *e;

    s->cur_offset = 0;
    if ((key & FW_CFG_ENTRY_MASK) >= fw_cfg_max_entry(s)) {
        s->cur_entry = FW_CFG_INVALID;
        ret = 0;
    } else {
        s->cur_entry = key;
        ret = 1;
        /* entry successfully selected, now run callback if present */
        arch = !!(key & FW_CFG_ARCH_LOCAL);
        e = &s->entries[arch][key & FW_CFG_ENTRY_MASK];
        if (e->read_callback) {
            e->read_callback(e->callback_opaque);
        }
    }

    trace_fw_cfg_select(s, key, ret);
    return ret;
}

static uint64_t fw_cfg_data_read(void *opaque, hwaddr addr, unsigned size)
{
    FWCfgState *s = opaque;
    int arch = !!(s->cur_entry & FW_CFG_ARCH_LOCAL);
    FWCfgEntry *e = (s->cur_entry == FW_CFG_INVALID) ? NULL :
                    &s->entries[arch][s->cur_entry & FW_CFG_ENTRY_MASK];
    uint64_t value = 0;

    assert(size > 0 && size <= sizeof(value));
    if (s->cur_entry != FW_CFG_INVALID && e->data && s->cur_offset < e->len) {
        /* The least significant 'size' bytes of the return value are
         * expected to contain a string preserving portion of the item
         * data, padded with zeros on the right in case we run out early.
         * In technical terms, we're composing the host-endian representation
         * of the big endian interpretation of the fw_cfg string.
         */
        do {
            value = (value << 8) | e->data[s->cur_offset++];
        } while (--size && s->cur_offset < e->len);
        /* If size is still not zero, we *did* run out early, so continue
         * left-shifting, to add the appropriate number of padding zeros
         * on the right.
         */
        value <<= 8 * size;
    }

    trace_fw_cfg_read(s, value);
    return value;
}

static void fw_cfg_data_mem_write(void *opaque, hwaddr addr,
                                  uint64_t value, unsigned size)
{
    FWCfgState *s = opaque;
    unsigned i = size;

    do {
        fw_cfg_write(s, value >> (8 * --i));
    } while (i);
}

static void fw_cfg_dma_transfer(FWCfgState *s)
{
    dma_addr_t len;
    FWCfgDmaAccess dma;
    int arch;
    FWCfgEntry *e;
    int read = 0, write = 0;
    dma_addr_t dma_addr;

    /* Reset the address before the next access */
    dma_addr = s->dma_addr;
    s->dma_addr = 0;

    if (dma_memory_read(s->dma_as, dma_addr, &dma, sizeof(dma))) {
        stl_be_dma(s->dma_as, dma_addr + offsetof(FWCfgDmaAccess, control),
                   FW_CFG_DMA_CTL_ERROR);
        return;
    }

    dma.address = be64_to_cpu(dma.address);
    dma.length = be32_to_cpu(dma.length);
    dma.control = be32_to_cpu(dma.control);

    if (dma.control & FW_CFG_DMA_CTL_SELECT) {
        fw_cfg_select(s, dma.control >> 16);
    }

    arch = !!(s->cur_entry & FW_CFG_ARCH_LOCAL);
    e = (s->cur_entry == FW_CFG_INVALID) ? NULL :
        &s->entries[arch][s->cur_entry & FW_CFG_ENTRY_MASK];

    if (dma.control & FW_CFG_DMA_CTL_READ) {
        read = 1;
        write = 0;
    } else if (dma.control & FW_CFG_DMA_CTL_WRITE) {
        read = 0;
        write = 1;
    } else if (dma.control & FW_CFG_DMA_CTL_SKIP) {
        read = 0;
        write = 0;
    } else {
        dma.length = 0;
    }

    dma.control = 0;

    while (dma.length > 0 && !(dma.control & FW_CFG_DMA_CTL_ERROR)) {
        if (s->cur_entry == FW_CFG_INVALID || !e->data ||
                                s->cur_offset >= e->len) {
            len = dma.length;

            /* If the access is not a read access, it will be a skip access,
             * tested before.
             */
            if (read) {
                if (dma_memory_set(s->dma_as, dma.address, 0, len)) {
                    dma.control |= FW_CFG_DMA_CTL_ERROR;
                }
            }
            if (write) {
                dma.control |= FW_CFG_DMA_CTL_ERROR;
            }
        } else {
            if (dma.length <= (e->len - s->cur_offset)) {
                len = dma.length;
            } else {
                len = (e->len - s->cur_offset);
            }

            /* If the access is not a read access, it will be a skip access,
             * tested before.
             */
            if (read) {
                if (dma_memory_write(s->dma_as, dma.address,
                                    &e->data[s->cur_offset], len)) {
                    dma.control |= FW_CFG_DMA_CTL_ERROR;
                }
            }
            if (write) {
                if (!e->allow_write ||
                    len != dma.length ||
                    dma_memory_read(s->dma_as, dma.address,
                                    &e->data[s->cur_offset], len)) {
                    dma.control |= FW_CFG_DMA_CTL_ERROR;
                }
            }

            s->cur_offset += len;
        }

        dma.address += len;
        dma.length  -= len;

    }

    stl_be_dma(s->dma_as, dma_addr + offsetof(FWCfgDmaAccess, control),
                dma.control);

    trace_fw_cfg_read(s, 0);
}

static uint64_t fw_cfg_dma_mem_read(void *opaque, hwaddr addr,
                                    unsigned size)
{
    /* Return a signature value (and handle various read sizes) */
    return extract64(FW_CFG_DMA_SIGNATURE, (8 - addr - size) * 8, size * 8);
}

static void fw_cfg_dma_mem_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    FWCfgState *s = opaque;

    if (size == 4) {
        if (addr == 0) {
            /* FWCfgDmaAccess high address */
            s->dma_addr = value << 32;
        } else if (addr == 4) {
            /* FWCfgDmaAccess low address */
            s->dma_addr |= value;
            fw_cfg_dma_transfer(s);
        }
    } else if (size == 8 && addr == 0) {
        s->dma_addr = value;
        fw_cfg_dma_transfer(s);
    }
}

static bool fw_cfg_dma_mem_valid(void *opaque, hwaddr addr,
                                  unsigned size, bool is_write)
{
    return !is_write || ((size == 4 && (addr == 0 || addr == 4)) ||
                         (size == 8 && addr == 0));
}

static bool fw_cfg_data_mem_valid(void *opaque, hwaddr addr,
                                  unsigned size, bool is_write)
{
    return addr == 0;
}

static void fw_cfg_ctl_mem_write(void *opaque, hwaddr addr,
                                 uint64_t value, unsigned size)
{
    fw_cfg_select(opaque, (uint16_t)value);
}

static bool fw_cfg_ctl_mem_valid(void *opaque, hwaddr addr,
                                 unsigned size, bool is_write)
{
    return is_write && size == 2;
}

static void fw_cfg_comb_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    switch (size) {
    case 1:
        fw_cfg_write(opaque, (uint8_t)value);
        break;
    case 2:
        fw_cfg_select(opaque, (uint16_t)value);
        break;
    }
}

static bool fw_cfg_comb_valid(void *opaque, hwaddr addr,
                                  unsigned size, bool is_write)
{
    return (size == 1) || (is_write && size == 2);
}

static const MemoryRegionOps fw_cfg_ctl_mem_ops = {
    .write = fw_cfg_ctl_mem_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid.accepts = fw_cfg_ctl_mem_valid,
};

static const MemoryRegionOps fw_cfg_data_mem_ops = {
    .read = fw_cfg_data_read,
    .write = fw_cfg_data_mem_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
        .accepts = fw_cfg_data_mem_valid,
    },
};

static const MemoryRegionOps fw_cfg_comb_mem_ops = {
    .read = fw_cfg_data_read,
    .write = fw_cfg_comb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.accepts = fw_cfg_comb_valid,
};

static const MemoryRegionOps fw_cfg_dma_mem_ops = {
    .read = fw_cfg_dma_mem_read,
    .write = fw_cfg_dma_mem_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid.accepts = fw_cfg_dma_mem_valid,
    .valid.max_access_size = 8,
    .impl.max_access_size = 8,
};

static void fw_cfg_reset(DeviceState *d)
{
    FWCfgState *s = FW_CFG(d);

    /* we never register a read callback for FW_CFG_SIGNATURE */
    fw_cfg_select(s, FW_CFG_SIGNATURE);
}

/* Save restore 32 bit int as uint16_t
   This is a Big hack, but it is how the old state did it.
   Or we broke compatibility in the state, or we can't use struct tm
 */

static int get_uint32_as_uint16(QEMUFile *f, void *pv, size_t size,
                                VMStateField *field)
{
    uint32_t *v = pv;
    *v = qemu_get_be16(f);
    return 0;
}

static int put_unused(QEMUFile *f, void *pv, size_t size, VMStateField *field,
                      QJSON *vmdesc)
{
    fprintf(stderr, "uint32_as_uint16 is only used for backward compatibility.\n");
    fprintf(stderr, "This functions shouldn't be called.\n");

    return 0;
}

static const VMStateInfo vmstate_hack_uint32_as_uint16 = {
    .name = "int32_as_uint16",
    .get  = get_uint32_as_uint16,
    .put  = put_unused,
};

#define VMSTATE_UINT16_HACK(_f, _s, _t)                                    \
    VMSTATE_SINGLE_TEST(_f, _s, _t, 0, vmstate_hack_uint32_as_uint16, uint32_t)


static bool is_version_1(void *opaque, int version_id)
{
    return version_id == 1;
}

bool fw_cfg_dma_enabled(void *opaque)
{
    FWCfgState *s = opaque;

    return s->dma_enabled;
}

static const VMStateDescription vmstate_fw_cfg_dma = {
    .name = "fw_cfg/dma",
    .needed = fw_cfg_dma_enabled,
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(dma_addr, FWCfgState),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_fw_cfg = {
    .name = "fw_cfg",
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(cur_entry, FWCfgState),
        VMSTATE_UINT16_HACK(cur_offset, FWCfgState, is_version_1),
        VMSTATE_UINT32_V(cur_offset, FWCfgState, 2),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription*[]) {
        &vmstate_fw_cfg_dma,
        NULL,
    }
};

static void fw_cfg_add_bytes_read_callback(FWCfgState *s, uint16_t key,
                                           FWCfgReadCallback callback,
                                           void *callback_opaque,
                                           void *data, size_t len,
                                           bool read_only)
{
    int arch = !!(key & FW_CFG_ARCH_LOCAL);

    key &= FW_CFG_ENTRY_MASK;

    assert(key < fw_cfg_max_entry(s) && len < UINT32_MAX);
    assert(s->entries[arch][key].data == NULL); /* avoid key conflict */

    s->entries[arch][key].data = data;
    s->entries[arch][key].len = (uint32_t)len;
    s->entries[arch][key].read_callback = callback;
    s->entries[arch][key].callback_opaque = callback_opaque;
    s->entries[arch][key].allow_write = !read_only;
}

static void *fw_cfg_modify_bytes_read(FWCfgState *s, uint16_t key,
                                              void *data, size_t len)
{
    void *ptr;
    int arch = !!(key & FW_CFG_ARCH_LOCAL);

    key &= FW_CFG_ENTRY_MASK;

    assert(key < fw_cfg_max_entry(s) && len < UINT32_MAX);

    /* return the old data to the function caller, avoid memory leak */
    ptr = s->entries[arch][key].data;
    s->entries[arch][key].data = data;
    s->entries[arch][key].len = len;
    s->entries[arch][key].callback_opaque = NULL;
    s->entries[arch][key].allow_write = false;

    return ptr;
}

void fw_cfg_add_bytes(FWCfgState *s, uint16_t key, void *data, size_t len)
{
    fw_cfg_add_bytes_read_callback(s, key, NULL, NULL, data, len, true);
}

void fw_cfg_add_string(FWCfgState *s, uint16_t key, const char *value)
{
    size_t sz = strlen(value) + 1;

    fw_cfg_add_bytes(s, key, g_memdup(value, sz), sz);
}

void fw_cfg_add_i16(FWCfgState *s, uint16_t key, uint16_t value)
{
    uint16_t *copy;

    copy = g_malloc(sizeof(value));
    *copy = cpu_to_le16(value);
    fw_cfg_add_bytes(s, key, copy, sizeof(value));
}

void fw_cfg_modify_i16(FWCfgState *s, uint16_t key, uint16_t value)
{
    uint16_t *copy, *old;

    copy = g_malloc(sizeof(value));
    *copy = cpu_to_le16(value);
    old = fw_cfg_modify_bytes_read(s, key, copy, sizeof(value));
    g_free(old);
}

void fw_cfg_add_i32(FWCfgState *s, uint16_t key, uint32_t value)
{
    uint32_t *copy;

    copy = g_malloc(sizeof(value));
    *copy = cpu_to_le32(value);
    fw_cfg_add_bytes(s, key, copy, sizeof(value));
}

void fw_cfg_add_i64(FWCfgState *s, uint16_t key, uint64_t value)
{
    uint64_t *copy;

    copy = g_malloc(sizeof(value));
    *copy = cpu_to_le64(value);
    fw_cfg_add_bytes(s, key, copy, sizeof(value));
}

void fw_cfg_set_order_override(FWCfgState *s, int order)
{
    assert(s->fw_cfg_order_override == 0);
    s->fw_cfg_order_override = order;
}

void fw_cfg_reset_order_override(FWCfgState *s)
{
    assert(s->fw_cfg_order_override != 0);
    s->fw_cfg_order_override = 0;
}

/*
 * This is the legacy order list.  For legacy systems, files are in
 * the fw_cfg in the order defined below, by the "order" value.  Note
 * that some entries (VGA ROMs, NIC option ROMS, etc.) go into a
 * specific area, but there may be more than one and they occur in the
 * order that the user specifies them on the command line.  Those are
 * handled in a special manner, using the order override above.
 *
 * For non-legacy, the files are sorted by filename to avoid this kind
 * of complexity in the future.
 *
 * This is only for x86, other arches don't implement versioning so
 * they won't set legacy mode.
 */
static struct {
    const char *name;
    int order;
} fw_cfg_order[] = {
    { "etc/boot-menu-wait", 10 },
    { "bootsplash.jpg", 11 },
    { "bootsplash.bmp", 12 },
    { "etc/boot-fail-wait", 15 },
    { "etc/smbios/smbios-tables", 20 },
    { "etc/smbios/smbios-anchor", 30 },
    { "etc/e820", 40 },
    { "etc/reserved-memory-end", 50 },
    { "genroms/kvmvapic.bin", 55 },
    { "genroms/linuxboot.bin", 60 },
    { }, /* VGA ROMs from pc_vga_init come here, 70. */
    { }, /* NIC option ROMs from pc_nic_init come here, 80. */
    { "etc/system-states", 90 },
    { }, /* User ROMs come here, 100. */
    { }, /* Device FW comes here, 110. */
    { "etc/extra-pci-roots", 120 },
    { "etc/acpi/tables", 130 },
    { "etc/table-loader", 140 },
    { "etc/tpm/log", 150 },
    { "etc/acpi/rsdp", 160 },
    { "bootorder", 170 },

#define FW_CFG_ORDER_OVERRIDE_LAST 200
};

static int get_fw_cfg_order(FWCfgState *s, const char *name)
{
    int i;

    if (s->fw_cfg_order_override > 0) {
        return s->fw_cfg_order_override;
    }

    for (i = 0; i < ARRAY_SIZE(fw_cfg_order); i++) {
        if (fw_cfg_order[i].name == NULL) {
            continue;
        }

        if (strcmp(name, fw_cfg_order[i].name) == 0) {
            return fw_cfg_order[i].order;
        }
    }

    /* Stick unknown stuff at the end. */
    error_report("warning: Unknown firmware file in legacy mode: %s", name);
    return FW_CFG_ORDER_OVERRIDE_LAST;
}

void fw_cfg_add_file_callback(FWCfgState *s,  const char *filename,
                              FWCfgReadCallback callback, void *callback_opaque,
                              void *data, size_t len, bool read_only)
{
    int i, index, count;
    size_t dsize;
    MachineClass *mc = MACHINE_GET_CLASS(qdev_get_machine());
    int order = 0;

    if (!s->files) {
        dsize = sizeof(uint32_t) + sizeof(FWCfgFile) * fw_cfg_file_slots(s);
        s->files = g_malloc0(dsize);
        fw_cfg_add_bytes(s, FW_CFG_FILE_DIR, s->files, dsize);
    }

    count = be32_to_cpu(s->files->count);
    assert(count < fw_cfg_file_slots(s));

    /* Find the insertion point. */
    if (mc->legacy_fw_cfg_order) {
        /*
         * Sort by order. For files with the same order, we keep them
         * in the sequence in which they were added.
         */
        order = get_fw_cfg_order(s, filename);
        for (index = count;
             index > 0 && order < s->entry_order[index - 1];
             index--);
    } else {
        /* Sort by file name. */
        for (index = count;
             index > 0 && strcmp(filename, s->files->f[index - 1].name) < 0;
             index--);
    }

    /*
     * Move all the entries from the index point and after down one
     * to create a slot for the new entry.  Because calculations are
     * being done with the index, make it so that "i" is the current
     * index and "i - 1" is the one being copied from, thus the
     * unusual start and end in the for statement.
     */
    for (i = count + 1; i > index; i--) {
        s->files->f[i] = s->files->f[i - 1];
        s->files->f[i].select = cpu_to_be16(FW_CFG_FILE_FIRST + i);
        s->entries[0][FW_CFG_FILE_FIRST + i] =
            s->entries[0][FW_CFG_FILE_FIRST + i - 1];
        s->entry_order[i] = s->entry_order[i - 1];
    }

    memset(&s->files->f[index], 0, sizeof(FWCfgFile));
    memset(&s->entries[0][FW_CFG_FILE_FIRST + index], 0, sizeof(FWCfgEntry));

    pstrcpy(s->files->f[index].name, sizeof(s->files->f[index].name), filename);
    for (i = 0; i <= count; i++) {
        if (i != index &&
            strcmp(s->files->f[index].name, s->files->f[i].name) == 0) {
            error_report("duplicate fw_cfg file name: %s",
                         s->files->f[index].name);
            exit(1);
        }
    }

    fw_cfg_add_bytes_read_callback(s, FW_CFG_FILE_FIRST + index,
                                   callback, callback_opaque, data, len,
                                   read_only);

    s->files->f[index].size   = cpu_to_be32(len);
    s->files->f[index].select = cpu_to_be16(FW_CFG_FILE_FIRST + index);
    s->entry_order[index] = order;
    trace_fw_cfg_add_file(s, index, s->files->f[index].name, len);

    s->files->count = cpu_to_be32(count+1);
}

void fw_cfg_add_file(FWCfgState *s,  const char *filename,
                     void *data, size_t len)
{
    fw_cfg_add_file_callback(s, filename, NULL, NULL, data, len, true);
}

void *fw_cfg_modify_file(FWCfgState *s, const char *filename,
                        void *data, size_t len)
{
    int i, index;
    void *ptr = NULL;

    assert(s->files);

    index = be32_to_cpu(s->files->count);
    assert(index < fw_cfg_file_slots(s));

    for (i = 0; i < index; i++) {
        if (strcmp(filename, s->files->f[i].name) == 0) {
            ptr = fw_cfg_modify_bytes_read(s, FW_CFG_FILE_FIRST + i,
                                           data, len);
            s->files->f[i].size   = cpu_to_be32(len);
            return ptr;
        }
    }
    /* add new one */
    fw_cfg_add_file_callback(s, filename, NULL, NULL, data, len, true);
    return NULL;
}

static void fw_cfg_machine_reset(void *opaque)
{
    void *ptr;
    size_t len;
    FWCfgState *s = opaque;
    char *bootindex = get_boot_devices_list(&len, false);

    ptr = fw_cfg_modify_file(s, "bootorder", (uint8_t *)bootindex, len);
    g_free(ptr);
}

static void fw_cfg_machine_ready(struct Notifier *n, void *data)
{
    FWCfgState *s = container_of(n, FWCfgState, machine_ready);
    qemu_register_reset(fw_cfg_machine_reset, s);
}



static void fw_cfg_init1(DeviceState *dev)
{
    FWCfgState *s = FW_CFG(dev);
    MachineState *machine = MACHINE(qdev_get_machine());

    assert(!object_resolve_path(FW_CFG_PATH, NULL));

    object_property_add_child(OBJECT(machine), FW_CFG_NAME, OBJECT(s), NULL);

    qdev_init_nofail(dev);

    fw_cfg_add_bytes(s, FW_CFG_SIGNATURE, (char *)"QEMU", 4);
    fw_cfg_add_bytes(s, FW_CFG_UUID, &qemu_uuid, 16);
    fw_cfg_add_i16(s, FW_CFG_NOGRAPHIC, (uint16_t)!machine->enable_graphics);
    fw_cfg_add_i16(s, FW_CFG_BOOT_MENU, (uint16_t)boot_menu);
    fw_cfg_bootsplash(s);
    fw_cfg_reboot(s);

    s->machine_ready.notify = fw_cfg_machine_ready;
    qemu_add_machine_init_done_notifier(&s->machine_ready);
}

FWCfgState *fw_cfg_init_io_dma(uint32_t iobase, uint32_t dma_iobase,
                                AddressSpace *dma_as)
{
    DeviceState *dev;
    FWCfgState *s;
    uint32_t version = FW_CFG_VERSION;
    bool dma_requested = dma_iobase && dma_as;

    dev = qdev_create(NULL, TYPE_FW_CFG_IO);
    qdev_prop_set_uint32(dev, "iobase", iobase);
    qdev_prop_set_uint32(dev, "dma_iobase", dma_iobase);
    if (!dma_requested) {
        qdev_prop_set_bit(dev, "dma_enabled", false);
    }

    fw_cfg_init1(dev);
    s = FW_CFG(dev);

    if (s->dma_enabled) {
        /* 64 bits for the address field */
        s->dma_as = dma_as;
        s->dma_addr = 0;

        version |= FW_CFG_VERSION_DMA;
    }

    fw_cfg_add_i32(s, FW_CFG_ID, version);

    return s;
}

FWCfgState *fw_cfg_init_io(uint32_t iobase)
{
    return fw_cfg_init_io_dma(iobase, 0, NULL);
}

FWCfgState *fw_cfg_init_mem_wide(hwaddr ctl_addr,
                                 hwaddr data_addr, uint32_t data_width,
                                 hwaddr dma_addr, AddressSpace *dma_as)
{
    DeviceState *dev;
    SysBusDevice *sbd;
    FWCfgState *s;
    uint32_t version = FW_CFG_VERSION;
    bool dma_requested = dma_addr && dma_as;

    dev = qdev_create(NULL, TYPE_FW_CFG_MEM);
    qdev_prop_set_uint32(dev, "data_width", data_width);
    if (!dma_requested) {
        qdev_prop_set_bit(dev, "dma_enabled", false);
    }

    fw_cfg_init1(dev);

    sbd = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(sbd, 0, ctl_addr);
    sysbus_mmio_map(sbd, 1, data_addr);

    s = FW_CFG(dev);

    if (s->dma_enabled) {
        s->dma_as = dma_as;
        s->dma_addr = 0;
        sysbus_mmio_map(sbd, 2, dma_addr);
        version |= FW_CFG_VERSION_DMA;
    }

    fw_cfg_add_i32(s, FW_CFG_ID, version);

    return s;
}

FWCfgState *fw_cfg_init_mem(hwaddr ctl_addr, hwaddr data_addr)
{
    return fw_cfg_init_mem_wide(ctl_addr, data_addr,
                                fw_cfg_data_mem_ops.valid.max_access_size,
                                0, NULL);
}


FWCfgState *fw_cfg_find(void)
{
    return FW_CFG(object_resolve_path(FW_CFG_PATH, NULL));
}

static void fw_cfg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = fw_cfg_reset;
    dc->vmsd = &vmstate_fw_cfg;
}

static const TypeInfo fw_cfg_info = {
    .name          = TYPE_FW_CFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .abstract      = true,
    .instance_size = sizeof(FWCfgState),
    .class_init    = fw_cfg_class_init,
};

static void fw_cfg_file_slots_allocate(FWCfgState *s, Error **errp)
{
    uint16_t file_slots_max;

    if (fw_cfg_file_slots(s) < FW_CFG_FILE_SLOTS_MIN) {
        error_setg(errp, "\"file_slots\" must be at least 0x%x",
                   FW_CFG_FILE_SLOTS_MIN);
        return;
    }

    /* (UINT16_MAX & FW_CFG_ENTRY_MASK) is the highest inclusive selector value
     * that we permit. The actual (exclusive) value coming from the
     * configuration is (FW_CFG_FILE_FIRST + fw_cfg_file_slots(s)). */
    file_slots_max = (UINT16_MAX & FW_CFG_ENTRY_MASK) - FW_CFG_FILE_FIRST + 1;
    if (fw_cfg_file_slots(s) > file_slots_max) {
        error_setg(errp, "\"file_slots\" must not exceed 0x%" PRIx16,
                   file_slots_max);
        return;
    }

    s->entries[0] = g_new0(FWCfgEntry, fw_cfg_max_entry(s));
    s->entries[1] = g_new0(FWCfgEntry, fw_cfg_max_entry(s));
    s->entry_order = g_new0(int, fw_cfg_max_entry(s));
}

static Property fw_cfg_io_properties[] = {
    DEFINE_PROP_UINT32("iobase", FWCfgIoState, iobase, -1),
    DEFINE_PROP_UINT32("dma_iobase", FWCfgIoState, dma_iobase, -1),
    DEFINE_PROP_BOOL("dma_enabled", FWCfgIoState, parent_obj.dma_enabled,
                     true),
    DEFINE_PROP_UINT16("x-file-slots", FWCfgIoState, parent_obj.file_slots,
                       FW_CFG_FILE_SLOTS_DFLT),
    DEFINE_PROP_END_OF_LIST(),
};

static void fw_cfg_io_realize(DeviceState *dev, Error **errp)
{
    FWCfgIoState *s = FW_CFG_IO(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Error *local_err = NULL;

    fw_cfg_file_slots_allocate(FW_CFG(s), &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* when using port i/o, the 8-bit data register ALWAYS overlaps
     * with half of the 16-bit control register. Hence, the total size
     * of the i/o region used is FW_CFG_CTL_SIZE */
    memory_region_init_io(&s->comb_iomem, OBJECT(s), &fw_cfg_comb_mem_ops,
                          FW_CFG(s), "fwcfg", FW_CFG_CTL_SIZE);
    sysbus_add_io(sbd, s->iobase, &s->comb_iomem);

    if (FW_CFG(s)->dma_enabled) {
        memory_region_init_io(&FW_CFG(s)->dma_iomem, OBJECT(s),
                              &fw_cfg_dma_mem_ops, FW_CFG(s), "fwcfg.dma",
                              sizeof(dma_addr_t));
        sysbus_add_io(sbd, s->dma_iobase, &FW_CFG(s)->dma_iomem);
    }
}

static void fw_cfg_io_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = fw_cfg_io_realize;
    dc->props = fw_cfg_io_properties;
}

static const TypeInfo fw_cfg_io_info = {
    .name          = TYPE_FW_CFG_IO,
    .parent        = TYPE_FW_CFG,
    .instance_size = sizeof(FWCfgIoState),
    .class_init    = fw_cfg_io_class_init,
};


static Property fw_cfg_mem_properties[] = {
    DEFINE_PROP_UINT32("data_width", FWCfgMemState, data_width, -1),
    DEFINE_PROP_BOOL("dma_enabled", FWCfgMemState, parent_obj.dma_enabled,
                     true),
    DEFINE_PROP_UINT16("x-file-slots", FWCfgMemState, parent_obj.file_slots,
                       FW_CFG_FILE_SLOTS_DFLT),
    DEFINE_PROP_END_OF_LIST(),
};

static void fw_cfg_mem_realize(DeviceState *dev, Error **errp)
{
    FWCfgMemState *s = FW_CFG_MEM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    const MemoryRegionOps *data_ops = &fw_cfg_data_mem_ops;
    Error *local_err = NULL;

    fw_cfg_file_slots_allocate(FW_CFG(s), &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    memory_region_init_io(&s->ctl_iomem, OBJECT(s), &fw_cfg_ctl_mem_ops,
                          FW_CFG(s), "fwcfg.ctl", FW_CFG_CTL_SIZE);
    sysbus_init_mmio(sbd, &s->ctl_iomem);

    if (s->data_width > data_ops->valid.max_access_size) {
        /* memberwise copy because the "old_mmio" member is const */
        s->wide_data_ops.read       = data_ops->read;
        s->wide_data_ops.write      = data_ops->write;
        s->wide_data_ops.endianness = data_ops->endianness;
        s->wide_data_ops.valid      = data_ops->valid;
        s->wide_data_ops.impl       = data_ops->impl;

        s->wide_data_ops.valid.max_access_size = s->data_width;
        s->wide_data_ops.impl.max_access_size  = s->data_width;
        data_ops = &s->wide_data_ops;
    }
    memory_region_init_io(&s->data_iomem, OBJECT(s), data_ops, FW_CFG(s),
                          "fwcfg.data", data_ops->valid.max_access_size);
    sysbus_init_mmio(sbd, &s->data_iomem);

    if (FW_CFG(s)->dma_enabled) {
        memory_region_init_io(&FW_CFG(s)->dma_iomem, OBJECT(s),
                              &fw_cfg_dma_mem_ops, FW_CFG(s), "fwcfg.dma",
                              sizeof(dma_addr_t));
        sysbus_init_mmio(sbd, &FW_CFG(s)->dma_iomem);
    }
}

static void fw_cfg_mem_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = fw_cfg_mem_realize;
    dc->props = fw_cfg_mem_properties;
}

static const TypeInfo fw_cfg_mem_info = {
    .name          = TYPE_FW_CFG_MEM,
    .parent        = TYPE_FW_CFG,
    .instance_size = sizeof(FWCfgMemState),
    .class_init    = fw_cfg_mem_class_init,
};


static void fw_cfg_register_types(void)
{
    type_register_static(&fw_cfg_info);
    type_register_static(&fw_cfg_io_info);
    type_register_static(&fw_cfg_mem_info);
}

type_init(fw_cfg_register_types)
