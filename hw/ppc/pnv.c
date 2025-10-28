/*
 * QEMU PowerPC PowerNV machine model
 *
 * Copyright (c) 2016-2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "qemu/datadir.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "system/qtest.h"
#include "system/system.h"
#include "system/numa.h"
#include "system/reset.h"
#include "system/runstate.h"
#include "system/cpus.h"
#include "system/device_tree.h"
#include "system/hw_accel.h"
#include "target/ppc/cpu.h"
#include "hw/ppc/fdt.h"
#include "hw/ppc/ppc.h"
#include "hw/ppc/pnv.h"
#include "hw/ppc/pnv_core.h"
#include "hw/loader.h"
#include "hw/nmi.h"
#include "qapi/visitor.h"
#include "hw/intc/intc.h"
#include "hw/ipmi/ipmi.h"
#include "target/ppc/mmu-hash64.h"
#include "hw/pci/msi.h"
#include "hw/pci-host/pnv_phb.h"
#include "hw/pci-host/pnv_phb3.h"
#include "hw/pci-host/pnv_phb4.h"

#include "hw/ppc/xics.h"
#include "hw/qdev-properties.h"
#include "hw/ppc/pnv_chip.h"
#include "hw/ppc/pnv_xscom.h"
#include "hw/ppc/pnv_pnor.h"

#include "hw/isa/isa.h"
#include "hw/char/serial-isa.h"
#include "hw/rtc/mc146818rtc.h"

#include <libfdt.h>

#define FDT_MAX_SIZE            (1 * MiB)

#define FW_FILE_NAME            "skiboot.lid"
#define FW_LOAD_ADDR            0x0
#define FW_MAX_SIZE             (16 * MiB)

#define PNOR_FILE_NAME          "pnv-pnor.bin"

#define KERNEL_LOAD_ADDR        0x20000000
#define KERNEL_MAX_SIZE         (128 * MiB)
#define INITRD_LOAD_ADDR        0x28000000
#define INITRD_MAX_SIZE         (128 * MiB)

static const char *pnv_chip_core_typename(const PnvChip *o)
{
    const char *chip_type = object_class_get_name(object_get_class(OBJECT(o)));
    int len = strlen(chip_type) - strlen(PNV_CHIP_TYPE_SUFFIX);
    char *s = g_strdup_printf(PNV_CORE_TYPE_NAME("%.*s"), len, chip_type);
    const char *core_type = object_class_get_name(object_class_by_name(s));
    g_free(s);
    return core_type;
}

/*
 * On Power Systems E880 (POWER8), the max cpus (threads) should be :
 *     4 * 4 sockets * 12 cores * 8 threads = 1536
 * Let's make it 2^11
 */
#define MAX_CPUS                2048

/*
 * Memory nodes are created by hostboot, one for each range of memory
 * that has a different "affinity". In practice, it means one range
 * per chip.
 */
static void pnv_dt_memory(void *fdt, int chip_id, hwaddr start, hwaddr size)
{
    char *mem_name;
    uint64_t mem_reg_property[2];
    int off;

    mem_reg_property[0] = cpu_to_be64(start);
    mem_reg_property[1] = cpu_to_be64(size);

    mem_name = g_strdup_printf("memory@%"HWADDR_PRIx, start);
    off = fdt_add_subnode(fdt, 0, mem_name);
    g_free(mem_name);

    _FDT((fdt_setprop_string(fdt, off, "device_type", "memory")));
    _FDT((fdt_setprop(fdt, off, "reg", mem_reg_property,
                       sizeof(mem_reg_property))));
    _FDT((fdt_setprop_cell(fdt, off, "ibm,chip-id", chip_id)));
}

static int get_cpus_node(void *fdt)
{
    int cpus_offset = fdt_path_offset(fdt, "/cpus");

    if (cpus_offset < 0) {
        cpus_offset = fdt_add_subnode(fdt, 0, "cpus");
        if (cpus_offset) {
            _FDT((fdt_setprop_cell(fdt, cpus_offset, "#address-cells", 0x1)));
            _FDT((fdt_setprop_cell(fdt, cpus_offset, "#size-cells", 0x0)));
        }
    }
    _FDT(cpus_offset);
    return cpus_offset;
}

/*
 * The PowerNV cores (and threads) need to use real HW ids and not an
 * incremental index like it has been done on other platforms. This HW
 * id is stored in the CPU PIR, it is used to create cpu nodes in the
 * device tree, used in XSCOM to address cores and in interrupt
 * servers.
 */
static int pnv_dt_core(PnvChip *chip, PnvCore *pc, void *fdt)
{
    PowerPCCPU *cpu = pc->threads[0];
    CPUState *cs = CPU(cpu);
    DeviceClass *dc = DEVICE_GET_CLASS(cs);
    int smt_threads = CPU_CORE(pc)->nr_threads;
    CPUPPCState *env = &cpu->env;
    PowerPCCPUClass *pcc = POWERPC_CPU_GET_CLASS(cs);
    PnvChipClass *pnv_cc = PNV_CHIP_GET_CLASS(chip);
    uint32_t *servers_prop;
    int i;
    uint32_t pir, tir;
    uint32_t segs[] = {cpu_to_be32(28), cpu_to_be32(40),
                       0xffffffff, 0xffffffff};
    uint32_t tbfreq = PNV_TIMEBASE_FREQ;
    uint32_t cpufreq = 1000000000;
    uint32_t page_sizes_prop[64];
    size_t page_sizes_prop_size;
    int offset;
    char *nodename;
    int cpus_offset = get_cpus_node(fdt);

    pnv_cc->get_pir_tir(chip, pc->hwid, 0, &pir, &tir);

    /* Only one DT node per (big) core */
    g_assert(tir == 0);

    nodename = g_strdup_printf("%s@%x", dc->fw_name, pir);
    offset = fdt_add_subnode(fdt, cpus_offset, nodename);
    _FDT(offset);
    g_free(nodename);

    _FDT((fdt_setprop_cell(fdt, offset, "ibm,chip-id", chip->chip_id)));

    _FDT((fdt_setprop_cell(fdt, offset, "reg", pir)));
    _FDT((fdt_setprop_cell(fdt, offset, "ibm,pir", pir)));
    _FDT((fdt_setprop_string(fdt, offset, "device_type", "cpu")));

    _FDT((fdt_setprop_cell(fdt, offset, "cpu-version", env->spr[SPR_PVR])));
    _FDT((fdt_setprop_cell(fdt, offset, "d-cache-block-size",
                            env->dcache_line_size)));
    _FDT((fdt_setprop_cell(fdt, offset, "d-cache-line-size",
                            env->dcache_line_size)));
    _FDT((fdt_setprop_cell(fdt, offset, "i-cache-block-size",
                            env->icache_line_size)));
    _FDT((fdt_setprop_cell(fdt, offset, "i-cache-line-size",
                            env->icache_line_size)));

    if (pcc->l1_dcache_size) {
        _FDT((fdt_setprop_cell(fdt, offset, "d-cache-size",
                               pcc->l1_dcache_size)));
    } else {
        warn_report("Unknown L1 dcache size for cpu");
    }
    if (pcc->l1_icache_size) {
        _FDT((fdt_setprop_cell(fdt, offset, "i-cache-size",
                               pcc->l1_icache_size)));
    } else {
        warn_report("Unknown L1 icache size for cpu");
    }

    _FDT((fdt_setprop_cell(fdt, offset, "timebase-frequency", tbfreq)));
    _FDT((fdt_setprop_cell(fdt, offset, "clock-frequency", cpufreq)));
    _FDT((fdt_setprop_cell(fdt, offset, "ibm,slb-size",
                           cpu->hash64_opts->slb_size)));
    _FDT((fdt_setprop_string(fdt, offset, "status", "okay")));
    _FDT((fdt_setprop(fdt, offset, "64-bit", NULL, 0)));

    if (ppc_has_spr(cpu, SPR_PURR)) {
        _FDT((fdt_setprop(fdt, offset, "ibm,purr", NULL, 0)));
    }

    if (ppc_hash64_has(cpu, PPC_HASH64_1TSEG)) {
        _FDT((fdt_setprop(fdt, offset, "ibm,processor-segment-sizes",
                           segs, sizeof(segs))));
    }

    /*
     * Advertise VMX/VSX (vector extensions) if available
     *   0 / no property == no vector extensions
     *   1               == VMX / Altivec available
     *   2               == VSX available
     */
    if (env->insns_flags & PPC_ALTIVEC) {
        uint32_t vmx = (env->insns_flags2 & PPC2_VSX) ? 2 : 1;

        _FDT((fdt_setprop_cell(fdt, offset, "ibm,vmx", vmx)));
    }

    /*
     * Advertise DFP (Decimal Floating Point) if available
     *   0 / no property == no DFP
     *   1               == DFP available
     */
    if (env->insns_flags2 & PPC2_DFP) {
        _FDT((fdt_setprop_cell(fdt, offset, "ibm,dfp", 1)));
    }

    page_sizes_prop_size = ppc_create_page_sizes_prop(cpu, page_sizes_prop,
                                                      sizeof(page_sizes_prop));
    if (page_sizes_prop_size) {
        _FDT((fdt_setprop(fdt, offset, "ibm,segment-page-sizes",
                           page_sizes_prop, page_sizes_prop_size)));
    }

    /* Build interrupt servers properties */
    if (pc->big_core) {
        servers_prop = g_new(uint32_t, smt_threads * 2);
        for (i = 0; i < smt_threads; i++) {
            pnv_cc->get_pir_tir(chip, pc->hwid, i, &pir, NULL);
            servers_prop[i * 2] = cpu_to_be32(pir);

            pnv_cc->get_pir_tir(chip, pc->hwid + 1, i, &pir, NULL);
            servers_prop[i * 2 + 1] = cpu_to_be32(pir);
        }
        _FDT((fdt_setprop(fdt, offset, "ibm,ppc-interrupt-server#s",
                          servers_prop, sizeof(*servers_prop) * smt_threads
                                        * 2)));
    } else {
        servers_prop = g_new(uint32_t, smt_threads);
        for (i = 0; i < smt_threads; i++) {
            pnv_cc->get_pir_tir(chip, pc->hwid, i, &pir, NULL);
            servers_prop[i] = cpu_to_be32(pir);
        }
        _FDT((fdt_setprop(fdt, offset, "ibm,ppc-interrupt-server#s",
                          servers_prop, sizeof(*servers_prop) * smt_threads)));
    }
    g_free(servers_prop);

    return offset;
}

static void pnv_dt_icp(PnvChip *chip, void *fdt, uint32_t hwid,
                       uint32_t nr_threads)
{
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);
    uint32_t pir;
    uint64_t addr;
    char *name;
    const char compat[] = "IBM,power8-icp\0IBM,ppc-xicp";
    uint32_t irange[2], i, rsize;
    uint64_t *reg;
    int offset;

    pcc->get_pir_tir(chip, hwid, 0, &pir, NULL);
    addr = PNV_ICP_BASE(chip) | (pir << 12);

    irange[0] = cpu_to_be32(pir);
    irange[1] = cpu_to_be32(nr_threads);

    rsize = sizeof(uint64_t) * 2 * nr_threads;
    reg = g_malloc(rsize);
    for (i = 0; i < nr_threads; i++) {
        /* We know P8 PIR is linear with thread id */
        reg[i * 2] = cpu_to_be64(addr | ((pir + i) * 0x1000));
        reg[i * 2 + 1] = cpu_to_be64(0x1000);
    }

    name = g_strdup_printf("interrupt-controller@%"PRIX64, addr);
    offset = fdt_add_subnode(fdt, 0, name);
    _FDT(offset);
    g_free(name);

    _FDT((fdt_setprop(fdt, offset, "compatible", compat, sizeof(compat))));
    _FDT((fdt_setprop(fdt, offset, "reg", reg, rsize)));
    _FDT((fdt_setprop_string(fdt, offset, "device_type",
                              "PowerPC-External-Interrupt-Presentation")));
    _FDT((fdt_setprop(fdt, offset, "interrupt-controller", NULL, 0)));
    _FDT((fdt_setprop(fdt, offset, "ibm,interrupt-server-ranges",
                       irange, sizeof(irange))));
    _FDT((fdt_setprop_cell(fdt, offset, "#interrupt-cells", 1)));
    _FDT((fdt_setprop_cell(fdt, offset, "#address-cells", 0)));
    g_free(reg);
}

/*
 * Adds a PnvPHB to the chip on P8.
 * Implemented here, like for defaults PHBs
 */
PnvChip *pnv_chip_add_phb(PnvChip *chip, PnvPHB *phb)
{
    Pnv8Chip *chip8 = PNV8_CHIP(chip);

    phb->chip = chip;

    chip8->phbs[chip8->num_phbs] = phb;
    chip8->num_phbs++;
    return chip;
}

/*
 * Same as spapr pa_features_207 except pnv always enables CI largepages bit.
 * HTM is always enabled because TCG does implement HTM, it's just a
 * degenerate implementation.
 */
static const uint8_t pa_features_207[] = { 24, 0,
                 0xf6, 0x3f, 0xc7, 0xc0, 0x00, 0xf0,
                 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x00, 0x00, 0x00, 0x00, 0x80, 0x00,
                 0x80, 0x00, 0x80, 0x00, 0x80, 0x00 };

static void pnv_chip_power8_dt_populate(PnvChip *chip, void *fdt)
{
    static const char compat[] = "ibm,power8-xscom\0ibm,xscom";
    int i;

    pnv_dt_xscom(chip, fdt, 0,
                 cpu_to_be64(PNV_XSCOM_BASE(chip)),
                 cpu_to_be64(PNV_XSCOM_SIZE),
                 compat, sizeof(compat));

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pnv_core = chip->cores[i];
        int offset;

        offset = pnv_dt_core(chip, pnv_core, fdt);

        _FDT((fdt_setprop(fdt, offset, "ibm,pa-features",
                           pa_features_207, sizeof(pa_features_207))));

        /* Interrupt Control Presenters (ICP). One per core. */
        pnv_dt_icp(chip, fdt, pnv_core->hwid, CPU_CORE(pnv_core)->nr_threads);
    }

    if (chip->ram_size) {
        pnv_dt_memory(fdt, chip->chip_id, chip->ram_start, chip->ram_size);
    }
}

/*
 * Same as spapr pa_features_300 except pnv always enables CI largepages bit.
 */
static const uint8_t pa_features_300[] = { 66, 0,
    /* 0: MMU|FPU|SLB|RUN|DABR|NX, 1: CILRG|fri[nzpm]|DABRX|SPRG3|SLB0|PP110 */
    /* 2: VPM|DS205|PPR|DS202|DS206, 3: LSD|URG, 5: LE|CFAR|EB|LSQ */
    0xf6, 0x3f, 0xc7, 0xc0, 0x00, 0xf0, /* 0 - 5 */
    /* 6: DS207 */
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, /* 6 - 11 */
    /* 16: Vector */
    0x00, 0x00, 0x00, 0x00, 0x80, 0x00, /* 12 - 17 */
    /* 18: Vec. Scalar, 20: Vec. XOR, 22: HTM */
    0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 18 - 23 */
    /* 24: Ext. Dec, 26: 64 bit ftrs, 28: PM ftrs */
    0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 24 - 29 */
    /* 32: LE atomic, 34: EBB + ext EBB */
    0x00, 0x00, 0x80, 0x00, 0xC0, 0x00, /* 30 - 35 */
    /* 40: Radix MMU */
    0x00, 0x00, 0x00, 0x00, 0x80, 0x00, /* 36 - 41 */
    /* 42: PM, 44: PC RA, 46: SC vec'd */
    0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 42 - 47 */
    /* 48: SIMD, 50: QP BFP, 52: String */
    0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 48 - 53 */
    /* 54: DecFP, 56: DecI, 58: SHA */
    0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 54 - 59 */
    /* 60: NM atomic, 62: RNG */
    0x80, 0x00, 0x80, 0x00, 0x00, 0x00, /* 60 - 65 */
};

static void pnv_chip_power9_dt_populate(PnvChip *chip, void *fdt)
{
    static const char compat[] = "ibm,power9-xscom\0ibm,xscom";
    int i;

    pnv_dt_xscom(chip, fdt, 0,
                 cpu_to_be64(PNV9_XSCOM_BASE(chip)),
                 cpu_to_be64(PNV9_XSCOM_SIZE),
                 compat, sizeof(compat));

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pnv_core = chip->cores[i];
        int offset;

        offset = pnv_dt_core(chip, pnv_core, fdt);

        _FDT((fdt_setprop(fdt, offset, "ibm,pa-features",
                           pa_features_300, sizeof(pa_features_300))));

        if (pnv_core->big_core) {
            i++; /* Big-core groups two QEMU cores */
        }
    }

    if (chip->ram_size) {
        pnv_dt_memory(fdt, chip->chip_id, chip->ram_start, chip->ram_size);
    }

    pnv_dt_lpc(chip, fdt, 0, PNV9_LPCM_BASE(chip), PNV9_LPCM_SIZE);
}

/*
 * Same as spapr pa_features_31 except pnv always enables CI largepages bit,
 * always disables copy/paste.
 */
