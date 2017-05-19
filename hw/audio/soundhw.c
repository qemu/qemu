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
#include "qemu-common.h"
#include "qemu/help_option.h"
#include "qemu/error-report.h"
#include "qom/object.h"
#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/audio/soundhw.h"

struct soundhw {
    const char *name;
    const char *descr;
    int enabled;
    int isa;
    union {
        int (*init_isa) (ISABus *bus);
        int (*init_pci) (PCIBus *bus);
    } init;
};

static struct soundhw soundhw[9];
static int soundhw_count;

void isa_register_soundhw(const char *name, const char *descr,
                          int (*init_isa)(ISABus *bus))
{
    assert(soundhw_count < ARRAY_SIZE(soundhw) - 1);
    soundhw[soundhw_count].name = name;
    soundhw[soundhw_count].descr = descr;
    soundhw[soundhw_count].isa = 1;
    soundhw[soundhw_count].init.init_isa = init_isa;
    soundhw_count++;
}

void pci_register_soundhw(const char *name, const char *descr,
                          int (*init_pci)(PCIBus *bus))
{
    assert(soundhw_count < ARRAY_SIZE(soundhw) - 1);
    soundhw[soundhw_count].name = name;
    soundhw[soundhw_count].descr = descr;
    soundhw[soundhw_count].isa = 0;
    soundhw[soundhw_count].init.init_pci = init_pci;
    soundhw_count++;
}

void select_soundhw(const char *optarg)
{
    struct soundhw *c;

    if (is_help_option(optarg)) {
    show_valid_cards:

        if (soundhw_count) {
             printf("Valid sound card names (comma separated):\n");
             for (c = soundhw; c->name; ++c) {
                 printf ("%-11s %s\n", c->name, c->descr);
             }
             printf("\n-soundhw all will enable all of the above\n");
        } else {
             printf("Machine has no user-selectable audio hardware "
                    "(it may or may not have always-present audio hardware).\n");
        }
        exit(!is_help_option(optarg));
    }
    else {
        size_t l;
        const char *p;
        char *e;
        int bad_card = 0;

        if (!strcmp(optarg, "all")) {
            for (c = soundhw; c->name; ++c) {
                c->enabled = 1;
            }
            return;
        }

        p = optarg;
        while (*p) {
            e = strchr(p, ',');
            l = !e ? strlen(p) : (size_t) (e - p);

            for (c = soundhw; c->name; ++c) {
                if (!strncmp(c->name, p, l) && !c->name[l]) {
                    c->enabled = 1;
                    break;
                }
            }

            if (!c->name) {
                if (l > 80) {
                    error_report("Unknown sound card name (too big to show)");
                }
                else {
                    error_report("Unknown sound card name `%.*s'",
                                 (int) l, p);
                }
                bad_card = 1;
            }
            p += l + (e != NULL);
        }

        if (bad_card) {
            goto show_valid_cards;
        }
    }
}

void soundhw_init(void)
{
    struct soundhw *c;
    ISABus *isa_bus = (ISABus *) object_resolve_path_type("", TYPE_ISA_BUS, NULL);
    PCIBus *pci_bus = (PCIBus *) object_resolve_path_type("", TYPE_PCI_BUS, NULL);

    for (c = soundhw; c->name; ++c) {
        if (c->enabled) {
            if (c->isa) {
                if (!isa_bus) {
                    error_report("ISA bus not available for %s", c->name);
                    exit(1);
                }
                c->init.init_isa(isa_bus);
            } else {
                if (!pci_bus) {
                    error_report("PCI bus not available for %s", c->name);
                    exit(1);
                }
                c->init.init_pci(pci_bus);
            }
        }
    }
}

