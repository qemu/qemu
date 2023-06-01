/*
 * QEMU Sun4m & Sun4d & Sun4c System Emulator
 *
 * Copyright (c) 2003-2005 Fabrice Bellard
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
#include "qemu/units.h"
#include "qapi/error.h"
#include "qemu/datadir.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/sparc/sun4m_iommu.h"
#include "hw/rtc/m48t59.h"
#include "migration/vmstate.h"
#include "hw/sparc/sparc32_dma.h"
#include "hw/block/fdc.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "sysemu/sysemu.h"
#include "net/net.h"
#include "hw/boards.h"
#include "hw/scsi/esp.h"
#include "hw/nvram/sun_nvram.h"
#include "hw/qdev-properties.h"
#include "hw/nvram/chrp_nvram.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/char/escc.h"
#include "hw/misc/empty_slot.h"
#include "hw/misc/unimp.h"
#include "hw/irq.h"
#include "hw/or-irq.h"
#include "hw/loader.h"
#include "elf.h"
#include "trace.h"
#include "qom/object.h"

/*
 * Sun4m architecture was used in the following machines:
 *
 * SPARCserver 6xxMP/xx
 * SPARCclassic (SPARCclassic Server)(SPARCstation LC) (4/15),
 * SPARCclassic X (4/10)
 * SPARCstation LX/ZX (4/30)
 * SPARCstation Voyager
 * SPARCstation 10/xx, SPARCserver 10/xx
 * SPARCstation 5, SPARCserver 5
 * SPARCstation 20/xx, SPARCserver 20
 * SPARCstation 4
 *
 * See for example: http://www.sunhelp.org/faq/sunref1.html
 */

#define KERNEL_LOAD_ADDR     0x00004000
#define CMDLINE_ADDR         0x007ff000
#define INITRD_LOAD_ADDR     0x00800000
#define PROM_SIZE_MAX        (1 * MiB)
#define PROM_VADDR           0xffd00000
#define PROM_FILENAME        "openbios-sparc32"
#define CFG_ADDR             0xd00000510ULL
#define FW_CFG_SUN4M_DEPTH   (FW_CFG_ARCH_LOCAL + 0x00)
#define FW_CFG_SUN4M_WIDTH   (FW_CFG_ARCH_LOCAL + 0x01)
#define FW_CFG_SUN4M_HEIGHT  (FW_CFG_ARCH_LOCAL + 0x02)

#define MAX_CPUS 16
#define MAX_PILS 16
#define MAX_VSIMMS 4

#define ESCC_CLOCK 4915200

struct sun4m_hwdef {
    hwaddr iommu_base, iommu_pad_base, iommu_pad_len, slavio_base;
    hwaddr intctl_base, counter_base, nvram_base, ms_kb_base;
    hwaddr serial_base, fd_base;
    hwaddr afx_base, idreg_base, dma_base, esp_base, le_base;
    hwaddr tcx_base, cs_base, apc_base, aux1_base, aux2_base;
    hwaddr bpp_base, dbri_base, sx_base;
    struct {
        hwaddr reg_base, vram_base;
    } vsimm[MAX_VSIMMS];
    hwaddr ecc_base;
    uint64_t max_mem;
    uint32_t ecc_version;
    uint32_t iommu_version;
    uint16_t machine_id;
    uint8_t nvram_machine_id;
};

struct Sun4mMachineClass {
    /*< private >*/
    MachineClass parent_obj;
    /*< public >*/
    const struct sun4m_hwdef *hwdef;
};
typedef struct Sun4mMachineClass Sun4mMachineClass;

#define TYPE_SUN4M_MACHINE MACHINE_TYPE_NAME("sun4m-common")
DECLARE_CLASS_CHECKERS(Sun4mMachineClass, SUN4M_MACHINE, TYPE_SUN4M_MACHINE)

const char *fw_cfg_arch_key_name(uint16_t key)
{
    static const struct {
        uint16_t key;
        const char *name;
    } fw_cfg_arch_wellknown_keys[] = {
        {FW_CFG_SUN4M_DEPTH, "depth"},
        {FW_CFG_SUN4M_WIDTH, "width"},
        {FW_CFG_SUN4M_HEIGHT, "height"},
    };

    for (size_t i = 0; i < ARRAY_SIZE(fw_cfg_arch_wellknown_keys); i++) {
        if (fw_cfg_arch_wellknown_keys[i].key == key) {
            return fw_cfg_arch_wellknown_keys[i].name;
        }
    }
    return NULL;
}

static void fw_cfg_boot_set(void *opaque, const char *boot_device,
                            Error **errp)
{
    fw_cfg_modify_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
}

static void nvram_init(Nvram *nvram, uint8_t *macaddr,
                       const char *cmdline, const char *boot_devices,
                       ram_addr_t RAM_size, uint32_t kernel_size,
                       int width, int height, int depth,
                       int nvram_machine_id, const char *arch)
{
    unsigned int i;
    int sysp_end;
    uint8_t image[0x1ff0];
    NvramClass *k = NVRAM_GET_CLASS(nvram);

    memset(image, '\0', sizeof(image));

    /* OpenBIOS nvram variables partition */
    sysp_end = chrp_nvram_create_system_partition(image, 0, 0x1fd0);

    /* Free space partition */
    chrp_nvram_create_free_partition(&image[sysp_end], 0x1fd0 - sysp_end);

    Sun_init_header((struct Sun_nvram *)&image[0x1fd8], macaddr,
                    nvram_machine_id);

    for (i = 0; i < sizeof(image); i++) {
        (k->write)(nvram, i, image[i]);
    }
}

static void cpu_kick_irq(SPARCCPU *cpu)
{
    CPUSPARCState *env = &cpu->env;
    CPUState *cs = CPU(cpu);

    cs->halted = 0;
    cpu_check_irqs(env);
    qemu_cpu_kick(cs);
}

static void cpu_set_irq(void *opaque, int irq, int level)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;

    if (level) {
        trace_sun4m_cpu_set_irq_raise(irq);
        env->pil_in |= 1 << irq;
        cpu_kick_irq(cpu);
    } else {
        trace_sun4m_cpu_set_irq_lower(irq);
        env->pil_in &= ~(1 << irq);
        cpu_check_irqs(env);
    }
}

static void dummy_cpu_set_irq(void *opaque, int irq, int level)
{
}

static void sun4m_cpu_reset(void *opaque)
{
    SPARCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    cpu_reset(cs);
}

