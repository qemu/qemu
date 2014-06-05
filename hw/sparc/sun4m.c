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
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/sparc/sun4m.h"
#include "hw/timer/m48t59.h"
#include "hw/sparc/sparc32_dma.h"
#include "hw/block/fdc.h"
#include "sysemu/sysemu.h"
#include "net/net.h"
#include "hw/boards.h"
#include "hw/nvram/openbios_firmware_abi.h"
#include "hw/scsi/esp.h"
#include "hw/i386/pc.h"
#include "hw/isa/isa.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/char/escc.h"
#include "hw/empty_slot.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/blockdev.h"
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
#define PROM_SIZE_MAX        (1024 * 1024)
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
    const char * const default_cpu_model;
    uint32_t ecc_version;
    uint32_t iommu_version;
    uint16_t machine_id;
    uint8_t nvram_machine_id;
};

int DMA_get_channel_mode (int nchan)
{
    return 0;
}
int DMA_read_memory (int nchan, void *buf, int pos, int size)
{
    return 0;
}
int DMA_write_memory (int nchan, void *buf, int pos, int size)
{
    return 0;
}
void DMA_hold_DREQ (int nchan) {}
void DMA_release_DREQ (int nchan) {}
void DMA_schedule(int nchan) {}

void DMA_init(int high_page_enable, qemu_irq *cpu_request_exit)
{
}

void DMA_register_channel (int nchan,
                           DMA_transfer_handler transfer_handler,
                           void *opaque)
{
}

static int fw_cfg_boot_set(void *opaque, const char *boot_device)
{
    fw_cfg_add_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
    return 0;
}

static void nvram_init(M48t59State *nvram, uint8_t *macaddr,
                       const char *cmdline, const char *boot_devices,
                       ram_addr_t RAM_size, uint32_t kernel_size,
                       int width, int height, int depth,
                       int nvram_machine_id, const char *arch)
{
    unsigned int i;
    uint32_t start, end;
    uint8_t image[0x1ff0];
    struct OpenBIOS_nvpart_v1 *part_header;

    memset(image, '\0', sizeof(image));

    start = 0;

    // OpenBIOS nvram variables
    // Variable partition
    part_header = (struct OpenBIOS_nvpart_v1 *)&image[start];
    part_header->signature = OPENBIOS_PART_SYSTEM;
    pstrcpy(part_header->name, sizeof(part_header->name), "system");

    end = start + sizeof(struct OpenBIOS_nvpart_v1);
    for (i = 0; i < nb_prom_envs; i++)
        end = OpenBIOS_set_var(image, end, prom_envs[i]);

    // End marker
    image[end++] = '\0';

    end = start + ((end - start + 15) & ~15);
    OpenBIOS_finish_partition(part_header, end - start);

    // free partition
    start = end;
    part_header = (struct OpenBIOS_nvpart_v1 *)&image[start];
    part_header->signature = OPENBIOS_PART_FREE;
    pstrcpy(part_header->name, sizeof(part_header->name), "free");

    end = 0x1fd0;
    OpenBIOS_finish_partition(part_header, end - start);

    Sun_init_header((struct Sun_nvram *)&image[0x1fd8], macaddr,
                    nvram_machine_id);

    for (i = 0; i < sizeof(image); i++)
        m48t59_write(nvram, i, image[i]);
}

static DeviceState *slavio_intctl;

void sun4m_pic_info(Monitor *mon, const QDict *qdict)
{
    if (slavio_intctl)
        slavio_pic_info(mon, slavio_intctl);
}

void sun4m_irq_info(Monitor *mon, const QDict *qdict)
{
    if (slavio_intctl)
        slavio_irq_info(mon, slavio_intctl);
}

