/*
 * ARM IoTKit system control element
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the "system control element" which is part of the
 * Arm IoTKit and documented in
 * https://developer.arm.com/documentation/ecm0601256/latest
 * Specifically, it implements the "system control register" blocks.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/runstate.h"
#include "trace.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/registerfields.h"
#include "hw/misc/iotkit-sysctl.h"
#include "hw/qdev-properties.h"
#include "hw/arm/armsse-version.h"
#include "target/arm/arm-powerctl.h"

REG32(SECDBGSTAT, 0x0)
REG32(SECDBGSET, 0x4)
REG32(SECDBGCLR, 0x8)
REG32(SCSECCTRL, 0xc)
REG32(FCLK_DIV, 0x10)
REG32(SYSCLK_DIV, 0x14)
REG32(CLOCK_FORCE, 0x18)
REG32(RESET_SYNDROME, 0x100)
REG32(RESET_MASK, 0x104)
REG32(SWRESET, 0x108)
    FIELD(SWRESET, SWRESETREQ, 9, 1)
REG32(GRETREG, 0x10c)
REG32(INITSVTOR0, 0x110)
    FIELD(INITSVTOR0, LOCK, 0, 1)
    FIELD(INITSVTOR0, VTOR, 7, 25)
REG32(INITSVTOR1, 0x114)
REG32(CPUWAIT, 0x118)
REG32(NMI_ENABLE, 0x11c) /* BUSWAIT in IoTKit */
REG32(WICCTRL, 0x120)
REG32(EWCTRL, 0x124)
REG32(PWRCTRL, 0x1fc)
    FIELD(PWRCTRL, PPU_ACCESS_UNLOCK, 0, 1)
    FIELD(PWRCTRL, PPU_ACCESS_FILTER, 1, 1)
REG32(PDCM_PD_SYS_SENSE, 0x200)
REG32(PDCM_PD_CPU0_SENSE, 0x204)
REG32(PDCM_PD_SRAM0_SENSE, 0x20c)
REG32(PDCM_PD_SRAM1_SENSE, 0x210)
REG32(PDCM_PD_SRAM2_SENSE, 0x214) /* PDCM_PD_VMR0_SENSE on SSE300 */
REG32(PDCM_PD_SRAM3_SENSE, 0x218) /* PDCM_PD_VMR1_SENSE on SSE300 */
REG32(PID4, 0xfd0)
REG32(PID5, 0xfd4)
REG32(PID6, 0xfd8)
REG32(PID7, 0xfdc)
REG32(PID0, 0xfe0)
REG32(PID1, 0xfe4)
REG32(PID2, 0xfe8)
REG32(PID3, 0xfec)
REG32(CID0, 0xff0)
REG32(CID1, 0xff4)
REG32(CID2, 0xff8)
REG32(CID3, 0xffc)

/* PID/CID values */
static const int iotkit_sysctl_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0x54, 0xb8, 0x0b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

/* Also used by the SSE300 */
static const int sse200_sysctl_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0x54, 0xb8, 0x1b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

/*
 * Set the initial secure vector table offset address for the core.
 * This will take effect when the CPU next resets.
 */
static void set_init_vtor(uint64_t cpuid, uint32_t vtor)
{
    Object *cpuobj = OBJECT(arm_get_cpu_by_id(cpuid));

    if (cpuobj) {
        if (object_property_find(cpuobj, "init-svtor")) {
            object_property_set_uint(cpuobj, "init-svtor", vtor, &error_abort);
        }
    }
}