static const uint8_t pa_features_31[] = { 74, 0,
    /* 0: MMU|FPU|SLB|RUN|DABR|NX, 1: CILRG|fri[nzpm]|DABRX|SPRG3|SLB0|PP110 */
    /* 2: VPM|DS205|PPR|DS202|DS206, 3: LSD|URG, 5: LE|CFAR|EB|LSQ */
    0xf6, 0x3f, 0xc7, 0xc0, 0x00, 0xf0, /* 0 - 5 */
    /* 6: DS207 */
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, /* 6 - 11 */
    /* 16: Vector */
    0x00, 0x00, 0x00, 0x00, 0x80, 0x00, /* 12 - 17 */
    /* 18: Vec. Scalar, 20: Vec. XOR */
    0x80, 0x00, 0x80, 0x00, 0x00, 0x00, /* 18 - 23 */
    /* 24: Ext. Dec, 26: 64 bit ftrs, 28: PM ftrs */
    0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 24 - 29 */
    /* 32: LE atomic, 34: EBB + ext EBB */
    0x00, 0x00, 0x80, 0x00, 0xC0, 0x00, /* 30 - 35 */
    /* 40: Radix MMU */
    0x00, 0x00, 0x00, 0x00, 0x80, 0x00, /* 36 - 41 */
    /* 42: PM, 44: PC RA, 46: SC vec'd */
    0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 42 - 47 */
    /* 48: SIMD, 50: QP BFP, 52: String */
    0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 48 - 53 */
    /* 54: DecFP, 56: DecI, 58: SHA */
    0x80, 0x00, 0x80, 0x00, 0x80, 0x00, /* 54 - 59 */
    /* 60: NM atomic, 62: RNG */
    0x80, 0x00, 0x80, 0x00, 0x00, 0x00, /* 60 - 65 */
    /* 68: DEXCR[SBHE|IBRTPDUS|SRAPD|NPHIE|PHIE] */
    0x00, 0x00, 0xce, 0x00, 0x00, 0x00, /* 66 - 71 */
    /* 72: [P]HASHST/[P]HASHCHK */
    0x80, 0x00,                         /* 72 - 73 */
};

static void pnv_chip_power10_dt_populate(PnvChip *chip, void *fdt)
{
    static const char compat[] = "ibm,power10-xscom\0ibm,xscom";
    int i;

    pnv_dt_xscom(chip, fdt, 0,
                 cpu_to_be64(PNV10_XSCOM_BASE(chip)),
                 cpu_to_be64(PNV10_XSCOM_SIZE),
                 compat, sizeof(compat));

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pnv_core = chip->cores[i];
        int offset;

        offset = pnv_dt_core(chip, pnv_core, fdt);

        _FDT((fdt_setprop(fdt, offset, "ibm,pa-features",
                           pa_features_31, sizeof(pa_features_31))));

        if (pnv_core->big_core) {
            i++; /* Big-core groups two QEMU cores */
        }
    }

    if (chip->ram_size) {
        pnv_dt_memory(fdt, chip->chip_id, chip->ram_start, chip->ram_size);
    }

    pnv_dt_lpc(chip, fdt, 0, PNV10_LPCM_BASE(chip), PNV10_LPCM_SIZE);
}

static void pnv_chip_power11_dt_populate(PnvChip *chip, void *fdt)
{
    static const char compat[] = "ibm,power11-xscom\0ibm,xscom";
    int i;

    pnv_dt_xscom(chip, fdt, 0,
                 cpu_to_be64(PNV11_XSCOM_BASE(chip)),
                 cpu_to_be64(PNV11_XSCOM_SIZE),
                 compat, sizeof(compat));

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pnv_core = chip->cores[i];
        int offset;

        offset = pnv_dt_core(chip, pnv_core, fdt);

        _FDT((fdt_setprop(fdt, offset, "ibm,pa-features",
                           pa_features_31, sizeof(pa_features_31))));

        if (pnv_core->big_core) {
            i++; /* Big-core groups two QEMU cores */
        }
    }

    if (chip->ram_size) {
        pnv_dt_memory(fdt, chip->chip_id, chip->ram_start, chip->ram_size);
    }

    pnv_dt_lpc(chip, fdt, 0, PNV11_LPCM_BASE(chip), PNV11_LPCM_SIZE);
}

static void pnv_dt_rtc(ISADevice *d, void *fdt, int lpc_off)
{
    uint32_t io_base = d->ioport_id;
    uint32_t io_regs[] = {
        cpu_to_be32(1),
        cpu_to_be32(io_base),
        cpu_to_be32(2)
    };
    char *name;
    int node;

    name = g_strdup_printf("%s@i%x", qdev_fw_name(DEVICE(d)), io_base);
    node = fdt_add_subnode(fdt, lpc_off, name);
    _FDT(node);
    g_free(name);

    _FDT((fdt_setprop(fdt, node, "reg", io_regs, sizeof(io_regs))));
    _FDT((fdt_setprop_string(fdt, node, "compatible", "pnpPNP,b00")));
}

static void pnv_dt_serial(ISADevice *d, void *fdt, int lpc_off)
{
    const char compatible[] = "ns16550\0pnpPNP,501";
    uint32_t io_base = d->ioport_id;
    uint32_t io_regs[] = {
        cpu_to_be32(1),
        cpu_to_be32(io_base),
        cpu_to_be32(8)
    };
    uint32_t irq;
    char *name;
    int node;

    irq = object_property_get_uint(OBJECT(d), "irq", &error_fatal);

    name = g_strdup_printf("%s@i%x", qdev_fw_name(DEVICE(d)), io_base);
    node = fdt_add_subnode(fdt, lpc_off, name);
    _FDT(node);
    g_free(name);

    _FDT((fdt_setprop(fdt, node, "reg", io_regs, sizeof(io_regs))));
    _FDT((fdt_setprop(fdt, node, "compatible", compatible,
                      sizeof(compatible))));

    _FDT((fdt_setprop_cell(fdt, node, "clock-frequency", 1843200)));
    _FDT((fdt_setprop_cell(fdt, node, "current-speed", 115200)));
    _FDT((fdt_setprop_cell(fdt, node, "interrupts", irq)));
    _FDT((fdt_setprop_cell(fdt, node, "interrupt-parent",
                           fdt_get_phandle(fdt, lpc_off))));

    /* This is needed by Linux */
    _FDT((fdt_setprop_string(fdt, node, "device_type", "serial")));
}

static void pnv_dt_ipmi_bt(ISADevice *d, void *fdt, int lpc_off)
{
    const char compatible[] = "bt\0ipmi-bt";
    uint32_t io_base;
    uint32_t io_regs[] = {
        cpu_to_be32(1),
        0, /* 'io_base' retrieved from the 'ioport' property of 'isa-ipmi-bt' */
        cpu_to_be32(3)
    };
    uint32_t irq;
    char *name;
    int node;

    io_base = object_property_get_int(OBJECT(d), "ioport", &error_fatal);
    io_regs[1] = cpu_to_be32(io_base);

    irq = object_property_get_int(OBJECT(d), "irq", &error_fatal);

    name = g_strdup_printf("%s@i%x", qdev_fw_name(DEVICE(d)), io_base);
    node = fdt_add_subnode(fdt, lpc_off, name);
    _FDT(node);
    g_free(name);

    _FDT((fdt_setprop(fdt, node, "reg", io_regs, sizeof(io_regs))));
    _FDT((fdt_setprop(fdt, node, "compatible", compatible,
                      sizeof(compatible))));

    /* Mark it as reserved to avoid Linux trying to claim it */
    _FDT((fdt_setprop_string(fdt, node, "status", "reserved")));
    _FDT((fdt_setprop_cell(fdt, node, "interrupts", irq)));
    _FDT((fdt_setprop_cell(fdt, node, "interrupt-parent",
                           fdt_get_phandle(fdt, lpc_off))));
}

typedef struct ForeachPopulateArgs {
    void *fdt;
    int offset;
} ForeachPopulateArgs;

static int pnv_dt_isa_device(DeviceState *dev, void *opaque)
{
    ForeachPopulateArgs *args = opaque;
    ISADevice *d = ISA_DEVICE(dev);

    if (object_dynamic_cast(OBJECT(dev), TYPE_MC146818_RTC)) {
        pnv_dt_rtc(d, args->fdt, args->offset);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_ISA_SERIAL)) {
        pnv_dt_serial(d, args->fdt, args->offset);
    } else if (object_dynamic_cast(OBJECT(dev), "isa-ipmi-bt")) {
        pnv_dt_ipmi_bt(d, args->fdt, args->offset);
    } else {
        error_report("unknown isa device %s@i%x", qdev_fw_name(dev),
                     d->ioport_id);
    }

    return 0;
}

/*
 * The default LPC bus of a multichip system is on chip 0. It's
 * recognized by the firmware (skiboot) using a "primary" property.
 */
static void pnv_dt_isa(PnvMachineState *pnv, void *fdt)
{
    int isa_offset = fdt_path_offset(fdt, pnv->chips[0]->dt_isa_nodename);
    ForeachPopulateArgs args = {
        .fdt = fdt,
        .offset = isa_offset,
    };
    uint32_t phandle;

    _FDT((fdt_setprop(fdt, isa_offset, "primary", NULL, 0)));

    phandle = qemu_fdt_alloc_phandle(fdt);
    assert(phandle > 0);
    _FDT((fdt_setprop_cell(fdt, isa_offset, "phandle", phandle)));

    /*
     * ISA devices are not necessarily parented to the ISA bus so we
     * can not use object_child_foreach()
     */
    qbus_walk_children(BUS(pnv->isa_bus), pnv_dt_isa_device, NULL, NULL, NULL,
                       &args);
}

static void pnv_dt_power_mgt(PnvMachineState *pnv, void *fdt)
{
    int off;

    off = fdt_add_subnode(fdt, 0, "ibm,opal");
    off = fdt_add_subnode(fdt, off, "power-mgt");

    _FDT(fdt_setprop_cell(fdt, off, "ibm,enabled-stop-levels", 0xc0000000));
}

static void *pnv_dt_create(MachineState *machine)
{
    PnvMachineClass *pmc = PNV_MACHINE_GET_CLASS(machine);
    PnvMachineState *pnv = PNV_MACHINE(machine);
    void *fdt;
    char *buf;
    int off;
    int i;

    fdt = g_malloc0(FDT_MAX_SIZE);
    _FDT((fdt_create_empty_tree(fdt, FDT_MAX_SIZE)));

    /* /qemu node */
    _FDT((fdt_add_subnode(fdt, 0, "qemu")));

    /* Root node */
    _FDT((fdt_setprop_cell(fdt, 0, "#address-cells", 0x2)));
    _FDT((fdt_setprop_cell(fdt, 0, "#size-cells", 0x2)));
    _FDT((fdt_setprop_string(fdt, 0, "model",
                             "IBM PowerNV (emulated by qemu)")));
    _FDT((fdt_setprop(fdt, 0, "compatible", pmc->compat, pmc->compat_size)));

    buf =  qemu_uuid_unparse_strdup(&qemu_uuid);
    _FDT((fdt_setprop_string(fdt, 0, "vm,uuid", buf)));
    if (qemu_uuid_set) {
        _FDT((fdt_setprop_string(fdt, 0, "system-id", buf)));
    }
    g_free(buf);

    off = fdt_add_subnode(fdt, 0, "chosen");
    if (machine->kernel_cmdline) {
        _FDT((fdt_setprop_string(fdt, off, "bootargs",
                                 machine->kernel_cmdline)));
    }

    if (pnv->initrd_size) {
        uint32_t start_prop = cpu_to_be32(pnv->initrd_base);
        uint32_t end_prop = cpu_to_be32(pnv->initrd_base + pnv->initrd_size);

        _FDT((fdt_setprop(fdt, off, "linux,initrd-start",
                               &start_prop, sizeof(start_prop))));
        _FDT((fdt_setprop(fdt, off, "linux,initrd-end",
                               &end_prop, sizeof(end_prop))));
    }

    /* Populate device tree for each chip */
    for (i = 0; i < pnv->num_chips; i++) {
        PNV_CHIP_GET_CLASS(pnv->chips[i])->dt_populate(pnv->chips[i], fdt);
    }

    /* Populate ISA devices on chip 0 */
    pnv_dt_isa(pnv, fdt);

    if (pnv->bmc) {
        pnv_dt_bmc_sensors(pnv->bmc, fdt);
    }

    /* Create an extra node for power management on machines that support it */
    if (pmc->dt_power_mgt) {
        pmc->dt_power_mgt(pnv, fdt);
    }

    return fdt;
}

static void pnv_powerdown_notify(Notifier *n, void *opaque)
{
    PnvMachineState *pnv = container_of(n, PnvMachineState, powerdown_notifier);

    if (pnv->bmc) {
        pnv_bmc_powerdown(pnv->bmc);
    }
}

static void pnv_reset(MachineState *machine, ResetType type)
{
    PnvMachineState *pnv = PNV_MACHINE(machine);
    IPMIBmc *bmc;
    void *fdt;

    qemu_devices_reset(type);

    /*
     * The machine should provide by default an internal BMC simulator.
     * If not, try to use the BMC device that was provided on the command
     * line.
     */
    bmc = pnv_bmc_find(&error_fatal);
    if (!pnv->bmc) {
        if (!bmc) {
            if (!qtest_enabled()) {
                warn_report("machine has no BMC device. Use '-device "
                            "ipmi-bmc-sim,id=bmc0 -device isa-ipmi-bt,bmc=bmc0,irq=10' "
                            "to define one");
            }
        } else {
            pnv_bmc_set_pnor(bmc, pnv->pnor);
            pnv->bmc = bmc;
        }
    }

    if (machine->fdt) {
        fdt = machine->fdt;
    } else {
        fdt = pnv_dt_create(machine);
        /* Pack resulting tree */
        _FDT((fdt_pack(fdt)));
    }

    cpu_physical_memory_write(PNV_FDT_ADDR, fdt, fdt_totalsize(fdt));

    /* Update machine->fdt with latest fdt */
    if (machine->fdt != fdt) {
        /*
         * Set machine->fdt for 'dumpdtb' QMP/HMP command. Free
         * the existing machine->fdt to avoid leaking it during
         * a reset.
         */
        g_free(machine->fdt);
        machine->fdt = fdt;
    }
}

static ISABus *pnv_chip_power8_isa_create(PnvChip *chip, Error **errp)
{
    Pnv8Chip *chip8 = PNV8_CHIP(chip);
    qemu_irq irq = qdev_get_gpio_in(DEVICE(&chip8->psi), PSIHB_IRQ_EXTERNAL);

    qdev_connect_gpio_out_named(DEVICE(&chip8->lpc), "LPCHC", 0, irq);

    return pnv_lpc_isa_create(&chip8->lpc, true, errp);
}

static ISABus *pnv_chip_power8nvl_isa_create(PnvChip *chip, Error **errp)
{
    Pnv8Chip *chip8 = PNV8_CHIP(chip);
    qemu_irq irq = qdev_get_gpio_in(DEVICE(&chip8->psi), PSIHB_IRQ_LPC_I2C);

    qdev_connect_gpio_out_named(DEVICE(&chip8->lpc), "LPCHC", 0, irq);

    return pnv_lpc_isa_create(&chip8->lpc, false, errp);
}

static ISABus *pnv_chip_power9_isa_create(PnvChip *chip, Error **errp)
{
    Pnv9Chip *chip9 = PNV9_CHIP(chip);
    qemu_irq irq;

    irq = qdev_get_gpio_in(DEVICE(&chip9->psi), PSIHB9_IRQ_LPCHC);
    qdev_connect_gpio_out_named(DEVICE(&chip9->lpc), "LPCHC", 0, irq);

    irq = qdev_get_gpio_in(DEVICE(&chip9->psi), PSIHB9_IRQ_LPC_SIRQ0);
    qdev_connect_gpio_out_named(DEVICE(&chip9->lpc), "SERIRQ", 0, irq);
    irq = qdev_get_gpio_in(DEVICE(&chip9->psi), PSIHB9_IRQ_LPC_SIRQ1);
    qdev_connect_gpio_out_named(DEVICE(&chip9->lpc), "SERIRQ", 1, irq);
    irq = qdev_get_gpio_in(DEVICE(&chip9->psi), PSIHB9_IRQ_LPC_SIRQ2);
    qdev_connect_gpio_out_named(DEVICE(&chip9->lpc), "SERIRQ", 2, irq);
    irq = qdev_get_gpio_in(DEVICE(&chip9->psi), PSIHB9_IRQ_LPC_SIRQ3);
    qdev_connect_gpio_out_named(DEVICE(&chip9->lpc), "SERIRQ", 3, irq);

    return pnv_lpc_isa_create(&chip9->lpc, false, errp);
}

static ISABus *pnv_chip_power10_isa_create(PnvChip *chip, Error **errp)
{
    Pnv10Chip *chip10 = PNV10_CHIP(chip);
    qemu_irq irq;

    irq = qdev_get_gpio_in(DEVICE(&chip10->psi), PSIHB9_IRQ_LPCHC);
    qdev_connect_gpio_out_named(DEVICE(&chip10->lpc), "LPCHC", 0, irq);

    irq = qdev_get_gpio_in(DEVICE(&chip10->psi), PSIHB9_IRQ_LPC_SIRQ0);
    qdev_connect_gpio_out_named(DEVICE(&chip10->lpc), "SERIRQ", 0, irq);
    irq = qdev_get_gpio_in(DEVICE(&chip10->psi), PSIHB9_IRQ_LPC_SIRQ1);
    qdev_connect_gpio_out_named(DEVICE(&chip10->lpc), "SERIRQ", 1, irq);
    irq = qdev_get_gpio_in(DEVICE(&chip10->psi), PSIHB9_IRQ_LPC_SIRQ2);
    qdev_connect_gpio_out_named(DEVICE(&chip10->lpc), "SERIRQ", 2, irq);
    irq = qdev_get_gpio_in(DEVICE(&chip10->psi), PSIHB9_IRQ_LPC_SIRQ3);
    qdev_connect_gpio_out_named(DEVICE(&chip10->lpc), "SERIRQ", 3, irq);

    return pnv_lpc_isa_create(&chip10->lpc, false, errp);
}