static void cpu_halt_signal(void *opaque, int irq, int level)
{
    if (level && current_cpu) {
        cpu_interrupt(current_cpu, CPU_INTERRUPT_HALT);
    }
}

static uint64_t translate_kernel_address(void *opaque, uint64_t addr)
{
    return addr - 0xf0000000ULL;
}

static unsigned long sun4m_load_kernel(const char *kernel_filename,
                                       const char *initrd_filename,
                                       ram_addr_t RAM_size,
                                       uint32_t *initrd_size)
{
    int linux_boot;
    unsigned int i;
    long kernel_size;
    uint8_t *ptr;

    linux_boot = (kernel_filename != NULL);

    kernel_size = 0;
    if (linux_boot) {
        int bswap_needed;

#ifdef BSWAP_NEEDED
        bswap_needed = 1;
#else
        bswap_needed = 0;
#endif
        kernel_size = load_elf(kernel_filename, NULL,
                               translate_kernel_address, NULL,
                               NULL, NULL, NULL, NULL, 1, EM_SPARC, 0, 0);
        if (kernel_size < 0)
            kernel_size = load_aout(kernel_filename, KERNEL_LOAD_ADDR,
                                    RAM_size - KERNEL_LOAD_ADDR, bswap_needed,
                                    TARGET_PAGE_SIZE);
        if (kernel_size < 0)
            kernel_size = load_image_targphys(kernel_filename,
                                              KERNEL_LOAD_ADDR,
                                              RAM_size - KERNEL_LOAD_ADDR);
        if (kernel_size < 0) {
            error_report("could not load kernel '%s'", kernel_filename);
            exit(1);
        }

        /* load initrd */
        *initrd_size = 0;
        if (initrd_filename) {
            *initrd_size = load_image_targphys(initrd_filename,
                                               INITRD_LOAD_ADDR,
                                               RAM_size - INITRD_LOAD_ADDR);
            if ((int)*initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             initrd_filename);
                exit(1);
            }
        }
        if (*initrd_size > 0) {
            for (i = 0; i < 64 * TARGET_PAGE_SIZE; i += TARGET_PAGE_SIZE) {
                ptr = rom_ptr(KERNEL_LOAD_ADDR + i, 24);
                if (ptr && ldl_p(ptr) == 0x48647253) { /* HdrS */
                    stl_p(ptr + 16, INITRD_LOAD_ADDR);
                    stl_p(ptr + 20, *initrd_size);
                    break;
                }
            }
        }
    }
    return kernel_size;
}

static void *iommu_init(hwaddr addr, uint32_t version, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_SUN4M_IOMMU);
    qdev_prop_set_uint32(dev, "version", version);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_connect_irq(s, 0, irq);
    sysbus_mmio_map(s, 0, addr);

    return s;
}

static void *sparc32_dma_init(hwaddr dma_base,
                              hwaddr esp_base, qemu_irq espdma_irq,
                              hwaddr le_base, qemu_irq ledma_irq, NICInfo *nd)
{
    DeviceState *dma;
    ESPDMADeviceState *espdma;
    LEDMADeviceState *ledma;
    SysBusESPState *esp;
    SysBusPCNetState *lance;

    dma = qdev_new(TYPE_SPARC32_DMA);
    espdma = SPARC32_ESPDMA_DEVICE(object_resolve_path_component(
                                   OBJECT(dma), "espdma"));
    sysbus_connect_irq(SYS_BUS_DEVICE(espdma), 0, espdma_irq);

    esp = SYSBUS_ESP(object_resolve_path_component(OBJECT(espdma), "esp"));

    ledma = SPARC32_LEDMA_DEVICE(object_resolve_path_component(
                                 OBJECT(dma), "ledma"));
    sysbus_connect_irq(SYS_BUS_DEVICE(ledma), 0, ledma_irq);

    lance = SYSBUS_PCNET(object_resolve_path_component(
                         OBJECT(ledma), "lance"));
    qdev_set_nic_properties(DEVICE(lance), nd);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dma), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dma), 0, dma_base);

    sysbus_mmio_map(SYS_BUS_DEVICE(esp), 0, esp_base);
    scsi_bus_legacy_handle_cmdline(&esp->esp.bus);

    sysbus_mmio_map(SYS_BUS_DEVICE(lance), 0, le_base);

    return dma;
}

static DeviceState *slavio_intctl_init(hwaddr addr,
                                       hwaddr addrg,
                                       qemu_irq **parent_irq)
{
    DeviceState *dev;
    SysBusDevice *s;
    unsigned int i, j;

    dev = qdev_new("slavio_intctl");

    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);

    for (i = 0; i < MAX_CPUS; i++) {
        for (j = 0; j < MAX_PILS; j++) {
            sysbus_connect_irq(s, i * MAX_PILS + j, parent_irq[i][j]);
        }
    }
    sysbus_mmio_map(s, 0, addrg);
    for (i = 0; i < MAX_CPUS; i++) {
        sysbus_mmio_map(s, i + 1, addr + i * TARGET_PAGE_SIZE);
    }

    return dev;
}

#define SYS_TIMER_OFFSET      0x10000ULL
#define CPU_TIMER_OFFSET(cpu) (0x1000ULL * cpu)

static void slavio_timer_init_all(hwaddr addr, qemu_irq master_irq,
                                  qemu_irq *cpu_irqs, unsigned int num_cpus)
{
    DeviceState *dev;
    SysBusDevice *s;
    unsigned int i;

    dev = qdev_new("slavio_timer");
    qdev_prop_set_uint32(dev, "num_cpus", num_cpus);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_connect_irq(s, 0, master_irq);
    sysbus_mmio_map(s, 0, addr + SYS_TIMER_OFFSET);

    for (i = 0; i < MAX_CPUS; i++) {
        sysbus_mmio_map(s, i + 1, addr + (hwaddr)CPU_TIMER_OFFSET(i));
        sysbus_connect_irq(s, i + 1, cpu_irqs[i]);
    }
}

static qemu_irq  slavio_system_powerdown;

static void slavio_powerdown_req(Notifier *n, void *opaque)
{
    qemu_irq_raise(slavio_system_powerdown);
}

static Notifier slavio_system_powerdown_notifier = {
    .notify = slavio_powerdown_req
};

#define MISC_LEDS 0x01600000
#define MISC_CFG  0x01800000
#define MISC_DIAG 0x01a00000
#define MISC_MDM  0x01b00000
#define MISC_SYS  0x01f00000