static uint64_t iotkit_sysctl_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    IoTKitSysCtl *s = IOTKIT_SYSCTL(opaque);
    uint64_t r;

    switch (offset) {
    case A_SECDBGSTAT:
        r = s->secure_debug;
        break;
    case A_SCSECCTRL:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
        case ARMSSE_SSE300:
            r = s->scsecctrl;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_FCLK_DIV:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
        case ARMSSE_SSE300:
            r = s->fclk_div;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_SYSCLK_DIV:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
        case ARMSSE_SSE300:
            r = s->sysclk_div;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_CLOCK_FORCE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
        case ARMSSE_SSE300:
            r = s->clock_force;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_RESET_SYNDROME:
        r = s->reset_syndrome;
        break;
    case A_RESET_MASK:
        r = s->reset_mask;
        break;
    case A_GRETREG:
        r = s->gretreg;
        break;
    case A_INITSVTOR0:
        r = s->initsvtor0;
        break;
    case A_INITSVTOR1:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
            r = s->initsvtor1;
            break;
        case ARMSSE_SSE300:
            goto bad_offset;
        default:
            g_assert_not_reached();
        }
        break;
    case A_CPUWAIT:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
        case ARMSSE_SSE200:
            r = s->cpuwait;
            break;
        case ARMSSE_SSE300:
            /* In SSE300 this is reserved (for INITSVTOR2) */
            goto bad_offset;
        default:
            g_assert_not_reached();
        }
        break;
    case A_NMI_ENABLE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            /* In IoTKit this is named BUSWAIT but marked reserved, R/O, zero */
            r = 0;
            break;
        case ARMSSE_SSE200:
            r = s->nmi_enable;
            break;
        case ARMSSE_SSE300:
            /* In SSE300 this is reserved (for INITSVTOR3) */
            goto bad_offset;
        default:
            g_assert_not_reached();
        }
        break;
    case A_WICCTRL:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
        case ARMSSE_SSE200:
            r = s->wicctrl;
            break;
        case ARMSSE_SSE300:
            /* In SSE300 this offset is CPUWAIT */
            r = s->cpuwait;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_EWCTRL:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
            r = s->ewctrl;
            break;
        case ARMSSE_SSE300:
            /* In SSE300 this offset is NMI_ENABLE */
            r = s->nmi_enable;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PWRCTRL:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
        case ARMSSE_SSE200:
            goto bad_offset;
        case ARMSSE_SSE300:
            r = s->pwrctrl;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PDCM_PD_SYS_SENSE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
        case ARMSSE_SSE300:
            r = s->pdcm_pd_sys_sense;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PDCM_PD_CPU0_SENSE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
        case ARMSSE_SSE200:
            goto bad_offset;
        case ARMSSE_SSE300:
            r = s->pdcm_pd_cpu0_sense;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PDCM_PD_SRAM0_SENSE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
            r = s->pdcm_pd_sram0_sense;
            break;
        case ARMSSE_SSE300:
            goto bad_offset;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PDCM_PD_SRAM1_SENSE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
            r = s->pdcm_pd_sram1_sense;
            break;
        case ARMSSE_SSE300:
            goto bad_offset;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PDCM_PD_SRAM2_SENSE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
            r = s->pdcm_pd_sram2_sense;
            break;
        case ARMSSE_SSE300:
            r = s->pdcm_pd_vmr0_sense;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PDCM_PD_SRAM3_SENSE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
            r = s->pdcm_pd_sram3_sense;
            break;
        case ARMSSE_SSE300:
            r = s->pdcm_pd_vmr1_sense;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PID4 ... A_CID3:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            r = iotkit_sysctl_id[(offset - A_PID4) / 4];
            break;
        case ARMSSE_SSE200:
        case ARMSSE_SSE300:
            r = sse200_sysctl_id[(offset - A_PID4) / 4];
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_SECDBGSET:
    case A_SECDBGCLR:
    case A_SWRESET:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IoTKit SysCtl read: read of WO offset %x\n",
                      (int)offset);
        r = 0;
        break;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IoTKit SysCtl read: bad offset %x\n", (int)offset);
        r = 0;
        break;
    }
    trace_iotkit_sysctl_read(offset, r, size);
    return r;
}

static void cpuwait_write(IoTKitSysCtl *s, uint32_t value)
{
    int num_cpus = (s->sse_version == ARMSSE_SSE300) ? 1 : 2;
    int i;

    for (i = 0; i < num_cpus; i++) {
        uint32_t mask = 1 << i;
        if ((s->cpuwait & mask) && !(value & mask)) {
            /* Powering up CPU 0 */
            arm_set_cpu_on_and_reset(i);
        }
    }
    s->cpuwait = value;
}

