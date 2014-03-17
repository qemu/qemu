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
#include "hw/hw.h"
#include "sysemu/sysemu.h"
#include "hw/isa/isa.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/sysbus.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "qemu/config-file.h"

#define FW_CFG_SIZE 2
#define FW_CFG_DATA_SIZE 1
#define TYPE_FW_CFG "fw_cfg"
#define FW_CFG_NAME "fw_cfg"
#define FW_CFG_PATH "/machine/" FW_CFG_NAME
#define FW_CFG(obj) OBJECT_CHECK(FWCfgState, (obj), TYPE_FW_CFG)

typedef struct FWCfgEntry {
    uint32_t len;
    uint8_t *data;
    void *callback_opaque;
    FWCfgCallback callback;
    FWCfgReadCallback read_callback;
} FWCfgEntry;

struct FWCfgState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    MemoryRegion ctl_iomem, data_iomem, comb_iomem;
    uint32_t ctl_iobase, data_iobase;
    FWCfgEntry entries[2][FW_CFG_MAX_ENTRY];
    FWCfgFiles *files;
    uint16_t cur_entry;
    uint32_t cur_offset;
    Notifier machine_ready;
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
            reboot_timeout = strtol(p, (char **)&p, 10);
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
    int arch = !!(s->cur_entry & FW_CFG_ARCH_LOCAL);
    FWCfgEntry *e = &s->entries[arch][s->cur_entry & FW_CFG_ENTRY_MASK];

    trace_fw_cfg_write(s, value);

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

    trace_fw_cfg_select(s, key, ret);
    return ret;
}

static uint8_t fw_cfg_read(FWCfgState *s)
{
    int arch = !!(s->cur_entry & FW_CFG_ARCH_LOCAL);
    FWCfgEntry *e = &s->entries[arch][s->cur_entry & FW_CFG_ENTRY_MASK];
    uint8_t ret;

    if (s->cur_entry == FW_CFG_INVALID || !e->data || s->cur_offset >= e->len)
        ret = 0;
    else {
        if (e->read_callback) {
            e->read_callback(e->callback_opaque, s->cur_offset);
        }
        ret = e->data[s->cur_offset++];
    }

    trace_fw_cfg_read(s, ret);
    return ret;
}

static uint64_t fw_cfg_data_mem_read(void *opaque, hwaddr addr,
                                     unsigned size)
{
    return fw_cfg_read(opaque);
}

