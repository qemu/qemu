/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qemu/option.h"
#include "qemu/help_option.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "hw/qdev-properties.h"
#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/audio/soundhw.h"

struct soundhw {
    const char *name;
    const char *descr;
    const char *typename;
    int isa;
    int (*init_pci) (PCIBus *bus, const char *audiodev);
};

static struct soundhw soundhw[9];
static int soundhw_count;

void pci_register_soundhw(const char *name, const char *descr,
                          int (*init_pci)(PCIBus *bus, const char *audiodev))
{
    assert(soundhw_count < ARRAY_SIZE(soundhw) - 1);
    soundhw[soundhw_count].name = name;
    soundhw[soundhw_count].descr = descr;
    soundhw[soundhw_count].isa = 0;
    soundhw[soundhw_count].init_pci = init_pci;
    soundhw_count++;
}

void deprecated_register_soundhw(const char *name, const char *descr,
                                 int isa, const char *typename)
{
    assert(soundhw_count < ARRAY_SIZE(soundhw) - 1);
    soundhw[soundhw_count].name = name;
    soundhw[soundhw_count].descr = descr;
    soundhw[soundhw_count].isa = isa;
    soundhw[soundhw_count].typename = typename;
    soundhw_count++;
}

void show_valid_soundhw(void)
{
    struct soundhw *c;

    if (soundhw_count) {
         printf("Valid sound card names (comma separated):\n");
         for (c = soundhw; c->name; ++c) {
             printf ("%-11s %s\n", c->name, c->descr);
         }
    } else {
         printf("Machine has no user-selectable audio hardware "
                "(it may or may not have always-present audio hardware).\n");
    }
}

static struct soundhw *selected = NULL;
static const char *audiodev_id;

void select_soundhw(const char *optarg, const char *audiodev)
{
    struct soundhw *c;

    if (selected) {
        error_setg(&error_fatal, "only one -soundhw option is allowed");
    }

    for (c = soundhw; c->name; ++c) {
        if (g_str_equal(c->name, optarg)) {
            selected = c;
            audiodev_id = audiodev;
            break;
        }
    }

    if (!c->name) {
        error_report("Unknown sound card name `%s'", optarg);
        show_valid_soundhw();
        exit(1);
    }
}

void soundhw_init(void)
{
    struct soundhw *c = selected;
    ISABus *isa_bus = (ISABus *) object_resolve_path_type("", TYPE_ISA_BUS, NULL);
    PCIBus *pci_bus = (PCIBus *) object_resolve_path_type("", TYPE_PCI_BUS, NULL);
    BusState *bus;

    if (!c) {
        return;
    }
    if (c->isa) {
        if (!isa_bus) {
            error_report("ISA bus not available for %s", c->name);
            exit(1);
        }
        bus = BUS(isa_bus);
    } else {
        if (!pci_bus) {
            error_report("PCI bus not available for %s", c->name);
            exit(1);
        }
        bus = BUS(pci_bus);
    }

    if (c->typename) {
        DeviceState *dev = qdev_new(c->typename);
        qdev_prop_set_string(dev, "audiodev", audiodev_id);
        qdev_realize_and_unref(dev, bus, &error_fatal);
    } else {
        assert(!c->isa);
        c->init_pci(pci_bus, audiodev_id);
    }
}

