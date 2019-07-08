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
#include "qemu-common.h"
#include "cpu.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/sparc/sun4m_iommu.h"
#include "hw/timer/m48t59.h"
#include "hw/sparc/sparc32_dma.h"
#include "hw/block/fdc.h"
#include "sysemu/sysemu.h"
#include "net/net.h"
#include "hw/boards.h"
#include "hw/scsi/esp.h"
#include "hw/nvram/sun_nvram.h"
#include "hw/nvram/chrp_nvram.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/char/escc.h"
#include "hw/empty_slot.h"
#include "hw/loader.h"
#include "elf.h"
#include "trace.h"

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
    sysp_end = chrp_nvram_create_system_partition(image, 0);

    /* Free space partition */
    chrp_nvram_create_free_partition(&image[sysp_end], 0x1fd0 - sysp_end);

    Sun_init_header((struct Sun_nvram *)&image[0x1fd8], macaddr,
                    nvram_machine_id);

    for (i = 0; i < sizeof(image); i++) {
        (k->write)(nvram, i, image[i]);
    }
}

void cpu_check_irqs(CPUSPARCState *env)
{
    CPUState *cs;

    /* We should be holding the BQL before we mess with IRQs */
    g_assert(qemu_mutex_iothread_locked());

    if (env->pil_in && (env->interrupt_index == 0 ||
                        (env->interrupt_index & ~15) == TT_EXTINT)) {
        unsigned int i;

        for (i = 15; i > 0; i--) {
            if (env->pil_in & (1 << i)) {
                int old_interrupt = env->interrupt_index;

                env->interrupt_index = TT_EXTINT | i;
                if (old_interrupt != env->interrupt_index) {
                    cs = env_cpu(env);
                    trace_sun4m_cpu_interrupt(i);
                    cpu_interrupt(cs, CPU_INTERRUPT_HARD);
                }
                break;
            }
        }
    } else if (!env->pil_in && (env->interrupt_index & ~15) == TT_EXTINT) {
        cs = env_cpu(env);
        trace_sun4m_cpu_reset_interrupt(env->interrupt_index & 15);
        env->interrupt_index = 0;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
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

static void main_cpu_reset(void *opaque)
{
    SPARCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    cpu_reset(cs);
    cs->halted = 0;
}

static void secondary_cpu_reset(void *opaque)
{
    SPARCCPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    cpu_reset(cs);
    cs->halted = 1;
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
                               NULL, NULL, NULL, 1, EM_SPARC, 0, 0);
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

    dev = qdev_create(NULL, TYPE_SUN4M_IOMMU);
    qdev_prop_set_uint32(dev, "version", version);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(s, 0, irq);
    sysbus_mmio_map(s, 0, addr);

    return s;
}

static void *sparc32_dma_init(hwaddr dma_base,
                              hwaddr esp_base, qemu_irq espdma_irq,
                              hwaddr le_base, qemu_irq ledma_irq)
{
    DeviceState *dma;
    ESPDMADeviceState *espdma;
    LEDMADeviceState *ledma;
    SysBusESPState *esp;
    SysBusPCNetState *lance;

    dma = qdev_create(NULL, TYPE_SPARC32_DMA);
    qdev_init_nofail(dma);
    sysbus_mmio_map(SYS_BUS_DEVICE(dma), 0, dma_base);

    espdma = SPARC32_ESPDMA_DEVICE(object_resolve_path_component(
                                   OBJECT(dma), "espdma"));
    sysbus_connect_irq(SYS_BUS_DEVICE(espdma), 0, espdma_irq);

    esp = ESP_STATE(object_resolve_path_component(OBJECT(espdma), "esp"));
    sysbus_mmio_map(SYS_BUS_DEVICE(esp), 0, esp_base);
    scsi_bus_legacy_handle_cmdline(&esp->esp.bus);

    ledma = SPARC32_LEDMA_DEVICE(object_resolve_path_component(
                                 OBJECT(dma), "ledma"));
    sysbus_connect_irq(SYS_BUS_DEVICE(ledma), 0, ledma_irq);

    lance = SYSBUS_PCNET(object_resolve_path_component(
                         OBJECT(ledma), "lance"));
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

    dev = qdev_create(NULL, "slavio_intctl");
    qdev_init_nofail(dev);

    s = SYS_BUS_DEVICE(dev);

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

    dev = qdev_create(NULL, "slavio_timer");
    qdev_prop_set_uint32(dev, "num_cpus", num_cpus);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
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

    dev = qdev_create(NULL, "slavio_misc");
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
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

    dev = qdev_create(NULL, "eccmemctl");
    qdev_prop_set_uint32(dev, "version", version);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
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

    dev = qdev_create(NULL, "apc");
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    /* Power management (APC) XXX: not a Slavio device */
    sysbus_mmio_map(s, 0, power_base);
    sysbus_connect_irq(s, 0, cpu_halt);
}

static void tcx_init(hwaddr addr, qemu_irq irq, int vram_size, int width,
                     int height, int depth)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "SUNW,tcx");
    qdev_prop_set_uint32(dev, "vram_size", vram_size);
    qdev_prop_set_uint16(dev, "width", width);
    qdev_prop_set_uint16(dev, "height", height);
    qdev_prop_set_uint16(dev, "depth", depth);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);

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

    dev = qdev_create(NULL, "cgthree");
    qdev_prop_set_uint32(dev, "vram-size", vram_size);
    qdev_prop_set_uint16(dev, "width", width);
    qdev_prop_set_uint16(dev, "height", height);
    qdev_prop_set_uint16(dev, "depth", depth);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);

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

    dev = qdev_create(NULL, TYPE_MACIO_ID_REGISTER);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);

    sysbus_mmio_map(s, 0, addr);
    address_space_write_rom(&address_space_memory, addr,
                            MEMTXATTRS_UNSPECIFIED,
                            idreg_data, sizeof(idreg_data));
}

