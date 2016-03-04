/*
 * QEMU Sun4u/Sun4v System Emulator
 *
 * Copyright (c) 2005 Fabrice Bellard
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
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/apb.h"
#include "hw/i386/pc.h"
#include "hw/char/serial.h"
#include "hw/timer/m48t59.h"
#include "hw/block/fdc.h"
#include "net/net.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "hw/boards.h"
#include "hw/nvram/openbios_firmware_abi.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/sysbus.h"
#include "hw/ide.h"
#include "hw/loader.h"
#include "elf.h"
#include "sysemu/block-backend.h"
#include "exec/address-spaces.h"

//#define DEBUG_IRQ
//#define DEBUG_EBUS
//#define DEBUG_TIMER

#ifdef DEBUG_IRQ
#define CPUIRQ_DPRINTF(fmt, ...)                                \
    do { printf("CPUIRQ: " fmt , ## __VA_ARGS__); } while (0)
#else
#define CPUIRQ_DPRINTF(fmt, ...)
#endif

#ifdef DEBUG_EBUS
#define EBUS_DPRINTF(fmt, ...)                                  \
    do { printf("EBUS: " fmt , ## __VA_ARGS__); } while (0)
#else
#define EBUS_DPRINTF(fmt, ...)
#endif

#ifdef DEBUG_TIMER
#define TIMER_DPRINTF(fmt, ...)                                  \
    do { printf("TIMER: " fmt , ## __VA_ARGS__); } while (0)
#else
#define TIMER_DPRINTF(fmt, ...)
#endif

#define KERNEL_LOAD_ADDR     0x00404000
#define CMDLINE_ADDR         0x003ff000
#define PROM_SIZE_MAX        (4 * 1024 * 1024)
#define PROM_VADDR           0x000ffd00000ULL
#define APB_SPECIAL_BASE     0x1fe00000000ULL
#define APB_MEM_BASE         0x1ff00000000ULL
#define APB_PCI_IO_BASE      (APB_SPECIAL_BASE + 0x02000000ULL)
#define PROM_FILENAME        "openbios-sparc64"
#define NVRAM_SIZE           0x2000
#define MAX_IDE_BUS          2
#define BIOS_CFG_IOPORT      0x510
#define FW_CFG_SPARC64_WIDTH (FW_CFG_ARCH_LOCAL + 0x00)
#define FW_CFG_SPARC64_HEIGHT (FW_CFG_ARCH_LOCAL + 0x01)
#define FW_CFG_SPARC64_DEPTH (FW_CFG_ARCH_LOCAL + 0x02)

#define IVEC_MAX             0x40

#define TICK_MAX             0x7fffffffffffffffULL

struct hwdef {
    const char * const default_cpu_model;
    uint16_t machine_id;
    uint64_t prom_addr;
    uint64_t console_serial_base;
};

typedef struct EbusState {
    PCIDevice pci_dev;
    MemoryRegion bar0;
    MemoryRegion bar1;
} EbusState;

void DMA_init(ISABus *bus, int high_page_enable)
{
}

static void fw_cfg_boot_set(void *opaque, const char *boot_device,
                            Error **errp)
{
    fw_cfg_modify_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
}

static int sun4u_NVRAM_set_params(Nvram *nvram, uint16_t NVRAM_size,
                                  const char *arch, ram_addr_t RAM_size,
                                  const char *boot_devices,
                                  uint32_t kernel_image, uint32_t kernel_size,
                                  const char *cmdline,
                                  uint32_t initrd_image, uint32_t initrd_size,
                                  uint32_t NVRAM_image,
                                  int width, int height, int depth,
                                  const uint8_t *macaddr)
{
    unsigned int i;
    uint32_t start, end;
    uint8_t image[0x1ff0];
    struct OpenBIOS_nvpart_v1 *part_header;
    NvramClass *k = NVRAM_GET_CLASS(nvram);

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

    Sun_init_header((struct Sun_nvram *)&image[0x1fd8], macaddr, 0x80);

    for (i = 0; i < sizeof(image); i++) {
        (k->write)(nvram, i, image[i]);
    }

    return 0;
}

static uint64_t sun4u_load_kernel(const char *kernel_filename,
                                  const char *initrd_filename,
                                  ram_addr_t RAM_size, uint64_t *initrd_size,
                                  uint64_t *initrd_addr, uint64_t *kernel_addr,
                                  uint64_t *kernel_entry)
{
    int linux_boot;
    unsigned int i;
    long kernel_size;
    uint8_t *ptr;
    uint64_t kernel_top;

    linux_boot = (kernel_filename != NULL);

    kernel_size = 0;
    if (linux_boot) {
        int bswap_needed;

#ifdef BSWAP_NEEDED
        bswap_needed = 1;
#else
        bswap_needed = 0;
#endif
        kernel_size = load_elf(kernel_filename, NULL, NULL, kernel_entry,
                               kernel_addr, &kernel_top, 1, EM_SPARCV9, 0, 0);
        if (kernel_size < 0) {
            *kernel_addr = KERNEL_LOAD_ADDR;
            *kernel_entry = KERNEL_LOAD_ADDR;
            kernel_size = load_aout(kernel_filename, KERNEL_LOAD_ADDR,
                                    RAM_size - KERNEL_LOAD_ADDR, bswap_needed,
                                    TARGET_PAGE_SIZE);
        }
        if (kernel_size < 0) {
            kernel_size = load_image_targphys(kernel_filename,
                                              KERNEL_LOAD_ADDR,
                                              RAM_size - KERNEL_LOAD_ADDR);
        }
        if (kernel_size < 0) {
            fprintf(stderr, "qemu: could not load kernel '%s'\n",
                    kernel_filename);
            exit(1);
        }
        /* load initrd above kernel */
        *initrd_size = 0;
        if (initrd_filename) {
            *initrd_addr = TARGET_PAGE_ALIGN(kernel_top);

            *initrd_size = load_image_targphys(initrd_filename,
                                               *initrd_addr,
                                               RAM_size - *initrd_addr);
            if ((int)*initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initial ram disk '%s'\n",
                        initrd_filename);
                exit(1);
            }
        }
        if (*initrd_size > 0) {
            for (i = 0; i < 64 * TARGET_PAGE_SIZE; i += TARGET_PAGE_SIZE) {
                ptr = rom_ptr(*kernel_addr + i);
                if (ldl_p(ptr + 8) == 0x48647253) { /* HdrS */
                    stl_p(ptr + 24, *initrd_addr + *kernel_addr);
                    stl_p(ptr + 28, *initrd_size);
                    break;
                }
            }
        }
    }
    return kernel_size;
}