static ISABus *pnv_chip_power11_isa_create(PnvChip *chip, Error **errp)
{
    Pnv11Chip *chip11 = PNV11_CHIP(chip);
    qemu_irq irq;

    irq = qdev_get_gpio_in(DEVICE(&chip11->psi), PSIHB9_IRQ_LPCHC);
    qdev_connect_gpio_out_named(DEVICE(&chip11->lpc), "LPCHC", 0, irq);

    irq = qdev_get_gpio_in(DEVICE(&chip11->psi), PSIHB9_IRQ_LPC_SIRQ0);
    qdev_connect_gpio_out_named(DEVICE(&chip11->lpc), "SERIRQ", 0, irq);
    irq = qdev_get_gpio_in(DEVICE(&chip11->psi), PSIHB9_IRQ_LPC_SIRQ1);
    qdev_connect_gpio_out_named(DEVICE(&chip11->lpc), "SERIRQ", 1, irq);
    irq = qdev_get_gpio_in(DEVICE(&chip11->psi), PSIHB9_IRQ_LPC_SIRQ2);
    qdev_connect_gpio_out_named(DEVICE(&chip11->lpc), "SERIRQ", 2, irq);
    irq = qdev_get_gpio_in(DEVICE(&chip11->psi), PSIHB9_IRQ_LPC_SIRQ3);
    qdev_connect_gpio_out_named(DEVICE(&chip11->lpc), "SERIRQ", 3, irq);

    return pnv_lpc_isa_create(&chip11->lpc, false, errp);
}

static ISABus *pnv_isa_create(PnvChip *chip, Error **errp)
{
    return PNV_CHIP_GET_CLASS(chip)->isa_create(chip, errp);
}

static void pnv_chip_power8_pic_print_info(PnvChip *chip, GString *buf)
{
    Pnv8Chip *chip8 = PNV8_CHIP(chip);
    int i;

    ics_pic_print_info(&chip8->psi.ics, buf);

    for (i = 0; i < chip8->num_phbs; i++) {
        PnvPHB *phb = chip8->phbs[i];
        PnvPHB3 *phb3 = PNV_PHB3(phb->backend);

        pnv_phb3_msi_pic_print_info(&phb3->msis, buf);
        ics_pic_print_info(&phb3->lsis, buf);
    }
}

static int pnv_chip_power9_pic_print_info_child(Object *child, void *opaque)
{
    GString *buf = opaque;
    PnvPHB *phb =  (PnvPHB *) object_dynamic_cast(child, TYPE_PNV_PHB);

    if (!phb) {
        return 0;
    }

    pnv_phb4_pic_print_info(PNV_PHB4(phb->backend), buf);

    return 0;
}

static void pnv_chip_power9_pic_print_info(PnvChip *chip, GString *buf)
{
    Pnv9Chip *chip9 = PNV9_CHIP(chip);

    pnv_xive_pic_print_info(&chip9->xive, buf);
    pnv_psi_pic_print_info(&chip9->psi, buf);
    object_child_foreach_recursive(OBJECT(chip),
                         pnv_chip_power9_pic_print_info_child, buf);
}

static uint64_t pnv_chip_power8_xscom_core_base(PnvChip *chip,
                                                uint32_t core_id)
{
    return PNV_XSCOM_EX_BASE(core_id);
}

static uint64_t pnv_chip_power9_xscom_core_base(PnvChip *chip,
                                                uint32_t core_id)
{
    return PNV9_XSCOM_EC_BASE(core_id);
}

static uint64_t pnv_chip_power10_xscom_core_base(PnvChip *chip,
                                                 uint32_t core_id)
{
    return PNV10_XSCOM_EC_BASE(core_id);
}

static uint64_t pnv_chip_power11_xscom_core_base(PnvChip *chip,
                                                 uint32_t core_id)
{
    return PNV11_XSCOM_EC_BASE(core_id);
}

static bool pnv_match_cpu(const char *default_type, const char *cpu_type)
{
    PowerPCCPUClass *ppc_default =
        POWERPC_CPU_CLASS(object_class_by_name(default_type));
    PowerPCCPUClass *ppc =
        POWERPC_CPU_CLASS(object_class_by_name(cpu_type));

    return ppc_default->pvr_match(ppc_default, ppc->pvr, false);
}

static void pnv_ipmi_bt_init(ISABus *bus, IPMIBmc *bmc, uint32_t irq)
{
    ISADevice *dev = isa_new("isa-ipmi-bt");

    object_property_set_link(OBJECT(dev), "bmc", OBJECT(bmc), &error_fatal);
    object_property_set_int(OBJECT(dev), "irq", irq, &error_fatal);
    isa_realize_and_unref(dev, bus, &error_fatal);
}

static void pnv_chip_power10_pic_print_info(PnvChip *chip, GString *buf)
{
    Pnv10Chip *chip10 = PNV10_CHIP(chip);

    pnv_xive2_pic_print_info(&chip10->xive, buf);
    pnv_psi_pic_print_info(&chip10->psi, buf);
    object_child_foreach_recursive(OBJECT(chip),
                         pnv_chip_power9_pic_print_info_child, buf);
}

static void pnv_chip_power11_pic_print_info(PnvChip *chip, GString *buf)
{
    Pnv11Chip *chip11 = PNV11_CHIP(chip);

    pnv_xive2_pic_print_info(&chip11->xive, buf);
    pnv_psi_pic_print_info(&chip11->psi, buf);
    object_child_foreach_recursive(OBJECT(chip),
                         pnv_chip_power9_pic_print_info_child, buf);
}

/* Always give the first 1GB to chip 0 else we won't boot */
static uint64_t pnv_chip_get_ram_size(PnvMachineState *pnv, int chip_id)
{
    MachineState *machine = MACHINE(pnv);
    uint64_t ram_per_chip;

    assert(machine->ram_size >= 1 * GiB);

    ram_per_chip = machine->ram_size / pnv->num_chips;
    if (ram_per_chip >= 1 * GiB) {
        return QEMU_ALIGN_DOWN(ram_per_chip, 1 * MiB);
    }

    assert(pnv->num_chips > 1);

    ram_per_chip = (machine->ram_size - 1 * GiB) / (pnv->num_chips - 1);
    return chip_id == 0 ? 1 * GiB : QEMU_ALIGN_DOWN(ram_per_chip, 1 * MiB);
}

