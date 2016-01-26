/*
 * QEMU Smram/pam logic implementation
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2011 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 * Copyright (c) 2012 Jason Baron <jbaron@redhat.com>
 *
 * Split out from piix.c
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
#include "qom/object.h"
#include "sysemu/sysemu.h"
#include "hw/pci-host/pam.h"

void init_pam(DeviceState *dev, MemoryRegion *ram_memory,
              MemoryRegion *system_memory, MemoryRegion *pci_address_space,
              PAMMemoryRegion *mem, uint32_t start, uint32_t size)
{
    int i;

    /* RAM */
    memory_region_init_alias(&mem->alias[3], OBJECT(dev), "pam-ram", ram_memory,
                             start, size);
    /* ROM (XXX: not quite correct) */
    memory_region_init_alias(&mem->alias[1], OBJECT(dev), "pam-rom", ram_memory,
                             start, size);
    memory_region_set_readonly(&mem->alias[1], true);

    /* XXX: should distinguish read/write cases */
    memory_region_init_alias(&mem->alias[0], OBJECT(dev), "pam-pci", pci_address_space,
                             start, size);
    memory_region_init_alias(&mem->alias[2], OBJECT(dev), "pam-pci", ram_memory,
                             start, size);

    for (i = 0; i < 4; ++i) {
        memory_region_set_enabled(&mem->alias[i], false);
        memory_region_add_subregion_overlap(system_memory, start,
                                            &mem->alias[i], 1);
    }
    mem->current = 0;
}

void pam_update(PAMMemoryRegion *pam, int idx, uint8_t val)
{
    assert(0 <= idx && idx <= 12);

    memory_region_set_enabled(&pam->alias[pam->current], false);
    pam->current = (val >> ((!(idx & 1)) * 4)) & PAM_ATTR_MASK;
    memory_region_set_enabled(&pam->alias[pam->current], true);
}
