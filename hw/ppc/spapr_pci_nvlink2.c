/*
 * QEMU sPAPR PCI for NVLink2 pass through
 *
 * Copyright (c) 2019 Alexey Kardashevskiy, IBM Corporation.
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
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/spapr.h"
#include "qemu/error-report.h"
#include "hw/ppc/fdt.h"
#include "hw/pci/pci_bridge.h"

#define PHANDLE_PCIDEV(phb, pdev)    (0x12000000 | \
                                     (((phb)->index) << 16) | ((pdev)->devfn))
#define PHANDLE_GPURAM(phb, n)       (0x110000FF | ((n) << 8) | \
                                     (((phb)->index) << 16))
#define PHANDLE_NVLINK(phb, gn, nn)  (0x00130000 | (((phb)->index) << 8) | \
                                     ((gn) << 4) | (nn))

#define SPAPR_GPU_NUMA_ID           (cpu_to_be32(1))

typedef struct SpaprPhbPciNvGpuSlot {
        uint64_t tgt;
        uint64_t gpa;
        unsigned numa_id;
        PCIDevice *gpdev;
        int linknum;
        struct {
            uint64_t atsd_gpa;
            PCIDevice *npdev;
            uint32_t link_speed;
        } links[NVGPU_MAX_LINKS];
} SpaprPhbPciNvGpuSlot;

struct SpaprPhbPciNvGpuConfig {
    uint64_t nv2_ram_current;
    uint64_t nv2_atsd_current;
    int num; /* number of non empty (i.e. tgt!=0) entries in slots[] */
    SpaprPhbPciNvGpuSlot slots[NVGPU_MAX_NUM];
    Error *errp;
};

static SpaprPhbPciNvGpuSlot *
spapr_nvgpu_get_slot(SpaprPhbPciNvGpuConfig *nvgpus, uint64_t tgt)
{
    int i;

    /* Search for partially collected "slot" */
    for (i = 0; i < nvgpus->num; ++i) {
        if (nvgpus->slots[i].tgt == tgt) {
            return &nvgpus->slots[i];
        }
    }

    if (nvgpus->num == ARRAY_SIZE(nvgpus->slots)) {
        return NULL;
    }

    i = nvgpus->num;
    nvgpus->slots[i].tgt = tgt;
    ++nvgpus->num;

    return &nvgpus->slots[i];
}

static void spapr_pci_collect_nvgpu(SpaprPhbPciNvGpuConfig *nvgpus,
                                    PCIDevice *pdev, uint64_t tgt,
                                    MemoryRegion *mr, Error **errp)
{
    MachineState *machine = MACHINE(qdev_get_machine());
    SpaprMachineState *spapr = SPAPR_MACHINE(machine);
    SpaprPhbPciNvGpuSlot *nvslot = spapr_nvgpu_get_slot(nvgpus, tgt);

    if (!nvslot) {
        error_setg(errp, "Found too many GPUs per vPHB");
        return;
    }
    g_assert(!nvslot->gpdev);
    nvslot->gpdev = pdev;

    nvslot->gpa = nvgpus->nv2_ram_current;
    nvgpus->nv2_ram_current += memory_region_size(mr);
    nvslot->numa_id = spapr->gpu_numa_id;
    ++spapr->gpu_numa_id;
}

static void spapr_pci_collect_nvnpu(SpaprPhbPciNvGpuConfig *nvgpus,
                                    PCIDevice *pdev, uint64_t tgt,
                                    MemoryRegion *mr, Error **errp)
{
    SpaprPhbPciNvGpuSlot *nvslot = spapr_nvgpu_get_slot(nvgpus, tgt);
    int j;

    if (!nvslot) {
        error_setg(errp, "Found too many NVLink bridges per vPHB");
        return;
    }

    j = nvslot->linknum;
    if (j == ARRAY_SIZE(nvslot->links)) {
        error_setg(errp, "Found too many NVLink bridges per GPU");
        return;
    }
    ++nvslot->linknum;

    g_assert(!nvslot->links[j].npdev);
    nvslot->links[j].npdev = pdev;
    nvslot->links[j].atsd_gpa = nvgpus->nv2_atsd_current;
    nvgpus->nv2_atsd_current += memory_region_size(mr);
    nvslot->links[j].link_speed =
        object_property_get_uint(OBJECT(pdev), "nvlink2-link-speed", NULL);
}