static void pnv_init(MachineState *machine)
{
    const char *bios_name = machine->firmware ?: FW_FILE_NAME;
    PnvMachineState *pnv = PNV_MACHINE(machine);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    PnvMachineClass *pmc = PNV_MACHINE_GET_CLASS(machine);
    int max_smt_threads = pmc->max_smt_threads;
    char *fw_filename;
    uint64_t chip_ram_start = 0;
    int i;
    char *chip_typename;
    DriveInfo *pnor;
    DeviceState *dev;

    if (kvm_enabled()) {
        error_report("machine %s does not support the KVM accelerator",
                     mc->name);
        exit(EXIT_FAILURE);
    }

    /* allocate RAM */
    if (machine->ram_size < mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be bigger than %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    /* checks for invalid option combinations */
    if (machine->dtb && (strlen(machine->kernel_cmdline) != 0)) {
        error_report("-append and -dtb cannot be used together, as passed"
                " command line is ignored in case of custom dtb");
        exit(EXIT_FAILURE);
    }

    memory_region_add_subregion(get_system_memory(), 0, machine->ram);

    /*
     * Create our simple PNOR device
     */
    dev = qdev_new(TYPE_PNV_PNOR);
    pnor = drive_get(IF_MTD, 0, 0);
    if (!pnor && defaults_enabled()) {
        fw_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, PNOR_FILE_NAME);
        if (!fw_filename) {
            warn_report("Could not find PNOR '%s'", PNOR_FILE_NAME);
        } else {
            QemuOpts *opts;
            opts = drive_add(IF_MTD, -1, fw_filename, "format=raw,readonly=on");
            pnor = drive_new(opts, IF_MTD, &error_fatal);
            g_free(fw_filename);
        }
    }
    if (pnor) {
        qdev_prop_set_drive(dev, "drive", blk_by_legacy_dinfo(pnor));
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    pnv->pnor = PNV_PNOR(dev);

    /* load skiboot firmware  */
    fw_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (!fw_filename) {
        error_report("Could not find OPAL firmware '%s'", bios_name);
        exit(1);
    }

    load_image_targphys(fw_filename, pnv->fw_load_addr, FW_MAX_SIZE,
                        &error_fatal);
    g_free(fw_filename);

    /* load kernel */
    if (machine->kernel_filename) {
        load_image_targphys(machine->kernel_filename,
                            KERNEL_LOAD_ADDR, KERNEL_MAX_SIZE, &error_fatal);
    }

    /* load initrd */
    if (machine->initrd_filename) {
        pnv->initrd_base = INITRD_LOAD_ADDR;
        pnv->initrd_size = load_image_targphys(machine->initrd_filename,
                                               pnv->initrd_base,
                                               INITRD_MAX_SIZE, &error_fatal);
    }

    /* load dtb if passed */
    if (machine->dtb) {
        int fdt_size;

        warn_report("with manually passed dtb, some options like '-append'"
                " will get ignored and the dtb passed will be used as-is");

        /* read the file 'machine->dtb', and load it into 'fdt' buffer */
        machine->fdt = load_device_tree(machine->dtb, &fdt_size);
        if (!machine->fdt) {
            error_report("Could not load dtb '%s'", machine->dtb);
            exit(1);
        }
    }

    /* MSIs are supported on this platform */
    msi_nonbroken = true;

    /*
     * Check compatibility of the specified CPU with the machine
     * default.
     */
    if (!pnv_match_cpu(mc->default_cpu_type, machine->cpu_type)) {
        error_report("invalid CPU model '%s' for %s machine",
                     machine->cpu_type, mc->name);
        exit(1);
    }

    /* Create the processor chips */
    i = strlen(machine->cpu_type) - strlen(POWERPC_CPU_TYPE_SUFFIX);
    chip_typename = g_strdup_printf(PNV_CHIP_TYPE_NAME("%.*s"),
                                    i, machine->cpu_type);
    if (!object_class_by_name(chip_typename)) {
        error_report("invalid chip model '%.*s' for %s machine",
                     i, machine->cpu_type, mc->name);
        exit(1);
    }

    /* Set lpar-per-core mode if lpar-per-thread is not supported */
    if (!pmc->has_lpar_per_thread) {
        pnv->lpar_per_core = true;
    }

    pnv->num_chips =
        machine->smp.max_cpus / (machine->smp.cores * machine->smp.threads);

    if (pnv->big_core) {
        if (machine->smp.threads % 2 == 1) {
            error_report("Cannot support %d threads with big-core option "
                         "because it must be an even number",
                         machine->smp.threads);
            exit(1);
        }
        max_smt_threads *= 2;
    }

    if (machine->smp.threads > max_smt_threads) {
        error_report("Cannot support more than %d threads/core "
                     "on %s machine", max_smt_threads, mc->desc);
        if (pmc->max_smt_threads == 4) {
            error_report("(use big-core=on for 8 threads per core)");
        }
        exit(1);
    }

    if (pnv->big_core) {
        /*
         * powernv models PnvCore as a SMT4 core. Big-core requires 2xPnvCore
         * per core, so adjust topology here. pnv_dt_core() processor
         * device-tree and TCG SMT code make the 2 cores appear as one big core
         * from software point of view. pnv pervasive models and xscoms tend to
         * see the big core as 2 small core halves.
         */
        machine->smp.cores *= 2;
        machine->smp.threads /= 2;
    }

    if (!is_power_of_2(machine->smp.threads)) {
        error_report("Cannot support %d threads/core on a powernv "
                     "machine because it must be a power of 2",
                     machine->smp.threads);
        exit(1);
    }

    /*
     * TODO: should we decide on how many chips we can create based
     * on #cores and Venice vs. Murano vs. Naples chip type etc...,
     */
    if (!is_power_of_2(pnv->num_chips) || pnv->num_chips > 16) {
        error_report("invalid number of chips: '%d'", pnv->num_chips);
        error_printf(
            "Try '-smp sockets=N'. Valid values are : 1, 2, 4, 8 and 16.\n");
        exit(1);
    }

    pnv->chips = g_new0(PnvChip *, pnv->num_chips);
    for (i = 0; i < pnv->num_chips; i++) {
        char chip_name[32];
        Object *chip = OBJECT(qdev_new(chip_typename));
        uint64_t chip_ram_size =  pnv_chip_get_ram_size(pnv, i);

        pnv->chips[i] = PNV_CHIP(chip);

        /* Distribute RAM among the chips  */
        object_property_set_int(chip, "ram-start", chip_ram_start,
                                &error_fatal);
        object_property_set_int(chip, "ram-size", chip_ram_size,
                                &error_fatal);
        chip_ram_start += chip_ram_size;

        snprintf(chip_name, sizeof(chip_name), "chip[%d]", i);
        object_property_add_child(OBJECT(pnv), chip_name, chip);
        object_property_set_int(chip, "chip-id", i, &error_fatal);
        object_property_set_int(chip, "nr-cores", machine->smp.cores,
                                &error_fatal);
        object_property_set_int(chip, "nr-threads", machine->smp.threads,
                                &error_fatal);
        object_property_set_bool(chip, "big-core", pnv->big_core,
                                &error_fatal);
        object_property_set_bool(chip, "lpar-per-core", pnv->lpar_per_core,
                                &error_fatal);
        /*
         * The POWER8 machine use the XICS interrupt interface.
         * Propagate the XICS fabric to the chip and its controllers.
         */
        if (object_dynamic_cast(OBJECT(pnv), TYPE_XICS_FABRIC)) {
            object_property_set_link(chip, "xics", OBJECT(pnv), &error_abort);
        }
        if (object_dynamic_cast(OBJECT(pnv), TYPE_XIVE_FABRIC)) {
            object_property_set_link(chip, "xive-fabric", OBJECT(pnv),
                                     &error_abort);
        }
        sysbus_realize_and_unref(SYS_BUS_DEVICE(chip), &error_fatal);
    }
    g_free(chip_typename);

    /* Instantiate ISA bus on chip 0 */
    pnv->isa_bus = pnv_isa_create(pnv->chips[0], &error_fatal);

    /* Create serial port */
    serial_hds_isa_init(pnv->isa_bus, 0, MAX_ISA_SERIAL_PORTS);

    /* Create an RTC ISA device too */
    mc146818_rtc_init(pnv->isa_bus, 2000, NULL);

    /*
     * Create the machine BMC simulator and the IPMI BT device for
     * communication with the BMC
     */
    if (defaults_enabled()) {
        pnv->bmc = pnv_bmc_create(pnv->pnor);
        pnv_ipmi_bt_init(pnv->isa_bus, pnv->bmc, 10);
    }

    /*
     * The PNOR is mapped on the LPC FW address space by the BMC.
     * Since we can not reach the remote BMC machine with LPC memops,
     * map it always for now.
     */
    memory_region_add_subregion(pnv->chips[0]->fw_mr, pnv->pnor->lpc_address,
                                &pnv->pnor->mmio);

    /*
     * OpenPOWER systems use a IPMI SEL Event message to notify the
     * host to powerdown
     */
    pnv->powerdown_notifier.notify = pnv_powerdown_notify;
    qemu_register_powerdown_notifier(&pnv->powerdown_notifier);

    /*
     * Create/Connect any machine-specific I2C devices
     */
    if (pmc->i2c_init) {
        pmc->i2c_init(pnv);
    }
}

/*
 *    0:21  Reserved - Read as zeros
 *   22:24  Chip ID
 *   25:28  Core number
 *   29:31  Thread ID
 */
static void pnv_get_pir_tir_p8(PnvChip *chip,
                                uint32_t core_id, uint32_t thread_id,
                                uint32_t *pir, uint32_t *tir)
{
    if (pir) {
        *pir = (chip->chip_id << 7) | (core_id << 3) | thread_id;
    }
    if (tir) {
        *tir = thread_id;
    }
}

static void pnv_chip_power8_intc_create(PnvChip *chip, PowerPCCPU *cpu,
                                        Error **errp)
{
    Pnv8Chip *chip8 = PNV8_CHIP(chip);
    Error *local_err = NULL;
    Object *obj;
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    obj = icp_create(OBJECT(cpu), TYPE_PNV_ICP, chip8->xics, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    pnv_cpu->intc = obj;
}


static void pnv_chip_power8_intc_reset(PnvChip *chip, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    icp_reset(ICP(pnv_cpu->intc));
}

static void pnv_chip_power8_intc_destroy(PnvChip *chip, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    icp_destroy(ICP(pnv_cpu->intc));
    pnv_cpu->intc = NULL;
}

static void pnv_chip_power8_intc_print_info(PnvChip *chip, PowerPCCPU *cpu,
                                            GString *buf)
{
    icp_pic_print_info(ICP(pnv_cpu_state(cpu)->intc), buf);
}

/*
 *    0:48  Reserved - Read as zeroes
 *   49:52  Node ID
 *   53:55  Chip ID
 *   56     Reserved - Read as zero
 *   57:61  Core number
 *   62:63  Thread ID
 *
 * We only care about the lower bits. uint32_t is fine for the moment.
 */
static void pnv_get_pir_tir_p9(PnvChip *chip,
                                uint32_t core_id, uint32_t thread_id,
                                uint32_t *pir, uint32_t *tir)
{
    if (chip->big_core) {
        /* Big-core interleaves thread ID between small-cores */
        thread_id <<= 1;
        thread_id |= core_id & 1;
        core_id >>= 1;

        if (pir) {
            *pir = (chip->chip_id << 8) | (core_id << 3) | thread_id;
        }
    } else {
        if (pir) {
            *pir = (chip->chip_id << 8) | (core_id << 2) | thread_id;
        }
    }
    if (tir) {
        *tir = thread_id;
    }
}

/*
 *    0:48  Reserved - Read as zeroes
 *   49:52  Node ID
 *   53:55  Chip ID
 *   56     Reserved - Read as zero
 *   57:59  Quad ID
 *   60     Core Chiplet Pair ID
 *   61:63  Thread/Core Chiplet ID t0-t2
 *
 * We only care about the lower bits. uint32_t is fine for the moment.
 */
static void pnv_get_pir_tir_p10(PnvChip *chip,
                                uint32_t core_id, uint32_t thread_id,
                                uint32_t *pir, uint32_t *tir)
{
    if (chip->big_core) {
        /* Big-core interleaves thread ID between small-cores */
        thread_id <<= 1;
        thread_id |= core_id & 1;
        core_id >>= 1;

        if (pir) {
            *pir = (chip->chip_id << 8) | (core_id << 3) | thread_id;
        }
    } else {
        if (pir) {
            *pir = (chip->chip_id << 8) | (core_id << 2) | thread_id;
        }
    }
    if (tir) {
        *tir = thread_id;
    }
}

static void pnv_chip_power9_intc_create(PnvChip *chip, PowerPCCPU *cpu,
                                        Error **errp)
{
    Pnv9Chip *chip9 = PNV9_CHIP(chip);
    Error *local_err = NULL;
    Object *obj;
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    /*
     * The core creates its interrupt presenter but the XIVE interrupt
     * controller object is initialized afterwards. Hopefully, it's
     * only used at runtime.
     */
    obj = xive_tctx_create(OBJECT(cpu), XIVE_PRESENTER(&chip9->xive),
                           &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    pnv_cpu->intc = obj;
}

static void pnv_chip_power9_intc_reset(PnvChip *chip, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    xive_tctx_reset(XIVE_TCTX(pnv_cpu->intc));
}

static void pnv_chip_power9_intc_destroy(PnvChip *chip, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    xive_tctx_destroy(XIVE_TCTX(pnv_cpu->intc));
    pnv_cpu->intc = NULL;
}

static void pnv_chip_power9_intc_print_info(PnvChip *chip, PowerPCCPU *cpu,
                                            GString *buf)
{
    xive_tctx_pic_print_info(XIVE_TCTX(pnv_cpu_state(cpu)->intc), buf);
}

static void pnv_chip_power10_intc_create(PnvChip *chip, PowerPCCPU *cpu,
                                        Error **errp)
{
    Pnv10Chip *chip10 = PNV10_CHIP(chip);
    Error *local_err = NULL;
    Object *obj;
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    /*
     * The core creates its interrupt presenter but the XIVE2 interrupt
     * controller object is initialized afterwards. Hopefully, it's
     * only used at runtime.
     */
    obj = xive_tctx_create(OBJECT(cpu), XIVE_PRESENTER(&chip10->xive),
                           &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    pnv_cpu->intc = obj;
}

static void pnv_chip_power10_intc_reset(PnvChip *chip, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    xive_tctx_reset(XIVE_TCTX(pnv_cpu->intc));
}

static void pnv_chip_power10_intc_destroy(PnvChip *chip, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    xive_tctx_destroy(XIVE_TCTX(pnv_cpu->intc));
    pnv_cpu->intc = NULL;
}

static void pnv_chip_power10_intc_print_info(PnvChip *chip, PowerPCCPU *cpu,
                                             GString *buf)
{
    xive_tctx_pic_print_info(XIVE_TCTX(pnv_cpu_state(cpu)->intc), buf);
}

static void *pnv_chip_power10_intc_get(PnvChip *chip)
{
    return &PNV10_CHIP(chip)->xive;
}

static void pnv_chip_power11_intc_create(PnvChip *chip, PowerPCCPU *cpu,
                                        Error **errp)
{
    Pnv11Chip *chip11 = PNV11_CHIP(chip);
    Error *local_err = NULL;
    Object *obj;
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    /*
     * The core creates its interrupt presenter but the XIVE2 interrupt
     * controller object is initialized afterwards. Hopefully, it's
     * only used at runtime.
     */
    obj = xive_tctx_create(OBJECT(cpu), XIVE_PRESENTER(&chip11->xive),
                           &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    pnv_cpu->intc = obj;
}

static void pnv_chip_power11_intc_reset(PnvChip *chip, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    xive_tctx_reset(XIVE_TCTX(pnv_cpu->intc));
}

static void pnv_chip_power11_intc_destroy(PnvChip *chip, PowerPCCPU *cpu)
{
    PnvCPUState *pnv_cpu = pnv_cpu_state(cpu);

    xive_tctx_destroy(XIVE_TCTX(pnv_cpu->intc));
    pnv_cpu->intc = NULL;
}

static void pnv_chip_power11_intc_print_info(PnvChip *chip, PowerPCCPU *cpu,
                                             GString *buf)
{
    xive_tctx_pic_print_info(XIVE_TCTX(pnv_cpu_state(cpu)->intc), buf);
}

static void *pnv_chip_power11_intc_get(PnvChip *chip)
{
    return &PNV11_CHIP(chip)->xive;
}

/*
 * Allowed core identifiers on a POWER8 Processor Chip :
 *
 * <EX0 reserved>
 *  EX1  - Venice only
 *  EX2  - Venice only
 *  EX3  - Venice only
 *  EX4
 *  EX5
 *  EX6
 * <EX7,8 reserved> <reserved>
 *  EX9  - Venice only
 *  EX10 - Venice only
 *  EX11 - Venice only
 *  EX12
 *  EX13
 *  EX14
 * <EX15 reserved>
 */
#define POWER8E_CORE_MASK  (0x7070ull)
#define POWER8_CORE_MASK   (0x7e7eull)

/*
 * POWER9 has 24 cores, ids starting at 0x0
 */
#define POWER9_CORE_MASK   (0xffffffffffffffull)


#define POWER10_CORE_MASK  (0xffffffffffffffull)

#define POWER11_CORE_MASK  (0xffffffffffffffull)

static void pnv_chip_power8_instance_init(Object *obj)
{
    Pnv8Chip *chip8 = PNV8_CHIP(obj);
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(obj);
    int i;

    object_property_add_link(obj, "xics", TYPE_XICS_FABRIC,
                             (Object **)&chip8->xics,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_STRONG);

    object_initialize_child(obj, "psi", &chip8->psi, TYPE_PNV8_PSI);

    object_initialize_child(obj, "lpc", &chip8->lpc, TYPE_PNV8_LPC);

    object_initialize_child(obj, "occ", &chip8->occ, TYPE_PNV8_OCC);

    object_initialize_child(obj, "homer", &chip8->homer, TYPE_PNV8_HOMER);

    if (defaults_enabled()) {
        chip8->num_phbs = pcc->num_phbs;

        for (i = 0; i < chip8->num_phbs; i++) {
            Object *phb = object_new(TYPE_PNV_PHB);

            /*
             * We need the chip to parent the PHB to allow the DT
             * to build correctly (via pnv_xscom_dt()).
             *
             * TODO: the PHB should be parented by a PEC device that, at
             * this moment, is not modelled powernv8/phb3.
             */
            object_property_add_child(obj, "phb[*]", phb);
            chip8->phbs[i] = PNV_PHB(phb);
        }
    }

}

static void pnv_chip_icp_realize(Pnv8Chip *chip8, Error **errp)
 {
    PnvChip *chip = PNV_CHIP(chip8);
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);
    int i, j;
    char *name;

    name = g_strdup_printf("icp-%x", chip->chip_id);
    memory_region_init(&chip8->icp_mmio, OBJECT(chip), name, PNV_ICP_SIZE);
    g_free(name);
    memory_region_add_subregion(get_system_memory(), PNV_ICP_BASE(chip),
                                &chip8->icp_mmio);

    /* Map the ICP registers for each thread */
    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pnv_core = chip->cores[i];
        int core_hwid = CPU_CORE(pnv_core)->core_id;

        for (j = 0; j < CPU_CORE(pnv_core)->nr_threads; j++) {
            uint32_t pir;
            PnvICPState *icp;

            pcc->get_pir_tir(chip, core_hwid, j, &pir, NULL);
            icp = PNV_ICP(xics_icp_get(chip8->xics, pir));

            memory_region_add_subregion(&chip8->icp_mmio, pir << 12,
                                        &icp->mmio);
        }
    }
}

static void pnv_chip_power8_realize(DeviceState *dev, Error **errp)
{
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(dev);
    PnvChip *chip = PNV_CHIP(dev);
    Pnv8Chip *chip8 = PNV8_CHIP(dev);
    Pnv8Psi *psi8 = &chip8->psi;
    Error *local_err = NULL;
    int i;

    assert(chip8->xics);

    /* XSCOM bridge is first */
    pnv_xscom_init(chip, PNV_XSCOM_SIZE, PNV_XSCOM_BASE(chip));

    pcc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Processor Service Interface (PSI) Host Bridge */
    object_property_set_int(OBJECT(psi8), "bar", PNV_PSIHB_BASE(chip),
                            &error_fatal);
    object_property_set_link(OBJECT(psi8), ICS_PROP_XICS,
                             OBJECT(chip8->xics), &error_abort);
    if (!qdev_realize(DEVICE(psi8), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV_XSCOM_PSIHB_BASE,
                            &PNV_PSI(psi8)->xscom_regs);

    /* Create LPC controller */
    qdev_realize(DEVICE(&chip8->lpc), NULL, &error_fatal);
    pnv_xscom_add_subregion(chip, PNV_XSCOM_LPC_BASE, &chip8->lpc.xscom_regs);

    chip->fw_mr = &chip8->lpc.isa_fw;
    chip->dt_isa_nodename = g_strdup_printf("/xscom@%" PRIx64 "/isa@%x",
                                            (uint64_t) PNV_XSCOM_BASE(chip),
                                            PNV_XSCOM_LPC_BASE);

    /*
     * Interrupt Management Area. This is the memory region holding
     * all the Interrupt Control Presenter (ICP) registers
     */
    pnv_chip_icp_realize(chip8, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* HOMER (must be created before OCC) */
    object_property_set_link(OBJECT(&chip8->homer), "chip", OBJECT(chip),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip8->homer), NULL, errp)) {
        return;
    }
    /* Homer Xscom region */
    pnv_xscom_add_subregion(chip, PNV_XSCOM_PBA_BASE, &chip8->homer.pba_regs);
    /* Homer RAM region */
    memory_region_add_subregion(get_system_memory(), chip8->homer.base,
                                &chip8->homer.mem);

    /* Create the simplified OCC model */
    object_property_set_link(OBJECT(&chip8->occ), "homer",
                             OBJECT(&chip8->homer), &error_abort);
    if (!qdev_realize(DEVICE(&chip8->occ), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV_XSCOM_OCC_BASE, &chip8->occ.xscom_regs);
    qdev_connect_gpio_out(DEVICE(&chip8->occ), 0,
                          qdev_get_gpio_in(DEVICE(psi8), PSIHB_IRQ_OCC));

    /* OCC SRAM model */
    memory_region_add_subregion(get_system_memory(), PNV_OCC_SENSOR_BASE(chip),
                                &chip8->occ.sram_regs);

    /* PHB controllers */
    for (i = 0; i < chip8->num_phbs; i++) {
        PnvPHB *phb = chip8->phbs[i];

        object_property_set_int(OBJECT(phb), "index", i, &error_fatal);
        object_property_set_int(OBJECT(phb), "chip-id", chip->chip_id,
                                &error_fatal);
        object_property_set_link(OBJECT(phb), "chip", OBJECT(chip),
                                 &error_fatal);
        if (!sysbus_realize(SYS_BUS_DEVICE(phb), errp)) {
            return;
        }
    }
}

static uint32_t pnv_chip_power8_xscom_pcba(PnvChip *chip, uint64_t addr)
{
    addr &= (PNV_XSCOM_SIZE - 1);
    return ((addr >> 4) & ~0xfull) | ((addr >> 3) & 0xf);
}

static void pnv_chip_power8e_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->chip_cfam_id = 0x221ef04980000000ull;  /* P8 Murano DD2.1 */
    k->cores_mask = POWER8E_CORE_MASK;
    k->num_phbs = 3;
    k->get_pir_tir = pnv_get_pir_tir_p8;
    k->intc_create = pnv_chip_power8_intc_create;
    k->intc_reset = pnv_chip_power8_intc_reset;
    k->intc_destroy = pnv_chip_power8_intc_destroy;
    k->intc_print_info = pnv_chip_power8_intc_print_info;
    k->isa_create = pnv_chip_power8_isa_create;
    k->dt_populate = pnv_chip_power8_dt_populate;
    k->pic_print_info = pnv_chip_power8_pic_print_info;
    k->xscom_core_base = pnv_chip_power8_xscom_core_base;
    k->xscom_pcba = pnv_chip_power8_xscom_pcba;
    dc->desc = "PowerNV Chip POWER8E";

    device_class_set_parent_realize(dc, pnv_chip_power8_realize,
                                    &k->parent_realize);
}

static void pnv_chip_power8_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->chip_cfam_id = 0x220ea04980000000ull; /* P8 Venice DD2.0 */
    k->cores_mask = POWER8_CORE_MASK;
    k->num_phbs = 3;
    k->get_pir_tir = pnv_get_pir_tir_p8;
    k->intc_create = pnv_chip_power8_intc_create;
    k->intc_reset = pnv_chip_power8_intc_reset;
    k->intc_destroy = pnv_chip_power8_intc_destroy;
    k->intc_print_info = pnv_chip_power8_intc_print_info;
    k->isa_create = pnv_chip_power8_isa_create;
    k->dt_populate = pnv_chip_power8_dt_populate;
    k->pic_print_info = pnv_chip_power8_pic_print_info;
    k->xscom_core_base = pnv_chip_power8_xscom_core_base;
    k->xscom_pcba = pnv_chip_power8_xscom_pcba;
    dc->desc = "PowerNV Chip POWER8";

    device_class_set_parent_realize(dc, pnv_chip_power8_realize,
                                    &k->parent_realize);
}

static void pnv_chip_power8nvl_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);

    k->chip_cfam_id = 0x120d304980000000ull;  /* P8 Naples DD1.0 */
    k->cores_mask = POWER8_CORE_MASK;
    k->num_phbs = 4;
    k->get_pir_tir = pnv_get_pir_tir_p8;
    k->intc_create = pnv_chip_power8_intc_create;
    k->intc_reset = pnv_chip_power8_intc_reset;
    k->intc_destroy = pnv_chip_power8_intc_destroy;
    k->intc_print_info = pnv_chip_power8_intc_print_info;
    k->isa_create = pnv_chip_power8nvl_isa_create;
    k->dt_populate = pnv_chip_power8_dt_populate;
    k->pic_print_info = pnv_chip_power8_pic_print_info;
    k->xscom_core_base = pnv_chip_power8_xscom_core_base;
    k->xscom_pcba = pnv_chip_power8_xscom_pcba;
    dc->desc = "PowerNV Chip POWER8NVL";

    device_class_set_parent_realize(dc, pnv_chip_power8_realize,
                                    &k->parent_realize);
}

static void pnv_chip_power9_instance_init(Object *obj)
{
    PnvChip *chip = PNV_CHIP(obj);
    Pnv9Chip *chip9 = PNV9_CHIP(obj);
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(obj);
    int i;

    object_initialize_child(obj, "adu",  &chip9->adu, TYPE_PNV_ADU);
    object_initialize_child(obj, "xive", &chip9->xive, TYPE_PNV_XIVE);
    object_property_add_alias(obj, "xive-fabric", OBJECT(&chip9->xive),
                              "xive-fabric");

    object_initialize_child(obj, "psi", &chip9->psi, TYPE_PNV9_PSI);

    object_initialize_child(obj, "lpc", &chip9->lpc, TYPE_PNV9_LPC);

    object_initialize_child(obj, "chiptod", &chip9->chiptod, TYPE_PNV9_CHIPTOD);

    object_initialize_child(obj, "occ", &chip9->occ, TYPE_PNV9_OCC);

    object_initialize_child(obj, "sbe", &chip9->sbe, TYPE_PNV9_SBE);

    object_initialize_child(obj, "homer", &chip9->homer, TYPE_PNV9_HOMER);

    /* Number of PECs is the chip default */
    chip->num_pecs = pcc->num_pecs;

    for (i = 0; i < chip->num_pecs; i++) {
        object_initialize_child(obj, "pec[*]", &chip9->pecs[i],
                                TYPE_PNV_PHB4_PEC);
    }

    for (i = 0; i < pcc->i2c_num_engines; i++) {
        object_initialize_child(obj, "i2c[*]", &chip9->i2c[i], TYPE_PNV_I2C);
    }
}

static void pnv_chip_quad_realize_one(PnvChip *chip, PnvQuad *eq,
                                      PnvCore *pnv_core,
                                      const char *type)
{
    char eq_name[32];
    int core_id = CPU_CORE(pnv_core)->core_id;

    snprintf(eq_name, sizeof(eq_name), "eq[%d]", core_id);
    object_initialize_child_with_props(OBJECT(chip), eq_name, eq,
                                       sizeof(*eq), type,
                                       &error_fatal, NULL);

    object_property_set_int(OBJECT(eq), "quad-id", core_id, &error_fatal);
    qdev_realize(DEVICE(eq), NULL, &error_fatal);
}