void cpu_check_irqs(CPUSPARCState *env)
{
    CPUState *cs;
    uint32_t pil = env->pil_in |
                  (env->softint & ~(SOFTINT_TIMER | SOFTINT_STIMER));

    /* TT_IVEC has a higher priority (16) than TT_EXTINT (31..17) */
    if (env->ivec_status & 0x20) {
        return;
    }
    cs = CPU(sparc_env_get_cpu(env));
    /* check if TM or SM in SOFTINT are set
       setting these also causes interrupt 14 */
    if (env->softint & (SOFTINT_TIMER | SOFTINT_STIMER)) {
        pil |= 1 << 14;
    }

    /* The bit corresponding to psrpil is (1<< psrpil), the next bit
       is (2 << psrpil). */
    if (pil < (2 << env->psrpil)){
        if (cs->interrupt_request & CPU_INTERRUPT_HARD) {
            CPUIRQ_DPRINTF("Reset CPU IRQ (current interrupt %x)\n",
                           env->interrupt_index);
            env->interrupt_index = 0;
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
        return;
    }

    if (cpu_interrupts_enabled(env)) {

        unsigned int i;

        for (i = 15; i > env->psrpil; i--) {
            if (pil & (1 << i)) {
                int old_interrupt = env->interrupt_index;
                int new_interrupt = TT_EXTINT | i;

                if (unlikely(env->tl > 0 && cpu_tsptr(env)->tt > new_interrupt
                  && ((cpu_tsptr(env)->tt & 0x1f0) == TT_EXTINT))) {
                    CPUIRQ_DPRINTF("Not setting CPU IRQ: TL=%d "
                                   "current %x >= pending %x\n",
                                   env->tl, cpu_tsptr(env)->tt, new_interrupt);
                } else if (old_interrupt != new_interrupt) {
                    env->interrupt_index = new_interrupt;
                    CPUIRQ_DPRINTF("Set CPU IRQ %d old=%x new=%x\n", i,
                                   old_interrupt, new_interrupt);
                    cpu_interrupt(cs, CPU_INTERRUPT_HARD);
                }
                break;
            }
        }
    } else if (cs->interrupt_request & CPU_INTERRUPT_HARD) {
        CPUIRQ_DPRINTF("Interrupts disabled, pil=%08x pil_in=%08x softint=%08x "
                       "current interrupt %x\n",
                       pil, env->pil_in, env->softint, env->interrupt_index);
        env->interrupt_index = 0;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }
}

static void cpu_kick_irq(SPARCCPU *cpu)
{
    CPUState *cs = CPU(cpu);
    CPUSPARCState *env = &cpu->env;

    cs->halted = 0;
    cpu_check_irqs(env);
    qemu_cpu_kick(cs);
}

static void cpu_set_ivec_irq(void *opaque, int irq, int level)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;
    CPUState *cs;

    if (level) {
        if (!(env->ivec_status & 0x20)) {
            CPUIRQ_DPRINTF("Raise IVEC IRQ %d\n", irq);
            cs = CPU(cpu);
            cs->halted = 0;
            env->interrupt_index = TT_IVEC;
            env->ivec_status |= 0x20;
            env->ivec_data[0] = (0x1f << 6) | irq;
            env->ivec_data[1] = 0;
            env->ivec_data[2] = 0;
            cpu_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    } else {
        if (env->ivec_status & 0x20) {
            CPUIRQ_DPRINTF("Lower IVEC IRQ %d\n", irq);
            cs = CPU(cpu);
            env->ivec_status &= ~0x20;
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }
}

typedef struct ResetData {
    SPARCCPU *cpu;
    uint64_t prom_addr;
} ResetData;

static CPUTimer *cpu_timer_create(const char *name, SPARCCPU *cpu,
                                  QEMUBHFunc *cb, uint32_t frequency,
                                  uint64_t disabled_mask, uint64_t npt_mask)
{
    CPUTimer *timer = g_malloc0(sizeof (CPUTimer));

    timer->name = name;
    timer->frequency = frequency;
    timer->disabled_mask = disabled_mask;
    timer->npt_mask = npt_mask;

    timer->disabled = 1;
    timer->npt = 1;
    timer->clock_offset = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    timer->qtimer = timer_new_ns(QEMU_CLOCK_VIRTUAL, cb, cpu);

    return timer;
}

static void cpu_timer_reset(CPUTimer *timer)
{
    timer->disabled = 1;
    timer->clock_offset = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    timer_del(timer->qtimer);
}

static void main_cpu_reset(void *opaque)
{
    ResetData *s = (ResetData *)opaque;
    CPUSPARCState *env = &s->cpu->env;
    static unsigned int nr_resets;

    cpu_reset(CPU(s->cpu));

    cpu_timer_reset(env->tick);
    cpu_timer_reset(env->stick);
    cpu_timer_reset(env->hstick);

    env->gregs[1] = 0; // Memory start
    env->gregs[2] = ram_size; // Memory size
    env->gregs[3] = 0; // Machine description XXX
    if (nr_resets++ == 0) {
        /* Power on reset */
        env->pc = s->prom_addr + 0x20ULL;
    } else {
        env->pc = s->prom_addr + 0x40ULL;
    }
    env->npc = env->pc + 4;
}

static void tick_irq(void *opaque)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;

    CPUTimer* timer = env->tick;

    if (timer->disabled) {
        CPUIRQ_DPRINTF("tick_irq: softint disabled\n");
        return;
    } else {
        CPUIRQ_DPRINTF("tick: fire\n");
    }

    env->softint |= SOFTINT_TIMER;
    cpu_kick_irq(cpu);
}

static void stick_irq(void *opaque)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;

    CPUTimer* timer = env->stick;

    if (timer->disabled) {
        CPUIRQ_DPRINTF("stick_irq: softint disabled\n");
        return;
    } else {
        CPUIRQ_DPRINTF("stick: fire\n");
    }

    env->softint |= SOFTINT_STIMER;
    cpu_kick_irq(cpu);
}