#define MACIO_ID_REGISTER(obj) \
    OBJECT_CHECK(IDRegState, (obj), TYPE_MACIO_ID_REGISTER)

typedef struct IDRegState {
    SysBusDevice parent_obj;

    MemoryRegion mem;
} IDRegState;

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
#define TCX_AFX(obj) OBJECT_CHECK(AFXState, (obj), TYPE_TCX_AFX)

typedef struct AFXState {
    SysBusDevice parent_obj;

    MemoryRegion mem;
} AFXState;

/* SS-5 TCX AFX register */
static void afx_init(hwaddr addr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, TYPE_TCX_AFX);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);

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
#define OPENPROM(obj) OBJECT_CHECK(PROMState, (obj), TYPE_OPENPROM)

typedef struct PROMState {
    SysBusDevice parent_obj;

    MemoryRegion prom;
} PROMState;

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

    dev = qdev_create(NULL, TYPE_OPENPROM);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);

    sysbus_mmio_map(s, 0, addr);

    /* load boot prom */
    if (bios_name == NULL) {
        bios_name = PROM_FILENAME;
    }
    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, bios_name);
    if (filename) {
        ret = load_elf(filename, NULL,
                       translate_prom_address, &addr, NULL,
                       NULL, NULL, 1, EM_SPARC, 0, 0);
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

    dc->props = prom_properties;
    dc->realize = prom_realize;
}

static const TypeInfo prom_info = {
    .name          = TYPE_OPENPROM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PROMState),
    .class_init    = prom_class_init,
};

#define TYPE_SUN4M_MEMORY "memory"
#define SUN4M_RAM(obj) OBJECT_CHECK(RamDevice, (obj), TYPE_SUN4M_MEMORY)

typedef struct RamDevice {
    SysBusDevice parent_obj;

    MemoryRegion ram;
    uint64_t size;
} RamDevice;

/* System RAM */
static void ram_realize(DeviceState *dev, Error **errp)
{
    RamDevice *d = SUN4M_RAM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_allocate_system_memory(&d->ram, OBJECT(d), "sun4m.ram",
                                         d->size);
    sysbus_init_mmio(sbd, &d->ram);
}

