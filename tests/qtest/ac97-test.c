/*
 * QTest testcase for AC97
 *
 * Copyright (c) 2014 SUSE LINUX Products GmbH
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "qemu/module.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"

typedef struct QAC97 QAC97;

struct QAC97 {
    QOSGraphObject obj;
    QPCIDevice dev;
};

static void *ac97_get_driver(void *obj, const char *interface)
{
    QAC97 *ac97 = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &ac97->dev;
    }

    fprintf(stderr, "%s not present in ac97\n", interface);
    g_assert_not_reached();
}

static void *ac97_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QAC97 *ac97 = g_new0(QAC97, 1);
    QPCIBus *bus = pci_bus;

    qpci_device_init(&ac97->dev, bus, addr);
    ac97->obj.get_driver = ac97_get_driver;
    return &ac97->obj;
}

/*
 * This is rather a test of the audio subsystem and not an AC97 test. Test if
 * the audio subsystem can handle a 44100/1 upsample ratio. For some time this
 * used to trigger QEMU aborts.
 */
static void ac97_playback_upsample(void *obj, void *data, QGuestAllocator *alloc)
{
    QAC97 *ac97 = obj;
    QPCIDevice *dev = &ac97->dev;
    QPCIBar bar0;

    qpci_device_enable(dev);
    bar0 = qpci_iomap(dev, 0, NULL);
    /* IOBAR0 offset 0x2c: PCM Front DAC Rate */
    qpci_io_writew(dev, bar0, 0x2c, 0x1);
}

/*
 * This test is similar to the playback upsample test. QEMU shouldn't abort if
 * asked for a 1/44100 downsample ratio.
 */
static void ac97_record_downsample(void *obj, void *data, QGuestAllocator *alloc)
{
    QAC97 *ac97 = obj;
    QPCIDevice *dev = &ac97->dev;
    QPCIBar bar0;

    qpci_device_enable(dev);
    bar0 = qpci_iomap(dev, 0, NULL);
    /* IOBAR0 offset 0x32: PCM L/R ADC Rate */
    qpci_io_writew(dev, bar0, 0x32, 0x1);
}

static void ac97_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0,audiodev=snd0",
        .after_cmd_line = "-audiodev none,id=snd0"
                          ",out.frequency=44100,in.frequency=44100",
    };
    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });

    qos_node_create_driver("AC97", ac97_create);
    qos_node_produces("AC97", "pci-device");
    qos_node_consumes("AC97", "pci-bus", &opts);

    qos_add_test("playback_upsample", "AC97", ac97_playback_upsample, NULL);
    qos_add_test("record_downsample", "AC97", ac97_record_downsample, NULL);
}

libqos_init(ac97_register_nodes);