static void hstick_irq(void *opaque)
{
    SPARCCPU *cpu = opaque;
    CPUSPARCState *env = &cpu->env;

    CPUTimer* timer = env->hstick;

    if (timer->disabled) {
        CPUIRQ_DPRINTF("hstick_irq: softint disabled\n");
        return;
    } else {
        CPUIRQ_DPRINTF("hstick: fire\n");
    }

    env->softint |= SOFTINT_STIMER;
    cpu_kick_irq(cpu);
}

static int64_t cpu_to_timer_ticks(int64_t cpu_ticks, uint32_t frequency)
{
    return muldiv64(cpu_ticks, get_ticks_per_sec(), frequency);
}

static uint64_t timer_to_cpu_ticks(int64_t timer_ticks, uint32_t frequency)
{
    return muldiv64(timer_ticks, frequency, get_ticks_per_sec());
}

void cpu_tick_set_count(CPUTimer *timer, uint64_t count)
{
    uint64_t real_count = count & ~timer->npt_mask;
    uint64_t npt_bit = count & timer->npt_mask;

    int64_t vm_clock_offset = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                    cpu_to_timer_ticks(real_count, timer->frequency);

    TIMER_DPRINTF("%s set_count count=0x%016lx (npt %s) p=%p\n",
                  timer->name, real_count,
                  timer->npt ? "disabled" : "enabled", timer);

    timer->npt = npt_bit ? 1 : 0;
    timer->clock_offset = vm_clock_offset;
}