static void ram_init(hwaddr addr, ram_addr_t RAM_size,
                     uint64_t max_mem)
{
    DeviceState *dev;
    SysBusDevice *s;
    RamDevice *d;

    /* allocate RAM */
    if ((uint64_t)RAM_size > max_mem) {
        error_report("Too much memory for this machine: %" PRId64 ","
                     " maximum %" PRId64,
                     RAM_size / MiB, max_mem / MiB);
        exit(1);
    }
    dev = qdev_create(NULL, "memory");
    s = SYS_BUS_DEVICE(dev);

    d = SUN4M_RAM(dev);
    d->size = RAM_size;
    qdev_init_nofail(dev);

    sysbus_mmio_map(s, 0, addr);
}

static Property ram_properties[] = {
    DEFINE_PROP_UINT64("size", RamDevice, size, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void ram_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ram_realize;
    dc->props = ram_properties;
}

static const TypeInfo ram_info = {
    .name          = TYPE_SUN4M_MEMORY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RamDevice),
    .class_init    = ram_class_init,
};

static void cpu_devinit(const char *cpu_type, unsigned int id,
                        uint64_t prom_addr, qemu_irq **cpu_irqs)
{
    CPUState *cs;
    SPARCCPU *cpu;
    CPUSPARCState *env;

    cpu = SPARC_CPU(cpu_create(cpu_type));
    env = &cpu->env;

    cpu_sparc_set_id(env, id);
    if (id == 0) {
        qemu_register_reset(main_cpu_reset, cpu);
    } else {
        qemu_register_reset(secondary_cpu_reset, cpu);
        cs = CPU(cpu);
        cs->halted = 1;
    }
    *cpu_irqs = qemu_allocate_irqs(cpu_set_irq, cpu, MAX_PILS);
    env->prom_addr = prom_addr;
}

static void dummy_fdc_tc(void *opaque, int irq, int level)
{
}