static void slavio_misc_init(hwaddr base,
                             hwaddr aux1_base,
                             hwaddr aux2_base, qemu_irq irq,
                             qemu_irq fdc_tc)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new("slavio_misc");
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    if (base) {
        /* 8 bit registers */
        /* Slavio control */
        sysbus_mmio_map(s, 0, base + MISC_CFG);
        /* Diagnostics */
        sysbus_mmio_map(s, 1, base + MISC_DIAG);
        /* Modem control */
        sysbus_mmio_map(s, 2, base + MISC_MDM);
        /* 16 bit registers */
        /* ss600mp diag LEDs */
        sysbus_mmio_map(s, 3, base + MISC_LEDS);
        /* 32 bit registers */
        /* System control */
        sysbus_mmio_map(s, 4, base + MISC_SYS);
    }
    if (aux1_base) {
        /* AUX 1 (Misc System Functions) */
        sysbus_mmio_map(s, 5, aux1_base);
    }
    if (aux2_base) {
        /* AUX 2 (Software Powerdown Control) */
        sysbus_mmio_map(s, 6, aux2_base);
    }
    sysbus_connect_irq(s, 0, irq);
    sysbus_connect_irq(s, 1, fdc_tc);
    slavio_system_powerdown = qdev_get_gpio_in(dev, 0);
    qemu_register_powerdown_notifier(&slavio_system_powerdown_notifier);
}

static void ecc_init(hwaddr base, qemu_irq irq, uint32_t version)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new("eccmemctl");
    qdev_prop_set_uint32(dev, "version", version);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_connect_irq(s, 0, irq);
    sysbus_mmio_map(s, 0, base);
    if (version == 0) { // SS-600MP only
        sysbus_mmio_map(s, 1, base + 0x1000);
    }
}

static void apc_init(hwaddr power_base, qemu_irq cpu_halt)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new("apc");
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    /* Power management (APC) XXX: not a Slavio device */
    sysbus_mmio_map(s, 0, power_base);
    sysbus_connect_irq(s, 0, cpu_halt);
}

static void tcx_init(hwaddr addr, qemu_irq irq, int vram_size, int width,
                     int height, int depth)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new("sun-tcx");
    qdev_prop_set_uint32(dev, "vram_size", vram_size);
    qdev_prop_set_uint16(dev, "width", width);
    qdev_prop_set_uint16(dev, "height", height);
    qdev_prop_set_uint16(dev, "depth", depth);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);

    /* 10/ROM : FCode ROM */
    sysbus_mmio_map(s, 0, addr);
    /* 2/STIP : Stipple */
    sysbus_mmio_map(s, 1, addr + 0x04000000ULL);
    /* 3/BLIT : Blitter */
    sysbus_mmio_map(s, 2, addr + 0x06000000ULL);
    /* 5/RSTIP : Raw Stipple */
    sysbus_mmio_map(s, 3, addr + 0x0c000000ULL);
    /* 6/RBLIT : Raw Blitter */
    sysbus_mmio_map(s, 4, addr + 0x0e000000ULL);
    /* 7/TEC : Transform Engine */
    sysbus_mmio_map(s, 5, addr + 0x00700000ULL);
    /* 8/CMAP  : DAC */
    sysbus_mmio_map(s, 6, addr + 0x00200000ULL);
    /* 9/THC : */
    if (depth == 8) {
        sysbus_mmio_map(s, 7, addr + 0x00300000ULL);
    } else {
        sysbus_mmio_map(s, 7, addr + 0x00301000ULL);
    }
    /* 11/DHC : */
    sysbus_mmio_map(s, 8, addr + 0x00240000ULL);
    /* 12/ALT : */
    sysbus_mmio_map(s, 9, addr + 0x00280000ULL);
    /* 0/DFB8 : 8-bit plane */
    sysbus_mmio_map(s, 10, addr + 0x00800000ULL);
    /* 1/DFB24 : 24bit plane */
    sysbus_mmio_map(s, 11, addr + 0x02000000ULL);
    /* 4/RDFB32: Raw framebuffer. Control plane */
    sysbus_mmio_map(s, 12, addr + 0x0a000000ULL);
    /* 9/THC24bits : NetBSD writes here even with 8-bit display: dummy */
    if (depth == 8) {
        sysbus_mmio_map(s, 13, addr + 0x00301000ULL);
    }

    sysbus_connect_irq(s, 0, irq);
}

static void cg3_init(hwaddr addr, qemu_irq irq, int vram_size, int width,
                     int height, int depth)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new("cgthree");
    qdev_prop_set_uint32(dev, "vram-size", vram_size);
    qdev_prop_set_uint16(dev, "width", width);
    qdev_prop_set_uint16(dev, "height", height);
    qdev_prop_set_uint16(dev, "depth", depth);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);

    /* FCode ROM */
    sysbus_mmio_map(s, 0, addr);
    /* DAC */
    sysbus_mmio_map(s, 1, addr + 0x400000ULL);
    /* 8-bit plane */
    sysbus_mmio_map(s, 2, addr + 0x800000ULL);

    sysbus_connect_irq(s, 0, irq);
}

/* NCR89C100/MACIO Internal ID register */

#define TYPE_MACIO_ID_REGISTER "macio_idreg"

static const uint8_t idreg_data[] = { 0xfe, 0x81, 0x01, 0x03 };

static void idreg_init(hwaddr addr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_MACIO_ID_REGISTER);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);

    sysbus_mmio_map(s, 0, addr);
    address_space_write_rom(&address_space_memory, addr,
                            MEMTXATTRS_UNSPECIFIED,
                            idreg_data, sizeof(idreg_data));
}

OBJECT_DECLARE_SIMPLE_TYPE(IDRegState, MACIO_ID_REGISTER)

struct IDRegState {
    SysBusDevice parent_obj;

    MemoryRegion mem;
};

static void idreg_realize(DeviceState *ds, Error **errp)
{
    IDRegState *s = MACIO_ID_REGISTER(ds);
    SysBusDevice *dev = SYS_BUS_DEVICE(ds);
    Error *local_err = NULL;

    memory_region_init_ram_nomigrate(&s->mem, OBJECT(ds), "sun4m.idreg",
                                     sizeof(idreg_data), &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    vmstate_register_ram_global(&s->mem);
    memory_region_set_readonly(&s->mem, true);
    sysbus_init_mmio(dev, &s->mem);
}

static void idreg_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = idreg_realize;
}