uint64_t cpu_tick_get_count(CPUTimer *timer)
{
    uint64_t real_count = timer_to_cpu_ticks(
                    qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - timer->clock_offset,
                    timer->frequency);

    TIMER_DPRINTF("%s get_count count=0x%016lx (npt %s) p=%p\n",
           timer->name, real_count,
           timer->npt ? "disabled" : "enabled", timer);

    if (timer->npt) {
        real_count |= timer->npt_mask;
    }

    return real_count;
}

void cpu_tick_set_limit(CPUTimer *timer, uint64_t limit)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    uint64_t real_limit = limit & ~timer->disabled_mask;
    timer->disabled = (limit & timer->disabled_mask) ? 1 : 0;

    int64_t expires = cpu_to_timer_ticks(real_limit, timer->frequency) +
                    timer->clock_offset;

    if (expires < now) {
        expires = now + 1;
    }

    TIMER_DPRINTF("%s set_limit limit=0x%016lx (%s) p=%p "
                  "called with limit=0x%016lx at 0x%016lx (delta=0x%016lx)\n",
                  timer->name, real_limit,
                  timer->disabled?"disabled":"enabled",
                  timer, limit,
                  timer_to_cpu_ticks(now - timer->clock_offset,
                                     timer->frequency),
                  timer_to_cpu_ticks(expires - now, timer->frequency));

    if (!real_limit) {
        TIMER_DPRINTF("%s set_limit limit=ZERO - not starting timer\n",
                timer->name);
        timer_del(timer->qtimer);
    } else if (timer->disabled) {
        timer_del(timer->qtimer);
    } else {
        timer_mod(timer->qtimer, expires);
    }
}

static void isa_irq_handler(void *opaque, int n, int level)
{
    static const int isa_irq_to_ivec[16] = {
        [1] = 0x29, /* keyboard */
        [4] = 0x2b, /* serial */
        [6] = 0x27, /* floppy */
        [7] = 0x22, /* parallel */
        [12] = 0x2a, /* mouse */
    };
    qemu_irq *irqs = opaque;
    int ivec;

    assert(n < 16);
    ivec = isa_irq_to_ivec[n];
    EBUS_DPRINTF("Set ISA IRQ %d level %d -> ivec 0x%x\n", n, level, ivec);
    if (ivec) {
        qemu_set_irq(irqs[ivec], level);
    }
}

/* EBUS (Eight bit bus) bridge */
static ISABus *
pci_ebus_init(PCIBus *bus, int devfn, qemu_irq *irqs)
{
    qemu_irq *isa_irq;
    PCIDevice *pci_dev;
    ISABus *isa_bus;

    pci_dev = pci_create_simple(bus, devfn, "ebus");
    isa_bus = ISA_BUS(qdev_get_child_bus(DEVICE(pci_dev), "isa.0"));
    isa_irq = qemu_allocate_irqs(isa_irq_handler, irqs, 16);
    isa_bus_irqs(isa_bus, isa_irq);
    return isa_bus;
}

