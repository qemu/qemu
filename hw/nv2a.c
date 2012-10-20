/*
 * QEMU Geforce NV2A implementation
 *
 * Copyright (c) 2012 espes
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
#include "hw.h"
#include "pc.h"
#include "console.h"
#include "pci.h"
#include "vga_int.h"

#include "nv2a.h"

#define DEBUG_NV2A
#ifdef DEBUG_NV2A
# define NV2A_DPRINTF(format, ...)       printf(format, ## __VA_ARGS__)
#else
# define NV2A_DPRINTF(format, ...)       do { } while (0)
#endif


#define NV_NUM_BLOCKS 20
#define NV_PMC          0   /* card master control */
#define NV_PBUS         1   /* bus control */
#define NV_PFIFO        2   /* MMIO and DMA FIFO submission to PGRAPH and VPE */
#define NV_PFIFO_CACHE  3
#define NV_PRMA         4   /* access to BAR0/BAR1 from real mode */
#define NV_PVIDEO       5   /* video overlay */
#define NV_PTIMER       6   /* time measurement and time-based alarms */
#define NV_PCOUNTER     7   /* performance monitoring counters */
#define NV_PVPE         8   /* MPEG2 decoding engine */
#define NV_PTV          9   /* TV encoder */
#define NV_PRMFB        10  /* aliases VGA memory window */
#define NV_PRMVIO       11  /* aliases VGA sequencer and graphics controller registers */
#define NV_PSTRAPS      12  /* straps readout / override */
#define NV_PGRAPH       13  /* accelerated 2d/3d drawing engine */
#define NV_PCRTC        14  /* more CRTC controls */
#define NV_PRMCIO       15  /* aliases VGA CRTC and attribute controller registers */
#define NV_PRAMDAC      16  /* RAMDAC, cursor, and PLL control */
#define NV_PRMDIO       17  /* aliases VGA palette registers */
#define NV_PRAMIN       18  /* RAMIN access */
#define NV_USER         19  /* PFIFO MMIO and DMA submission area */


typedef struct NV2AState {
    PCIDevice dev;
    VGACommonState vga;

    MemoryRegion vram;
    MemoryRegion mmio;

    MemoryRegion block_mmio[NV_NUM_BLOCKS];
} NV2AState;




static uint64_t nv2a_pmc_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PMC: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pmc_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PMC: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_pbus_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PBUS: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pbus_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PBUS: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_pfifo_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PFIFO: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pfifo_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PFIFO: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_prma_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMA: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_prma_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMA: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_pvideo_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PVIDEO: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pvideo_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PVIDEO: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_ptimer_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PTIMER: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_ptimer_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PTIMER: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_pcounter_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PCOUNTER: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pcounter_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PCOUNTER: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_pvpe_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PVPE: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pvpe_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PVPE: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_ptv_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PTV: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_ptv_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PTV: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_prmfb_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMFB: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_prmfb_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMFB: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_prmvio_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMVIO: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_prmvio_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMVIO: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_pstraps_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PSTRAPS: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pstraps_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PSTRAPS: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_pgraph_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PGRAPH: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pgraph_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PGRAPH: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_pcrtc_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PCRTC: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pcrtc_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PCRTC: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_prmcio_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMCIO: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_prmcio_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMCIO: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_pramdac_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRAMDAC: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pramdac_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRAMDAC: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_prmdio_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMDIO: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_prmdio_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRMDIO: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_pramin_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRAMIN: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_pramin_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a PRAMIN: [0x%llx] = 0x%02llx\n", addr, val);
}


static uint64_t nv2a_user_read(void *opaque,
                                  target_phys_addr_t addr, unsigned int size)
{
    NV2A_DPRINTF("nv2a USER: read [0x%llx]\n", addr);
    return 0;
}
static void nv2a_user_write(void *opaque, target_phys_addr_t addr,
                               uint64_t val, unsigned int size)
{
    NV2A_DPRINTF("nv2a USER: [0x%llx] = 0x%02llx\n", addr, val);
}




typedef struct NV2ABlockInfo {
    const char* name;
    target_phys_addr_t offset;
    uint64_t size;
    MemoryRegionOps ops;
} NV2ABlockInfo;

