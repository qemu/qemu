/*
 * QEMU ICH9 Timer emulation
 *
 * Copyright (c) 2024 Dominic Prinz <git@dprinz.de>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/core/cpu.h"
#include "hw/pci/pci.h"
#include "hw/southbridge/ich9.h"
#include "qemu/timer.h"

#include "hw/acpi/ich9_timer.h"

void ich9_pm_update_swsmi_timer(ICH9LPCPMRegs *pm, bool enable)
{
    uint16_t swsmi_rate_sel;
    int64_t expire_time;
    ICH9LPCState *lpc;

    if (enable) {
        lpc = container_of(pm, ICH9LPCState, pm);
        swsmi_rate_sel =
            (pci_get_word(lpc->d.config + ICH9_LPC_GEN_PMCON_3) & 0xc0) >> 6;

        if (swsmi_rate_sel == 0) {
            expire_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1500000LL;
        } else {
            expire_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                          8 * (1 << swsmi_rate_sel) * 1000000LL;
        }

        timer_mod(pm->swsmi_timer, expire_time);
    } else {
        timer_del(pm->swsmi_timer);
    }
}

static void ich9_pm_swsmi_timer_expired(void *opaque)
{
    ICH9LPCPMRegs *pm = opaque;

    pm->smi_sts |= ICH9_PMIO_SMI_STS_SWSMI_STS;
    ich9_generate_smi();

    ich9_pm_update_swsmi_timer(pm, pm->smi_en & ICH9_PMIO_SMI_EN_SWSMI_EN);
}

void ich9_pm_swsmi_timer_init(ICH9LPCPMRegs *pm)
{
    pm->smi_sts_wmask |= ICH9_PMIO_SMI_STS_SWSMI_STS;
    pm->swsmi_timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, ich9_pm_swsmi_timer_expired, pm);
}

void ich9_pm_update_periodic_timer(ICH9LPCPMRegs *pm, bool enable)
{
    uint16_t per_smi_sel;
    int64_t expire_time;
    ICH9LPCState *lpc;

    if (enable) {
        lpc = container_of(pm, ICH9LPCState, pm);
        per_smi_sel = pci_get_word(lpc->d.config + ICH9_LPC_GEN_PMCON_1) & 3;
        expire_time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      8 * (1 << (3 - per_smi_sel)) * NANOSECONDS_PER_SECOND;

        timer_mod(pm->periodic_timer, expire_time);
    } else {
        timer_del(pm->periodic_timer);
    }
}

static void ich9_pm_periodic_timer_expired(void *opaque)
{
    ICH9LPCPMRegs *pm = opaque;

    pm->smi_sts = ICH9_PMIO_SMI_STS_PERIODIC_STS;
    ich9_generate_smi();

    ich9_pm_update_periodic_timer(pm,
                                  pm->smi_en & ICH9_PMIO_SMI_EN_PERIODIC_EN);
}

void ich9_pm_periodic_timer_init(ICH9LPCPMRegs *pm)
{
    pm->smi_sts_wmask |= ICH9_PMIO_SMI_STS_PERIODIC_STS;
    pm->periodic_timer =
        timer_new_ns(QEMU_CLOCK_VIRTUAL, ich9_pm_periodic_timer_expired, pm);
}
