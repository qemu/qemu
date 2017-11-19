/*
 * CSKY memlog.
 *
 * using to print log QEMU from target linux.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "trace.h"

typedef struct {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    int fd;
} csky_memlog_state;

#define TYPE_CSKY_MEMLOG  "csky_memlog"
#define CSKY_MEMLOG(obj)  OBJECT_CHECK(csky_memlog_state, (obj), \
                                       TYPE_CSKY_MEMLOG)

static uint64_t csky_memlog_read(void *opaque, hwaddr offset, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "csky_memlog_read: should not read\n");

    return 0;
}

static void csky_memlog_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size)
{
    csky_memlog_state *s = (csky_memlog_state *)opaque;
    char a = value;
    int ret;

    if (offset != 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "csky_memlog_write: bad offset\n");
    }

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "csky_memlog_write: only support word align access\n");
    }

    do {
        ret = write(s->fd, &a, 1);
    } while (ret < 0 && errno == EINTR);
}

static const MemoryRegionOps csky_memlog_ops = {
    .read = csky_memlog_read,
    .write = csky_memlog_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_csky_memlog = {
    .name = TYPE_CSKY_MEMLOG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_INT32(fd, csky_memlog_state),
        VMSTATE_END_OF_LIST()
    }
};

static void csky_memlog_init(Object *obj)
{
    csky_memlog_state *s = CSKY_MEMLOG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &csky_memlog_ops, s,
                          TYPE_CSKY_MEMLOG, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    do {
        s->fd = open("mem.log",  O_CREAT | O_TRUNC | O_WRONLY, 0644);
    } while (s->fd < 0 && errno == EINTR);
}

static void csky_memlog_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->vmsd = &vmstate_csky_memlog;
}

static const TypeInfo csky_memlog_info = {
    .name          = TYPE_CSKY_MEMLOG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(csky_memlog_state),
    .instance_init = csky_memlog_init,
    .class_init    = csky_memlog_class_init,
};


static void csky_memlog_register_types(void)
{
    type_register_static(&csky_memlog_info);
}

type_init(csky_memlog_register_types)