static const TypeInfo idreg_info = {
    .name          = TYPE_MACIO_ID_REGISTER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IDRegState),
    .class_init    = idreg_class_init,
};

#define TYPE_TCX_AFX "tcx_afx"
OBJECT_DECLARE_SIMPLE_TYPE(AFXState, TCX_AFX)

struct AFXState {
    SysBusDevice parent_obj;

    MemoryRegion mem;
};

/* SS-5 TCX AFX register */
static void afx_init(hwaddr addr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_TCX_AFX);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);

    sysbus_mmio_map(s, 0, addr);
}

static void afx_realize(DeviceState *ds, Error **errp)
{
    AFXState *s = TCX_AFX(ds);
    SysBusDevice *dev = SYS_BUS_DEVICE(ds);
    Error *local_err = NULL;

    memory_region_init_ram_nomigrate(&s->mem, OBJECT(ds), "sun4m.afx", 4,
                                     &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    vmstate_register_ram_global(&s->mem);
    sysbus_init_mmio(dev, &s->mem);
}

static void afx_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = afx_realize;
}

static const TypeInfo afx_info = {
    .name          = TYPE_TCX_AFX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AFXState),
    .class_init    = afx_class_init,
};

#define TYPE_OPENPROM "openprom"
typedef struct PROMState PROMState;
DECLARE_INSTANCE_CHECKER(PROMState, OPENPROM,
                         TYPE_OPENPROM)

struct PROMState {
    SysBusDevice parent_obj;

    MemoryRegion prom;
};

/* Boot PROM (OpenBIOS) */
static uint64_t translate_prom_address(void *opaque, uint64_t addr)
{
    hwaddr *base_addr = (hwaddr *)opaque;
    return addr + *base_addr - PROM_VADDR;
}

static void prom_init(hwaddr addr, const char *bios_name)
{
    DeviceState *dev;
    SysBusDevice *s;
    char *filename;
    int ret;

    dev = qdev_new(TYPE_OPENPROM);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);

    sysbus_mmio_map(s, 0, addr);

    /* load boot prom */
    if (bios_name == NULL) {
        bios_name = PROM_FILENAME;
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        ret = load_elf(filename, NULL,
                       translate_prom_address, &addr, NULL,
                       NULL, NULL, NULL, 1, EM_SPARC, 0, 0);
        if (ret < 0 || ret > PROM_SIZE_MAX) {
            ret = load_image_targphys(filename, addr, PROM_SIZE_MAX);
        }
        g_free(filename);
    } else {
        ret = -1;
    }
    if (ret < 0 || ret > PROM_SIZE_MAX) {
        error_report("could not load prom '%s'", bios_name);
        exit(1);
    }
}

static void prom_realize(DeviceState *ds, Error **errp)
{
    PROMState *s = OPENPROM(ds);
    SysBusDevice *dev = SYS_BUS_DEVICE(ds);
    Error *local_err = NULL;

    memory_region_init_ram_nomigrate(&s->prom, OBJECT(ds), "sun4m.prom",
                                     PROM_SIZE_MAX, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    vmstate_register_ram_global(&s->prom);
    memory_region_set_readonly(&s->prom, true);
    sysbus_init_mmio(dev, &s->prom);
}

static Property prom_properties[] = {
    {/* end of property list */},
};

static void prom_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, prom_properties);
    dc->realize = prom_realize;
}

static const TypeInfo prom_info = {
    .name          = TYPE_OPENPROM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PROMState),
    .class_init    = prom_class_init,
};

#define TYPE_SUN4M_MEMORY "memory"
typedef struct RamDevice RamDevice;
DECLARE_INSTANCE_CHECKER(RamDevice, SUN4M_RAM,
                         TYPE_SUN4M_MEMORY)

struct RamDevice {
    SysBusDevice parent_obj;
    HostMemoryBackend *memdev;
};

/* System RAM */
static void ram_realize(DeviceState *dev, Error **errp)
{
    RamDevice *d = SUN4M_RAM(dev);
    MemoryRegion *ram = host_memory_backend_get_memory(d->memdev);

    sysbus_init_mmio(SYS_BUS_DEVICE(dev), ram);
}

static void ram_initfn(Object *obj)
{
    RamDevice *d = SUN4M_RAM(obj);
    object_property_add_link(obj, "memdev", TYPE_MEMORY_BACKEND,
                             (Object **)&d->memdev,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_STRONG);
    object_property_set_description(obj, "memdev", "Set RAM backend"
                                    "Valid value is ID of a hostmem backend");
}

static void ram_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ram_realize;
}

static const TypeInfo ram_info = {
    .name          = TYPE_SUN4M_MEMORY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RamDevice),
    .instance_init = ram_initfn,
    .class_init    = ram_class_init,
};

static void cpu_devinit(const char *cpu_type, unsigned int id,
                        uint64_t prom_addr, qemu_irq **cpu_irqs)
{
    SPARCCPU *cpu;
    CPUSPARCState *env;

    cpu = SPARC_CPU(object_new(cpu_type));
    env = &cpu->env;

    qemu_register_reset(sun4m_cpu_reset, cpu);
    object_property_set_bool(OBJECT(cpu), "start-powered-off", id != 0,
                             &error_fatal);
    qdev_realize_and_unref(DEVICE(cpu), NULL, &error_fatal);
    cpu_sparc_set_id(env, id);
    *cpu_irqs = qemu_allocate_irqs(cpu_set_irq, cpu, MAX_PILS);
    env->prom_addr = prom_addr;
}

static void dummy_fdc_tc(void *opaque, int irq, int level)
{
}