static void spapr_phb_pci_collect_nvgpu(PCIBus *bus, PCIDevice *pdev,
                                        void *opaque)
{
    PCIBus *sec_bus;
    Object *po = OBJECT(pdev);
    uint64_t tgt = object_property_get_uint(po, "nvlink2-tgt", NULL);

    if (tgt) {
        Error *local_err = NULL;
        SpaprPhbPciNvGpuConfig *nvgpus = opaque;
        Object *mr_gpu = object_property_get_link(po, "nvlink2-mr[0]", NULL);
        Object *mr_npu = object_property_get_link(po, "nvlink2-atsd-mr[0]",
                                                  NULL);

        g_assert(mr_gpu || mr_npu);
        if (mr_gpu) {
            spapr_pci_collect_nvgpu(nvgpus, pdev, tgt, MEMORY_REGION(mr_gpu),
                                    &local_err);
        } else {
            spapr_pci_collect_nvnpu(nvgpus, pdev, tgt, MEMORY_REGION(mr_npu),
                                    &local_err);
        }
        error_propagate(&nvgpus->errp, local_err);
    }
    if ((pci_default_read_config(pdev, PCI_HEADER_TYPE, 1) !=
         PCI_HEADER_TYPE_BRIDGE)) {
        return;
    }

    sec_bus = pci_bridge_get_sec_bus(PCI_BRIDGE(pdev));
    if (!sec_bus) {
        return;
    }

    pci_for_each_device(sec_bus, pci_bus_num(sec_bus),
                        spapr_phb_pci_collect_nvgpu, opaque);
}

void spapr_phb_nvgpu_setup(SpaprPhbState *sphb, Error **errp)
{
    int i, j, valid_gpu_num;
    PCIBus *bus;

    /* Search for GPUs and NPUs */
    if (!sphb->nv2_gpa_win_addr || !sphb->nv2_atsd_win_addr) {
        return;
    }

    sphb->nvgpus = g_new0(SpaprPhbPciNvGpuConfig, 1);
    sphb->nvgpus->nv2_ram_current = sphb->nv2_gpa_win_addr;
    sphb->nvgpus->nv2_atsd_current = sphb->nv2_atsd_win_addr;

    bus = PCI_HOST_BRIDGE(sphb)->bus;
    pci_for_each_device(bus, pci_bus_num(bus),
                        spapr_phb_pci_collect_nvgpu, sphb->nvgpus);

    if (sphb->nvgpus->errp) {
        error_propagate(errp, sphb->nvgpus->errp);
        sphb->nvgpus->errp = NULL;
        goto cleanup_exit;
    }

    /* Add found GPU RAM and ATSD MRs if found */
    for (i = 0, valid_gpu_num = 0; i < sphb->nvgpus->num; ++i) {
        Object *nvmrobj;
        SpaprPhbPciNvGpuSlot *nvslot = &sphb->nvgpus->slots[i];

        if (!nvslot->gpdev) {
            continue;
        }
        nvmrobj = object_property_get_link(OBJECT(nvslot->gpdev),
                                           "nvlink2-mr[0]", NULL);
        /* ATSD is pointless without GPU RAM MR so skip those */
        if (!nvmrobj) {
            continue;
        }

        ++valid_gpu_num;
        memory_region_add_subregion(get_system_memory(), nvslot->gpa,
                                    MEMORY_REGION(nvmrobj));

        for (j = 0; j < nvslot->linknum; ++j) {
            Object *atsdmrobj;

            atsdmrobj = object_property_get_link(OBJECT(nvslot->links[j].npdev),
                                                 "nvlink2-atsd-mr[0]", NULL);
            if (!atsdmrobj) {
                continue;
            }
            memory_region_add_subregion(get_system_memory(),
                                        nvslot->links[j].atsd_gpa,
                                        MEMORY_REGION(atsdmrobj));
        }
    }

    if (valid_gpu_num) {
        return;
    }
    /* We did not find any interesting GPU */
cleanup_exit:
    g_free(sphb->nvgpus);
    sphb->nvgpus = NULL;
}