static void pnv_chip_quad_realize(Pnv9Chip *chip9, Error **errp)
{
    PnvChip *chip = PNV_CHIP(chip9);
    int i;

    chip9->nr_quads = DIV_ROUND_UP(chip->nr_cores, 4);
    chip9->quads = g_new0(PnvQuad, chip9->nr_quads);

    for (i = 0; i < chip9->nr_quads; i++) {
        PnvQuad *eq = &chip9->quads[i];

        pnv_chip_quad_realize_one(chip, eq, chip->cores[i * 4],
                                  PNV_QUAD_TYPE_NAME("power9"));

        pnv_xscom_add_subregion(chip, PNV9_XSCOM_EQ_BASE(eq->quad_id),
                                &eq->xscom_regs);
    }
}

static void pnv_chip_power9_pec_realize(PnvChip *chip, Error **errp)
{
    Pnv9Chip *chip9 = PNV9_CHIP(chip);
    int i;

    for (i = 0; i < chip->num_pecs; i++) {
        PnvPhb4PecState *pec = &chip9->pecs[i];
        PnvPhb4PecClass *pecc = PNV_PHB4_PEC_GET_CLASS(pec);
        uint32_t pec_cplt_base;
        uint32_t pec_nest_base;
        uint32_t pec_pci_base;

        object_property_set_int(OBJECT(pec), "index", i, &error_fatal);
        object_property_set_int(OBJECT(pec), "chip-id", chip->chip_id,
                                &error_fatal);
        object_property_set_link(OBJECT(pec), "chip", OBJECT(chip),
                                 &error_fatal);
        if (!qdev_realize(DEVICE(pec), NULL, errp)) {
            return;
        }

        pec_cplt_base = pecc->xscom_cplt_base(pec);
        pec_nest_base = pecc->xscom_nest_base(pec);
        pec_pci_base = pecc->xscom_pci_base(pec);

        pnv_xscom_add_subregion(chip, pec_cplt_base,
                 &pec->nest_pervasive.xscom_ctrl_regs_mr);
        pnv_xscom_add_subregion(chip, pec_nest_base, &pec->nest_regs_mr);
        pnv_xscom_add_subregion(chip, pec_pci_base, &pec->pci_regs_mr);
    }
}

static uint64_t pnv_handle_sprd_load(CPUPPCState *env)
{
    PowerPCCPU *cpu = env_archcpu(env);
    PnvCore *pc = pnv_cpu_state(cpu)->pnv_core;
    uint64_t sprc = env->spr[SPR_POWER_SPRC];

    if (pc->big_core) {
        pc = pnv_chip_find_core(pc->chip, CPU_CORE(pc)->core_id & ~0x1);
    }

    switch (sprc & 0x3e0) {
    case 0: /* SCRATCH0-3 */
    case 1: /* SCRATCH4-7 */
        return pc->scratch[(sprc >> 3) & 0x7];

    case 0x1e0: /* core thread state */
        if (env->excp_model == POWERPC_EXCP_POWER9) {
            /*
             * Only implement for POWER9 because skiboot uses it to check
             * big-core mode. Other bits are unimplemented so we would
             * prefer to get unimplemented message on POWER10 if it were
             * used anywhere.
             */
            if (pc->big_core) {
                return PPC_BIT(63);
            } else {
                return 0;
            }
        }
        /* fallthru */

    default:
        qemu_log_mask(LOG_UNIMP, "mfSPRD: Unimplemented SPRC:0x"
                                  TARGET_FMT_lx"\n", sprc);
        break;
    }
    return 0;
}

static void pnv_handle_sprd_store(CPUPPCState *env, uint64_t val)
{
    PowerPCCPU *cpu = env_archcpu(env);
    uint64_t sprc = env->spr[SPR_POWER_SPRC];
    PnvCore *pc = pnv_cpu_state(cpu)->pnv_core;
    int nr;

    if (pc->big_core) {
        pc = pnv_chip_find_core(pc->chip, CPU_CORE(pc)->core_id & ~0x1);
    }

    switch (sprc & 0x3e0) {
    case 0: /* SCRATCH0-3 */
    case 1: /* SCRATCH4-7 */
        /*
         * Log stores to SCRATCH, because some firmware uses these for
         * debugging and logging, but they would normally be read by the BMC,
         * which is not implemented in QEMU yet. This gives a way to get at the
         * information. Could also dump these upon checkstop.
         */
        nr = (sprc >> 3) & 0x7;
        pc->scratch[nr] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "mtSPRD: Unimplemented SPRC:0x"
                                  TARGET_FMT_lx"\n", sprc);
        break;
    }
}

static void pnv_chip_power9_realize(DeviceState *dev, Error **errp)
{
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(dev);
    Pnv9Chip *chip9 = PNV9_CHIP(dev);
    PnvChip *chip = PNV_CHIP(dev);
    Pnv9Psi *psi9 = &chip9->psi;
    PowerPCCPU *cpu;
    PowerPCCPUClass *cpu_class;
    Error *local_err = NULL;
    int i;

    /* XSCOM bridge is first */
    pnv_xscom_init(chip, PNV9_XSCOM_SIZE, PNV9_XSCOM_BASE(chip));

    pcc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* ADU */
    object_property_set_link(OBJECT(&chip9->adu), "lpc", OBJECT(&chip9->lpc),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip9->adu), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_ADU_BASE,
                            &chip9->adu.xscom_regs);

    pnv_chip_quad_realize(chip9, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Set handlers for Special registers, such as SPRD */
    cpu = chip->cores[0]->threads[0];
    cpu_class = POWERPC_CPU_GET_CLASS(cpu);
    cpu_class->load_sprd = pnv_handle_sprd_load;
    cpu_class->store_sprd = pnv_handle_sprd_store;

    /* XIVE interrupt controller (POWER9) */
    object_property_set_int(OBJECT(&chip9->xive), "ic-bar",
                            PNV9_XIVE_IC_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip9->xive), "vc-bar",
                            PNV9_XIVE_VC_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip9->xive), "pc-bar",
                            PNV9_XIVE_PC_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip9->xive), "tm-bar",
                            PNV9_XIVE_TM_BASE(chip), &error_fatal);
    object_property_set_link(OBJECT(&chip9->xive), "chip", OBJECT(chip),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&chip9->xive), errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_XIVE_BASE,
                            &chip9->xive.xscom_regs);

    /* Processor Service Interface (PSI) Host Bridge */
    object_property_set_int(OBJECT(psi9), "bar", PNV9_PSIHB_BASE(chip),
                            &error_fatal);
    /* This is the only device with 4k ESB pages */
    object_property_set_int(OBJECT(psi9), "shift", XIVE_ESB_4K,
                            &error_fatal);
    if (!qdev_realize(DEVICE(psi9), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_PSIHB_BASE,
                            &PNV_PSI(psi9)->xscom_regs);

    /* LPC */
    if (!qdev_realize(DEVICE(&chip9->lpc), NULL, errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), PNV9_LPCM_BASE(chip),
                                &chip9->lpc.xscom_regs);

    chip->fw_mr = &chip9->lpc.isa_fw;
    chip->dt_isa_nodename = g_strdup_printf("/lpcm-opb@%" PRIx64 "/lpc@0",
                                            (uint64_t) PNV9_LPCM_BASE(chip));

    /* ChipTOD */
    object_property_set_bool(OBJECT(&chip9->chiptod), "primary",
                             chip->chip_id == 0, &error_abort);
    object_property_set_bool(OBJECT(&chip9->chiptod), "secondary",
                             chip->chip_id == 1, &error_abort);
    object_property_set_link(OBJECT(&chip9->chiptod), "chip", OBJECT(chip),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip9->chiptod), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_CHIPTOD_BASE,
                            &chip9->chiptod.xscom_regs);

    /* SBE */
    if (!qdev_realize(DEVICE(&chip9->sbe), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_SBE_CTRL_BASE,
                            &chip9->sbe.xscom_ctrl_regs);
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_SBE_MBOX_BASE,
                            &chip9->sbe.xscom_mbox_regs);
    qdev_connect_gpio_out(DEVICE(&chip9->sbe), 0, qdev_get_gpio_in(
                              DEVICE(psi9), PSIHB9_IRQ_PSU));

    /* HOMER (must be created before OCC) */
    object_property_set_link(OBJECT(&chip9->homer), "chip", OBJECT(chip),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip9->homer), NULL, errp)) {
        return;
    }
    /* Homer Xscom region */
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_PBA_BASE, &chip9->homer.pba_regs);
    /* Homer RAM region */
    memory_region_add_subregion(get_system_memory(), chip9->homer.base,
                                &chip9->homer.mem);

    /* Create the simplified OCC model */
    object_property_set_link(OBJECT(&chip9->occ), "homer",
                             OBJECT(&chip9->homer), &error_abort);
    if (!qdev_realize(DEVICE(&chip9->occ), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV9_XSCOM_OCC_BASE, &chip9->occ.xscom_regs);
    qdev_connect_gpio_out(DEVICE(&chip9->occ), 0, qdev_get_gpio_in(
                              DEVICE(psi9), PSIHB9_IRQ_OCC));

    /* OCC SRAM model */
    memory_region_add_subregion(get_system_memory(), PNV9_OCC_SENSOR_BASE(chip),
                                &chip9->occ.sram_regs);

    /* PEC PHBs */
    pnv_chip_power9_pec_realize(chip, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /*
     * I2C
     */
    for (i = 0; i < pcc->i2c_num_engines; i++) {
        Object *obj =  OBJECT(&chip9->i2c[i]);

        object_property_set_int(obj, "engine", i + 1, &error_fatal);
        object_property_set_int(obj, "num-busses",
                                pcc->i2c_ports_per_engine[i],
                                &error_fatal);
        object_property_set_link(obj, "chip", OBJECT(chip), &error_abort);
        if (!qdev_realize(DEVICE(obj), NULL, errp)) {
            return;
        }
        pnv_xscom_add_subregion(chip, PNV9_XSCOM_I2CM_BASE +
                                (chip9->i2c[i].engine - 1) *
                                        PNV9_XSCOM_I2CM_SIZE,
                                &chip9->i2c[i].xscom_regs);
        qdev_connect_gpio_out(DEVICE(&chip9->i2c[i]), 0,
                              qdev_get_gpio_in(DEVICE(psi9),
                                               PSIHB9_IRQ_SBE_I2C));
    }
}

static uint32_t pnv_chip_power9_xscom_pcba(PnvChip *chip, uint64_t addr)
{
    addr &= (PNV9_XSCOM_SIZE - 1);
    return addr >> 3;
}

static void pnv_chip_power9_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);
    static const int i2c_ports_per_engine[PNV9_CHIP_MAX_I2C] = {2, 13, 2, 2};

    k->chip_cfam_id = 0x220d104900008000ull; /* P9 Nimbus DD2.0 */
    k->cores_mask = POWER9_CORE_MASK;
    k->get_pir_tir = pnv_get_pir_tir_p9;
    k->intc_create = pnv_chip_power9_intc_create;
    k->intc_reset = pnv_chip_power9_intc_reset;
    k->intc_destroy = pnv_chip_power9_intc_destroy;
    k->intc_print_info = pnv_chip_power9_intc_print_info;
    k->isa_create = pnv_chip_power9_isa_create;
    k->dt_populate = pnv_chip_power9_dt_populate;
    k->pic_print_info = pnv_chip_power9_pic_print_info;
    k->xscom_core_base = pnv_chip_power9_xscom_core_base;
    k->xscom_pcba = pnv_chip_power9_xscom_pcba;
    dc->desc = "PowerNV Chip POWER9";
    k->num_pecs = PNV9_CHIP_MAX_PEC;
    k->i2c_num_engines = PNV9_CHIP_MAX_I2C;
    k->i2c_ports_per_engine = i2c_ports_per_engine;

    device_class_set_parent_realize(dc, pnv_chip_power9_realize,
                                    &k->parent_realize);
}

static void pnv_chip_power10_instance_init(Object *obj)
{
    PnvChip *chip = PNV_CHIP(obj);
    Pnv10Chip *chip10 = PNV10_CHIP(obj);
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(obj);
    int i;

    object_initialize_child(obj, "adu",  &chip10->adu, TYPE_PNV_ADU);
    object_initialize_child(obj, "xive", &chip10->xive, TYPE_PNV_XIVE2);
    object_property_add_alias(obj, "xive-fabric", OBJECT(&chip10->xive),
                              "xive-fabric");
    object_initialize_child(obj, "psi", &chip10->psi, TYPE_PNV10_PSI);
    object_initialize_child(obj, "lpc", &chip10->lpc, TYPE_PNV10_LPC);
    object_initialize_child(obj, "chiptod", &chip10->chiptod,
                            TYPE_PNV10_CHIPTOD);
    object_initialize_child(obj, "occ",  &chip10->occ, TYPE_PNV10_OCC);
    object_initialize_child(obj, "sbe",  &chip10->sbe, TYPE_PNV10_SBE);
    object_initialize_child(obj, "homer", &chip10->homer, TYPE_PNV10_HOMER);
    object_initialize_child(obj, "n1-chiplet", &chip10->n1_chiplet,
                            TYPE_PNV_N1_CHIPLET);

    chip->num_pecs = pcc->num_pecs;

    for (i = 0; i < chip->num_pecs; i++) {
        object_initialize_child(obj, "pec[*]", &chip10->pecs[i],
                                TYPE_PNV_PHB5_PEC);
    }

    for (i = 0; i < pcc->i2c_num_engines; i++) {
        object_initialize_child(obj, "i2c[*]", &chip10->i2c[i], TYPE_PNV_I2C);
    }

    for (i = 0; i < PNV10_CHIP_MAX_PIB_SPIC; i++) {
        object_initialize_child(obj, "pib_spic[*]", &chip10->pib_spic[i],
                                TYPE_PNV_SPI);
    }
}

static void pnv_chip_power10_quad_realize(Pnv10Chip *chip10, Error **errp)
{
    PnvChip *chip = PNV_CHIP(chip10);
    int i;

    chip10->nr_quads = DIV_ROUND_UP(chip->nr_cores, 4);
    chip10->quads = g_new0(PnvQuad, chip10->nr_quads);

    for (i = 0; i < chip10->nr_quads; i++) {
        PnvQuad *eq = &chip10->quads[i];

        pnv_chip_quad_realize_one(chip, eq, chip->cores[i * 4],
                                  PNV_QUAD_TYPE_NAME("power10"));

        pnv_xscom_add_subregion(chip, PNV10_XSCOM_EQ_BASE(eq->quad_id),
                                &eq->xscom_regs);

        pnv_xscom_add_subregion(chip, PNV10_XSCOM_QME_BASE(eq->quad_id),
                                &eq->xscom_qme_regs);
    }
}

static void pnv_chip_power10_phb_realize(PnvChip *chip, Error **errp)
{
    Pnv10Chip *chip10 = PNV10_CHIP(chip);
    int i;

    for (i = 0; i < chip->num_pecs; i++) {
        PnvPhb4PecState *pec = &chip10->pecs[i];
        PnvPhb4PecClass *pecc = PNV_PHB4_PEC_GET_CLASS(pec);
        uint32_t pec_cplt_base;
        uint32_t pec_nest_base;
        uint32_t pec_pci_base;

        object_property_set_int(OBJECT(pec), "index", i, &error_fatal);
        object_property_set_int(OBJECT(pec), "chip-id", chip->chip_id,
                                &error_fatal);
        object_property_set_link(OBJECT(pec), "chip", OBJECT(chip),
                                 &error_fatal);
        if (!qdev_realize(DEVICE(pec), NULL, errp)) {
            return;
        }

        pec_cplt_base = pecc->xscom_cplt_base(pec);
        pec_nest_base = pecc->xscom_nest_base(pec);
        pec_pci_base = pecc->xscom_pci_base(pec);

        pnv_xscom_add_subregion(chip, pec_cplt_base,
                 &pec->nest_pervasive.xscom_ctrl_regs_mr);
        pnv_xscom_add_subregion(chip, pec_nest_base, &pec->nest_regs_mr);
        pnv_xscom_add_subregion(chip, pec_pci_base, &pec->pci_regs_mr);
    }
}