static void pci_ebus_realize(PCIDevice *pci_dev, Error **errp)
{
    EbusState *s = DO_UPCAST(EbusState, pci_dev, pci_dev);

    if (!isa_bus_new(DEVICE(pci_dev), get_system_memory(),
                     pci_address_space_io(pci_dev), errp)) {
        return;
    }

    pci_dev->config[0x04] = 0x06; // command = bus master, pci mem
    pci_dev->config[0x05] = 0x00;
    pci_dev->config[0x06] = 0xa0; // status = fast back-to-back, 66MHz, no error
    pci_dev->config[0x07] = 0x03; // status = medium devsel
    pci_dev->config[0x09] = 0x00; // programming i/f
    pci_dev->config[0x0D] = 0x0a; // latency_timer

    memory_region_init_alias(&s->bar0, OBJECT(s), "bar0", get_system_io(),
                             0, 0x1000000);
    pci_register_bar(pci_dev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);
    memory_region_init_alias(&s->bar1, OBJECT(s), "bar1", get_system_io(),
                             0, 0x4000);
    pci_register_bar(pci_dev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar1);
}

static void ebus_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pci_ebus_realize;
    k->vendor_id = PCI_VENDOR_ID_SUN;
    k->device_id = PCI_DEVICE_ID_SUN_EBUS;
    k->revision = 0x01;
    k->class_id = PCI_CLASS_BRIDGE_OTHER;
}

static const TypeInfo ebus_info = {
    .name          = "ebus",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(EbusState),
    .class_init    = ebus_class_init,
};

#define TYPE_OPENPROM "openprom"
#define OPENPROM(obj) OBJECT_CHECK(PROMState, (obj), TYPE_OPENPROM)

typedef struct PROMState {
    SysBusDevice parent_obj;

    MemoryRegion prom;
} PROMState;

static uint64_t translate_prom_address(void *opaque, uint64_t addr)
{
    hwaddr *base_addr = (hwaddr *)opaque;
    return addr + *base_addr - PROM_VADDR;
}

/* Boot PROM (OpenBIOS) */
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
        ret = load_elf(filename, translate_prom_address, &addr,
                       NULL, NULL, NULL, 1, EM_SPARCV9, 0, 0);
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

    memory_region_init_ram(&s->prom, OBJECT(s), "sun4u.prom", PROM_SIZE_MAX,
                           &error_fatal);
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


#define TYPE_SUN4U_MEMORY "memory"
#define SUN4U_RAM(obj) OBJECT_CHECK(RamDevice, (obj), TYPE_SUN4U_MEMORY)

typedef struct RamDevice {
    SysBusDevice parent_obj;

    MemoryRegion ram;
    uint64_t size;
} RamDevice;

/* System RAM */
static int ram_init1(SysBusDevice *dev)
{
    RamDevice *d = SUN4U_RAM(dev);

    memory_region_init_ram(&d->ram, OBJECT(d), "sun4u.ram", d->size,
                           &error_fatal);
    vmstate_register_ram_global(&d->ram);
    sysbus_init_mmio(dev, &d->ram);
    return 0;
}

static void ram_init(hwaddr addr, ram_addr_t RAM_size)
{
    DeviceState *dev;
    SysBusDevice *s;
    RamDevice *d;

    /* allocate RAM */
    dev = qdev_create(NULL, TYPE_SUN4U_MEMORY);
    s = SYS_BUS_DEVICE(dev);

    d = SUN4U_RAM(dev);
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
    .name          = TYPE_SUN4U_MEMORY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RamDevice),
    .class_init    = ram_class_init,
};

static SPARCCPU *cpu_devinit(const char *cpu_model, const struct hwdef *hwdef)
{
    SPARCCPU *cpu;
    CPUSPARCState *env;
    ResetData *reset_info;

    uint32_t   tick_frequency = 100*1000000;
    uint32_t  stick_frequency = 100*1000000;
    uint32_t hstick_frequency = 100*1000000;

    if (cpu_model == NULL) {
        cpu_model = hwdef->default_cpu_model;
    }
    cpu = cpu_sparc_init(cpu_model);
    if (cpu == NULL) {
        fprintf(stderr, "Unable to find Sparc CPU definition\n");
        exit(1);
    }
    env = &cpu->env;

    env->tick = cpu_timer_create("tick", cpu, tick_irq,
                                  tick_frequency, TICK_INT_DIS,
                                  TICK_NPT_MASK);

    env->stick = cpu_timer_create("stick", cpu, stick_irq,
                                   stick_frequency, TICK_INT_DIS,
                                   TICK_NPT_MASK);

    env->hstick = cpu_timer_create("hstick", cpu, hstick_irq,
                                    hstick_frequency, TICK_INT_DIS,
                                    TICK_NPT_MASK);

    reset_info = g_malloc0(sizeof(ResetData));
    reset_info->cpu = cpu;
    reset_info->prom_addr = hwdef->prom_addr;
    qemu_register_reset(main_cpu_reset, reset_info);

    return cpu;
}