static void fw_cfg_data_mem_write(void *opaque, hwaddr addr,
                                  uint64_t value, unsigned size)
{
    fw_cfg_write(opaque, (uint8_t)value);
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

static uint64_t fw_cfg_comb_read(void *opaque, hwaddr addr,
                                 unsigned size)
{
    return fw_cfg_read(opaque);
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
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.accepts = fw_cfg_ctl_mem_valid,
};

static const MemoryRegionOps fw_cfg_data_mem_ops = {
    .read = fw_cfg_data_mem_read,
    .write = fw_cfg_data_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const MemoryRegionOps fw_cfg_comb_mem_ops = {
    .read = fw_cfg_comb_read,
    .write = fw_cfg_comb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.accepts = fw_cfg_comb_valid,
};

static void fw_cfg_reset(DeviceState *d)
{
    FWCfgState *s = FW_CFG(d);

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

static void fw_cfg_add_bytes_read_callback(FWCfgState *s, uint16_t key,
                                           FWCfgReadCallback callback,
                                           void *callback_opaque,
                                           void *data, size_t len)
{
    int arch = !!(key & FW_CFG_ARCH_LOCAL);

    key &= FW_CFG_ENTRY_MASK;

    assert(key < FW_CFG_MAX_ENTRY && len < UINT32_MAX);

    s->entries[arch][key].data = data;
    s->entries[arch][key].len = (uint32_t)len;
    s->entries[arch][key].read_callback = callback;
    s->entries[arch][key].callback_opaque = callback_opaque;
}

void fw_cfg_add_bytes(FWCfgState *s, uint16_t key, void *data, size_t len)
{
    fw_cfg_add_bytes_read_callback(s, key, NULL, NULL, data, len);
}

void fw_cfg_add_string(FWCfgState *s, uint16_t key, const char *value)
{
    size_t sz = strlen(value) + 1;

    return fw_cfg_add_bytes(s, key, g_memdup(value, sz), sz);
}

void fw_cfg_add_i16(FWCfgState *s, uint16_t key, uint16_t value)
{
    uint16_t *copy;

    copy = g_malloc(sizeof(value));
    *copy = cpu_to_le16(value);
    fw_cfg_add_bytes(s, key, copy, sizeof(value));
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

void fw_cfg_add_callback(FWCfgState *s, uint16_t key, FWCfgCallback callback,
                         void *callback_opaque, void *data, size_t len)
{
    int arch = !!(key & FW_CFG_ARCH_LOCAL);

    assert(key & FW_CFG_WRITE_CHANNEL);

    key &= FW_CFG_ENTRY_MASK;

    assert(key < FW_CFG_MAX_ENTRY && len <= UINT32_MAX);

    s->entries[arch][key].data = data;
    s->entries[arch][key].len = (uint32_t)len;
    s->entries[arch][key].callback_opaque = callback_opaque;
    s->entries[arch][key].callback = callback;
}

void fw_cfg_add_file_callback(FWCfgState *s,  const char *filename,
                              FWCfgReadCallback callback, void *callback_opaque,
                              void *data, size_t len)
{
    int i, index;
    size_t dsize;

    if (!s->files) {
        dsize = sizeof(uint32_t) + sizeof(FWCfgFile) * FW_CFG_FILE_SLOTS;
        s->files = g_malloc0(dsize);
        fw_cfg_add_bytes(s, FW_CFG_FILE_DIR, s->files, dsize);
    }

    index = be32_to_cpu(s->files->count);
    assert(index < FW_CFG_FILE_SLOTS);

    fw_cfg_add_bytes_read_callback(s, FW_CFG_FILE_FIRST + index,
                                   callback, callback_opaque, data, len);

    pstrcpy(s->files->f[index].name, sizeof(s->files->f[index].name),
            filename);
    for (i = 0; i < index; i++) {
        if (strcmp(s->files->f[index].name, s->files->f[i].name) == 0) {
            trace_fw_cfg_add_file_dupe(s, s->files->f[index].name);
            return;
        }
    }

    s->files->f[index].size   = cpu_to_be32(len);
    s->files->f[index].select = cpu_to_be16(FW_CFG_FILE_FIRST + index);
    trace_fw_cfg_add_file(s, index, s->files->f[index].name, len);

    s->files->count = cpu_to_be32(index+1);
}

void fw_cfg_add_file(FWCfgState *s,  const char *filename,
                     void *data, size_t len)
{
    fw_cfg_add_file_callback(s, filename, NULL, NULL, data, len);
}

static void fw_cfg_machine_ready(struct Notifier *n, void *data)
{
    size_t len;
    FWCfgState *s = container_of(n, FWCfgState, machine_ready);
    char *bootindex = get_boot_devices_list(&len, false);

    fw_cfg_add_file(s, "bootorder", (uint8_t*)bootindex, len);
}

FWCfgState *fw_cfg_init(uint32_t ctl_port, uint32_t data_port,
                        hwaddr ctl_addr, hwaddr data_addr)
{
    DeviceState *dev;
    SysBusDevice *d;
    FWCfgState *s;

    dev = qdev_create(NULL, TYPE_FW_CFG);
    qdev_prop_set_uint32(dev, "ctl_iobase", ctl_port);
    qdev_prop_set_uint32(dev, "data_iobase", data_port);
    d = SYS_BUS_DEVICE(dev);

    s = FW_CFG(dev);

    assert(!object_resolve_path(FW_CFG_PATH, NULL));

    object_property_add_child(qdev_get_machine(), FW_CFG_NAME, OBJECT(s), NULL);

    qdev_init_nofail(dev);

    if (ctl_addr) {
        sysbus_mmio_map(d, 0, ctl_addr);
    }
    if (data_addr) {
        sysbus_mmio_map(d, 1, data_addr);
    }
    fw_cfg_add_bytes(s, FW_CFG_SIGNATURE, (char *)"QEMU", 4);
    fw_cfg_add_bytes(s, FW_CFG_UUID, qemu_uuid, 16);
    fw_cfg_add_i16(s, FW_CFG_NOGRAPHIC, (uint16_t)(display_type == DT_NOGRAPHIC));
    fw_cfg_add_i16(s, FW_CFG_NB_CPUS, (uint16_t)smp_cpus);
    fw_cfg_add_i16(s, FW_CFG_BOOT_MENU, (uint16_t)boot_menu);
    fw_cfg_bootsplash(s);
    fw_cfg_reboot(s);

    s->machine_ready.notify = fw_cfg_machine_ready;
    qemu_add_machine_init_done_notifier(&s->machine_ready);

    return s;
}

static void fw_cfg_initfn(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    FWCfgState *s = FW_CFG(obj);

    memory_region_init_io(&s->ctl_iomem, OBJECT(s), &fw_cfg_ctl_mem_ops, s,
                          "fwcfg.ctl", FW_CFG_SIZE);
    sysbus_init_mmio(sbd, &s->ctl_iomem);
    memory_region_init_io(&s->data_iomem, OBJECT(s), &fw_cfg_data_mem_ops, s,
                          "fwcfg.data", FW_CFG_DATA_SIZE);
    sysbus_init_mmio(sbd, &s->data_iomem);
    /* In case ctl and data overlap: */
    memory_region_init_io(&s->comb_iomem, OBJECT(s), &fw_cfg_comb_mem_ops, s,
                          "fwcfg", FW_CFG_SIZE);
}

static void fw_cfg_realize(DeviceState *dev, Error **errp)
{
    FWCfgState *s = FW_CFG(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);


    if (s->ctl_iobase + 1 == s->data_iobase) {
        sysbus_add_io(sbd, s->ctl_iobase, &s->comb_iomem);
    } else {
        if (s->ctl_iobase) {
            sysbus_add_io(sbd, s->ctl_iobase, &s->ctl_iomem);
        }
        if (s->data_iobase) {
            sysbus_add_io(sbd, s->data_iobase, &s->data_iomem);
        }
    }
}

static Property fw_cfg_properties[] = {
    DEFINE_PROP_UINT32("ctl_iobase", FWCfgState, ctl_iobase, -1),
    DEFINE_PROP_UINT32("data_iobase", FWCfgState, data_iobase, -1),
    DEFINE_PROP_END_OF_LIST(),
};

FWCfgState *fw_cfg_find(void)
{
    return FW_CFG(object_resolve_path(FW_CFG_PATH, NULL));
}

static void fw_cfg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = fw_cfg_realize;
    dc->reset = fw_cfg_reset;
    dc->vmsd = &vmstate_fw_cfg;
    dc->props = fw_cfg_properties;
}

static const TypeInfo fw_cfg_info = {
    .name          = TYPE_FW_CFG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(FWCfgState),
    .instance_init = fw_cfg_initfn,
    .class_init    = fw_cfg_class_init,
};

static void fw_cfg_register_types(void)
{
    type_register_static(&fw_cfg_info);
}

type_init(fw_cfg_register_types)