static void sun4m_hw_init(const struct sun4m_hwdef *hwdef,
                          MachineState *machine)
{
    DeviceState *slavio_intctl;
    unsigned int i;
    void *nvram;
    qemu_irq *cpu_irqs[MAX_CPUS], slavio_irq[32], slavio_cpu_irq[MAX_CPUS];
    qemu_irq fdc_tc;
    unsigned long kernel_size;
    uint32_t initrd_size;
    DriveInfo *fd[MAX_FD];
    FWCfgState *fw_cfg;
    DeviceState *dev;
    SysBusDevice *s;
    unsigned int smp_cpus = machine->smp.cpus;
    unsigned int max_cpus = machine->smp.max_cpus;

    /* init CPUs */
    for(i = 0; i < smp_cpus; i++) {
        cpu_devinit(machine->cpu_type, i, hwdef->slavio_base, &cpu_irqs[i]);
    }

    for (i = smp_cpus; i < MAX_CPUS; i++)
        cpu_irqs[i] = qemu_allocate_irqs(dummy_cpu_set_irq, NULL, MAX_PILS);


    /* set up devices */
    ram_init(0, machine->ram_size, hwdef->max_mem);
    /* models without ECC don't trap when missing ram is accessed */
    if (!hwdef->ecc_base) {
        empty_slot_init(machine->ram_size, hwdef->max_mem - machine->ram_size);
    }

    prom_init(hwdef->slavio_base, bios_name);

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
        empty_slot_init(hwdef->iommu_pad_base,hwdef->iommu_pad_len);
    }

    sparc32_dma_init(hwdef->dma_base,
                     hwdef->esp_base, slavio_irq[18],
                     hwdef->le_base, slavio_irq[16]);

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
        }
    }

    for (i = 0; i < MAX_VSIMMS; i++) {
        /* vsimm registers probed by OBP */
        if (hwdef->vsimm[i].reg_base) {
            empty_slot_init(hwdef->vsimm[i].reg_base, 0x2000);
        }
    }

    if (hwdef->sx_base) {
        empty_slot_init(hwdef->sx_base, 0x2000);
    }

    nvram = m48t59_init(slavio_irq[0], hwdef->nvram_base, 0, 0x2000, 1968, 8);

    slavio_timer_init_all(hwdef->counter_base, slavio_irq[19], slavio_cpu_irq, smp_cpus);

    /* Slavio TTYA (base+4, Linux ttyS0) is the first QEMU serial device
       Slavio TTYB (base+0, Linux ttyS1) is the second QEMU serial device */
    dev = qdev_create(NULL, TYPE_ESCC);
    qdev_prop_set_uint32(dev, "disabled", !machine->enable_graphics);
    qdev_prop_set_uint32(dev, "frequency", ESCC_CLOCK);
    qdev_prop_set_uint32(dev, "it_shift", 1);
    qdev_prop_set_chr(dev, "chrB", NULL);
    qdev_prop_set_chr(dev, "chrA", NULL);
    qdev_prop_set_uint32(dev, "chnBtype", escc_mouse);
    qdev_prop_set_uint32(dev, "chnAtype", escc_kbd);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(s, 0, slavio_irq[14]);
    sysbus_connect_irq(s, 1, slavio_irq[14]);
    sysbus_mmio_map(s, 0, hwdef->ms_kb_base);

    dev = qdev_create(NULL, TYPE_ESCC);
    qdev_prop_set_uint32(dev, "disabled", 0);
    qdev_prop_set_uint32(dev, "frequency", ESCC_CLOCK);
    qdev_prop_set_uint32(dev, "it_shift", 1);
    qdev_prop_set_chr(dev, "chrB", serial_hd(1));
    qdev_prop_set_chr(dev, "chrA", serial_hd(0));
    qdev_prop_set_uint32(dev, "chnBtype", escc_serial);
    qdev_prop_set_uint32(dev, "chnAtype", escc_serial);
    qdev_init_nofail(dev);

    s = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(s, 0, slavio_irq[15]);
    sysbus_connect_irq(s, 1,  slavio_irq[15]);
    sysbus_mmio_map(s, 0, hwdef->serial_base);

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
        sysbus_create_simple("SUNW,CS4231", hwdef->cs_base,
                             slavio_irq[5]);
    }

    if (hwdef->dbri_base) {
        /* ISDN chip with attached CS4215 audio codec */
        /* prom space */
        empty_slot_init(hwdef->dbri_base+0x1000, 0x30);
        /* reg space */
        empty_slot_init(hwdef->dbri_base+0x10000, 0x100);
    }

    if (hwdef->bpp_base) {
        /* parallel port */
        empty_slot_init(hwdef->bpp_base, 0x20);
    }

    initrd_size = 0;
    kernel_size = sun4m_load_kernel(machine->kernel_filename,
                                    machine->initrd_filename,
                                    machine->ram_size, &initrd_size);

    nvram_init(nvram, (uint8_t *)&nd_table[0].macaddr, machine->kernel_cmdline,
               machine->boot_order, machine->ram_size, kernel_size,
               graphic_width, graphic_height, graphic_depth,
               hwdef->nvram_machine_id, "Sun4m");

    if (hwdef->ecc_base)
        ecc_init(hwdef->ecc_base, slavio_irq[28],
                 hwdef->ecc_version);

    dev = qdev_create(NULL, TYPE_FW_CFG_MEM);
    fw_cfg = FW_CFG(dev);
    qdev_prop_set_uint32(dev, "data_width", 1);
    qdev_prop_set_bit(dev, "dma_enabled", false);
    object_property_add_child(OBJECT(qdev_get_machine()), TYPE_FW_CFG,
                              OBJECT(fw_cfg), NULL);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(s, 0, CFG_ADDR);
    sysbus_mmio_map(s, 1, CFG_ADDR + 2);

    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, (uint16_t)smp_cpus);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)max_cpus);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
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
    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, machine->boot_order[0]);
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