static void sun4uv_init(MemoryRegion *address_space_mem,
                        MachineState *machine,
                        const struct hwdef *hwdef)
{
    SPARCCPU *cpu;
    Nvram *nvram;
    unsigned int i;
    uint64_t initrd_addr, initrd_size, kernel_addr, kernel_size, kernel_entry;
    PCIBus *pci_bus, *pci_bus2, *pci_bus3;
    ISABus *isa_bus;
    SysBusDevice *s;
    qemu_irq *ivec_irqs, *pbm_irqs;
    DriveInfo *hd[MAX_IDE_BUS * MAX_IDE_DEVS];
    DriveInfo *fd[MAX_FD];
    DeviceState *dev;
    FWCfgState *fw_cfg;

    /* init CPUs */
    cpu = cpu_devinit(machine->cpu_model, hwdef);

    /* set up devices */
    ram_init(0, machine->ram_size);

    prom_init(hwdef->prom_addr, bios_name);

    ivec_irqs = qemu_allocate_irqs(cpu_set_ivec_irq, cpu, IVEC_MAX);
    pci_bus = pci_apb_init(APB_SPECIAL_BASE, APB_MEM_BASE, ivec_irqs, &pci_bus2,
                           &pci_bus3, &pbm_irqs);
    pci_vga_init(pci_bus);

    // XXX Should be pci_bus3
    isa_bus = pci_ebus_init(pci_bus, -1, pbm_irqs);

    i = 0;
    if (hwdef->console_serial_base) {
        serial_mm_init(address_space_mem, hwdef->console_serial_base, 0,
                       NULL, 115200, serial_hds[i], DEVICE_BIG_ENDIAN);
        i++;
    }

    serial_hds_isa_init(isa_bus, MAX_SERIAL_PORTS);
    parallel_hds_isa_init(isa_bus, MAX_PARALLEL_PORTS);

    for(i = 0; i < nb_nics; i++)
        pci_nic_init_nofail(&nd_table[i], pci_bus, "ne2k_pci", NULL);

    ide_drive_get(hd, ARRAY_SIZE(hd));

    pci_cmd646_ide_init(pci_bus, hd, 1);

    isa_create_simple(isa_bus, "i8042");

    /* Floppy */
    for(i = 0; i < MAX_FD; i++) {
        fd[i] = drive_get(IF_FLOPPY, 0, i);
    }
    dev = DEVICE(isa_create(isa_bus, TYPE_ISA_FDC));
    if (fd[0]) {
        qdev_prop_set_drive(dev, "driveA", blk_by_legacy_dinfo(fd[0]),
                            &error_abort);
    }
    if (fd[1]) {
        qdev_prop_set_drive(dev, "driveB", blk_by_legacy_dinfo(fd[1]),
                            &error_abort);
    }
    qdev_prop_set_uint32(dev, "dma", -1);
    qdev_init_nofail(dev);

    /* Map NVRAM into I/O (ebus) space */
    nvram = m48t59_init(NULL, 0, 0, NVRAM_SIZE, 1968, 59);
    s = SYS_BUS_DEVICE(nvram);
    memory_region_add_subregion(get_system_io(), 0x2000,
                                sysbus_mmio_get_region(s, 0));
 
    initrd_size = 0;
    initrd_addr = 0;
    kernel_size = sun4u_load_kernel(machine->kernel_filename,
                                    machine->initrd_filename,
                                    ram_size, &initrd_size, &initrd_addr,
                                    &kernel_addr, &kernel_entry);

    sun4u_NVRAM_set_params(nvram, NVRAM_SIZE, "Sun4u", machine->ram_size,
                           machine->boot_order,
                           kernel_addr, kernel_size,
                           machine->kernel_cmdline,
                           initrd_addr, initrd_size,
                           /* XXX: need an option to load a NVRAM image */
                           0,
                           graphic_width, graphic_height, graphic_depth,
                           (uint8_t *)&nd_table[0].macaddr);

    fw_cfg = fw_cfg_init_io(BIOS_CFG_IOPORT);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)max_cpus);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MACHINE_ID, hwdef->machine_id);
    fw_cfg_add_i64(fw_cfg, FW_CFG_KERNEL_ADDR, kernel_entry);
    fw_cfg_add_i64(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
    if (machine->kernel_cmdline) {
        fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE,
                       strlen(machine->kernel_cmdline) + 1);
        fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA, machine->kernel_cmdline);
    } else {
        fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE, 0);
    }
    fw_cfg_add_i64(fw_cfg, FW_CFG_INITRD_ADDR, initrd_addr);
    fw_cfg_add_i64(fw_cfg, FW_CFG_INITRD_SIZE, initrd_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, machine->boot_order[0]);

    fw_cfg_add_i16(fw_cfg, FW_CFG_SPARC64_WIDTH, graphic_width);
    fw_cfg_add_i16(fw_cfg, FW_CFG_SPARC64_HEIGHT, graphic_height);
    fw_cfg_add_i16(fw_cfg, FW_CFG_SPARC64_DEPTH, graphic_depth);

    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);
}