static void sun4m_hw_init(MachineState *machine)
{
    const struct sun4m_hwdef *hwdef = SUN4M_MACHINE_GET_CLASS(machine)->hwdef;
    DeviceState *slavio_intctl;
    unsigned int i;
    Nvram *nvram;
    qemu_irq *cpu_irqs[MAX_CPUS], slavio_irq[32], slavio_cpu_irq[MAX_CPUS];
    qemu_irq fdc_tc;
    unsigned long kernel_size;
    uint32_t initrd_size;
    DriveInfo *fd[MAX_FD];
    FWCfgState *fw_cfg;
    DeviceState *dev, *ms_kb_orgate, *serial_orgate;
    SysBusDevice *s;
    unsigned int smp_cpus = machine->smp.cpus;
    unsigned int max_cpus = machine->smp.max_cpus;
    HostMemoryBackend *ram_memdev = machine->memdev;
    NICInfo *nd = &nd_table[0];

    if (machine->ram_size > hwdef->max_mem) {
        error_report("Too much memory for this machine: %" PRId64 ","
                     " maximum %" PRId64,
                     machine->ram_size / MiB, hwdef->max_mem / MiB);
        exit(1);
    }

    /* init CPUs */
    for(i = 0; i < smp_cpus; i++) {
        cpu_devinit(machine->cpu_type, i, hwdef->slavio_base, &cpu_irqs[i]);
    }

    for (i = smp_cpus; i < MAX_CPUS; i++)
        cpu_irqs[i] = qemu_allocate_irqs(dummy_cpu_set_irq, NULL, MAX_PILS);

    /* Create and map RAM frontend */
    dev = qdev_new("memory");
    object_property_set_link(OBJECT(dev), "memdev", OBJECT(ram_memdev), &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0);

    /* models without ECC don't trap when missing ram is accessed */
    if (!hwdef->ecc_base) {
        empty_slot_init("ecc", machine->ram_size,
                        hwdef->max_mem - machine->ram_size);
    }

    prom_init(hwdef->slavio_base, machine->firmware);

    slavio_intctl = slavio_intctl_init(hwdef->intctl_base,
                                       hwdef->intctl_base + 0x10000ULL,
                                       cpu_irqs);

    for (i = 0; i < 32; i++) {
        slavio_irq[i] = qdev_get_gpio_in(slavio_intctl, i);
    }
    for (i = 0; i < MAX_CPUS; i++) {
        slavio_cpu_irq[i] = qdev_get_gpio_in(slavio_intctl, 32 + i);
    }

    if (hwdef->idreg_base) {
        idreg_init(hwdef->idreg_base);
    }

    if (hwdef->afx_base) {
        afx_init(hwdef->afx_base);
    }

    iommu_init(hwdef->iommu_base, hwdef->iommu_version, slavio_irq[30]);

    if (hwdef->iommu_pad_base) {
        /* On the real hardware (SS-5, LX) the MMU is not padded, but aliased.
           Software shouldn't use aliased addresses, neither should it crash
           when does. Using empty_slot instead of aliasing can help with
           debugging such accesses */
        empty_slot_init("iommu.alias",
                        hwdef->iommu_pad_base, hwdef->iommu_pad_len);
    }

    qemu_check_nic_model(nd, TYPE_LANCE);
    sparc32_dma_init(hwdef->dma_base,
                     hwdef->esp_base, slavio_irq[18],
                     hwdef->le_base, slavio_irq[16], nd);

    if (graphic_depth != 8 && graphic_depth != 24) {
        error_report("Unsupported depth: %d", graphic_depth);
        exit (1);
    }
    if (vga_interface_type != VGA_NONE) {
        if (vga_interface_type == VGA_CG3) {
            if (graphic_depth != 8) {
                error_report("Unsupported depth: %d", graphic_depth);
                exit(1);
            }

            if (!(graphic_width == 1024 && graphic_height == 768) &&
                !(graphic_width == 1152 && graphic_height == 900)) {
                error_report("Unsupported resolution: %d x %d", graphic_width,
                             graphic_height);
                exit(1);
            }

            /* sbus irq 5 */
            cg3_init(hwdef->tcx_base, slavio_irq[11], 0x00100000,
                     graphic_width, graphic_height, graphic_depth);
            vga_interface_created = true;
        } else {
            /* If no display specified, default to TCX */
            if (graphic_depth != 8 && graphic_depth != 24) {
                error_report("Unsupported depth: %d", graphic_depth);
                exit(1);
            }

            if (!(graphic_width == 1024 && graphic_height == 768)) {
                error_report("Unsupported resolution: %d x %d",
                             graphic_width, graphic_height);
                exit(1);
            }

            tcx_init(hwdef->tcx_base, slavio_irq[11], 0x00100000,
                     graphic_width, graphic_height, graphic_depth);
            vga_interface_created = true;
        }
    }

    for (i = 0; i < MAX_VSIMMS; i++) {
        /* vsimm registers probed by OBP */
        if (hwdef->vsimm[i].reg_base) {
            char *name = g_strdup_printf("vsimm[%d]", i);
            empty_slot_init(name, hwdef->vsimm[i].reg_base, 0x2000);
            g_free(name);
        }
    }

    if (hwdef->sx_base) {
        create_unimplemented_device("sun-sx", hwdef->sx_base, 0x2000);
    }

    dev = qdev_new("sysbus-m48t08");
    qdev_prop_set_int32(dev, "base-year", 1968);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_connect_irq(s, 0, slavio_irq[0]);
    sysbus_mmio_map(s, 0, hwdef->nvram_base);
    nvram = NVRAM(dev);

    slavio_timer_init_all(hwdef->counter_base, slavio_irq[19], slavio_cpu_irq, smp_cpus);

    /* Slavio TTYA (base+4, Linux ttyS0) is the first QEMU serial device
       Slavio TTYB (base+0, Linux ttyS1) is the second QEMU serial device */
    dev = qdev_new(TYPE_ESCC);
    qdev_prop_set_uint32(dev, "disabled", !machine->enable_graphics);
    qdev_prop_set_uint32(dev, "frequency", ESCC_CLOCK);
    qdev_prop_set_uint32(dev, "it_shift", 1);
    qdev_prop_set_chr(dev, "chrB", NULL);
    qdev_prop_set_chr(dev, "chrA", NULL);
    qdev_prop_set_uint32(dev, "chnBtype", escc_mouse);
    qdev_prop_set_uint32(dev, "chnAtype", escc_kbd);
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, hwdef->ms_kb_base);

    /* Logically OR both its IRQs together */
    ms_kb_orgate = DEVICE(object_new(TYPE_OR_IRQ));
    object_property_set_int(OBJECT(ms_kb_orgate), "num-lines", 2, &error_fatal);
    qdev_realize_and_unref(ms_kb_orgate, NULL, &error_fatal);
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(ms_kb_orgate, 0));
    sysbus_connect_irq(s, 1, qdev_get_gpio_in(ms_kb_orgate, 1));
    qdev_connect_gpio_out(ms_kb_orgate, 0, slavio_irq[14]);

    dev = qdev_new(TYPE_ESCC);
    qdev_prop_set_uint32(dev, "disabled", 0);
    qdev_prop_set_uint32(dev, "frequency", ESCC_CLOCK);
    qdev_prop_set_uint32(dev, "it_shift", 1);
    qdev_prop_set_chr(dev, "chrB", serial_hd(1));
    qdev_prop_set_chr(dev, "chrA", serial_hd(0));
    qdev_prop_set_uint32(dev, "chnBtype", escc_serial);
    qdev_prop_set_uint32(dev, "chnAtype", escc_serial);

    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, hwdef->serial_base);

    /* Logically OR both its IRQs together */
    serial_orgate = DEVICE(object_new(TYPE_OR_IRQ));
    object_property_set_int(OBJECT(serial_orgate), "num-lines", 2,
                            &error_fatal);
    qdev_realize_and_unref(serial_orgate, NULL, &error_fatal);
    sysbus_connect_irq(s, 0, qdev_get_gpio_in(serial_orgate, 0));
    sysbus_connect_irq(s, 1, qdev_get_gpio_in(serial_orgate, 1));
    qdev_connect_gpio_out(serial_orgate, 0, slavio_irq[15]);

    if (hwdef->apc_base) {
        apc_init(hwdef->apc_base, qemu_allocate_irq(cpu_halt_signal, NULL, 0));
    }

    if (hwdef->fd_base) {
        /* there is zero or one floppy drive */
        memset(fd, 0, sizeof(fd));
        fd[0] = drive_get(IF_FLOPPY, 0, 0);
        sun4m_fdctrl_init(slavio_irq[22], hwdef->fd_base, fd,
                          &fdc_tc);
    } else {
        fdc_tc = qemu_allocate_irq(dummy_fdc_tc, NULL, 0);
    }

    slavio_misc_init(hwdef->slavio_base, hwdef->aux1_base, hwdef->aux2_base,
                     slavio_irq[30], fdc_tc);

    if (hwdef->cs_base) {
        sysbus_create_simple("sun-CS4231", hwdef->cs_base,
                             slavio_irq[5]);
    }

    if (hwdef->dbri_base) {
        /* ISDN chip with attached CS4215 audio codec */
        /* prom space */
        create_unimplemented_device("sun-DBRI.prom",
                                    hwdef->dbri_base + 0x1000, 0x30);
        /* reg space */
        create_unimplemented_device("sun-DBRI",
                                    hwdef->dbri_base + 0x10000, 0x100);
    }

    if (hwdef->bpp_base) {
        /* parallel port */
        create_unimplemented_device("sun-bpp", hwdef->bpp_base, 0x20);
    }

    initrd_size = 0;
    kernel_size = sun4m_load_kernel(machine->kernel_filename,
                                    machine->initrd_filename,
                                    machine->ram_size, &initrd_size);

    nvram_init(nvram, (uint8_t *)&nd->macaddr, machine->kernel_cmdline,
               machine->boot_config.order, machine->ram_size, kernel_size,
               graphic_width, graphic_height, graphic_depth,
               hwdef->nvram_machine_id, "Sun4m");

    if (hwdef->ecc_base)
        ecc_init(hwdef->ecc_base, slavio_irq[28],
                 hwdef->ecc_version);

    dev = qdev_new(TYPE_FW_CFG_MEM);
    fw_cfg = FW_CFG(dev);
    qdev_prop_set_uint32(dev, "data_width", 1);
    qdev_prop_set_bit(dev, "dma_enabled", false);
    object_property_add_child(OBJECT(qdev_get_machine()), TYPE_FW_CFG,
                              OBJECT(fw_cfg));
    s = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, CFG_ADDR);
    sysbus_mmio_map(s, 1, CFG_ADDR + 2);

    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, (uint16_t)smp_cpus);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)max_cpus);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)machine->ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MACHINE_ID, hwdef->machine_id);
    fw_cfg_add_i16(fw_cfg, FW_CFG_SUN4M_DEPTH, graphic_depth);
    fw_cfg_add_i16(fw_cfg, FW_CFG_SUN4M_WIDTH, graphic_width);
    fw_cfg_add_i16(fw_cfg, FW_CFG_SUN4M_HEIGHT, graphic_height);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, KERNEL_LOAD_ADDR);
    fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
    if (machine->kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, CMDLINE_ADDR);
        pstrcpy_targphys("cmdline", CMDLINE_ADDR, TARGET_PAGE_SIZE,
                         machine->kernel_cmdline);
        fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA, machine->kernel_cmdline);
        fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE,
                       strlen(machine->kernel_cmdline) + 1);
    } else {
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, 0);
        fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE, 0);
    }
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, INITRD_LOAD_ADDR);
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, initrd_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, machine->boot_config.order[0]);
    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);
}