static const struct NV2ABlockInfo blocktable[] = {
    [ NV_PMC ]  = {
        .name = "PMC",
        .offset = 0x000000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pmc_read,
            .write = nv2a_pmc_write,
        },
    },
    [ NV_PBUS ]  = {
        .name = "PBUS",
        .offset = 0x001000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pbus_read,
            .write = nv2a_pbus_write,
        },
    },
    [ NV_PFIFO ]  = {
        .name = "PFIFO",
        .offset = 0x002000,
        .size   = 0x002000,
        .ops = {
            .read = nv2a_pfifo_read,
            .write = nv2a_pfifo_write,
        },
    },
    [ NV_PRMA ]  = {
        .name = "PRMA",
        .offset = 0x007000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_prma_read,
            .write = nv2a_prma_write,
        },
    },
    [ NV_PVIDEO ]  = {
        .name = "PVIDEO",
        .offset = 0x008000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pvideo_read,
            .write = nv2a_pvideo_write,
        },
    },
    [ NV_PTIMER ]  = {
        .name = "PTIMER",
        .offset = 0x009000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_ptimer_read,
            .write = nv2a_ptimer_write,
        },
    },
    [ NV_PCOUNTER ]  = {
        .name = "PCOUNTER",
        .offset = 0x00a000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pcounter_read,
            .write = nv2a_pcounter_write,
        },
    },
    [ NV_PVPE ]  = {
        .name = "PVPE",
        .offset = 0x00b000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pvpe_read,
            .write = nv2a_pvpe_write,
        },
    },
    [ NV_PTV ]  = {
        .name = "PTV",
        .offset = 0x00d000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_ptv_read,
            .write = nv2a_ptv_write,
        },
    },
    [ NV_PRMFB ]  = {
        .name = "PRMFB",
        .offset = 0x0a0000,
        .size   = 0x020000,
        .ops = {
            .read = nv2a_prmfb_read,
            .write = nv2a_prmfb_write,
        },
    },
    [ NV_PRMVIO ]  = {
        .name = "PRMVIO",
        .offset = 0x0c0000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_prmvio_read,
            .write = nv2a_prmvio_write,
        },
    },
    [ NV_PSTRAPS ]  = {
        .name = "PSTRAPS",
        .offset = 0x101000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pstraps_read,
            .write = nv2a_pstraps_write,
        },
    },
    [ NV_PGRAPH ]  = {
        .name = "PGRAPH",
        .offset = 0x400000,
        .size   = 0x002000,
        .ops = {
            .read = nv2a_pgraph_read,
            .write = nv2a_pgraph_write,
        },
    },
    [ NV_PCRTC ]  = {
        .name = "PCRTC",
        .offset = 0x600000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pcrtc_read,
            .write = nv2a_pcrtc_write,
        },
    },
    [ NV_PRMCIO ]  = {
        .name = "PRMCIO",
        .offset = 0x601000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_prmcio_read,
            .write = nv2a_prmcio_write,
        },
    },
    [ NV_PRAMDAC ]  = {
        .name = "PRAMDAC",
        .offset = 0x680000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_pramdac_read,
            .write = nv2a_pramdac_write,
        },
    },
    [ NV_PRMDIO ]  = {
        .name = "PRMDIO",
        .offset = 0x681000,
        .size   = 0x001000,
        .ops = {
            .read = nv2a_prmdio_read,
            .write = nv2a_prmdio_write,
        },
    },
    [ NV_PRAMIN ]  = {
        .name = "PRAMIN",
        .offset = 0x700000,
        .size   = 0x100000,
        .ops = {
            .read = nv2a_pramin_read,
            .write = nv2a_pramin_write,
        },
    },
    [ NV_USER ]  = {
        .name = "USER",
        .offset = 0x800000,
        .size   = 0x800000,
        .ops = {
            .read = nv2a_user_read,
            .write = nv2a_user_write,
        },
    },
};




static int nv2a_initfn(PCIDevice *dev)
{
    int i;
    NV2AState *d = DO_UPCAST(NV2AState, dev, dev);
    //uint8_t *pci_conf = d->dev.config;

    /* setup legacy VGA */
    //d->vga.vram_size_mb = 16;
    //vga_common_init(&d->vga);

    memory_region_init(&d->mmio, "nv2a-mmio", 0x1000000);

    pci_register_bar(&d->dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &d->mmio);

    memory_region_init_ram(&d->vram, "nv2a-vram", 128 * 0x100000);
    pci_register_bar(&d->dev, 1, PCI_BASE_ADDRESS_MEM_PREFETCH, &d->vram);

    for (i=0; i<sizeof(blocktable)/sizeof(blocktable[0]); i++) {
        if (!blocktable[i].name) continue;
        memory_region_init_io(&d->block_mmio[i], &blocktable[i].ops, d,
                              blocktable[i].name, blocktable[i].size);
        memory_region_add_subregion(&d->mmio, blocktable[i].offset,
                                    &d->block_mmio[i]);
    }


    return 0;
}

static void nv2a_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_NVIDIA;
    k->device_id = PCI_DEVICE_ID_NVIDIA_GEFORCE_NV2A;
    k->revision = 161;
    k->class_id = PCI_CLASS_DISPLAY_3D;
    k->init = nv2a_initfn;

    dc->desc = "GeForce NV2A Integrated Graphics";
}

static const TypeInfo nv2a_info = {
    .name          = "nv2a",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(NV2AState),
    .class_init    = nv2a_class_init,
};

static void nv2a_register(void)
{
    type_register_static(&nv2a_info);
}
type_init(nv2a_register);





void nv2a_init(PCIBus *bus, int devfn)
{
    pci_create_simple(bus, devfn, "nv2a");
}