static void iotkit_sysctl_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    IoTKitSysCtl *s = IOTKIT_SYSCTL(opaque);

    trace_iotkit_sysctl_write(offset, value, size);

    /*
     * Most of the state here has to do with control of reset and
     * similar kinds of power up -- for instance the guest can ask
     * what the reason for the last reset was, or forbid reset for
     * some causes (like the non-secure watchdog). Most of this is
     * not relevant to QEMU, which doesn't really model anything other
     * than a full power-on reset.
     * We just model the registers as reads-as-written.
     */

    switch (offset) {
    case A_RESET_SYNDROME:
        qemu_log_mask(LOG_UNIMP,
                      "IoTKit SysCtl RESET_SYNDROME unimplemented\n");
        s->reset_syndrome = value;
        break;
    case A_RESET_MASK:
        qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl RESET_MASK unimplemented\n");
        s->reset_mask = value;
        break;
    case A_GRETREG:
        /*
         * General retention register, which is only reset by a power-on
         * reset. Technically this implementation is complete, since
         * QEMU only supports power-on resets...
         */
        s->gretreg = value;
        break;
    case A_INITSVTOR0:
        switch (s->sse_version) {
        case ARMSSE_SSE300:
            /* SSE300 has a LOCK bit which prevents further writes when set */
            if (s->initsvtor0 & R_INITSVTOR0_LOCK_MASK) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "IoTKit INITSVTOR0 write when register locked\n");
                break;
            }
            s->initsvtor0 = value;
            set_init_vtor(0, s->initsvtor0 & R_INITSVTOR0_VTOR_MASK);
            break;
        case ARMSSE_IOTKIT:
        case ARMSSE_SSE200:
            s->initsvtor0 = value;
            set_init_vtor(0, s->initsvtor0);
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_CPUWAIT:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
        case ARMSSE_SSE200:
            cpuwait_write(s, value);
            break;
        case ARMSSE_SSE300:
            /* In SSE300 this is reserved (for INITSVTOR2) */
            goto bad_offset;
        default:
            g_assert_not_reached();
        }
        break;
    case A_WICCTRL:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
        case ARMSSE_SSE200:
            qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl WICCTRL unimplemented\n");
            s->wicctrl = value;
            break;
        case ARMSSE_SSE300:
            /* In SSE300 this offset is CPUWAIT */
            cpuwait_write(s, value);
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_SECDBGSET:
        /* write-1-to-set */
        qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl SECDBGSET unimplemented\n");
        s->secure_debug |= value;
        break;
    case A_SECDBGCLR:
        /* write-1-to-clear */
        s->secure_debug &= ~value;
        break;
    case A_SWRESET:
        /* One w/o bit to request a reset; all other bits reserved */
        if (value & R_SWRESET_SWRESETREQ_MASK) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        break;
    case A_SCSECCTRL:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
        case ARMSSE_SSE300:
            qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl SCSECCTRL unimplemented\n");
            s->scsecctrl = value;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_FCLK_DIV:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
        case ARMSSE_SSE300:
            qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl FCLK_DIV unimplemented\n");
            s->fclk_div = value;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_SYSCLK_DIV:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
        case ARMSSE_SSE300:
            qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl SYSCLK_DIV unimplemented\n");
            s->sysclk_div = value;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_CLOCK_FORCE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
        case ARMSSE_SSE300:
            qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl CLOCK_FORCE unimplemented\n");
            s->clock_force = value;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_INITSVTOR1:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
            s->initsvtor1 = value;
            set_init_vtor(1, s->initsvtor1);
            break;
        case ARMSSE_SSE300:
            goto bad_offset;
        default:
            g_assert_not_reached();
        }
        break;
    case A_EWCTRL:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
            qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl EWCTRL unimplemented\n");
            s->ewctrl = value;
            break;
        case ARMSSE_SSE300:
            /* In SSE300 this offset is NMI_ENABLE */
            qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl NMI_ENABLE unimplemented\n");
            s->nmi_enable = value;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PWRCTRL:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
        case ARMSSE_SSE200:
            goto bad_offset;
        case ARMSSE_SSE300:
            if (!(s->pwrctrl & R_PWRCTRL_PPU_ACCESS_UNLOCK_MASK)) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "IoTKit PWRCTRL write when register locked\n");
                break;
            }
            s->pwrctrl = value;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PDCM_PD_SYS_SENSE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
        case ARMSSE_SSE300:
            qemu_log_mask(LOG_UNIMP,
                          "IoTKit SysCtl PDCM_PD_SYS_SENSE unimplemented\n");
            s->pdcm_pd_sys_sense = value;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PDCM_PD_CPU0_SENSE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
        case ARMSSE_SSE200:
            goto bad_offset;
        case ARMSSE_SSE300:
            qemu_log_mask(LOG_UNIMP,
                          "IoTKit SysCtl PDCM_PD_CPU0_SENSE unimplemented\n");
            s->pdcm_pd_cpu0_sense = value;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PDCM_PD_SRAM0_SENSE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
            qemu_log_mask(LOG_UNIMP,
                          "IoTKit SysCtl PDCM_PD_SRAM0_SENSE unimplemented\n");
            s->pdcm_pd_sram0_sense = value;
            break;
        case ARMSSE_SSE300:
            goto bad_offset;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PDCM_PD_SRAM1_SENSE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
            qemu_log_mask(LOG_UNIMP,
                          "IoTKit SysCtl PDCM_PD_SRAM1_SENSE unimplemented\n");
            s->pdcm_pd_sram1_sense = value;
            break;
        case ARMSSE_SSE300:
            goto bad_offset;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PDCM_PD_SRAM2_SENSE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
            qemu_log_mask(LOG_UNIMP,
                          "IoTKit SysCtl PDCM_PD_SRAM2_SENSE unimplemented\n");
            s->pdcm_pd_sram2_sense = value;
            break;
        case ARMSSE_SSE300:
            qemu_log_mask(LOG_UNIMP,
                          "IoTKit SysCtl PDCM_PD_VMR0_SENSE unimplemented\n");
            s->pdcm_pd_vmr0_sense = value;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_PDCM_PD_SRAM3_SENSE:
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto bad_offset;
        case ARMSSE_SSE200:
            qemu_log_mask(LOG_UNIMP,
                          "IoTKit SysCtl PDCM_PD_SRAM3_SENSE unimplemented\n");
            s->pdcm_pd_sram3_sense = value;
            break;
        case ARMSSE_SSE300:
            qemu_log_mask(LOG_UNIMP,
                          "IoTKit SysCtl PDCM_PD_VMR1_SENSE unimplemented\n");
            s->pdcm_pd_vmr1_sense = value;
            break;
        default:
            g_assert_not_reached();
        }
        break;
    case A_NMI_ENABLE:
        /* In IoTKit this is BUSWAIT: reserved, R/O, zero */
        switch (s->sse_version) {
        case ARMSSE_IOTKIT:
            goto ro_offset;
        case ARMSSE_SSE200:
            qemu_log_mask(LOG_UNIMP, "IoTKit SysCtl NMI_ENABLE unimplemented\n");
            s->nmi_enable = value;
            break;
        case ARMSSE_SSE300:
            /* In SSE300 this is reserved (for INITSVTOR3) */
            goto bad_offset;
        default:
            g_assert_not_reached();
        }
        break;
    case A_SECDBGSTAT:
    case A_PID4 ... A_CID3:
    ro_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IoTKit SysCtl write: write of RO offset %x\n",
                      (int)offset);
        break;
    default:
    bad_offset:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IoTKit SysCtl write: bad offset %x\n", (int)offset);
        break;
    }
}