void spapr_phb_nvgpu_free(SpaprPhbState *sphb)
{
    int i, j;

    if (!sphb->nvgpus) {
        return;
    }

    for (i = 0; i < sphb->nvgpus->num; ++i) {
        SpaprPhbPciNvGpuSlot *nvslot = &sphb->nvgpus->slots[i];
        Object *nv_mrobj = object_property_get_link(OBJECT(nvslot->gpdev),
                                                    "nvlink2-mr[0]", NULL);

        if (nv_mrobj) {
            memory_region_del_subregion(get_system_memory(),
                                        MEMORY_REGION(nv_mrobj));
        }
        for (j = 0; j < nvslot->linknum; ++j) {
            PCIDevice *npdev = nvslot->links[j].npdev;
            Object *atsd_mrobj;
            atsd_mrobj = object_property_get_link(OBJECT(npdev),
                                                  "nvlink2-atsd-mr[0]", NULL);
            if (atsd_mrobj) {
                memory_region_del_subregion(get_system_memory(),
                                            MEMORY_REGION(atsd_mrobj));
            }
        }
    }
    g_free(sphb->nvgpus);
    sphb->nvgpus = NULL;
}

void spapr_phb_nvgpu_populate_dt(SpaprPhbState *sphb, void *fdt, int bus_off,
                                 Error **errp)
{
    int i, j, atsdnum = 0;
    uint64_t atsd[8]; /* The existing limitation of known guests */

    if (!sphb->nvgpus) {
        return;
    }

    for (i = 0; (i < sphb->nvgpus->num) && (atsdnum < ARRAY_SIZE(atsd)); ++i) {
        SpaprPhbPciNvGpuSlot *nvslot = &sphb->nvgpus->slots[i];

        if (!nvslot->gpdev) {
            continue;
        }
        for (j = 0; j < nvslot->linknum; ++j) {
            if (!nvslot->links[j].atsd_gpa) {
                continue;
            }

            if (atsdnum == ARRAY_SIZE(atsd)) {
                error_report("Only %"PRIuPTR" ATSD registers supported",
                             ARRAY_SIZE(atsd));
                break;
            }
            atsd[atsdnum] = cpu_to_be64(nvslot->links[j].atsd_gpa);
            ++atsdnum;
        }
    }

    if (!atsdnum) {
        error_setg(errp, "No ATSD registers found");
        return;
    }

    if (!spapr_phb_eeh_available(sphb)) {
        /*
         * ibm,mmio-atsd contains ATSD registers; these belong to an NPU PHB
         * which we do not emulate as a separate device. Instead we put
         * ibm,mmio-atsd to the vPHB with GPU and make sure that we do not
         * put GPUs from different IOMMU groups to the same vPHB to ensure
         * that the guest will use ATSDs from the corresponding NPU.
         */
        error_setg(errp, "ATSD requires separate vPHB per GPU IOMMU group");
        return;
    }

    _FDT((fdt_setprop(fdt, bus_off, "ibm,mmio-atsd", atsd,
                      atsdnum * sizeof(atsd[0]))));
}