static void pnv_chip_power10_realize(DeviceState *dev, Error **errp)
{
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(dev);
    PnvChip *chip = PNV_CHIP(dev);
    Pnv10Chip *chip10 = PNV10_CHIP(dev);
    PowerPCCPU *cpu;
    PowerPCCPUClass *cpu_class;
    Error *local_err = NULL;
    int i;

    /* XSCOM bridge is first */
    pnv_xscom_init(chip, PNV10_XSCOM_SIZE, PNV10_XSCOM_BASE(chip));

    pcc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* ADU */
    object_property_set_link(OBJECT(&chip10->adu), "lpc", OBJECT(&chip10->lpc),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip10->adu), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV10_XSCOM_ADU_BASE,
                            &chip10->adu.xscom_regs);

    pnv_chip_power10_quad_realize(chip10, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Set handlers for Special registers, such as SPRD */
    cpu = chip->cores[0]->threads[0];
    cpu_class = POWERPC_CPU_GET_CLASS(cpu);
    cpu_class->load_sprd = pnv_handle_sprd_load;
    cpu_class->store_sprd = pnv_handle_sprd_store;

    /* XIVE2 interrupt controller (POWER10) */
    object_property_set_int(OBJECT(&chip10->xive), "ic-bar",
                            PNV10_XIVE2_IC_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip10->xive), "esb-bar",
                            PNV10_XIVE2_ESB_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip10->xive), "end-bar",
                            PNV10_XIVE2_END_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip10->xive), "nvpg-bar",
                            PNV10_XIVE2_NVPG_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip10->xive), "nvc-bar",
                            PNV10_XIVE2_NVC_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip10->xive), "tm-bar",
                            PNV10_XIVE2_TM_BASE(chip), &error_fatal);
    object_property_set_link(OBJECT(&chip10->xive), "chip", OBJECT(chip),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&chip10->xive), errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV10_XSCOM_XIVE2_BASE,
                            &chip10->xive.xscom_regs);

    /* Processor Service Interface (PSI) Host Bridge */
    object_property_set_int(OBJECT(&chip10->psi), "bar",
                            PNV10_PSIHB_BASE(chip), &error_fatal);
    /* PSI can now be configured to use 64k ESB pages on POWER10 */
    object_property_set_int(OBJECT(&chip10->psi), "shift", XIVE_ESB_64K,
                            &error_fatal);
    if (!qdev_realize(DEVICE(&chip10->psi), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV10_XSCOM_PSIHB_BASE,
                            &PNV_PSI(&chip10->psi)->xscom_regs);

    /* LPC */
    if (!qdev_realize(DEVICE(&chip10->lpc), NULL, errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), PNV10_LPCM_BASE(chip),
                                &chip10->lpc.xscom_regs);

    chip->fw_mr = &chip10->lpc.isa_fw;
    chip->dt_isa_nodename = g_strdup_printf("/lpcm-opb@%" PRIx64 "/lpc@0",
                                            (uint64_t) PNV10_LPCM_BASE(chip));

    /* ChipTOD */
    object_property_set_bool(OBJECT(&chip10->chiptod), "primary",
                             chip->chip_id == 0, &error_abort);
    object_property_set_bool(OBJECT(&chip10->chiptod), "secondary",
                             chip->chip_id == 1, &error_abort);
    object_property_set_link(OBJECT(&chip10->chiptod), "chip", OBJECT(chip),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip10->chiptod), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV10_XSCOM_CHIPTOD_BASE,
                            &chip10->chiptod.xscom_regs);

    /* HOMER (must be created before OCC) */
    object_property_set_link(OBJECT(&chip10->homer), "chip", OBJECT(chip),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip10->homer), NULL, errp)) {
        return;
    }
    /* Homer Xscom region */
    pnv_xscom_add_subregion(chip, PNV10_XSCOM_PBA_BASE,
                            &chip10->homer.pba_regs);
    /* Homer RAM region */
    memory_region_add_subregion(get_system_memory(), chip10->homer.base,
                                &chip10->homer.mem);

    /* Create the simplified OCC model */
    object_property_set_link(OBJECT(&chip10->occ), "homer",
                             OBJECT(&chip10->homer), &error_abort);
    if (!qdev_realize(DEVICE(&chip10->occ), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV10_XSCOM_OCC_BASE,
                            &chip10->occ.xscom_regs);
    qdev_connect_gpio_out(DEVICE(&chip10->occ), 0, qdev_get_gpio_in(
                              DEVICE(&chip10->psi), PSIHB9_IRQ_OCC));

    /* OCC SRAM model */
    memory_region_add_subregion(get_system_memory(),
                                PNV10_OCC_SENSOR_BASE(chip),
                                &chip10->occ.sram_regs);

    /* SBE */
    if (!qdev_realize(DEVICE(&chip10->sbe), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV10_XSCOM_SBE_CTRL_BASE,
                            &chip10->sbe.xscom_ctrl_regs);
    pnv_xscom_add_subregion(chip, PNV10_XSCOM_SBE_MBOX_BASE,
                            &chip10->sbe.xscom_mbox_regs);
    qdev_connect_gpio_out(DEVICE(&chip10->sbe), 0, qdev_get_gpio_in(
                              DEVICE(&chip10->psi), PSIHB9_IRQ_PSU));

    /* N1 chiplet */
    if (!qdev_realize(DEVICE(&chip10->n1_chiplet), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV10_XSCOM_N1_CHIPLET_CTRL_REGS_BASE,
             &chip10->n1_chiplet.nest_pervasive.xscom_ctrl_regs_mr);

    pnv_xscom_add_subregion(chip, PNV10_XSCOM_N1_PB_SCOM_EQ_BASE,
                           &chip10->n1_chiplet.xscom_pb_eq_mr);

    pnv_xscom_add_subregion(chip, PNV10_XSCOM_N1_PB_SCOM_ES_BASE,
                           &chip10->n1_chiplet.xscom_pb_es_mr);

    /* PHBs */
    pnv_chip_power10_phb_realize(chip, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }


    /*
     * I2C
     */
    for (i = 0; i < pcc->i2c_num_engines; i++) {
        Object *obj =  OBJECT(&chip10->i2c[i]);

        object_property_set_int(obj, "engine", i + 1, &error_fatal);
        object_property_set_int(obj, "num-busses",
                                pcc->i2c_ports_per_engine[i],
                                &error_fatal);
        object_property_set_link(obj, "chip", OBJECT(chip), &error_abort);
        if (!qdev_realize(DEVICE(obj), NULL, errp)) {
            return;
        }
        pnv_xscom_add_subregion(chip, PNV10_XSCOM_I2CM_BASE +
                                (chip10->i2c[i].engine - 1) *
                                        PNV10_XSCOM_I2CM_SIZE,
                                &chip10->i2c[i].xscom_regs);
        qdev_connect_gpio_out(DEVICE(&chip10->i2c[i]), 0,
                              qdev_get_gpio_in(DEVICE(&chip10->psi),
                                               PSIHB9_IRQ_SBE_I2C));
    }
    /* PIB SPI Controller */
    for (i = 0; i < PNV10_CHIP_MAX_PIB_SPIC; i++) {
        object_property_set_int(OBJECT(&chip10->pib_spic[i]), "spic_num",
                                i, &error_fatal);
        /* pib_spic[2] connected to 25csm04 which implements 1 byte transfer */
        object_property_set_int(OBJECT(&chip10->pib_spic[i]), "transfer_len",
                                (i == 2) ? 1 : 4, &error_fatal);
        object_property_set_int(OBJECT(&chip10->pib_spic[i]), "chip-id",
                                chip->chip_id, &error_fatal);
        if (!sysbus_realize(SYS_BUS_DEVICE(OBJECT
                                        (&chip10->pib_spic[i])), errp)) {
            return;
        }
        pnv_xscom_add_subregion(chip, PNV10_XSCOM_PIB_SPIC_BASE +
                                i * PNV10_XSCOM_PIB_SPIC_SIZE,
                                &chip10->pib_spic[i].xscom_spic_regs);
    }
}

static void pnv_chip_power11_instance_init(Object *obj)
{
    PnvChip *chip = PNV_CHIP(obj);
    Pnv11Chip *chip11 = PNV11_CHIP(obj);
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(obj);
    int i;

    object_initialize_child(obj, "adu",  &chip11->adu, TYPE_PNV_ADU);

    /*
     * Use Power10 device models for PSI/LPC/OCC/SBE/HOMER as corresponding
     * device models for Power11 are same
     */
    object_initialize_child(obj, "psi", &chip11->psi, TYPE_PNV10_PSI);
    object_initialize_child(obj, "lpc", &chip11->lpc, TYPE_PNV10_LPC);
    object_initialize_child(obj, "occ",  &chip11->occ, TYPE_PNV10_OCC);
    object_initialize_child(obj, "sbe",  &chip11->sbe, TYPE_PNV10_SBE);
    object_initialize_child(obj, "homer", &chip11->homer, TYPE_PNV10_HOMER);

    object_initialize_child(obj, "xive", &chip11->xive, TYPE_PNV_XIVE2);
    object_property_add_alias(obj, "xive-fabric", OBJECT(&chip11->xive),
                              "xive-fabric");
    object_initialize_child(obj, "chiptod", &chip11->chiptod,
                            TYPE_PNV11_CHIPTOD);
    object_initialize_child(obj, "n1-chiplet", &chip11->n1_chiplet,
                            TYPE_PNV_N1_CHIPLET);

    chip->num_pecs = pcc->num_pecs;

    for (i = 0; i < chip->num_pecs; i++) {
        object_initialize_child(obj, "pec[*]", &chip11->pecs[i],
                                TYPE_PNV_PHB5_PEC);
    }

    for (i = 0; i < pcc->i2c_num_engines; i++) {
        object_initialize_child(obj, "i2c[*]", &chip11->i2c[i], TYPE_PNV_I2C);
    }

    for (i = 0; i < PNV10_CHIP_MAX_PIB_SPIC; i++) {
        object_initialize_child(obj, "pib_spic[*]", &chip11->pib_spic[i],
                                TYPE_PNV_SPI);
    }
}

static void pnv_chip_power11_quad_realize(Pnv11Chip *chip11, Error **errp)
{
    PnvChip *chip = PNV_CHIP(chip11);
    int i;

    chip11->nr_quads = DIV_ROUND_UP(chip->nr_cores, 4);
    chip11->quads = g_new0(PnvQuad, chip11->nr_quads);

    for (i = 0; i < chip11->nr_quads; i++) {
        PnvQuad *eq = &chip11->quads[i];

        pnv_chip_quad_realize_one(chip, eq, chip->cores[i * 4],
                                  PNV_QUAD_TYPE_NAME("power11"));

        pnv_xscom_add_subregion(chip, PNV11_XSCOM_EQ_BASE(eq->quad_id),
                                &eq->xscom_regs);

        pnv_xscom_add_subregion(chip, PNV11_XSCOM_QME_BASE(eq->quad_id),
                                &eq->xscom_qme_regs);
    }
}

static void pnv_chip_power11_phb_realize(PnvChip *chip, Error **errp)
{
    Pnv11Chip *chip11 = PNV11_CHIP(chip);
    int i;

    for (i = 0; i < chip->num_pecs; i++) {
        PnvPhb4PecState *pec = &chip11->pecs[i];
        PnvPhb4PecClass *pecc = PNV_PHB4_PEC_GET_CLASS(pec);
        uint32_t pec_cplt_base;
        uint32_t pec_nest_base;
        uint32_t pec_pci_base;

        object_property_set_int(OBJECT(pec), "index", i, &error_fatal);
        object_property_set_int(OBJECT(pec), "chip-id", chip->chip_id,
                                &error_fatal);
        object_property_set_link(OBJECT(pec), "chip", OBJECT(chip),
                                 &error_fatal);
        if (!qdev_realize(DEVICE(pec), NULL, errp)) {
            return;
        }

        pec_cplt_base = pecc->xscom_cplt_base(pec);
        pec_nest_base = pecc->xscom_nest_base(pec);
        pec_pci_base = pecc->xscom_pci_base(pec);

        pnv_xscom_add_subregion(chip, pec_cplt_base,
                 &pec->nest_pervasive.xscom_ctrl_regs_mr);
        pnv_xscom_add_subregion(chip, pec_nest_base, &pec->nest_regs_mr);
        pnv_xscom_add_subregion(chip, pec_pci_base, &pec->pci_regs_mr);
    }
}

static void pnv_chip_power11_realize(DeviceState *dev, Error **errp)
{
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(dev);
    PnvChip *chip = PNV_CHIP(dev);
    Pnv11Chip *chip11 = PNV11_CHIP(dev);
    PowerPCCPU *cpu;
    PowerPCCPUClass *cpu_class;
    Error *local_err = NULL;
    int i;

    /* XSCOM bridge is first */
    pnv_xscom_init(chip, PNV11_XSCOM_SIZE, PNV11_XSCOM_BASE(chip));

    pcc->parent_realize(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* Set handlers for Special registers, such as SPRD */
    cpu = chip->cores[0]->threads[0];
    cpu_class = POWERPC_CPU_GET_CLASS(cpu);
    cpu_class->load_sprd = pnv_handle_sprd_load;
    cpu_class->store_sprd = pnv_handle_sprd_store;

    /* ADU */
    object_property_set_link(OBJECT(&chip11->adu), "lpc", OBJECT(&chip11->lpc),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip11->adu), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV11_XSCOM_ADU_BASE,
                            &chip11->adu.xscom_regs);

    pnv_chip_power11_quad_realize(chip11, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* XIVE2 interrupt controller */
    object_property_set_int(OBJECT(&chip11->xive), "ic-bar",
                            PNV11_XIVE2_IC_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip11->xive), "esb-bar",
                            PNV11_XIVE2_ESB_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip11->xive), "end-bar",
                            PNV11_XIVE2_END_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip11->xive), "nvpg-bar",
                            PNV11_XIVE2_NVPG_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip11->xive), "nvc-bar",
                            PNV11_XIVE2_NVC_BASE(chip), &error_fatal);
    object_property_set_int(OBJECT(&chip11->xive), "tm-bar",
                            PNV11_XIVE2_TM_BASE(chip), &error_fatal);
    object_property_set_link(OBJECT(&chip11->xive), "chip", OBJECT(chip),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&chip11->xive), errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV11_XSCOM_XIVE2_BASE,
                            &chip11->xive.xscom_regs);

    /* Processor Service Interface (PSI) Host Bridge */
    object_property_set_int(OBJECT(&chip11->psi), "bar",
                            PNV11_PSIHB_BASE(chip), &error_fatal);
    /* PSI can be configured to use 64k ESB pages on Power11 */
    object_property_set_int(OBJECT(&chip11->psi), "shift", XIVE_ESB_64K,
                            &error_fatal);
    if (!qdev_realize(DEVICE(&chip11->psi), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV11_XSCOM_PSIHB_BASE,
                            &PNV_PSI(&chip11->psi)->xscom_regs);

    /* LPC */
    if (!qdev_realize(DEVICE(&chip11->lpc), NULL, errp)) {
        return;
    }
    memory_region_add_subregion(get_system_memory(), PNV11_LPCM_BASE(chip),
                                &chip11->lpc.xscom_regs);

    chip->fw_mr = &chip11->lpc.isa_fw;
    chip->dt_isa_nodename = g_strdup_printf("/lpcm-opb@%" PRIx64 "/lpc@0",
                                            (uint64_t) PNV11_LPCM_BASE(chip));

    /* ChipTOD */
    object_property_set_bool(OBJECT(&chip11->chiptod), "primary",
                             chip->chip_id == 0, &error_abort);
    object_property_set_bool(OBJECT(&chip11->chiptod), "secondary",
                             chip->chip_id == 1, &error_abort);
    object_property_set_link(OBJECT(&chip11->chiptod), "chip", OBJECT(chip),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip11->chiptod), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV11_XSCOM_CHIPTOD_BASE,
                            &chip11->chiptod.xscom_regs);

    /* HOMER (must be created before OCC) */
    object_property_set_link(OBJECT(&chip11->homer), "chip", OBJECT(chip),
                             &error_abort);
    if (!qdev_realize(DEVICE(&chip11->homer), NULL, errp)) {
        return;
    }
    /* Homer Xscom region */
    pnv_xscom_add_subregion(chip, PNV11_XSCOM_PBA_BASE,
                            &chip11->homer.pba_regs);
    /* Homer RAM region */
    memory_region_add_subregion(get_system_memory(), chip11->homer.base,
                                &chip11->homer.mem);

    /* Create the simplified OCC model */
    object_property_set_link(OBJECT(&chip11->occ), "homer",
                             OBJECT(&chip11->homer), &error_abort);
    if (!qdev_realize(DEVICE(&chip11->occ), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV11_XSCOM_OCC_BASE,
                            &chip11->occ.xscom_regs);
    qdev_connect_gpio_out(DEVICE(&chip11->occ), 0, qdev_get_gpio_in(
                              DEVICE(&chip11->psi), PSIHB9_IRQ_OCC));

    /* OCC SRAM model */
    memory_region_add_subregion(get_system_memory(),
                                PNV11_OCC_SENSOR_BASE(chip),
                                &chip11->occ.sram_regs);

    /* SBE */
    if (!qdev_realize(DEVICE(&chip11->sbe), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV11_XSCOM_SBE_CTRL_BASE,
                            &chip11->sbe.xscom_ctrl_regs);
    pnv_xscom_add_subregion(chip, PNV11_XSCOM_SBE_MBOX_BASE,
                            &chip11->sbe.xscom_mbox_regs);
    qdev_connect_gpio_out(DEVICE(&chip11->sbe), 0, qdev_get_gpio_in(
                              DEVICE(&chip11->psi), PSIHB9_IRQ_PSU));

    /* N1 chiplet */
    if (!qdev_realize(DEVICE(&chip11->n1_chiplet), NULL, errp)) {
        return;
    }
    pnv_xscom_add_subregion(chip, PNV11_XSCOM_N1_CHIPLET_CTRL_REGS_BASE,
             &chip11->n1_chiplet.nest_pervasive.xscom_ctrl_regs_mr);

    pnv_xscom_add_subregion(chip, PNV11_XSCOM_N1_PB_SCOM_EQ_BASE,
                           &chip11->n1_chiplet.xscom_pb_eq_mr);

    pnv_xscom_add_subregion(chip, PNV11_XSCOM_N1_PB_SCOM_ES_BASE,
                           &chip11->n1_chiplet.xscom_pb_es_mr);

    /* PHBs */
    pnv_chip_power11_phb_realize(chip, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /*
     * I2C
     */
    for (i = 0; i < pcc->i2c_num_engines; i++) {
        Object *obj =  OBJECT(&chip11->i2c[i]);

        object_property_set_int(obj, "engine", i + 1, &error_fatal);
        object_property_set_int(obj, "num-busses",
                                pcc->i2c_ports_per_engine[i],
                                &error_fatal);
        object_property_set_link(obj, "chip", OBJECT(chip), &error_abort);
        if (!qdev_realize(DEVICE(obj), NULL, errp)) {
            return;
        }
        pnv_xscom_add_subregion(chip, PNV11_XSCOM_I2CM_BASE +
                                (chip11->i2c[i].engine - 1) *
                                        PNV11_XSCOM_I2CM_SIZE,
                                &chip11->i2c[i].xscom_regs);
        qdev_connect_gpio_out(DEVICE(&chip11->i2c[i]), 0,
                              qdev_get_gpio_in(DEVICE(&chip11->psi),
                                               PSIHB9_IRQ_SBE_I2C));
    }
    /* PIB SPI Controller */
    for (i = 0; i < PNV10_CHIP_MAX_PIB_SPIC; i++) {
        object_property_set_int(OBJECT(&chip11->pib_spic[i]), "spic_num",
                                i, &error_fatal);
        /* pib_spic[2] connected to 25csm04 which implements 1 byte transfer */
        object_property_set_int(OBJECT(&chip11->pib_spic[i]), "transfer_len",
                                (i == 2) ? 1 : 4, &error_fatal);
        object_property_set_int(OBJECT(&chip11->pib_spic[i]), "chip-id",
                                chip->chip_id, &error_fatal);
        if (!sysbus_realize(SYS_BUS_DEVICE(OBJECT
                                        (&chip11->pib_spic[i])), errp)) {
            return;
        }
        pnv_xscom_add_subregion(chip, PNV11_XSCOM_PIB_SPIC_BASE +
                                i * PNV11_XSCOM_PIB_SPIC_SIZE,
                                &chip11->pib_spic[i].xscom_spic_regs);
    }
}

static void pnv_rainier_i2c_init(PnvMachineState *pnv)
{
    int i;
    for (i = 0; i < pnv->num_chips; i++) {
        Pnv10Chip *chip10 = PNV10_CHIP(pnv->chips[i]);

        /*
         * Add a PCA9552 I2C device for PCIe hotplug control
         * to engine 2, bus 1, address 0x63
         */
        I2CSlave *dev = i2c_slave_create_simple(chip10->i2c[2].busses[1],
                                                "pca9552", 0x63);

        /*
         * Connect PCA9552 GPIO pins 0-4 (SLOTx_EN) outputs to GPIO pins 5-9
         * (SLOTx_PG) inputs in order to fake the pgood state of PCIe slots
         * after hypervisor code sets a SLOTx_EN pin high.
         */
        qdev_connect_gpio_out(DEVICE(dev), 0, qdev_get_gpio_in(DEVICE(dev), 5));
        qdev_connect_gpio_out(DEVICE(dev), 1, qdev_get_gpio_in(DEVICE(dev), 6));
        qdev_connect_gpio_out(DEVICE(dev), 2, qdev_get_gpio_in(DEVICE(dev), 7));
        qdev_connect_gpio_out(DEVICE(dev), 3, qdev_get_gpio_in(DEVICE(dev), 8));
        qdev_connect_gpio_out(DEVICE(dev), 4, qdev_get_gpio_in(DEVICE(dev), 9));

        /*
         * Add a PCA9554 I2C device for cable card presence detection
         * to engine 2, bus 1, address 0x25
         */
        i2c_slave_create_simple(chip10->i2c[2].busses[1], "pca9554", 0x25);
    }
}

static uint32_t pnv_chip_power10_xscom_pcba(PnvChip *chip, uint64_t addr)
{
    addr &= (PNV10_XSCOM_SIZE - 1);
    return addr >> 3;
}

static void pnv_chip_power10_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);
    static const int i2c_ports_per_engine[PNV10_CHIP_MAX_I2C] = {14, 14, 2, 16};

    k->chip_cfam_id = 0x220da04980000000ull; /* P10 DD2.0 (with NX) */
    k->cores_mask = POWER10_CORE_MASK;
    k->get_pir_tir = pnv_get_pir_tir_p10;
    k->intc_create = pnv_chip_power10_intc_create;
    k->intc_reset = pnv_chip_power10_intc_reset;
    k->intc_destroy = pnv_chip_power10_intc_destroy;
    k->intc_print_info = pnv_chip_power10_intc_print_info;
    k->intc_get = pnv_chip_power10_intc_get;
    k->isa_create = pnv_chip_power10_isa_create;
    k->dt_populate = pnv_chip_power10_dt_populate;
    k->pic_print_info = pnv_chip_power10_pic_print_info;
    k->xscom_core_base = pnv_chip_power10_xscom_core_base;
    k->xscom_pcba = pnv_chip_power10_xscom_pcba;
    dc->desc = "PowerNV Chip POWER10";
    k->num_pecs = PNV10_CHIP_MAX_PEC;
    k->i2c_num_engines = PNV10_CHIP_MAX_I2C;
    k->i2c_ports_per_engine = i2c_ports_per_engine;

    device_class_set_parent_realize(dc, pnv_chip_power10_realize,
                                    &k->parent_realize);
}