enum {
    sun4u_id = 0,
    sun4v_id = 64,
    niagara_id,
};

static const struct hwdef hwdefs[] = {
    /* Sun4u generic PC-like machine */
    {
        .default_cpu_model = "TI UltraSparc IIi",
        .machine_id = sun4u_id,
        .prom_addr = 0x1fff0000000ULL,
        .console_serial_base = 0,
    },
    /* Sun4v generic PC-like machine */
    {
        .default_cpu_model = "Sun UltraSparc T1",
        .machine_id = sun4v_id,
        .prom_addr = 0x1fff0000000ULL,
        .console_serial_base = 0,
    },
    /* Sun4v generic Niagara machine */
    {
        .default_cpu_model = "Sun UltraSparc T1",
        .machine_id = niagara_id,
        .prom_addr = 0xfff0000000ULL,
        .console_serial_base = 0xfff0c2c000ULL,
    },
};

/* Sun4u hardware initialisation */
static void sun4u_init(MachineState *machine)
{
    sun4uv_init(get_system_memory(), machine, &hwdefs[0]);
}

/* Sun4v hardware initialisation */
static void sun4v_init(MachineState *machine)
{
    sun4uv_init(get_system_memory(), machine, &hwdefs[1]);
}

/* Niagara hardware initialisation */
static void niagara_init(MachineState *machine)
{
    sun4uv_init(get_system_memory(), machine, &hwdefs[2]);
}

static void sun4u_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4u platform";
    mc->init = sun4u_init;
    mc->max_cpus = 1; /* XXX for now */
    mc->is_default = 1;
    mc->default_boot_order = "c";
}

static const TypeInfo sun4u_type = {
    .name = MACHINE_TYPE_NAME("sun4u"),
    .parent = TYPE_MACHINE,
    .class_init = sun4u_class_init,
};

static void sun4v_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4v platform";
    mc->init = sun4v_init;
    mc->max_cpus = 1; /* XXX for now */
    mc->default_boot_order = "c";
}

static const TypeInfo sun4v_type = {
    .name = MACHINE_TYPE_NAME("sun4v"),
    .parent = TYPE_MACHINE,
    .class_init = sun4v_class_init,
};

static void niagara_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sun4v platform, Niagara";
    mc->init = niagara_init;
    mc->max_cpus = 1; /* XXX for now */
    mc->default_boot_order = "c";
}

static const TypeInfo niagara_type = {
    .name = MACHINE_TYPE_NAME("Niagara"),
    .parent = TYPE_MACHINE,
    .class_init = niagara_class_init,
};

static void sun4u_register_types(void)
{
    type_register_static(&ebus_info);
    type_register_static(&prom_info);
    type_register_static(&ram_info);
}

static void sun4u_machine_init(void)
{
    type_register_static(&sun4u_type);
    type_register_static(&sun4v_type);
    type_register_static(&niagara_type);
}

type_init(sun4u_register_types)
machine_init(sun4u_machine_init)