static const struct sun4m_hwdef sun4m_hwdefs[] = {
    /* SS-5 */
    {
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
    },
    /* SS-10 */
    {
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
        .apc_base     = 0xefa000000ULL, // XXX should not exist
        .aux1_base    = 0xff1800000ULL,
        .aux2_base    = 0xff1a01000ULL,
        .ecc_base     = 0xf00000000ULL,
        .ecc_version  = 0x10000000, // version 0, implementation 1
        .nvram_machine_id = 0x72,
        .machine_id = ss10_id,
        .iommu_version = 0x03000000,
        .max_mem = 0xf00000000ULL,
    },
    /* SS-600MP */
    {
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
        .apc_base     = 0xefa000000ULL, // XXX should not exist
        .aux1_base    = 0xff1800000ULL,
        .aux2_base    = 0xff1a01000ULL, // XXX should not exist
        .ecc_base     = 0xf00000000ULL,
        .ecc_version  = 0x00000000, // version 0, implementation 0
        .nvram_machine_id = 0x71,
        .machine_id = ss600mp_id,
        .iommu_version = 0x01000000,
        .max_mem = 0xf00000000ULL,
    },
    /* SS-20 */
    {
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
        .apc_base     = 0xefa000000ULL, // XXX should not exist
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
        .ecc_version  = 0x20000000, // version 0, implementation 2
        .nvram_machine_id = 0x72,
        .machine_id = ss20_id,
        .iommu_version = 0x13000000,
        .max_mem = 0xf00000000ULL,
    },
    /* Voyager */
    {
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
        .apc_base     = 0x71300000, // pmc
        .aux1_base    = 0x71900000,
        .aux2_base    = 0x71910000,
        .nvram_machine_id = 0x80,
        .machine_id = vger_id,
        .iommu_version = 0x05000000,
        .max_mem = 0x10000000,
    },
    /* LX */
    {
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
    },
    /* SS-4 */
    {
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
    },
    /* SPARCClassic */
    {
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
    },
    /* SPARCbook */
    {
        .iommu_base   = 0x10000000,
        .tcx_base     = 0x50000000, // XXX
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
    },
};

/* SPARCstation 5 hardware initialisation */
static void ss5_init(MachineState *machine)
{
    sun4m_hw_init(&sun4m_hwdefs[0], machine);
}

/* SPARCstation 10 hardware initialisation */
static void ss10_init(MachineState *machine)
{
    sun4m_hw_init(&sun4m_hwdefs[1], machine);
}

/* SPARCserver 600MP hardware initialisation */
static void ss600mp_init(MachineState *machine)
{
    sun4m_hw_init(&sun4m_hwdefs[2], machine);
}

/* SPARCstation 20 hardware initialisation */
static void ss20_init(MachineState *machine)
{
    sun4m_hw_init(&sun4m_hwdefs[3], machine);
}

/* SPARCstation Voyager hardware initialisation */
static void vger_init(MachineState *machine)
{
    sun4m_hw_init(&sun4m_hwdefs[4], machine);
}

/* SPARCstation LX hardware initialisation */
static void ss_lx_init(MachineState *machine)
{
    sun4m_hw_init(&sun4m_hwdefs[5], machine);
}

/* SPARCstation 4 hardware initialisation */
static void ss4_init(MachineState *machine)
{
    sun4m_hw_init(&sun4m_hwdefs[6], machine);
}

/* SPARCClassic hardware initialisation */
static void scls_init(MachineState *machine)
{
    sun4m_hw_init(&sun4m_hwdefs[7], machine);
}

/* SPARCbook hardware initialisation */
static void sbook_init(MachineState *machine)
{
    sun4m_hw_init(&sun4m_hwdefs[8], machine);
}

static void ss5_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4m platform, SPARCstation 5";
    mc->init = ss5_init;
    mc->block_default_type = IF_SCSI;
    mc->is_default = 1;
    mc->default_boot_order = "c";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("Fujitsu-MB86904");
    mc->default_display = "tcx";
}

static const TypeInfo ss5_type = {
    .name = MACHINE_TYPE_NAME("SS-5"),
    .parent = TYPE_MACHINE,
    .class_init = ss5_class_init,
};

static void ss10_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4m platform, SPARCstation 10";
    mc->init = ss10_init;
    mc->block_default_type = IF_SCSI;
    mc->max_cpus = 4;
    mc->default_boot_order = "c";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("TI-SuperSparc-II");
    mc->default_display = "tcx";
}

