/*
 * ppc specific proc functions for linux-user
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PPC_TARGET_PROC_H
#define PPC_TARGET_PROC_H

#include <time.h>

#define PVR_VER(pvr)    (((pvr) >>  16) & 0xFFFF)      /* Version field */
#define PVR_REV(pvr)    (((pvr) >>   0) & 0xFFFF)      /* Revison field */
#define PVR_MAJ(pvr)    (((pvr) >>  4) & 0xF)   /* Major revision field */
#define PVR_MIN(pvr)    (((pvr) >>  0) & 0xF)   /* Minor revision field */

static int open_cpuinfo(CPUArchState *cpu_env, int fd)
{
    struct timespec res;
    double freq_mhz;
    int i, num_cpus;
    unsigned int maj, min, pvr;

    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(env_cpu(cpu_env));
    DeviceClass *dc = DEVICE_CLASS(ppc_cpu_get_family_class(pcc));

    pvr = pcc->pvr;

    /* Taken from Linux kernel: */
    /* If we are a Freescale core do a simple check so
     * we don't have to keep adding cases in the future */
    if (PVR_VER(pvr) & 0x8000) {
        switch (PVR_VER(pvr)) {
        case 0x8000: /* 7441/7450/7451, Voyager */
        case 0x8001: /* 7445/7455, Apollo 6 */
        case 0x8002: /* 7447/7457, Apollo 7 */
        case 0x8003: /* 7447A, Apollo 7 PM */
        case 0x8004: /* 7448, Apollo 8 */
        case 0x800c: /* 7410, Nitro */
            maj = ((pvr >> 8) & 0xF);
            min = PVR_MIN(pvr);
            break;
        default:     /* e500/book-e */
            maj = PVR_MAJ(pvr);
            min = PVR_MIN(pvr);
            break;
        }
    } else {
        switch (PVR_VER(pvr)) {
        case 0x1008: /* 740P/750P ?? */
            maj = ((pvr >> 8) & 0xFF) - 1;
            min = pvr & 0xFF;
            break;
        case 0x004e: /* POWER9 bits 12-15 give chip type */
        case 0x0080: /* POWER10 bit 12 gives SMT8/4 */
            maj = (pvr >> 8) & 0x0F;
            min = pvr & 0xFF;
            break;
        default:
            maj = (pvr >> 8) & 0xFF;
            min = pvr & 0xFF;
            break;
        }
    }

    if (clock_getres(CLOCK_REALTIME, &res) == -1) {
        res.tv_nsec = 1;
    }
    freq_mhz = 1000.0 / res.tv_nsec;

    num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    for (i = 0; i < num_cpus; i++) {
        dprintf(fd, "processor\t: %d\n", i);
        dprintf(fd, "cpu\t\t: %s%s\n",
                    dc->desc,
                    pcc->insns_flags & PPC_ALTIVEC ? ", altivec supported":"");
        dprintf(fd, "clock\t\t: %.3fMHz\n", freq_mhz);
        dprintf(fd, "revision\t: %d.%d (pvr %04x %04x)\n\n",
                    maj, min, PVR_VER(pvr), PVR_REV(pvr));
    }

    dprintf(fd, "timebase\t: 1000000000\n");
    dprintf(fd, "platform\t: pSeries\n");
    dprintf(fd, "model\t\t: IBM pSeries (QEMU user v" QEMU_VERSION ")\n");
    dprintf(fd, "machine\t\t: CHRP IBM pSeries\n");

    return 0;
}
#define HAVE_ARCH_PROC_CPUINFO

#endif /* PPC_TARGET_PROC_H */