void cpu_check_irqs(CPUSPARCState *env)
{
    CPUState *cs;

    if (env->pil_in && (env->interrupt_index == 0 ||
                        (env->interrupt_index & ~15) == TT_EXTINT)) {
        unsigned int i;

        for (i = 15; i > 0; i--) {
            if (env->pil_in & (1 << i)) {
                int old_interrupt = env->interrupt_index;

                env->interrupt_index = TT_EXTINT | i;
                if (old_interrupt != env->interrupt_index) {
                    cs = CPU(sparc_env_get_cpu(env));
                    trace_sun4m_cpu_interrupt(i);
                    cpu_interrupt(cs, CPU_INTERRUPT_HARD);
                }
                break;
            }
        }
    } else if (!env->pil_in && (env->interrupt_index & ~15) == TT_EXTINT) {
        cs = CPU(sparc_env_get_cpu(env));
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
                                       ram_addr_t RAM_size)
{
    int linux_boot;
    unsigned int i;
    long initrd_size, kernel_size;
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
        kernel_size = load_elf(kernel_filename, translate_kernel_address, NULL,
                               NULL, NULL, NULL, 1, ELF_MACHINE, 0);
        if (kernel_size < 0)
            kernel_size = load_aout(kernel_filename, KERNEL_LOAD_ADDR,
                                    RAM_size - KERNEL_LOAD_ADDR, bswap_needed,
                                    TARGET_PAGE_SIZE);
        if (kernel_size < 0)
            kernel_size = load_image_targphys(kernel_filename,
                                              KERNEL_LOAD_ADDR,
                                              RAM_size - KERNEL_LOAD_ADDR);
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }

        /* load initrd */
        initrd_size = 0;
        if (initrd_filename) {
            initrd_size = load_image_targphys(initrd_filename,
                                              INITRD_LOAD_ADDR,
                                              RAM_size - INITRD_LOAD_ADDR);
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                        initrd_filename);
                exit(1);
            }
        }
        if (initrd_size > 0) {
            for (i = 0; i < 64 * TARGET_PAGE_SIZE; i += TARGET_PAGE_SIZE) {
                ptr = rom_ptr(KERNEL_LOAD_ADDR + i);
                if (ldl_p(ptr) == 0x48647253) { // HdrS
                    stl_p(ptr + 16, INITRD_LOAD_ADDR);
                    stl_p(ptr + 20, initrd_size);
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

    dev = qdev_create(NULL, "iommu");
    qdev_prop_set_uint32(dev, "version", version);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(s, 0, irq);
    sysbus_mmio_map(s, 0, addr);

    return s;
}

static void *sparc32_dma_init(hwaddr daddr, qemu_irq parent_irq,
                              void *iommu, qemu_irq *dev_irq, int is_ledma)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "sparc32_dma");
    qdev_prop_set_ptr(dev, "iommu_opaque", iommu);
    qdev_prop_set_uint32(dev, "is_ledma", is_ledma);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(s, 0, parent_irq);
    *dev_irq = qdev_get_gpio_in(dev, 0);
    sysbus_mmio_map(s, 0, daddr);

    return s;
}

static void lance_init(NICInfo *nd, hwaddr leaddr,
                       void *dma_opaque, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;
    qemu_irq reset;

    qemu_check_nic_model(&nd_table[0], "lance");

    dev = qdev_create(NULL, "lance");
    qdev_set_nic_properties(dev, nd);
    qdev_prop_set_ptr(dev, "dma", dma_opaque);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    sysbus_mmio_map(s, 0, leaddr);
    sysbus_connect_irq(s, 0, irq);
    reset = qdev_get_gpio_in(dev, 0);
    qdev_connect_gpio_out(dma_opaque, 0, reset);
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

static void tcx_init(hwaddr addr, int vram_size, int width,
                     int height, int depth)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "SUNW,tcx");
    qdev_prop_set_uint32(dev, "vram_size", vram_size);
    qdev_prop_set_uint16(dev, "width", width);
    qdev_prop_set_uint16(dev, "height", height);
    qdev_prop_set_uint16(dev, "depth", depth);
    qdev_prop_set_uint64(dev, "prom_addr", addr);
    qdev_init_nofail(dev);
    s = SYS_BUS_DEVICE(dev);
    /* FCode ROM */
    sysbus_mmio_map(s, 0, addr);
    /* DAC */
    sysbus_mmio_map(s, 1, addr + 0x00200000ULL);
    /* TEC (dummy) */
    sysbus_mmio_map(s, 2, addr + 0x00700000ULL);
    /* THC 24 bit: NetBSD writes here even with 8-bit display: dummy */
    sysbus_mmio_map(s, 3, addr + 0x00301000ULL);
    /* 8-bit plane */
    sysbus_mmio_map(s, 4, addr + 0x00800000ULL);
    if (depth == 24) {
        /* 24-bit plane */
        sysbus_mmio_map(s, 5, addr + 0x02000000ULL);
        /* Control plane */
        sysbus_mmio_map(s, 6, addr + 0x0a000000ULL);
    } else {
        /* THC 8 bit (dummy) */
        sysbus_mmio_map(s, 5, addr + 0x00300000ULL);
    }
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
    qdev_prop_set_uint64(dev, "prom-addr", addr);
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
    cpu_physical_memory_write_rom(&address_space_memory,
                                  addr, idreg_data, sizeof(idreg_data));
}

