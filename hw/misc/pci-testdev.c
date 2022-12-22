/*
 * QEMU PCI test device
 *
 * Copyright (c) 2012 Red Hat Inc.
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "qemu/event_notifier.h"
#include "qemu/module.h"
#include "sysemu/kvm.h"
#include "qom/object.h"

typedef struct PCITestDevHdr {
    uint8_t test;
    uint8_t width;
    uint8_t pad0[2];
    uint32_t offset;
    uint8_t data;
    uint8_t pad1[3];
    uint32_t count;
    uint8_t name[];
} PCITestDevHdr;

typedef struct IOTest {
    MemoryRegion *mr;
    EventNotifier notifier;
    bool hasnotifier;
    unsigned size;
    bool match_data;
    PCITestDevHdr *hdr;
    unsigned bufsize;
} IOTest;

#define IOTEST_DATAMATCH 0xFA
#define IOTEST_NOMATCH   0xCE

#define IOTEST_IOSIZE 128
#define IOTEST_MEMSIZE 2048

static const char *iotest_test[] = {
    "no-eventfd",
    "wildcard-eventfd",
    "datamatch-eventfd"
};

static const char *iotest_type[] = {
    "mmio",
    "portio"
};

#define IOTEST_TEST(i) (iotest_test[((i) % ARRAY_SIZE(iotest_test))])
#define IOTEST_TYPE(i) (iotest_type[((i) / ARRAY_SIZE(iotest_test))])
#define IOTEST_MAX_TEST (ARRAY_SIZE(iotest_test))
#define IOTEST_MAX_TYPE (ARRAY_SIZE(iotest_type))
#define IOTEST_MAX (IOTEST_MAX_TEST * IOTEST_MAX_TYPE)

enum {
    IOTEST_ACCESS_NAME,
    IOTEST_ACCESS_DATA,
    IOTEST_ACCESS_MAX,
};

#define IOTEST_ACCESS_TYPE uint8_t
#define IOTEST_ACCESS_WIDTH (sizeof(uint8_t))

struct PCITestDevState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    MemoryRegion mmio;
    MemoryRegion portio;
    IOTest *tests;
    int current;

    uint64_t membar_size;
    MemoryRegion membar;
};

#define TYPE_PCI_TEST_DEV "pci-testdev"

OBJECT_DECLARE_SIMPLE_TYPE(PCITestDevState, PCI_TEST_DEV)

#define IOTEST_IS_MEM(i) (strcmp(IOTEST_TYPE(i), "portio"))
#define IOTEST_REGION(d, i) (IOTEST_IS_MEM(i) ?  &(d)->mmio : &(d)->portio)
#define IOTEST_SIZE(i) (IOTEST_IS_MEM(i) ? IOTEST_MEMSIZE : IOTEST_IOSIZE)
#define IOTEST_PCI_BAR(i) (IOTEST_IS_MEM(i) ? PCI_BASE_ADDRESS_SPACE_MEMORY : \
                           PCI_BASE_ADDRESS_SPACE_IO)

static int pci_testdev_start(IOTest *test)
{
    test->hdr->count = 0;
    if (!test->hasnotifier) {
        return 0;
    }
    event_notifier_test_and_clear(&test->notifier);
    memory_region_add_eventfd(test->mr,
                              le32_to_cpu(test->hdr->offset),
                              test->size,
                              test->match_data,
                              test->hdr->data,
                              &test->notifier);
    return 0;
}

static void pci_testdev_stop(IOTest *test)
{
    if (!test->hasnotifier) {
        return;
    }
    memory_region_del_eventfd(test->mr,
                              le32_to_cpu(test->hdr->offset),
                              test->size,
                              test->match_data,
                              test->hdr->data,
                              &test->notifier);
}

static void
pci_testdev_reset(PCITestDevState *d)
{
    if (d->current == -1) {
        return;
    }
    pci_testdev_stop(&d->tests[d->current]);
    d->current = -1;
}

static void pci_testdev_inc(IOTest *test, unsigned inc)
{
    uint32_t c = le32_to_cpu(test->hdr->count);
    test->hdr->count = cpu_to_le32(c + inc);
}

static void
pci_testdev_write(void *opaque, hwaddr addr, uint64_t val,
                  unsigned size, int type)
{
    PCITestDevState *d = opaque;
    IOTest *test;
    int t, r;

    if (addr == offsetof(PCITestDevHdr, test)) {
        pci_testdev_reset(d);
        if (val >= IOTEST_MAX_TEST) {
            return;
        }
        t = type * IOTEST_MAX_TEST + val;
        r = pci_testdev_start(&d->tests[t]);
        if (r < 0) {
            return;
        }
        d->current = t;
        return;
    }
    if (d->current < 0) {
        return;
    }
    test = &d->tests[d->current];
    if (addr != le32_to_cpu(test->hdr->offset)) {
        return;
    }
    if (test->match_data && test->size != size) {
        return;
    }
    if (test->match_data && val != test->hdr->data) {
        return;
    }
    pci_testdev_inc(test, 1);
}

static uint64_t
pci_testdev_read(void *opaque, hwaddr addr, unsigned size)
{
    PCITestDevState *d = opaque;
    const char *buf;
    IOTest *test;
    if (d->current < 0) {
        return 0;
    }
    test = &d->tests[d->current];
    buf = (const char *)test->hdr;
    if (addr + size >= test->bufsize) {
        return 0;
    }
    if (test->hasnotifier) {
        event_notifier_test_and_clear(&test->notifier);
    }
    return buf[addr];
}

static void
pci_testdev_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                       unsigned size)
{
    pci_testdev_write(opaque, addr, val, size, 0);
}

static void
pci_testdev_pio_write(void *opaque, hwaddr addr, uint64_t val,
                       unsigned size)
{
    pci_testdev_write(opaque, addr, val, size, 1);
}

static const MemoryRegionOps pci_testdev_mmio_ops = {
    .read = pci_testdev_read,
    .write = pci_testdev_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const MemoryRegionOps pci_testdev_pio_ops = {
    .read = pci_testdev_read,
    .write = pci_testdev_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void pci_testdev_realize(PCIDevice *pci_dev, Error **errp)
{
    PCITestDevState *d = PCI_TEST_DEV(pci_dev);
    uint8_t *pci_conf;
    char *name;
    int r, i;
    bool fastmmio = kvm_ioeventfd_any_length_enabled();

    pci_conf = pci_dev->config;

    pci_conf[PCI_INTERRUPT_PIN] = 0; /* no interrupt pin */

    memory_region_init_io(&d->mmio, OBJECT(d), &pci_testdev_mmio_ops, d,
                          "pci-testdev-mmio", IOTEST_MEMSIZE * 2);
    memory_region_init_io(&d->portio, OBJECT(d), &pci_testdev_pio_ops, d,
                          "pci-testdev-portio", IOTEST_IOSIZE * 2);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);
    pci_register_bar(pci_dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &d->portio);

    if (d->membar_size) {
        memory_region_init(&d->membar, OBJECT(d), "pci-testdev-membar",
                           d->membar_size);
        pci_register_bar(pci_dev, 2,
                         PCI_BASE_ADDRESS_SPACE_MEMORY |
                         PCI_BASE_ADDRESS_MEM_PREFETCH |
                         PCI_BASE_ADDRESS_MEM_TYPE_64,
                         &d->membar);
    }

    d->current = -1;
    d->tests = g_malloc0(IOTEST_MAX * sizeof *d->tests);
    for (i = 0; i < IOTEST_MAX; ++i) {
        IOTest *test = &d->tests[i];
        name = g_strdup_printf("%s-%s", IOTEST_TYPE(i), IOTEST_TEST(i));
        test->bufsize = sizeof(PCITestDevHdr) + strlen(name) + 1;
        test->hdr = g_malloc0(test->bufsize);
        memcpy(test->hdr->name, name, strlen(name) + 1);
        g_free(name);
        test->hdr->offset = cpu_to_le32(IOTEST_SIZE(i) + i * IOTEST_ACCESS_WIDTH);
        test->match_data = strcmp(IOTEST_TEST(i), "wildcard-eventfd");
        if (fastmmio && IOTEST_IS_MEM(i) && !test->match_data) {
            test->size = 0;
        } else {
            test->size = IOTEST_ACCESS_WIDTH;
        }
        test->hdr->test = i;
        test->hdr->data = test->match_data ? IOTEST_DATAMATCH : IOTEST_NOMATCH;
        test->hdr->width = IOTEST_ACCESS_WIDTH;
        test->mr = IOTEST_REGION(d, i);
        if (!strcmp(IOTEST_TEST(i), "no-eventfd")) {
            test->hasnotifier = false;
            continue;
        }
        r = event_notifier_init(&test->notifier, 0);
        assert(r >= 0);
        test->hasnotifier = true;
    }
}

