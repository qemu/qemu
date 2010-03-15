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
    FWCfgEntry entries[2][FW_CFG_MAX_ENTRY];
    FWCfgFiles *files;
    uint16_t cur_entry;
    uint32_t cur_offset;
};

static void fw_cfg_write(FWCfgState *s, uint8_t value)
{
    int arch = !!(s->cur_entry & FW_CFG_ARCH_LOCAL);
    FWCfgEntry *e = &s->entries[arch][s->cur_entry & FW_CFG_ENTRY_MASK];

    FW_CFG_DPRINTF("write %d\n", value);

    if (s->cur_entry & FW_CFG_WRITE_CHANNEL && s->cur_offset < e->len) {
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

static void fw_cfg_reset(void *opaque)
{
    FWCfgState *s = opaque;

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

    copy = qemu_malloc(sizeof(value));
    *copy = cpu_to_le16(value);
    return fw_cfg_add_bytes(s, key, (uint8_t *)copy, sizeof(value));
}

int fw_cfg_add_i32(FWCfgState *s, uint16_t key, uint32_t value)
{
    uint32_t *copy;

    copy = qemu_malloc(sizeof(value));
    *copy = cpu_to_le32(value);
    return fw_cfg_add_bytes(s, key, (uint8_t *)copy, sizeof(value));
}

int fw_cfg_add_i64(FWCfgState *s, uint16_t key, uint64_t value)
{
    uint64_t *copy;

    copy = qemu_malloc(sizeof(value));
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

int fw_cfg_add_file(FWCfgState *s,  const char *dir, const char *filename,
                    uint8_t *data, uint32_t len)
{
    const char *basename;
    int i, index;

    if (!s->files) {
        int dsize = sizeof(uint32_t) + sizeof(FWCfgFile) * FW_CFG_FILE_SLOTS;
        s->files = qemu_mallocz(dsize);
        fw_cfg_add_bytes(s, FW_CFG_FILE_DIR, (uint8_t*)s->files, dsize);
    }

    index = be32_to_cpu(s->files->count);
    if (index == FW_CFG_FILE_SLOTS) {
        fprintf(stderr, "fw_cfg: out of file slots\n");
        return 0;
    }

    fw_cfg_add_bytes(s, FW_CFG_FILE_FIRST + index, data, len);

    basename = strrchr(filename, '/');
    if (basename) {
        basename++;
    } else {
        basename = filename;
    }

    snprintf(s->files->f[index].name, sizeof(s->files->f[index].name),
             "%s/%s", dir, basename);
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

FWCfgState *fw_cfg_init(uint32_t ctl_port, uint32_t data_port,
                        target_phys_addr_t ctl_addr, target_phys_addr_t data_addr)
{
    FWCfgState *s;
    int io_ctl_memory, io_data_memory;

    s = qemu_mallocz(sizeof(FWCfgState));

    if (ctl_port) {
        register_ioport_write(ctl_port, 2, 2, fw_cfg_io_writew, s);
    }
    if (data_port) {
        register_ioport_read(data_port, 1, 1, fw_cfg_io_readb, s);
        register_ioport_write(data_port, 1, 1, fw_cfg_io_writeb, s);
    }
    if (ctl_addr) {
        io_ctl_memory = cpu_register_io_memory(fw_cfg_ctl_mem_read,
                                           fw_cfg_ctl_mem_write, s);
        cpu_register_physical_memory(ctl_addr, FW_CFG_SIZE, io_ctl_memory);
    }
    if (data_addr) {
        io_data_memory = cpu_register_io_memory(fw_cfg_data_mem_read,
                                           fw_cfg_data_mem_write, s);
        cpu_register_physical_memory(data_addr, FW_CFG_SIZE, io_data_memory);
    }
    fw_cfg_add_bytes(s, FW_CFG_SIGNATURE, (uint8_t *)"QEMU", 4);
    fw_cfg_add_bytes(s, FW_CFG_UUID, qemu_uuid, 16);
    fw_cfg_add_i16(s, FW_CFG_NOGRAPHIC, (uint16_t)(display_type == DT_NOGRAPHIC));
    fw_cfg_add_i16(s, FW_CFG_NB_CPUS, (uint16_t)smp_cpus);
    fw_cfg_add_i16(s, FW_CFG_MAX_CPUS, (uint16_t)max_cpus);
    fw_cfg_add_i16(s, FW_CFG_BOOT_MENU, (uint16_t)boot_menu);

    vmstate_register(-1, &vmstate_fw_cfg, s);
    qemu_register_reset(fw_cfg_reset, s);

    return s;
}