enum {
    ss5_id = 32,
    vger_id,
    lx_id,
    ss4_id,
    scls_id,
    sbook_id,
    ss10_id = 64,
    ss20_id,
    ss600mp_id,
};

static void sun4m_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = sun4m_hw_init;
    mc->block_default_type = IF_SCSI;
    mc->default_boot_order = "c";
    mc->default_display = "tcx";
    mc->default_ram_id = "sun4m.ram";
}

static void ss5_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    Sun4mMachineClass *smc = SUN4M_MACHINE_CLASS(mc);
    static const struct sun4m_hwdef ss5_hwdef = {
        .iommu_base   = 0x10000000,
        .iommu_pad_base = 0x10004000,
        .iommu_pad_len  = 0x0fffb000,
        .tcx_base     = 0x50000000,
        .cs_base      = 0x6c000000,
        .slavio_base  = 0x70000000,
        .ms_kb_base   = 0x71000000,
        .serial_base  = 0x71100000,
        .nvram_base   = 0x71200000,
        .fd_base      = 0x71400000,
        .counter_base = 0x71d00000,
        .intctl_base  = 0x71e00000,
        .idreg_base   = 0x78000000,
        .dma_base     = 0x78400000,
        .esp_base     = 0x78800000,
        .le_base      = 0x78c00000,
        .apc_base     = 0x6a000000,
        .afx_base     = 0x6e000000,
        .aux1_base    = 0x71900000,
        .aux2_base    = 0x71910000,
        .nvram_machine_id = 0x80,
        .machine_id = ss5_id,
        .iommu_version = 0x05000000,
        .max_mem = 0x10000000,
    };

    mc->desc = "Sun4m platform, SPARCstation 5";
    mc->is_default = true;
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("Fujitsu-MB86904");
    smc->hwdef = &ss5_hwdef;
}