static void
pci_testdev_uninit(PCIDevice *dev)
{
    PCITestDevState *d = PCI_TEST_DEV(dev);
    int i;

    pci_testdev_reset(d);
    for (i = 0; i < IOTEST_MAX; ++i) {
        if (d->tests[i].hasnotifier) {
            event_notifier_cleanup(&d->tests[i].notifier);
        }
        g_free(d->tests[i].hdr);
    }
    g_free(d->tests);
}

static void qdev_pci_testdev_reset(DeviceState *dev)
{
    PCITestDevState *d = PCI_TEST_DEV(dev);
    pci_testdev_reset(d);
}

static Property pci_testdev_properties[] = {
    DEFINE_PROP_SIZE("membar", PCITestDevState, membar_size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void pci_testdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_testdev_realize;
    k->exit = pci_testdev_uninit;
    k->vendor_id = PCI_VENDOR_ID_REDHAT;
    k->device_id = PCI_DEVICE_ID_REDHAT_TEST;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_OTHERS;
    dc->desc = "PCI Test Device";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->reset = qdev_pci_testdev_reset;
    device_class_set_props(dc, pci_testdev_properties);
}

static const TypeInfo pci_testdev_info = {
    .name          = TYPE_PCI_TEST_DEV,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCITestDevState),
    .class_init    = pci_testdev_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pci_testdev_register_types(void)
{
    type_register_static(&pci_testdev_info);
}

type_init(pci_testdev_register_types)
