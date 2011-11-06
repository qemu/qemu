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
#include "hw.h"
#include "sysemu.h"
#include "isa.h"
#include "fw_cfg.h"
#include "sysbus.h"
#include "qemu-error.h"

/* debug firmware config */
//#define DEBUG_FW_CFG

#ifdef DEBUG_FW_CFG
#define FW_CFG_DPRINTF(fmt, ...)                        \
    do { printf("FW_CFG: " fmt , ## __VA_ARGS__); } while (0)
#else
#define FW_CFG_DPRINTF(fmt, ...)
#endif

#define FW_CFG_SIZE 2

typedef struct FWCfgEntry {
    uint32_t len;
    uint8_t *data;
    void *callback_opaque;
    FWCfgCallback callback;
} FWCfgEntry;

struct FWCfgState {
    SysBusDevice busdev;
    uint32_t ctl_iobase, data_iobase;
    FWCfgEntry entries[2][FW_CFG_MAX_ENTRY];
    FWCfgFiles *files;
    uint16_t cur_entry;
    uint32_t cur_offset;
    Notifier machine_ready;
};

#define JPG_FILE 0
#define BMP_FILE 1

static char *read_splashfile(char *filename, int *file_sizep, int *file_typep)
{
    GError *err = NULL;
    gboolean res;
    gchar *content;
    int file_type = -1;
    unsigned int filehead = 0;
    int bmp_bpp;

    res = g_file_get_contents(filename, &content, (gsize *)file_sizep, &err);
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
    int file_size;
    int file_type = -1;
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
            boot_splash_time = strtol(p, (char **)&p, 10);
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
        if (boot_splash_filedata != NULL) {
            g_free(boot_splash_filedata);
        }
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

static void fw_cfg_write(FWCfgState *s, uint8_t value)
{
    int arch = !!(s->cur_entry & FW_CFG_ARCH_LOCAL);
    FWCfgEntry *e = &s->entries[arch][s->cur_entry & FW_CFG_ENTRY_MASK];

    FW_CFG_DPRINTF("write %d\n", value);

    if (s->cur_entry & FW_CFG_WRITE_CHANNEL && e->callback &&
        s->cur_offset < e->len) {
        e->data[s->cur_offset++] = value;
        if (s->cur_offset == e->len) {
            e->callback(e->callback_opaque, e->data);
            s->cur_offset = 0;
        }
    }
}

static int fw_cfg_select(FWCfgState *s, uint16_t key)
{
    int ret;

    s->cur_offset = 0;
    if ((key & FW_CFG_ENTRY_MASK) >= FW_CFG_MAX_ENTRY) {
        s->cur_entry = FW_CFG_INVALID;
        ret = 0;
    } else {
        s->cur_entry = key;
        ret = 1;
    }

    FW_CFG_DPRINTF("select key %d (%sfound)\n", key, ret ? "" : "not ");

    return ret;
}

static uint8_t fw_cfg_read(FWCfgState *s)
{
    int arch = !!(s->cur_entry & FW_CFG_ARCH_LOCAL);
    FWCfgEntry *e = &s->entries[arch][s->cur_entry & FW_CFG_ENTRY_MASK];
    uint8_t ret;

    if (s->cur_entry == FW_CFG_INVALID || !e->data || s->cur_offset >= e->len)
        ret = 0;
    else
        ret = e->data[s->cur_offset++];

    FW_CFG_DPRINTF("read %d\n", ret);

    return ret;
}

static uint32_t fw_cfg_io_readb(void *opaque, uint32_t addr)
{
    return fw_cfg_read(opaque);
}

static void fw_cfg_io_writeb(void *opaque, uint32_t addr, uint32_t value)
{
    fw_cfg_write(opaque, (uint8_t)value);
}

static void fw_cfg_io_writew(void *opaque, uint32_t addr, uint32_t value)
{
    fw_cfg_select(opaque, (uint16_t)value);
}

static uint32_t fw_cfg_mem_readb(void *opaque, target_phys_addr_t addr)
{
    return fw_cfg_read(opaque);
}

static void fw_cfg_mem_writeb(void *opaque, target_phys_addr_t addr,
                              uint32_t value)
{
    fw_cfg_write(opaque, (uint8_t)value);
}

static void fw_cfg_mem_writew(void *opaque, target_phys_addr_t addr,
                              uint32_t value)
{
    fw_cfg_select(opaque, (uint16_t)value);
}

static CPUReadMemoryFunc * const fw_cfg_ctl_mem_read[3] = {
    NULL,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc * const fw_cfg_ctl_mem_write[3] = {
    NULL,
    fw_cfg_mem_writew,
    NULL,
};

static CPUReadMemoryFunc * const fw_cfg_data_mem_read[3] = {
    fw_cfg_mem_readb,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc * const fw_cfg_data_mem_write[3] = {
    fw_cfg_mem_writeb,
    NULL,
    NULL,
};

static void fw_cfg_reset(DeviceState *d)
{
    FWCfgState *s = DO_UPCAST(FWCfgState, busdev.qdev, d);

    fw_cfg_select(s, 0);
}

/* Save restore 32 bit int as uint16_t
   This is a Big hack, but it is how the old state did it.
   Or we broke compatibility in the state, or we can't use struct tm
 */

static int get_uint32_as_uint16(QEMUFile *f, void *pv, size_t size)
{
    uint32_t *v = pv;
    *v = qemu_get_be16(f);
    return 0;
}

static void put_unused(QEMUFile *f, void *pv, size_t size)
{
    fprintf(stderr, "uint32_as_uint16 is only used for backward compatibility.\n");
    fprintf(stderr, "This functions shouldn't be called.\n");
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

static const VMStateDescription vmstate_fw_cfg = {
    .name = "fw_cfg",
    .version_id = 2,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField []) {
        VMSTATE_UINT16(cur_entry, FWCfgState),
        VMSTATE_UINT16_HACK(cur_offset, FWCfgState, is_version_1),
        VMSTATE_UINT32_V(cur_offset, FWCfgState, 2),
        VMSTATE_END_OF_LIST()
    }
};

int fw_cfg_add_bytes(FWCfgState *s, uint16_t key, uint8_t *data, uint32_t len)
{
    int arch = !!(key & FW_CFG_ARCH_LOCAL);

    key &= FW_CFG_ENTRY_MASK;

    if (key >= FW_CFG_MAX_ENTRY)
        return 0;

    s->entries[arch][key].data = data;
    s->entries[arch][key].len = len;

    return 1;
}

int fw_cfg_add_i16(FWCfgState *s, uint16_t key, uint16_t value)
{
    uint16_t *copy;

    copy = g_malloc(sizeof(value));
    *copy = cpu_to_le16(value);
    return fw_cfg_add_bytes(s, key, (uint8_t *)copy, sizeof(value));
}

int fw_cfg_add_i32(FWCfgState *s, uint16_t key, uint32_t value)
{
    uint32_t *copy;

    copy = g_malloc(sizeof(value));
    *copy = cpu_to_le32(value);
    return fw_cfg_add_bytes(s, key, (uint8_t *)copy, sizeof(value));
}

int fw_cfg_add_i64(FWCfgState *s, uint16_t key, uint64_t value)
{
    uint64_t *copy;

    copy = g_malloc(sizeof(value));
    *copy = cpu_to_le64(value);
    return fw_cfg_add_bytes(s, key, (uint8_t *)copy, sizeof(value));
}

int fw_cfg_add_callback(FWCfgState *s, uint16_t key, FWCfgCallback callback,
                        void *callback_opaque, uint8_t *data, size_t len)
{
    int arch = !!(key & FW_CFG_ARCH_LOCAL);

    if (!(key & FW_CFG_WRITE_CHANNEL))
        return 0;

    key &= FW_CFG_ENTRY_MASK;

    if (key >= FW_CFG_MAX_ENTRY || len > 65535)
        return 0;

    s->entries[arch][key].data = data;
    s->entries[arch][key].len = len;
    s->entries[arch][key].callback_opaque = callback_opaque;
    s->entries[arch][key].callback = callback;

    return 1;
}

int fw_cfg_add_file(FWCfgState *s,  const char *filename, uint8_t *data,
                    uint32_t len)
{
    int i, index;

    if (!s->files) {
        int dsize = sizeof(uint32_t) + sizeof(FWCfgFile) * FW_CFG_FILE_SLOTS;
        s->files = g_malloc0(dsize);
        fw_cfg_add_bytes(s, FW_CFG_FILE_DIR, (uint8_t*)s->files, dsize);
    }

    index = be32_to_cpu(s->files->count);
    if (index == FW_CFG_FILE_SLOTS) {
        fprintf(stderr, "fw_cfg: out of file slots\n");
        return 0;
    }

    fw_cfg_add_bytes(s, FW_CFG_FILE_FIRST + index, data, len);

    pstrcpy(s->files->f[index].name, sizeof(s->files->f[index].name),
            filename);
    for (i = 0; i < index; i++) {
        if (strcmp(s->files->f[index].name, s->files->f[i].name) == 0) {
            FW_CFG_DPRINTF("%s: skip duplicate: %s\n", __FUNCTION__,
                           s->files->f[index].name);
            return 1;
        }
    }

    s->files->f[index].size   = cpu_to_be32(len);
    s->files->f[index].select = cpu_to_be16(FW_CFG_FILE_FIRST + index);
    FW_CFG_DPRINTF("%s: #%d: %s (%d bytes)\n", __FUNCTION__,
                   index, s->files->f[index].name, len);

    s->files->count = cpu_to_be32(index+1);
    return 1;
}

static void fw_cfg_machine_ready(struct Notifier *n, void *data)
{
    uint32_t len;
    FWCfgState *s = container_of(n, FWCfgState, machine_ready);
    char *bootindex = get_boot_devices_list(&len);

    fw_cfg_add_file(s, "bootorder", (uint8_t*)bootindex, len);
}

FWCfgState *fw_cfg_init(uint32_t ctl_port, uint32_t data_port,
                        target_phys_addr_t ctl_addr, target_phys_addr_t data_addr)
{
    DeviceState *dev;
    SysBusDevice *d;
    FWCfgState *s;

    dev = qdev_create(NULL, "fw_cfg");
    qdev_prop_set_uint32(dev, "ctl_iobase", ctl_port);
    qdev_prop_set_uint32(dev, "data_iobase", data_port);
    qdev_init_nofail(dev);
    d = sysbus_from_qdev(dev);

    s = DO_UPCAST(FWCfgState, busdev.qdev, dev);

    if (ctl_addr) {
        sysbus_mmio_map(d, 0, ctl_addr);
    }
    if (data_addr) {
        sysbus_mmio_map(d, 1, data_addr);
    }
    fw_cfg_add_bytes(s, FW_CFG_SIGNATURE, (uint8_t *)"QEMU", 4);
    fw_cfg_add_bytes(s, FW_CFG_UUID, qemu_uuid, 16);
    fw_cfg_add_i16(s, FW_CFG_NOGRAPHIC, (uint16_t)(display_type == DT_NOGRAPHIC));
    fw_cfg_add_i16(s, FW_CFG_NB_CPUS, (uint16_t)smp_cpus);
    fw_cfg_add_i16(s, FW_CFG_MAX_CPUS, (uint16_t)max_cpus);
    fw_cfg_add_i16(s, FW_CFG_BOOT_MENU, (uint16_t)boot_menu);
    fw_cfg_bootsplash(s);

    s->machine_ready.notify = fw_cfg_machine_ready;
    qemu_add_machine_init_done_notifier(&s->machine_ready);

    return s;
}

static int fw_cfg_init1(SysBusDevice *dev)
{
    FWCfgState *s = FROM_SYSBUS(FWCfgState, dev);
    int io_ctl_memory, io_data_memory;

    io_ctl_memory = cpu_register_io_memory(fw_cfg_ctl_mem_read,
                                           fw_cfg_ctl_mem_write, s,
                                           DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, FW_CFG_SIZE, io_ctl_memory);

    io_data_memory = cpu_register_io_memory(fw_cfg_data_mem_read,
                                            fw_cfg_data_mem_write, s,
                                            DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, FW_CFG_SIZE, io_data_memory);

    if (s->ctl_iobase) {
        register_ioport_write(s->ctl_iobase, 2, 2, fw_cfg_io_writew, s);
    }
    if (s->data_iobase) {
        register_ioport_read(s->data_iobase, 1, 1, fw_cfg_io_readb, s);
        register_ioport_write(s->data_iobase, 1, 1, fw_cfg_io_writeb, s);
    }
    return 0;
}

static SysBusDeviceInfo fw_cfg_info = {
    .init = fw_cfg_init1,
    .qdev.name = "fw_cfg",
    .qdev.size = sizeof(FWCfgState),
    .qdev.vmsd = &vmstate_fw_cfg,
    .qdev.reset = fw_cfg_reset,
    .qdev.no_user = 1,
    .qdev.props = (Property[]) {
        DEFINE_PROP_HEX32("ctl_iobase", FWCfgState, ctl_iobase, -1),
        DEFINE_PROP_HEX32("data_iobase", FWCfgState, data_iobase, -1),
        DEFINE_PROP_END_OF_LIST(),
    },
};

static void fw_cfg_register_devices(void)
{
    sysbus_register_withprop(&fw_cfg_info);
}

device_init(fw_cfg_register_devices)