static void ss10_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    Sun4mMachineClass *smc = SUN4M_MACHINE_CLASS(mc);
    static const struct sun4m_hwdef ss10_hwdef = {
        .iommu_base   = 0xfe0000000ULL,
        .tcx_base     = 0xe20000000ULL,
        .slavio_base  = 0xff0000000ULL,
        .ms_kb_base   = 0xff1000000ULL,
        .serial_base  = 0xff1100000ULL,
        .nvram_base   = 0xff1200000ULL,
        .fd_base      = 0xff1700000ULL,
        .counter_base = 0xff1300000ULL,
        .intctl_base  = 0xff1400000ULL,
        .idreg_base   = 0xef0000000ULL,
        .dma_base     = 0xef0400000ULL,
        .esp_base     = 0xef0800000ULL,
        .le_base      = 0xef0c00000ULL,
        .apc_base     = 0xefa000000ULL, /* XXX should not exist */
        .aux1_base    = 0xff1800000ULL,
        .aux2_base    = 0xff1a01000ULL,
        .ecc_base     = 0xf00000000ULL,
        .ecc_version  = 0x10000000, /* version 0, implementation 1 */
        .nvram_machine_id = 0x72,
        .machine_id = ss10_id,
        .iommu_version = 0x03000000,
        .max_mem = 0xf00000000ULL,
    };

    mc->desc = "Sun4m platform, SPARCstation 10";
    mc->max_cpus = 4;
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("TI-SuperSparc-II");
    smc->hwdef = &ss10_hwdef;
}

static void ss600mp_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    Sun4mMachineClass *smc = SUN4M_MACHINE_CLASS(mc);
    static const struct sun4m_hwdef ss600mp_hwdef = {
        .iommu_base   = 0xfe0000000ULL,
        .tcx_base     = 0xe20000000ULL,
        .slavio_base  = 0xff0000000ULL,
        .ms_kb_base   = 0xff1000000ULL,
        .serial_base  = 0xff1100000ULL,
        .nvram_base   = 0xff1200000ULL,
        .counter_base = 0xff1300000ULL,
        .intctl_base  = 0xff1400000ULL,
        .dma_base     = 0xef0081000ULL,
        .esp_base     = 0xef0080000ULL,
        .le_base      = 0xef0060000ULL,
        .apc_base     = 0xefa000000ULL, /* XXX should not exist */
        .aux1_base    = 0xff1800000ULL,
        .aux2_base    = 0xff1a01000ULL, /* XXX should not exist */
        .ecc_base     = 0xf00000000ULL,
        .ecc_version  = 0x00000000, /* version 0, implementation 0 */
        .nvram_machine_id = 0x71,
        .machine_id = ss600mp_id,
        .iommu_version = 0x01000000,
        .max_mem = 0xf00000000ULL,
    };

    mc->desc = "Sun4m platform, SPARCserver 600MP";
    mc->max_cpus = 4;
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("TI-SuperSparc-II");
    smc->hwdef = &ss600mp_hwdef;
}

static void ss20_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    Sun4mMachineClass *smc = SUN4M_MACHINE_CLASS(mc);
    static const struct sun4m_hwdef ss20_hwdef = {
        .iommu_base   = 0xfe0000000ULL,
        .tcx_base     = 0xe20000000ULL,
        .slavio_base  = 0xff0000000ULL,
        .ms_kb_base   = 0xff1000000ULL,
        .serial_base  = 0xff1100000ULL,
        .nvram_base   = 0xff1200000ULL,
        .fd_base      = 0xff1700000ULL,
        .counter_base = 0xff1300000ULL,
        .intctl_base  = 0xff1400000ULL,
        .idreg_base   = 0xef0000000ULL,
        .dma_base     = 0xef0400000ULL,
        .esp_base     = 0xef0800000ULL,
        .le_base      = 0xef0c00000ULL,
        .bpp_base     = 0xef4800000ULL,
        .apc_base     = 0xefa000000ULL, /* XXX should not exist */
        .aux1_base    = 0xff1800000ULL,
        .aux2_base    = 0xff1a01000ULL,
        .dbri_base    = 0xee0000000ULL,
        .sx_base      = 0xf80000000ULL,
        .vsimm        = {
            {
                .reg_base  = 0x9c000000ULL,
                .vram_base = 0xfc000000ULL
            }, {
                .reg_base  = 0x90000000ULL,
                .vram_base = 0xf0000000ULL
            }, {
                .reg_base  = 0x94000000ULL
            }, {
                .reg_base  = 0x98000000ULL
            }
        },
        .ecc_base     = 0xf00000000ULL,
        .ecc_version  = 0x20000000, /* version 0, implementation 2 */
        .nvram_machine_id = 0x72,
        .machine_id = ss20_id,
        .iommu_version = 0x13000000,
        .max_mem = 0xf00000000ULL,
    };

    mc->desc = "Sun4m platform, SPARCstation 20";
    mc->max_cpus = 4;
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("TI-SuperSparc-II");
    smc->hwdef = &ss20_hwdef;
}

static void voyager_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    Sun4mMachineClass *smc = SUN4M_MACHINE_CLASS(mc);
    static const struct sun4m_hwdef voyager_hwdef = {
        .iommu_base   = 0x10000000,
        .tcx_base     = 0x50000000,
        .slavio_base  = 0x70000000,
        .ms_kb_base   = 0x71000000,
        .serial_base  = 0x71100000,
        .nvram_base   = 0x71200000,
        .fd_base      = 0x71400000,
        .counter_base = 0x71d00000,
        .intctl_base  = 0x71e00000,
        .idreg_base   = 0x78000000,
        .dma_base     = 0x78400000,
        .esp_base     = 0x78800000,
        .le_base      = 0x78c00000,
        .apc_base     = 0x71300000, /* pmc */
        .aux1_base    = 0x71900000,
        .aux2_base    = 0x71910000,
        .nvram_machine_id = 0x80,
        .machine_id = vger_id,
        .iommu_version = 0x05000000,
        .max_mem = 0x10000000,
    };

    mc->desc = "Sun4m platform, SPARCstation Voyager";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("Fujitsu-MB86904");
    smc->hwdef = &voyager_hwdef;
}