void spapr_phb_nvgpu_ram_populate_dt(SpaprPhbState *sphb, void *fdt)
{
    int i, j, linkidx, npuoff;
    char *npuname;

    if (!sphb->nvgpus) {
        return;
    }

    npuname = g_strdup_printf("npuphb%d", sphb->index);
    npuoff = fdt_add_subnode(fdt, 0, npuname);
    _FDT(npuoff);
    _FDT(fdt_setprop_cell(fdt, npuoff, "#address-cells", 1));
    _FDT(fdt_setprop_cell(fdt, npuoff, "#size-cells", 0));
    /* Advertise NPU as POWER9 so the guest can enable NPU2 contexts */
    _FDT((fdt_setprop_string(fdt, npuoff, "compatible", "ibm,power9-npu")));
    g_free(npuname);

    for (i = 0, linkidx = 0; i < sphb->nvgpus->num; ++i) {
        for (j = 0; j < sphb->nvgpus->slots[i].linknum; ++j) {
            char *linkname = g_strdup_printf("link@%d", linkidx);
            int off = fdt_add_subnode(fdt, npuoff, linkname);

            _FDT(off);
            /* _FDT((fdt_setprop_cell(fdt, off, "reg", linkidx))); */
            _FDT((fdt_setprop_string(fdt, off, "compatible",
                                     "ibm,npu-link")));
            _FDT((fdt_setprop_cell(fdt, off, "phandle",
                                   PHANDLE_NVLINK(sphb, i, j))));
            _FDT((fdt_setprop_cell(fdt, off, "ibm,npu-link-index", linkidx)));
            g_free(linkname);
            ++linkidx;
        }
    }

    /* Add memory nodes for GPU RAM and mark them unusable */
    for (i = 0; i < sphb->nvgpus->num; ++i) {
        SpaprPhbPciNvGpuSlot *nvslot = &sphb->nvgpus->slots[i];
        Object *nv_mrobj = object_property_get_link(OBJECT(nvslot->gpdev),
                                                    "nvlink2-mr[0]", NULL);
        uint32_t associativity[] = {
            cpu_to_be32(0x4),
            SPAPR_GPU_NUMA_ID,
            SPAPR_GPU_NUMA_ID,
            SPAPR_GPU_NUMA_ID,
            cpu_to_be32(nvslot->numa_id)
        };
        uint64_t size = object_property_get_uint(nv_mrobj, "size", NULL);
        uint64_t mem_reg[2] = { cpu_to_be64(nvslot->gpa), cpu_to_be64(size) };
        char *mem_name = g_strdup_printf("memory@%"PRIx64, nvslot->gpa);
        int off = fdt_add_subnode(fdt, 0, mem_name);

        _FDT(off);
        _FDT((fdt_setprop_string(fdt, off, "device_type", "memory")));
        _FDT((fdt_setprop(fdt, off, "reg", mem_reg, sizeof(mem_reg))));
        _FDT((fdt_setprop(fdt, off, "ibm,associativity", associativity,
                          sizeof(associativity))));

        _FDT((fdt_setprop_string(fdt, off, "compatible",
                                 "ibm,coherent-device-memory")));

        mem_reg[1] = cpu_to_be64(0);
        _FDT((fdt_setprop(fdt, off, "linux,usable-memory", mem_reg,
                          sizeof(mem_reg))));
        _FDT((fdt_setprop_cell(fdt, off, "phandle",
                               PHANDLE_GPURAM(sphb, i))));
        g_free(mem_name);
    }

}

void spapr_phb_nvgpu_populate_pcidev_dt(PCIDevice *dev, void *fdt, int offset,
                                        SpaprPhbState *sphb)
{
    int i, j;

    if (!sphb->nvgpus) {
        return;
    }

    for (i = 0; i < sphb->nvgpus->num; ++i) {
        SpaprPhbPciNvGpuSlot *nvslot = &sphb->nvgpus->slots[i];

        /* Skip "slot" without attached GPU */
        if (!nvslot->gpdev) {
            continue;
        }
        if (dev == nvslot->gpdev) {
            uint32_t npus[nvslot->linknum];

            for (j = 0; j < nvslot->linknum; ++j) {
                PCIDevice *npdev = nvslot->links[j].npdev;

                npus[j] = cpu_to_be32(PHANDLE_PCIDEV(sphb, npdev));
            }
            _FDT(fdt_setprop(fdt, offset, "ibm,npu", npus,
                             j * sizeof(npus[0])));
            _FDT((fdt_setprop_cell(fdt, offset, "phandle",
                                   PHANDLE_PCIDEV(sphb, dev))));
            continue;
        }

        for (j = 0; j < nvslot->linknum; ++j) {
            if (dev != nvslot->links[j].npdev) {
                continue;
            }

            _FDT((fdt_setprop_cell(fdt, offset, "phandle",
                                   PHANDLE_PCIDEV(sphb, dev))));
            _FDT(fdt_setprop_cell(fdt, offset, "ibm,gpu",
                                  PHANDLE_PCIDEV(sphb, nvslot->gpdev)));
            _FDT((fdt_setprop_cell(fdt, offset, "ibm,nvlink",
                                   PHANDLE_NVLINK(sphb, i, j))));
            /*
             * If we ever want to emulate GPU RAM at the same location as on
             * the host - here is the encoding GPA->TGT:
             *
             * gta  = ((sphb->nv2_gpa >> 42) & 0x1) << 42;
             * gta |= ((sphb->nv2_gpa >> 45) & 0x3) << 43;
             * gta |= ((sphb->nv2_gpa >> 49) & 0x3) << 45;
             * gta |= sphb->nv2_gpa & ((1UL << 43) - 1);
             */
            _FDT(fdt_setprop_cell(fdt, offset, "memory-region",
                                  PHANDLE_GPURAM(sphb, i)));
            _FDT(fdt_setprop_u64(fdt, offset, "ibm,device-tgt-addr",
                                 nvslot->tgt));
            _FDT(fdt_setprop_cell(fdt, offset, "ibm,nvlink-speed",
                                  nvslot->links[j].link_speed));
        }
    }
}