static uint32_t pnv_chip_power11_xscom_pcba(PnvChip *chip, uint64_t addr)
{
    addr &= (PNV11_XSCOM_SIZE - 1);
    return addr >> 3;
}

static void pnv_chip_power11_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PnvChipClass *k = PNV_CHIP_CLASS(klass);
    static const int i2c_ports_per_engine[PNV10_CHIP_MAX_I2C] = {14, 14, 2, 16};

    k->chip_cfam_id = 0x220da04980000000ull; /* P11 DD2.0 (with NX) */
    k->cores_mask = POWER11_CORE_MASK;
    k->get_pir_tir = pnv_get_pir_tir_p10;
    k->intc_create = pnv_chip_power11_intc_create;
    k->intc_reset = pnv_chip_power11_intc_reset;
    k->intc_destroy = pnv_chip_power11_intc_destroy;
    k->intc_print_info = pnv_chip_power11_intc_print_info;
    k->intc_get = pnv_chip_power11_intc_get;
    k->isa_create = pnv_chip_power11_isa_create;
    k->dt_populate = pnv_chip_power11_dt_populate;
    k->pic_print_info = pnv_chip_power11_pic_print_info;
    k->xscom_core_base = pnv_chip_power11_xscom_core_base;
    k->xscom_pcba = pnv_chip_power11_xscom_pcba;
    dc->desc = "PowerNV Chip Power11";
    k->num_pecs = PNV10_CHIP_MAX_PEC;
    k->i2c_num_engines = PNV10_CHIP_MAX_I2C;
    k->i2c_ports_per_engine = i2c_ports_per_engine;

    device_class_set_parent_realize(dc, pnv_chip_power11_realize,
                                    &k->parent_realize);
}

static void pnv_chip_core_sanitize(PnvMachineState *pnv, PnvChip *chip,
                                   Error **errp)
{
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);
    int cores_max;

    /*
     * No custom mask for this chip, let's use the default one from *
     * the chip class
     */
    if (!chip->cores_mask) {
        chip->cores_mask = pcc->cores_mask;
    }

    /* filter alien core ids ! some are reserved */
    if ((chip->cores_mask & pcc->cores_mask) != chip->cores_mask) {
        error_setg(errp, "warning: invalid core mask for chip Ox%"PRIx64" !",
                   chip->cores_mask);
        return;
    }
    chip->cores_mask &= pcc->cores_mask;

    /* Ensure small-cores a paired up in big-core mode */
    if (pnv->big_core) {
        uint64_t even_cores = chip->cores_mask & 0x5555555555555555ULL;
        uint64_t odd_cores = chip->cores_mask & 0xaaaaaaaaaaaaaaaaULL;

        if (even_cores ^ (odd_cores >> 1)) {
            error_setg(errp, "warning: unpaired cores in big-core mode !");
            return;
        }
    }

    /* now that we have a sane layout, let check the number of cores */
    cores_max = ctpop64(chip->cores_mask);
    if (chip->nr_cores > cores_max) {
        error_setg(errp, "warning: too many cores for chip ! Limit is %d",
                   cores_max);
        return;
    }
}

static void pnv_chip_core_realize(PnvChip *chip, Error **errp)
{
    PnvMachineState *pnv = PNV_MACHINE(qdev_get_machine());
    PnvMachineClass *pmc = PNV_MACHINE_GET_CLASS(pnv);
    Error *error = NULL;
    PnvChipClass *pcc = PNV_CHIP_GET_CLASS(chip);
    const char *typename = pnv_chip_core_typename(chip);
    int i, core_hwid;

    if (!object_class_by_name(typename)) {
        error_setg(errp, "Unable to find PowerNV CPU Core '%s'", typename);
        return;
    }

    /* Cores */
    pnv_chip_core_sanitize(pnv, chip, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }

    chip->cores = g_new0(PnvCore *, chip->nr_cores);

    for (i = 0, core_hwid = 0; (core_hwid < sizeof(chip->cores_mask) * 8)
             && (i < chip->nr_cores); core_hwid++) {
        char core_name[32];
        PnvCore *pnv_core;
        uint64_t xscom_core_base;

        if (!(chip->cores_mask & (1ull << core_hwid))) {
            continue;
        }

        pnv_core = PNV_CORE(object_new(typename));

        snprintf(core_name, sizeof(core_name), "core[%d]", core_hwid);
        object_property_add_child(OBJECT(chip), core_name, OBJECT(pnv_core));
        chip->cores[i] = pnv_core;
        object_property_set_int(OBJECT(pnv_core), "nr-threads",
                                chip->nr_threads, &error_fatal);
        object_property_set_int(OBJECT(pnv_core), CPU_CORE_PROP_CORE_ID,
                                core_hwid, &error_fatal);
        object_property_set_int(OBJECT(pnv_core), "hwid", core_hwid,
                                &error_fatal);
        object_property_set_int(OBJECT(pnv_core), "hrmor", pnv->fw_load_addr,
                                &error_fatal);
        object_property_set_bool(OBJECT(pnv_core), "big-core", chip->big_core,
                                &error_fatal);
        object_property_set_bool(OBJECT(pnv_core), "quirk-tb-big-core",
                                pmc->quirk_tb_big_core, &error_fatal);
        object_property_set_bool(OBJECT(pnv_core), "lpar-per-core",
                                chip->lpar_per_core, &error_fatal);
        object_property_set_link(OBJECT(pnv_core), "chip", OBJECT(chip),
                                 &error_abort);

        qdev_realize(DEVICE(pnv_core), NULL, &error_fatal);

        /* Each core has an XSCOM MMIO region */
        xscom_core_base = pcc->xscom_core_base(chip, core_hwid);

        pnv_xscom_add_subregion(chip, xscom_core_base,
                                &pnv_core->xscom_regs);
        i++;
    }
}

static void pnv_chip_realize(DeviceState *dev, Error **errp)
{
    PnvChip *chip = PNV_CHIP(dev);
    Error *error = NULL;

    /* Cores */
    pnv_chip_core_realize(chip, &error);
    if (error) {
        error_propagate(errp, error);
        return;
    }
}

static const Property pnv_chip_properties[] = {
    DEFINE_PROP_UINT32("chip-id", PnvChip, chip_id, 0),
    DEFINE_PROP_UINT64("ram-start", PnvChip, ram_start, 0),
    DEFINE_PROP_UINT64("ram-size", PnvChip, ram_size, 0),
    DEFINE_PROP_UINT32("nr-cores", PnvChip, nr_cores, 1),
    DEFINE_PROP_UINT64("cores-mask", PnvChip, cores_mask, 0x0),
    DEFINE_PROP_UINT32("nr-threads", PnvChip, nr_threads, 1),
    DEFINE_PROP_BOOL("big-core", PnvChip, big_core, false),
    DEFINE_PROP_BOOL("lpar-per-core", PnvChip, lpar_per_core, false),
};

static void pnv_chip_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_CPU, dc->categories);
    dc->realize = pnv_chip_realize;
    device_class_set_props(dc, pnv_chip_properties);
    dc->desc = "PowerNV Chip";
}

PnvCore *pnv_chip_find_core(PnvChip *chip, uint32_t core_id)
{
    int i;

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pc = chip->cores[i];
        CPUCore *cc = CPU_CORE(pc);

        if (cc->core_id == core_id) {
            return pc;
        }
    }
    return NULL;
}

PowerPCCPU *pnv_chip_find_cpu(PnvChip *chip, uint32_t pir)
{
    int i, j;

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pc = chip->cores[i];
        CPUCore *cc = CPU_CORE(pc);

        for (j = 0; j < cc->nr_threads; j++) {
            if (ppc_cpu_pir(pc->threads[j]) == pir) {
                return pc->threads[j];
            }
        }
    }
    return NULL;
}

static void pnv_chip_foreach_cpu(PnvChip *chip,
                   void (*fn)(PnvChip *chip, PowerPCCPU *cpu, void *opaque),
                   void *opaque)
{
    int i, j;

    for (i = 0; i < chip->nr_cores; i++) {
        PnvCore *pc = chip->cores[i];

        for (j = 0; j < CPU_CORE(pc)->nr_threads; j++) {
            fn(chip, pc->threads[j], opaque);
        }
    }
}

static ICSState *pnv_ics_get(XICSFabric *xi, int irq)
{
    PnvMachineState *pnv = PNV_MACHINE(xi);
    int i, j;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv8Chip *chip8 = PNV8_CHIP(pnv->chips[i]);

        if (ics_valid_irq(&chip8->psi.ics, irq)) {
            return &chip8->psi.ics;
        }

        for (j = 0; j < chip8->num_phbs; j++) {
            PnvPHB *phb = chip8->phbs[j];
            PnvPHB3 *phb3 = PNV_PHB3(phb->backend);

            if (ics_valid_irq(&phb3->lsis, irq)) {
                return &phb3->lsis;
            }

            if (ics_valid_irq(ICS(&phb3->msis), irq)) {
                return ICS(&phb3->msis);
            }
        }
    }
    return NULL;
}

PnvChip *pnv_get_chip(PnvMachineState *pnv, uint32_t chip_id)
{
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        PnvChip *chip = pnv->chips[i];
        if (chip->chip_id == chip_id) {
            return chip;
        }
    }
    return NULL;
}

static void pnv_ics_resend(XICSFabric *xi)
{
    PnvMachineState *pnv = PNV_MACHINE(xi);
    int i, j;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv8Chip *chip8 = PNV8_CHIP(pnv->chips[i]);

        ics_resend(&chip8->psi.ics);

        for (j = 0; j < chip8->num_phbs; j++) {
            PnvPHB *phb = chip8->phbs[j];
            PnvPHB3 *phb3 = PNV_PHB3(phb->backend);

            ics_resend(&phb3->lsis);
            ics_resend(ICS(&phb3->msis));
        }
    }
}

static ICPState *pnv_icp_get(XICSFabric *xi, int pir)
{
    PowerPCCPU *cpu = ppc_get_vcpu_by_pir(pir);

    return cpu ? ICP(pnv_cpu_state(cpu)->intc) : NULL;
}

static void pnv_pic_intc_print_info(PnvChip *chip, PowerPCCPU *cpu,
                                    void *opaque)
{
    PNV_CHIP_GET_CLASS(chip)->intc_print_info(chip, cpu, opaque);
}

static void pnv_pic_print_info(InterruptStatsProvider *obj, GString *buf)
{
    PnvMachineState *pnv = PNV_MACHINE(obj);
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        PnvChip *chip = pnv->chips[i];

        /* First CPU presenters */
        pnv_chip_foreach_cpu(chip, pnv_pic_intc_print_info, buf);

        /* Then other devices, PHB, PSI, XIVE */
        PNV_CHIP_GET_CLASS(chip)->pic_print_info(chip, buf);
    }
}

static bool pnv_match_nvt(XiveFabric *xfb, uint8_t format,
                          uint8_t nvt_blk, uint32_t nvt_idx,
                          bool crowd, bool cam_ignore, uint8_t priority,
                          uint32_t logic_serv,
                          XiveTCTXMatch *match)
{
    PnvMachineState *pnv = PNV_MACHINE(xfb);
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv9Chip *chip9 = PNV9_CHIP(pnv->chips[i]);
        XivePresenter *xptr = XIVE_PRESENTER(&chip9->xive);
        XivePresenterClass *xpc = XIVE_PRESENTER_GET_CLASS(xptr);

        xpc->match_nvt(xptr, format, nvt_blk, nvt_idx, crowd,
                       cam_ignore, priority, logic_serv, match);
    }

    return !!match->count;
}

static bool pnv10_xive_match_nvt(XiveFabric *xfb, uint8_t format,
                                 uint8_t nvt_blk, uint32_t nvt_idx,
                                 bool crowd, bool cam_ignore, uint8_t priority,
                                 uint32_t logic_serv,
                                 XiveTCTXMatch *match)
{
    PnvMachineState *pnv = PNV_MACHINE(xfb);
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv10Chip *chip10 = PNV10_CHIP(pnv->chips[i]);
        XivePresenter *xptr = XIVE_PRESENTER(&chip10->xive);
        XivePresenterClass *xpc = XIVE_PRESENTER_GET_CLASS(xptr);

        xpc->match_nvt(xptr, format, nvt_blk, nvt_idx, crowd,
                       cam_ignore, priority, logic_serv, match);
    }

    return !!match->count;
}

static int pnv10_xive_broadcast(XiveFabric *xfb,
                                uint8_t nvt_blk, uint32_t nvt_idx,
                                bool crowd, bool cam_ignore,
                                uint8_t priority)
{
    PnvMachineState *pnv = PNV_MACHINE(xfb);
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv10Chip *chip10 = PNV10_CHIP(pnv->chips[i]);
        XivePresenter *xptr = XIVE_PRESENTER(&chip10->xive);
        XivePresenterClass *xpc = XIVE_PRESENTER_GET_CLASS(xptr);

        xpc->broadcast(xptr, nvt_blk, nvt_idx, crowd, cam_ignore, priority);
    }
    return 0;
}