#define MACIO_ID_REGISTER(obj) \
    OBJECT_CHECK(IDRegState, (obj), TYPE_MACIO_ID_REGISTER)

typedef struct IDRegState {
    SysBusDevice parent_obj;

    MemoryRegion mem;
} IDRegState;

static int idreg_init1(SysBusDevice *dev)
{
    IDRegState *s = MACIO_ID_REGISTER(dev);

    memory_region_init_ram(&s->mem, OBJECT(s),
                           "sun4m.idreg", sizeof(idreg_data));
    vmstate_register_ram_global(&s->mem);
    memory_region_set_readonly(&s->mem, true);
    sysbus_init_mmio(dev, &s->mem);
    return 0;
}

static void idreg_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = idreg_init1;
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

static int afx_init1(SysBusDevice *dev)
{
    AFXState *s = TCX_AFX(dev);

    memory_region_init_ram(&s->mem, OBJECT(s), "sun4m.afx", 4);
    vmstate_register_ram_global(&s->mem);
    sysbus_init_mmio(dev, &s->mem);
    return 0;
}

static void afx_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = afx_init1;
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
        ret = load_elf(filename, translate_prom_address, &addr, NULL,
                       NULL, NULL, 1, ELF_MACHINE, 0);
        if (ret < 0 || ret > PROM_SIZE_MAX) {
            ret = load_image_targphys(filename, addr, PROM_SIZE_MAX);
        }
        g_free(filename);
    } else {
        ret = -1;
    }
    if (ret < 0 || ret > PROM_SIZE_MAX) {
        fprintf(stderr, "qemu: could not load prom '%s'\n", bios_name);
        exit(1);
    }
}

static int prom_init1(SysBusDevice *dev)
{
    PROMState *s = OPENPROM(dev);

    memory_region_init_ram(&s->prom, OBJECT(s), "sun4m.prom", PROM_SIZE_MAX);
    vmstate_register_ram_global(&s->prom);
    memory_region_set_readonly(&s->prom, true);
    sysbus_init_mmio(dev, &s->prom);
    return 0;
}

static Property prom_properties[] = {
    {/* end of property list */},
};

static void prom_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = prom_init1;
    dc->props = prom_properties;
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
static int ram_init1(SysBusDevice *dev)
{
    RamDevice *d = SUN4M_RAM(dev);

    memory_region_init_ram(&d->ram, OBJECT(d), "sun4m.ram", d->size);
    vmstate_register_ram_global(&d->ram);
    sysbus_init_mmio(dev, &d->ram);
    return 0;
}

static void ram_init(hwaddr addr, ram_addr_t RAM_size,
                     uint64_t max_mem)
{
    DeviceState *dev;
    SysBusDevice *s;
    RamDevice *d;

    /* allocate RAM */
    if ((uint64_t)RAM_size > max_mem) {
        fprintf(stderr,
                "qemu: Too much memory for this machine: %d, maximum %d\n",
                (unsigned int)(RAM_size / (1024 * 1024)),
                (unsigned int)(max_mem / (1024 * 1024)));
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
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = ram_init1;
    dc->props = ram_properties;
}

static const TypeInfo ram_info = {
    .name          = TYPE_SUN4M_MEMORY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RamDevice),
    .class_init    = ram_class_init,
};