static const MemoryRegionOps iotkit_sysctl_ops = {
    .read = iotkit_sysctl_read,
    .write = iotkit_sysctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* byte/halfword accesses are just zero-padded on reads and writes */
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void iotkit_sysctl_reset(DeviceState *dev)
{
    IoTKitSysCtl *s = IOTKIT_SYSCTL(dev);

    trace_iotkit_sysctl_reset();
    s->secure_debug = 0;
    s->reset_syndrome = 1;
    s->reset_mask = 0;
    s->gretreg = 0;
    s->initsvtor0 = s->initsvtor0_rst;
    s->initsvtor1 = s->initsvtor1_rst;
    s->cpuwait = s->cpuwait_rst;
    s->wicctrl = 0;
    s->scsecctrl = 0;
    s->fclk_div = 0;
    s->sysclk_div = 0;
    s->clock_force = 0;
    s->nmi_enable = 0;
    s->ewctrl = 0;
    s->pwrctrl = 0x3;
    s->pdcm_pd_sys_sense = 0x7f;
    s->pdcm_pd_sram0_sense = 0;
    s->pdcm_pd_sram1_sense = 0;
    s->pdcm_pd_sram2_sense = 0;
    s->pdcm_pd_sram3_sense = 0;
    s->pdcm_pd_cpu0_sense = 0;
    s->pdcm_pd_vmr0_sense = 0;
    s->pdcm_pd_vmr1_sense = 0;
}

static void iotkit_sysctl_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IoTKitSysCtl *s = IOTKIT_SYSCTL(obj);

    memory_region_init_io(&s->iomem, obj, &iotkit_sysctl_ops,
                          s, "iotkit-sysctl", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void iotkit_sysctl_realize(DeviceState *dev, Error **errp)
{
    IoTKitSysCtl *s = IOTKIT_SYSCTL(dev);

    if (!armsse_version_valid(s->sse_version)) {
        error_setg(errp, "invalid sse-version value %d", s->sse_version);
        return;
    }
}

static bool sse300_needed(void *opaque)
{
    IoTKitSysCtl *s = IOTKIT_SYSCTL(opaque);

    return s->sse_version == ARMSSE_SSE300;
}

static const VMStateDescription iotkit_sysctl_sse300_vmstate = {
    .name = "iotkit-sysctl/sse-300",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = sse300_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(pwrctrl, IoTKitSysCtl),
        VMSTATE_UINT32(pdcm_pd_cpu0_sense, IoTKitSysCtl),
        VMSTATE_UINT32(pdcm_pd_vmr0_sense, IoTKitSysCtl),
        VMSTATE_UINT32(pdcm_pd_vmr1_sense, IoTKitSysCtl),
        VMSTATE_END_OF_LIST()
    }
};

static bool sse200_needed(void *opaque)
{
    IoTKitSysCtl *s = IOTKIT_SYSCTL(opaque);

    return s->sse_version != ARMSSE_IOTKIT;
}

static const VMStateDescription iotkit_sysctl_sse200_vmstate = {
    .name = "iotkit-sysctl/sse-200",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = sse200_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(scsecctrl, IoTKitSysCtl),
        VMSTATE_UINT32(fclk_div, IoTKitSysCtl),
        VMSTATE_UINT32(sysclk_div, IoTKitSysCtl),
        VMSTATE_UINT32(clock_force, IoTKitSysCtl),
        VMSTATE_UINT32(initsvtor1, IoTKitSysCtl),
        VMSTATE_UINT32(nmi_enable, IoTKitSysCtl),
        VMSTATE_UINT32(pdcm_pd_sys_sense, IoTKitSysCtl),
        VMSTATE_UINT32(pdcm_pd_sram0_sense, IoTKitSysCtl),
        VMSTATE_UINT32(pdcm_pd_sram1_sense, IoTKitSysCtl),
        VMSTATE_UINT32(pdcm_pd_sram2_sense, IoTKitSysCtl),
        VMSTATE_UINT32(pdcm_pd_sram3_sense, IoTKitSysCtl),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription iotkit_sysctl_vmstate = {
    .name = "iotkit-sysctl",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(secure_debug, IoTKitSysCtl),
        VMSTATE_UINT32(reset_syndrome, IoTKitSysCtl),
        VMSTATE_UINT32(reset_mask, IoTKitSysCtl),
        VMSTATE_UINT32(gretreg, IoTKitSysCtl),
        VMSTATE_UINT32(initsvtor0, IoTKitSysCtl),
        VMSTATE_UINT32(cpuwait, IoTKitSysCtl),
        VMSTATE_UINT32(wicctrl, IoTKitSysCtl),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &iotkit_sysctl_sse200_vmstate,
        &iotkit_sysctl_sse300_vmstate,
        NULL
    }
};

static const Property iotkit_sysctl_props[] = {
    DEFINE_PROP_UINT32("sse-version", IoTKitSysCtl, sse_version, 0),
    DEFINE_PROP_UINT32("CPUWAIT_RST", IoTKitSysCtl, cpuwait_rst, 0),
    DEFINE_PROP_UINT32("INITSVTOR0_RST", IoTKitSysCtl, initsvtor0_rst,
                       0x10000000),
    DEFINE_PROP_UINT32("INITSVTOR1_RST", IoTKitSysCtl, initsvtor1_rst,
                       0x10000000),
};

static void iotkit_sysctl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &iotkit_sysctl_vmstate;
    device_class_set_legacy_reset(dc, iotkit_sysctl_reset);
    device_class_set_props(dc, iotkit_sysctl_props);
    dc->realize = iotkit_sysctl_realize;
}

static const TypeInfo iotkit_sysctl_info = {
    .name = TYPE_IOTKIT_SYSCTL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IoTKitSysCtl),
    .instance_init = iotkit_sysctl_init,
    .class_init = iotkit_sysctl_class_init,
};

static void iotkit_sysctl_register_types(void)
{
    type_register_static(&iotkit_sysctl_info);
}

type_init(iotkit_sysctl_register_types);