static bool pnv11_xive_match_nvt(XiveFabric *xfb, uint8_t format,
                                 uint8_t nvt_blk, uint32_t nvt_idx,
                                 bool crowd, bool cam_ignore, uint8_t priority,
                                 uint32_t logic_serv,
                                 XiveTCTXMatch *match)
{
    PnvMachineState *pnv = PNV_MACHINE(xfb);
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv11Chip *chip11 = PNV11_CHIP(pnv->chips[i]);
        XivePresenter *xptr = XIVE_PRESENTER(&chip11->xive);
        XivePresenterClass *xpc = XIVE_PRESENTER_GET_CLASS(xptr);

        xpc->match_nvt(xptr, format, nvt_blk, nvt_idx, crowd,
                       cam_ignore, priority, logic_serv, match);
    }

    return !!match->count;
}

static int pnv11_xive_broadcast(XiveFabric *xfb,
                                uint8_t nvt_blk, uint32_t nvt_idx,
                                bool crowd, bool cam_ignore,
                                uint8_t priority)
{
    PnvMachineState *pnv = PNV_MACHINE(xfb);
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        Pnv11Chip *chip11 = PNV11_CHIP(pnv->chips[i]);
        XivePresenter *xptr = XIVE_PRESENTER(&chip11->xive);
        XivePresenterClass *xpc = XIVE_PRESENTER_GET_CLASS(xptr);

        xpc->broadcast(xptr, nvt_blk, nvt_idx, crowd, cam_ignore, priority);
    }
    return 0;
}

static bool pnv_machine_get_big_core(Object *obj, Error **errp)
{
    PnvMachineState *pnv = PNV_MACHINE(obj);
    return pnv->big_core;
}

static void pnv_machine_set_big_core(Object *obj, bool value, Error **errp)
{
    PnvMachineState *pnv = PNV_MACHINE(obj);
    pnv->big_core = value;
}

static bool pnv_machine_get_lpar_per_core(Object *obj, Error **errp)
{
    PnvMachineState *pnv = PNV_MACHINE(obj);
    return pnv->lpar_per_core;
}

static void pnv_machine_set_lpar_per_core(Object *obj, bool value, Error **errp)
{
    PnvMachineState *pnv = PNV_MACHINE(obj);
    pnv->lpar_per_core = value;
}

static bool pnv_machine_get_hb(Object *obj, Error **errp)
{
    PnvMachineState *pnv = PNV_MACHINE(obj);

    return !!pnv->fw_load_addr;
}

static void pnv_machine_set_hb(Object *obj, bool value, Error **errp)
{
    PnvMachineState *pnv = PNV_MACHINE(obj);

    if (value) {
        pnv->fw_load_addr = 0x8000000;
    }
}

static void pnv_machine_power8_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    XICSFabricClass *xic = XICS_FABRIC_CLASS(oc);
    PnvMachineClass *pmc = PNV_MACHINE_CLASS(oc);
    static const char compat[] = "qemu,powernv8\0qemu,powernv\0ibm,powernv";

    static GlobalProperty phb_compat[] = {
        { TYPE_PNV_PHB, "version", "3" },
        { TYPE_PNV_PHB_ROOT_PORT, "version", "3" },
    };

    mc->desc = "IBM PowerNV (Non-Virtualized) POWER8";
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("power8_v2.0");
    compat_props_add(mc->compat_props, phb_compat, G_N_ELEMENTS(phb_compat));

    xic->icp_get = pnv_icp_get;
    xic->ics_get = pnv_ics_get;
    xic->ics_resend = pnv_ics_resend;

    pmc->compat = compat;
    pmc->compat_size = sizeof(compat);
    pmc->max_smt_threads = 8;
    /* POWER8 is always lpar-per-core mode */
    pmc->has_lpar_per_thread = false;

    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_PNV_PHB);
}

static void pnv_machine_power9_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    XiveFabricClass *xfc = XIVE_FABRIC_CLASS(oc);
    PnvMachineClass *pmc = PNV_MACHINE_CLASS(oc);
    static const char compat[] = "qemu,powernv9\0ibm,powernv";

    static GlobalProperty phb_compat[] = {
        { TYPE_PNV_PHB, "version", "4" },
        { TYPE_PNV_PHB_ROOT_PORT, "version", "4" },
    };

    mc->desc = "IBM PowerNV (Non-Virtualized) POWER9";
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("power9_v2.2");
    compat_props_add(mc->compat_props, phb_compat, G_N_ELEMENTS(phb_compat));

    xfc->match_nvt = pnv_match_nvt;

    pmc->compat = compat;
    pmc->compat_size = sizeof(compat);
    pmc->max_smt_threads = 4;
    pmc->has_lpar_per_thread = true;
    pmc->dt_power_mgt = pnv_dt_power_mgt;

    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_PNV_PHB);

    object_class_property_add_bool(oc, "big-core",
                                   pnv_machine_get_big_core,
                                   pnv_machine_set_big_core);
    object_class_property_set_description(oc, "big-core",
                              "Use big-core (aka fused-core) mode");

    object_class_property_add_bool(oc, "lpar-per-core",
                                   pnv_machine_get_lpar_per_core,
                                   pnv_machine_set_lpar_per_core);
    object_class_property_set_description(oc, "lpar-per-core",
                              "Use 1 LPAR per core mode");
}

static void pnv_machine_p10_common_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    PnvMachineClass *pmc = PNV_MACHINE_CLASS(oc);
    XiveFabricClass *xfc = XIVE_FABRIC_CLASS(oc);
    static const char compat[] = "qemu,powernv10\0ibm,powernv";

    static GlobalProperty phb_compat[] = {
        { TYPE_PNV_PHB, "version", "5" },
        { TYPE_PNV_PHB_ROOT_PORT, "version", "5" },
    };

    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("power10_v2.0");
    compat_props_add(mc->compat_props, phb_compat, G_N_ELEMENTS(phb_compat));

    mc->alias = "powernv";

    pmc->compat = compat;
    pmc->compat_size = sizeof(compat);
    pmc->max_smt_threads = 4;
    pmc->has_lpar_per_thread = true;
    pmc->quirk_tb_big_core = true;
    pmc->dt_power_mgt = pnv_dt_power_mgt;

    xfc->match_nvt = pnv10_xive_match_nvt;
    xfc->broadcast = pnv10_xive_broadcast;

    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_PNV_PHB);
}

static void pnv_machine_power10_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    pnv_machine_p10_common_class_init(oc, data);
    mc->desc = "IBM PowerNV (Non-Virtualized) POWER10";

    /*
     * This is the parent of POWER10 Rainier class, so properies go here
     * rather than common init (which would add them to both parent and
     * child which is invalid).
     */
    object_class_property_add_bool(oc, "big-core",
                                   pnv_machine_get_big_core,
                                   pnv_machine_set_big_core);
    object_class_property_set_description(oc, "big-core",
                              "Use big-core (aka fused-core) mode");

    object_class_property_add_bool(oc, "lpar-per-core",
                                   pnv_machine_get_lpar_per_core,
                                   pnv_machine_set_lpar_per_core);
    object_class_property_set_description(oc, "lpar-per-core",
                              "Use 1 LPAR per core mode");
}

static void pnv_machine_p10_rainier_class_init(ObjectClass *oc,
                                               const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    PnvMachineClass *pmc = PNV_MACHINE_CLASS(oc);

    pnv_machine_p10_common_class_init(oc, data);
    mc->desc = "IBM PowerNV (Non-Virtualized) POWER10 Rainier";
    pmc->i2c_init = pnv_rainier_i2c_init;
}

static void pnv_machine_power11_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    PnvMachineClass *pmc = PNV_MACHINE_CLASS(oc);
    XiveFabricClass *xfc = XIVE_FABRIC_CLASS(oc);
    static const char compat[] = "qemu,powernv11\0ibm,powernv";

    static GlobalProperty phb_compat[] = {
        { TYPE_PNV_PHB, "version", "5" },
        { TYPE_PNV_PHB_ROOT_PORT, "version", "5" },
    };

    compat_props_add(mc->compat_props, phb_compat, G_N_ELEMENTS(phb_compat));

    pmc->compat = compat;
    pmc->compat_size = sizeof(compat);
    pmc->max_smt_threads = 4;
    pmc->has_lpar_per_thread = true;
    pmc->quirk_tb_big_core = true;
    pmc->dt_power_mgt = pnv_dt_power_mgt;

    xfc->match_nvt = pnv11_xive_match_nvt;
    xfc->broadcast = pnv11_xive_broadcast;

    mc->desc = "IBM PowerNV (Non-Virtualized) Power11";
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("power11_v2.0");

    object_class_property_add_bool(oc, "big-core",
                                   pnv_machine_get_big_core,
                                   pnv_machine_set_big_core);
    object_class_property_set_description(oc, "big-core",
                              "Use big-core (aka fused-core) mode");

    object_class_property_add_bool(oc, "lpar-per-core",
                                   pnv_machine_get_lpar_per_core,
                                   pnv_machine_set_lpar_per_core);
    object_class_property_set_description(oc, "lpar-per-core",
                              "Use 1 LPAR per core mode");
}

static void pnv_cpu_do_nmi_on_cpu(CPUState *cs, run_on_cpu_data arg)
{
    CPUPPCState *env = cpu_env(cs);

    cpu_synchronize_state(cs);
    ppc_cpu_do_system_reset(cs);
    if (env->spr[SPR_SRR1] & SRR1_WAKESTATE) {
        /*
         * Power-save wakeups, as indicated by non-zero SRR1[46:47] put the
         * wakeup reason in SRR1[42:45], system reset is indicated with 0b0100
         * (PPC_BIT(43)).
         */
        if (!(env->spr[SPR_SRR1] & SRR1_WAKERESET)) {
            warn_report("ppc_cpu_do_system_reset does not set system reset wakeup reason");
            env->spr[SPR_SRR1] |= SRR1_WAKERESET;
        }
    } else {
        /*
         * For non-powersave system resets, SRR1[42:45] are defined to be
         * implementation-dependent. The POWER9 User Manual specifies that
         * an external (SCOM driven, which may come from a BMC nmi command or
         * another CPU requesting a NMI IPI) system reset exception should be
         * 0b0010 (PPC_BIT(44)).
         */
        env->spr[SPR_SRR1] |= SRR1_WAKESCOM;
    }
    if (arg.host_int == 1) {
        cpu_resume(cs);
    }
}

/*
 * Send a SRESET (NMI) interrupt to the CPU, and resume execution if it was
 * paused.
 */
void pnv_cpu_do_nmi_resume(CPUState *cs)
{
    async_run_on_cpu(cs, pnv_cpu_do_nmi_on_cpu, RUN_ON_CPU_HOST_INT(1));
}

static void pnv_cpu_do_nmi(PnvChip *chip, PowerPCCPU *cpu, void *opaque)
{
    async_run_on_cpu(CPU(cpu), pnv_cpu_do_nmi_on_cpu, RUN_ON_CPU_HOST_INT(0));
}

static void pnv_nmi(NMIState *n, int cpu_index, Error **errp)
{
    PnvMachineState *pnv = PNV_MACHINE(qdev_get_machine());
    int i;

    for (i = 0; i < pnv->num_chips; i++) {
        pnv_chip_foreach_cpu(pnv->chips[i], pnv_cpu_do_nmi, NULL);
    }
}

static void pnv_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    InterruptStatsProviderClass *ispc = INTERRUPT_STATS_PROVIDER_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);

    mc->desc = "IBM PowerNV (Non-Virtualized)";
    mc->init = pnv_init;
    mc->reset = pnv_reset;
    mc->max_cpus = MAX_CPUS;
    /* Pnv provides a AHCI device for storage */
    mc->block_default_type = IF_IDE;
    mc->no_parallel = 1;
    mc->default_boot_order = NULL;
    /*
     * RAM defaults to less than 2048 for 32-bit hosts, and large
     * enough to fit the maximum initrd size at it's load address
     */
    mc->default_ram_size = 1 * GiB;
    mc->default_ram_id = "pnv.ram";
    ispc->print_info = pnv_pic_print_info;
    nc->nmi_monitor_handler = pnv_nmi;

    object_class_property_add_bool(oc, "hb-mode",
                                   pnv_machine_get_hb, pnv_machine_set_hb);
    object_class_property_set_description(oc, "hb-mode",
                              "Use a hostboot like boot loader");
}

#define DEFINE_PNV8_CHIP_TYPE(type, class_initfn) \
    {                                             \
        .name          = type,                    \
        .class_init    = class_initfn,            \
        .parent        = TYPE_PNV8_CHIP,          \
    }

#define DEFINE_PNV9_CHIP_TYPE(type, class_initfn) \
    {                                             \
        .name          = type,                    \
        .class_init    = class_initfn,            \
        .parent        = TYPE_PNV9_CHIP,          \
    }

#define DEFINE_PNV10_CHIP_TYPE(type, class_initfn) \
    {                                              \
        .name          = type,                     \
        .class_init    = class_initfn,             \
        .parent        = TYPE_PNV10_CHIP,          \
    }

#define DEFINE_PNV11_CHIP_TYPE(type, class_initfn) \
    {                                              \
        .name          = type,                     \
        .class_init    = class_initfn,             \
        .parent        = TYPE_PNV11_CHIP,          \
    }

static const TypeInfo types[] = {
    {
        .name          = MACHINE_TYPE_NAME("powernv11"),
        .parent        = TYPE_PNV_MACHINE,
        .class_init    = pnv_machine_power11_class_init,
        .interfaces = (InterfaceInfo[]) {
            { TYPE_XIVE_FABRIC },
            { },
        },
    },
    {
        .name          = MACHINE_TYPE_NAME("powernv10-rainier"),
        .parent        = MACHINE_TYPE_NAME("powernv10"),
        .class_init    = pnv_machine_p10_rainier_class_init,
    },
    {
        .name          = MACHINE_TYPE_NAME("powernv10"),
        .parent        = TYPE_PNV_MACHINE,
        .class_init    = pnv_machine_power10_class_init,
        .interfaces = (const InterfaceInfo[]) {
            { TYPE_XIVE_FABRIC },
            { },
        },
    },
    {
        .name          = MACHINE_TYPE_NAME("powernv9"),
        .parent        = TYPE_PNV_MACHINE,
        .class_init    = pnv_machine_power9_class_init,
        .interfaces = (const InterfaceInfo[]) {
            { TYPE_XIVE_FABRIC },
            { },
        },
    },
    {
        .name          = MACHINE_TYPE_NAME("powernv8"),
        .parent        = TYPE_PNV_MACHINE,
        .class_init    = pnv_machine_power8_class_init,
        .interfaces = (const InterfaceInfo[]) {
            { TYPE_XICS_FABRIC },
            { },
        },
    },
    {
        .name          = TYPE_PNV_MACHINE,
        .parent        = TYPE_MACHINE,
        .abstract       = true,
        .instance_size = sizeof(PnvMachineState),
        .class_init    = pnv_machine_class_init,
        .class_size    = sizeof(PnvMachineClass),
        .interfaces = (const InterfaceInfo[]) {
            { TYPE_INTERRUPT_STATS_PROVIDER },
            { TYPE_NMI },
            { },
        },
    },
    {
        .name          = TYPE_PNV_CHIP,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .class_init    = pnv_chip_class_init,
        .instance_size = sizeof(PnvChip),
        .class_size    = sizeof(PnvChipClass),
        .abstract      = true,
    },

    /*
     * P11 chip and variants
     */
    {
        .name          = TYPE_PNV11_CHIP,
        .parent        = TYPE_PNV_CHIP,
        .instance_init = pnv_chip_power11_instance_init,
        .instance_size = sizeof(Pnv11Chip),
    },
    DEFINE_PNV11_CHIP_TYPE(TYPE_PNV_CHIP_POWER11, pnv_chip_power11_class_init),

    /*
     * P10 chip and variants
     */
    {
        .name          = TYPE_PNV10_CHIP,
        .parent        = TYPE_PNV_CHIP,
        .instance_init = pnv_chip_power10_instance_init,
        .instance_size = sizeof(Pnv10Chip),
    },
    DEFINE_PNV10_CHIP_TYPE(TYPE_PNV_CHIP_POWER10, pnv_chip_power10_class_init),

    /*
     * P9 chip and variants
     */
    {
        .name          = TYPE_PNV9_CHIP,
        .parent        = TYPE_PNV_CHIP,
        .instance_init = pnv_chip_power9_instance_init,
        .instance_size = sizeof(Pnv9Chip),
    },
    DEFINE_PNV9_CHIP_TYPE(TYPE_PNV_CHIP_POWER9, pnv_chip_power9_class_init),

    /*
     * P8 chip and variants
     */
    {
        .name          = TYPE_PNV8_CHIP,
        .parent        = TYPE_PNV_CHIP,
        .instance_init = pnv_chip_power8_instance_init,
        .instance_size = sizeof(Pnv8Chip),
    },
    DEFINE_PNV8_CHIP_TYPE(TYPE_PNV_CHIP_POWER8, pnv_chip_power8_class_init),
    DEFINE_PNV8_CHIP_TYPE(TYPE_PNV_CHIP_POWER8E, pnv_chip_power8e_class_init),
    DEFINE_PNV8_CHIP_TYPE(TYPE_PNV_CHIP_POWER8NVL,
                          pnv_chip_power8nvl_class_init),
};

DEFINE_TYPES(types)
