/*
 * QEMU PC APM controller Emulation
 * This is split out from acpi.c
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "apm.h"
#include "hw.h"

//#define DEBUG

#ifdef DEBUG
# define APM_DPRINTF(format, ...)       printf(format, ## __VA_ARGS__)
#else
# define APM_DPRINTF(format, ...)       do { } while (0)
#endif

/* fixed I/O location */
#define APM_CNT_IOPORT  0xb2
#define APM_STS_IOPORT  0xb3

static void apm_ioport_writeb(void *opaque, uint32_t addr, uint32_t val)
{
    APMState *apm = opaque;
    addr &= 1;
    APM_DPRINTF("apm_ioport_writeb addr=0x%x val=0x%02x\n", addr, val);
    if (addr == 0) {
        apm->apmc = val;

        if (apm->callback) {
            (apm->callback)(val, apm->arg);
        }
    } else {
        apm->apms = val;
    }
}

static uint32_t apm_ioport_readb(void *opaque, uint32_t addr)
{
    APMState *apm = opaque;
    uint32_t val;

    addr &= 1;
    if (addr == 0) {
        val = apm->apmc;
    } else {
        val = apm->apms;
    }
    APM_DPRINTF("apm_ioport_readb addr=0x%x val=0x%02x\n", addr, val);
    return val;
}

const VMStateDescription vmstate_apm = {
    .name = "APM State",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8(apmc, APMState),
        VMSTATE_UINT8(apms, APMState),
        VMSTATE_END_OF_LIST()
    }
};

void apm_init(APMState *apm, apm_ctrl_changed_t callback, void *arg)
{
    apm->callback = callback;
    apm->arg = arg;

    /* ioport 0xb2, 0xb3 */
    register_ioport_write(APM_CNT_IOPORT, 2, 1, apm_ioport_writeb, apm);
    register_ioport_read(APM_CNT_IOPORT, 2, 1, apm_ioport_readb, apm);
}