static void cpu_devinit(const char *cpu_model, unsigned int id,
                        uint64_t prom_addr, qemu_irq **cpu_irqs)
{
    CPUState *cs;
    SPARCCPU *cpu;
    CPUSPARCState *env;

    cpu = cpu_sparc_init(cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "qemu: Unable to find Sparc CPU definition\n");
        exit(1);
    }
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
    const char *cpu_model = machine->cpu_model;
    unsigned int i;
    void *iommu, *espdma, *ledma, *nvram;
    qemu_irq *cpu_irqs[MAX_CPUS], slavio_irq[32], slavio_cpu_irq[MAX_CPUS],
        espdma_irq, ledma_irq;
    qemu_irq esp_reset, dma_enable;
    qemu_irq fdc_tc;
    qemu_irq *cpu_halt;
    unsigned long kernel_size;
    DriveInfo *fd[MAX_FD];
    FWCfgState *fw_cfg;
    unsigned int num_vsimms;

    /* init CPUs */
    if (!cpu_model)
        cpu_model = hwdef->default_cpu_model;

    for(i = 0; i < smp_cpus; i++) {
        cpu_devinit(cpu_model, i, hwdef->slavio_base, &cpu_irqs[i]);
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

    iommu = iommu_init(hwdef->iommu_base, hwdef->iommu_version,
                       slavio_irq[30]);

    if (hwdef->iommu_pad_base) {
        /* On the real hardware (SS-5, LX) the MMU is not padded, but aliased.
           Software shouldn't use aliased addresses, neither should it crash
           when does. Using empty_slot instead of aliasing can help with
           debugging such accesses */
        empty_slot_init(hwdef->iommu_pad_base,hwdef->iommu_pad_len);
    }

    espdma = sparc32_dma_init(hwdef->dma_base, slavio_irq[18],
                              iommu, &espdma_irq, 0);

    ledma = sparc32_dma_init(hwdef->dma_base + 16ULL,
                             slavio_irq[16], iommu, &ledma_irq, 1);

    if (graphic_depth != 8 && graphic_depth != 24) {
        error_report("Unsupported depth: %d", graphic_depth);
        exit (1);
    }
    num_vsimms = 0;
    if (num_vsimms == 0) {
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

            tcx_init(hwdef->tcx_base, 0x00100000, graphic_width, graphic_height,
                     graphic_depth);
        }
    }

    for (i = num_vsimms; i < MAX_VSIMMS; i++) {
        /* vsimm registers probed by OBP */
        if (hwdef->vsimm[i].reg_base) {
            empty_slot_init(hwdef->vsimm[i].reg_base, 0x2000);
        }
    }

    if (hwdef->sx_base) {
        empty_slot_init(hwdef->sx_base, 0x2000);
    }

    lance_init(&nd_table[0], hwdef->le_base, ledma, ledma_irq);

    nvram = m48t59_init(slavio_irq[0], hwdef->nvram_base, 0, 0x2000, 8);

    slavio_timer_init_all(hwdef->counter_base, slavio_irq[19], slavio_cpu_irq, smp_cpus);

    slavio_serial_ms_kbd_init(hwdef->ms_kb_base, slavio_irq[14],
                              display_type == DT_NOGRAPHIC, ESCC_CLOCK, 1);
    /* Slavio TTYA (base+4, Linux ttyS0) is the first QEMU serial device
       Slavio TTYB (base+0, Linux ttyS1) is the second QEMU serial device */
    escc_init(hwdef->serial_base, slavio_irq[15], slavio_irq[15],
              serial_hds[0], serial_hds[1], ESCC_CLOCK, 1);

    cpu_halt = qemu_allocate_irqs(cpu_halt_signal, NULL, 1);
    if (hwdef->apc_base) {
        apc_init(hwdef->apc_base, cpu_halt[0]);
    }

    if (hwdef->fd_base) {
        /* there is zero or one floppy drive */
        memset(fd, 0, sizeof(fd));
        fd[0] = drive_get(IF_FLOPPY, 0, 0);
        sun4m_fdctrl_init(slavio_irq[22], hwdef->fd_base, fd,
                          &fdc_tc);
    } else {
        fdc_tc = *qemu_allocate_irqs(dummy_fdc_tc, NULL, 1);
    }

    slavio_misc_init(hwdef->slavio_base, hwdef->aux1_base, hwdef->aux2_base,
                     slavio_irq[30], fdc_tc);

    if (drive_get_max_bus(IF_SCSI) > 0) {
        fprintf(stderr, "qemu: too many SCSI bus\n");
        exit(1);
    }

    esp_init(hwdef->esp_base, 2,
             espdma_memory_read, espdma_memory_write,
             espdma, espdma_irq, &esp_reset, &dma_enable);

    qdev_connect_gpio_out(espdma, 0, esp_reset);
    qdev_connect_gpio_out(espdma, 1, dma_enable);

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

    kernel_size = sun4m_load_kernel(machine->kernel_filename,
                                    machine->initrd_filename,
                                    machine->ram_size);

    nvram_init(nvram, (uint8_t *)&nd_table[0].macaddr, machine->kernel_cmdline,
               machine->boot_order, machine->ram_size, kernel_size,
               graphic_width, graphic_height, graphic_depth,
               hwdef->nvram_machine_id, "Sun4m");

    if (hwdef->ecc_base)
        ecc_init(hwdef->ecc_base, slavio_irq[28],
                 hwdef->ecc_version);

    fw_cfg = fw_cfg_init(0, 0, CFG_ADDR, CFG_ADDR + 2);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)max_cpus);
    fw_cfg_add_i32(fw_cfg, FW_CFG_ID, 1);
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
    fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, 0); // not used
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
        .default_cpu_model = "Fujitsu MB86904",
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
        .default_cpu_model = "TI SuperSparc II",
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
        .default_cpu_model = "TI SuperSparc II",
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
        .default_cpu_model = "TI SuperSparc II",
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
        .default_cpu_model = "Fujitsu MB86904",
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
        .default_cpu_model = "TI MicroSparc I",
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
        .default_cpu_model = "Fujitsu MB86904",
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
        .default_cpu_model = "TI MicroSparc I",
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
        .default_cpu_model = "TI MicroSparc I",
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