static void ss_lx_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    Sun4mMachineClass *smc = SUN4M_MACHINE_CLASS(mc);
    static const struct sun4m_hwdef ss_lx_hwdef = {
        .iommu_base   = 0x10000000,
        .iommu_pad_base = 0x10004000,
        .iommu_pad_len  = 0x0fffb000,
        .tcx_base     = 0x50000000,
        .slavio_base  = 0x70000000,
        .ms_kb_base   = 0x71000000,
        .serial_base  = 0x71100000,
        .nvram_base   = 0x71200000,
        .fd_base      = 0x71400000,
        .counter_base = 0x71d00000,
        .intctl_base  = 0x71e00000,
        .idreg_base   = 0x78000000,
        .dma_base     = 0x78400000,
        .esp_base     = 0x78800000,
        .le_base      = 0x78c00000,
        .aux1_base    = 0x71900000,
        .aux2_base    = 0x71910000,
        .nvram_machine_id = 0x80,
        .machine_id = lx_id,
        .iommu_version = 0x04000000,
        .max_mem = 0x10000000,
    };

    mc->desc = "Sun4m platform, SPARCstation LX";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("TI-MicroSparc-I");
    smc->hwdef = &ss_lx_hwdef;
}

static void ss4_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    Sun4mMachineClass *smc = SUN4M_MACHINE_CLASS(mc);
    static const struct sun4m_hwdef ss4_hwdef = {
        .iommu_base   = 0x10000000,
        .tcx_base     = 0x50000000,
        .cs_base      = 0x6c000000,
        .slavio_base  = 0x70000000,
        .ms_kb_base   = 0x71000000,
        .serial_base  = 0x71100000,
        .nvram_base   = 0x71200000,
        .fd_base      = 0x71400000,
        .counter_base = 0x71d00000,
        .intctl_base  = 0x71e00000,
        .idreg_base   = 0x78000000,
        .dma_base     = 0x78400000,
        .esp_base     = 0x78800000,
        .le_base      = 0x78c00000,
        .apc_base     = 0x6a000000,
        .aux1_base    = 0x71900000,
        .aux2_base    = 0x71910000,
        .nvram_machine_id = 0x80,
        .machine_id = ss4_id,
        .iommu_version = 0x05000000,
        .max_mem = 0x10000000,
    };

    mc->desc = "Sun4m platform, SPARCstation 4";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("Fujitsu-MB86904");
    smc->hwdef = &ss4_hwdef;
}

static void scls_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    Sun4mMachineClass *smc = SUN4M_MACHINE_CLASS(mc);
    static const struct sun4m_hwdef scls_hwdef = {
        .iommu_base   = 0x10000000,
        .tcx_base     = 0x50000000,
        .slavio_base  = 0x70000000,
        .ms_kb_base   = 0x71000000,
        .serial_base  = 0x71100000,
        .nvram_base   = 0x71200000,
        .fd_base      = 0x71400000,
        .counter_base = 0x71d00000,
        .intctl_base  = 0x71e00000,
        .idreg_base   = 0x78000000,
        .dma_base     = 0x78400000,
        .esp_base     = 0x78800000,
        .le_base      = 0x78c00000,
        .apc_base     = 0x6a000000,
        .aux1_base    = 0x71900000,
        .aux2_base    = 0x71910000,
        .nvram_machine_id = 0x80,
        .machine_id = scls_id,
        .iommu_version = 0x05000000,
        .max_mem = 0x10000000,
    };

    mc->desc = "Sun4m platform, SPARCClassic";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("TI-MicroSparc-I");
    smc->hwdef = &scls_hwdef;
}

static void sbook_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    Sun4mMachineClass *smc = SUN4M_MACHINE_CLASS(mc);
    static const struct sun4m_hwdef sbook_hwdef = {
        .iommu_base   = 0x10000000,
        .tcx_base     = 0x50000000, /* XXX */
        .slavio_base  = 0x70000000,
        .ms_kb_base   = 0x71000000,
        .serial_base  = 0x71100000,
        .nvram_base   = 0x71200000,
        .fd_base      = 0x71400000,
        .counter_base = 0x71d00000,
        .intctl_base  = 0x71e00000,
        .idreg_base   = 0x78000000,
        .dma_base     = 0x78400000,
        .esp_base     = 0x78800000,
        .le_base      = 0x78c00000,
        .apc_base     = 0x6a000000,
        .aux1_base    = 0x71900000,
        .aux2_base    = 0x71910000,
        .nvram_machine_id = 0x80,
        .machine_id = sbook_id,
        .iommu_version = 0x05000000,
        .max_mem = 0x10000000,
    };

    mc->desc = "Sun4m platform, SPARCbook";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("TI-MicroSparc-I");
    smc->hwdef = &sbook_hwdef;
}

static const TypeInfo sun4m_machine_types[] = {
    {
        .name           = MACHINE_TYPE_NAME("SS-5"),
        .parent         = TYPE_SUN4M_MACHINE,
        .class_init     = ss5_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("SS-10"),
        .parent         = TYPE_SUN4M_MACHINE,
        .class_init     = ss10_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("SS-600MP"),
        .parent         = TYPE_SUN4M_MACHINE,
        .class_init     = ss600mp_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("SS-20"),
        .parent         = TYPE_SUN4M_MACHINE,
        .class_init     = ss20_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("Voyager"),
        .parent         = TYPE_SUN4M_MACHINE,
        .class_init     = voyager_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("LX"),
        .parent         = TYPE_SUN4M_MACHINE,
        .class_init     = ss_lx_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("SS-4"),
        .parent         = TYPE_SUN4M_MACHINE,
        .class_init     = ss4_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("SPARCClassic"),
        .parent         = TYPE_SUN4M_MACHINE,
        .class_init     = scls_class_init,
    }, {
        .name           = MACHINE_TYPE_NAME("SPARCbook"),
        .parent         = TYPE_SUN4M_MACHINE,
        .class_init     = sbook_class_init,
    }, {
        .name           = TYPE_SUN4M_MACHINE,
        .parent         = TYPE_MACHINE,
        .class_size     = sizeof(Sun4mMachineClass),
        .class_init     = sun4m_machine_class_init,
        .abstract       = true,
    }
};

DEFINE_TYPES(sun4m_machine_types)

static void sun4m_register_types(void)
{
    type_register_static(&idreg_info);
    type_register_static(&afx_info);
    type_register_static(&prom_info);
    type_register_static(&ram_info);
}

type_init(sun4m_register_types)