static const TypeInfo ss10_type = {
    .name = MACHINE_TYPE_NAME("SS-10"),
    .parent = TYPE_MACHINE,
    .class_init = ss10_class_init,
};

static void ss600mp_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4m platform, SPARCserver 600MP";
    mc->init = ss600mp_init;
    mc->block_default_type = IF_SCSI;
    mc->max_cpus = 4;
    mc->default_boot_order = "c";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("TI-SuperSparc-II");
    mc->default_display = "tcx";
}

static const TypeInfo ss600mp_type = {
    .name = MACHINE_TYPE_NAME("SS-600MP"),
    .parent = TYPE_MACHINE,
    .class_init = ss600mp_class_init,
};

static void ss20_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4m platform, SPARCstation 20";
    mc->init = ss20_init;
    mc->block_default_type = IF_SCSI;
    mc->max_cpus = 4;
    mc->default_boot_order = "c";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("TI-SuperSparc-II");
    mc->default_display = "tcx";
}

static const TypeInfo ss20_type = {
    .name = MACHINE_TYPE_NAME("SS-20"),
    .parent = TYPE_MACHINE,
    .class_init = ss20_class_init,
};

static void voyager_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4m platform, SPARCstation Voyager";
    mc->init = vger_init;
    mc->block_default_type = IF_SCSI;
    mc->default_boot_order = "c";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("Fujitsu-MB86904");
    mc->default_display = "tcx";
}

static const TypeInfo voyager_type = {
    .name = MACHINE_TYPE_NAME("Voyager"),
    .parent = TYPE_MACHINE,
    .class_init = voyager_class_init,
};

static void ss_lx_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4m platform, SPARCstation LX";
    mc->init = ss_lx_init;
    mc->block_default_type = IF_SCSI;
    mc->default_boot_order = "c";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("TI-MicroSparc-I");
    mc->default_display = "tcx";
}

static const TypeInfo ss_lx_type = {
    .name = MACHINE_TYPE_NAME("LX"),
    .parent = TYPE_MACHINE,
    .class_init = ss_lx_class_init,
};

static void ss4_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4m platform, SPARCstation 4";
    mc->init = ss4_init;
    mc->block_default_type = IF_SCSI;
    mc->default_boot_order = "c";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("Fujitsu-MB86904");
    mc->default_display = "tcx";
}

static const TypeInfo ss4_type = {
    .name = MACHINE_TYPE_NAME("SS-4"),
    .parent = TYPE_MACHINE,
    .class_init = ss4_class_init,
};

static void scls_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4m platform, SPARCClassic";
    mc->init = scls_init;
    mc->block_default_type = IF_SCSI;
    mc->default_boot_order = "c";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("TI-MicroSparc-I");
    mc->default_display = "tcx";
}

static const TypeInfo scls_type = {
    .name = MACHINE_TYPE_NAME("SPARCClassic"),
    .parent = TYPE_MACHINE,
    .class_init = scls_class_init,
};

static void sbook_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4m platform, SPARCbook";
    mc->init = sbook_init;
    mc->block_default_type = IF_SCSI;
    mc->default_boot_order = "c";
    mc->default_cpu_type = SPARC_CPU_TYPE_NAME("TI-MicroSparc-I");
    mc->default_display = "tcx";
}

static const TypeInfo sbook_type = {
    .name = MACHINE_TYPE_NAME("SPARCbook"),
    .parent = TYPE_MACHINE,
    .class_init = sbook_class_init,
};

static void sun4m_register_types(void)
{
    type_register_static(&idreg_info);
    type_register_static(&afx_info);
    type_register_static(&prom_info);
    type_register_static(&ram_info);

    type_register_static(&ss5_type);
    type_register_static(&ss10_type);
    type_register_static(&ss600mp_type);
    type_register_static(&ss20_type);
    type_register_static(&voyager_type);
    type_register_static(&ss_lx_type);
    type_register_static(&ss4_type);
    type_register_static(&scls_type);
    type_register_static(&sbook_type);
}

type_init(sun4m_register_types)