static QEMUMachine ss5_machine = {
    .name = "SS-5",
    .desc = "Sun4m platform, SPARCstation 5",
    .init = ss5_init,
    .block_default_type = IF_SCSI,
    .is_default = 1,
    .default_boot_order = "c",
};

static QEMUMachine ss10_machine = {
    .name = "SS-10",
    .desc = "Sun4m platform, SPARCstation 10",
    .init = ss10_init,
    .block_default_type = IF_SCSI,
    .max_cpus = 4,
    .default_boot_order = "c",
};

static QEMUMachine ss600mp_machine = {
    .name = "SS-600MP",
    .desc = "Sun4m platform, SPARCserver 600MP",
    .init = ss600mp_init,
    .block_default_type = IF_SCSI,
    .max_cpus = 4,
    .default_boot_order = "c",
};

static QEMUMachine ss20_machine = {
    .name = "SS-20",
    .desc = "Sun4m platform, SPARCstation 20",
    .init = ss20_init,
    .block_default_type = IF_SCSI,
    .max_cpus = 4,
    .default_boot_order = "c",
};

static QEMUMachine voyager_machine = {
    .name = "Voyager",
    .desc = "Sun4m platform, SPARCstation Voyager",
    .init = vger_init,
    .block_default_type = IF_SCSI,
    .default_boot_order = "c",
};

static QEMUMachine ss_lx_machine = {
    .name = "LX",
    .desc = "Sun4m platform, SPARCstation LX",
    .init = ss_lx_init,
    .block_default_type = IF_SCSI,
    .default_boot_order = "c",
};

static QEMUMachine ss4_machine = {
    .name = "SS-4",
    .desc = "Sun4m platform, SPARCstation 4",
    .init = ss4_init,
    .block_default_type = IF_SCSI,
    .default_boot_order = "c",
};

static QEMUMachine scls_machine = {
    .name = "SPARCClassic",
    .desc = "Sun4m platform, SPARCClassic",
    .init = scls_init,
    .block_default_type = IF_SCSI,
    .default_boot_order = "c",
};

static QEMUMachine sbook_machine = {
    .name = "SPARCbook",
    .desc = "Sun4m platform, SPARCbook",
    .init = sbook_init,
    .block_default_type = IF_SCSI,
    .default_boot_order = "c",
};

static void sun4m_register_types(void)
{
    type_register_static(&idreg_info);
    type_register_static(&afx_info);
    type_register_static(&prom_info);
    type_register_static(&ram_info);
}

static void sun4m_machine_init(void)
{
    qemu_register_machine(&ss5_machine);
    qemu_register_machine(&ss10_machine);
    qemu_register_machine(&ss600mp_machine);
    qemu_register_machine(&ss20_machine);
    qemu_register_machine(&voyager_machine);
    qemu_register_machine(&ss_lx_machine);
    qemu_register_machine(&ss4_machine);
    qemu_register_machine(&scls_machine);
    qemu_register_machine(&sbook_machine);
}

type_init(sun4m_register_types)
machine_init(sun4m_machine_init);
