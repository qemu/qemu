/*
 * QEMU PPC PREP hardware System Emulator
 *
 * Copyright (c) 2003-2007 Jocelyn Mayer
 * Copyright (c) 2017 Hervé Poussineau
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
#include "cpu.h"
#include "hw/rtc/m48t59.h"
#include "hw/char/serial.h"
#include "hw/block/fdc.h"
#include "net/net.h"
#include "sysemu/sysemu.h"
#include "hw/isa/isa.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/ppc/ppc.h"
#include "hw/boards.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/isa/pc87312.h"
#include "hw/qdev-properties.h"
#include "sysemu/arch_init.h"
#include "sysemu/kvm.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "exec/address-spaces.h"
#include "trace.h"
#include "elf.h"
#include "qemu/units.h"
#include "kvm_ppc.h"

/* SMP is not enabled, for now */
#define MAX_CPUS 1

#define MAX_IDE_BUS 2

#define CFG_ADDR 0xf0000510

#define KERNEL_LOAD_ADDR 0x01000000
#define INITRD_LOAD_ADDR 0x01800000

#define NVRAM_SIZE        0x2000

static void fw_cfg_boot_set(void *opaque, const char *boot_device,
                            Error **errp)
{
    fw_cfg_modify_i16(opaque, FW_CFG_BOOT_DEVICE, boot_device[0]);
}

static void ppc_prep_reset(void *opaque)
{
    PowerPCCPU *cpu = opaque;

    cpu_reset(CPU(cpu));
}


/*****************************************************************************/
/* NVRAM helpers */
static inline uint32_t nvram_read(Nvram *nvram, uint32_t addr)
{
    NvramClass *k = NVRAM_GET_CLASS(nvram);
    return (k->read)(nvram, addr);
}

static inline void nvram_write(Nvram *nvram, uint32_t addr, uint32_t val)
{
    NvramClass *k = NVRAM_GET_CLASS(nvram);
    (k->write)(nvram, addr, val);
}

static void NVRAM_set_byte(Nvram *nvram, uint32_t addr, uint8_t value)
{
    nvram_write(nvram, addr, value);
}

static uint8_t NVRAM_get_byte(Nvram *nvram, uint32_t addr)
{
    return nvram_read(nvram, addr);
}

static void NVRAM_set_word(Nvram *nvram, uint32_t addr, uint16_t value)
{
    nvram_write(nvram, addr, value >> 8);
    nvram_write(nvram, addr + 1, value & 0xFF);
}

static uint16_t NVRAM_get_word(Nvram *nvram, uint32_t addr)
{
    uint16_t tmp;

    tmp = nvram_read(nvram, addr) << 8;
    tmp |= nvram_read(nvram, addr + 1);

    return tmp;
}

static void NVRAM_set_lword(Nvram *nvram, uint32_t addr, uint32_t value)
{
    nvram_write(nvram, addr, value >> 24);
    nvram_write(nvram, addr + 1, (value >> 16) & 0xFF);
    nvram_write(nvram, addr + 2, (value >> 8) & 0xFF);
    nvram_write(nvram, addr + 3, value & 0xFF);
}

static void NVRAM_set_string(Nvram *nvram, uint32_t addr, const char *str,
                             uint32_t max)
{
    int i;

    for (i = 0; i < max && str[i] != '\0'; i++) {
        nvram_write(nvram, addr + i, str[i]);
    }
    nvram_write(nvram, addr + i, str[i]);
    nvram_write(nvram, addr + max - 1, '\0');
}

static uint16_t NVRAM_crc_update (uint16_t prev, uint16_t value)
{
    uint16_t tmp;
    uint16_t pd, pd1, pd2;

    tmp = prev >> 8;
    pd = prev ^ value;
    pd1 = pd & 0x000F;
    pd2 = ((pd >> 4) & 0x000F) ^ pd1;
    tmp ^= (pd1 << 3) | (pd1 << 8);
    tmp ^= pd2 | (pd2 << 7) | (pd2 << 12);

    return tmp;
}

static uint16_t NVRAM_compute_crc (Nvram *nvram, uint32_t start, uint32_t count)
{
    uint32_t i;
    uint16_t crc = 0xFFFF;
    int odd;

    odd = count & 1;
    count &= ~1;
    for (i = 0; i != count; i++) {
        crc = NVRAM_crc_update(crc, NVRAM_get_word(nvram, start + i));
    }
    if (odd) {
        crc = NVRAM_crc_update(crc, NVRAM_get_byte(nvram, start + i) << 8);
    }

    return crc;
}

#define CMDLINE_ADDR 0x017ff000

static int PPC_NVRAM_set_params (Nvram *nvram, uint16_t NVRAM_size,
                          const char *arch,
                          uint32_t RAM_size, int boot_device,
                          uint32_t kernel_image, uint32_t kernel_size,
                          const char *cmdline,
                          uint32_t initrd_image, uint32_t initrd_size,
                          uint32_t NVRAM_image,
                          int width, int height, int depth)
{
    uint16_t crc;

    /* Set parameters for Open Hack'Ware BIOS */
    NVRAM_set_string(nvram, 0x00, "QEMU_BIOS", 16);
    NVRAM_set_lword(nvram,  0x10, 0x00000002); /* structure v2 */
    NVRAM_set_word(nvram,   0x14, NVRAM_size);
    NVRAM_set_string(nvram, 0x20, arch, 16);
    NVRAM_set_lword(nvram,  0x30, RAM_size);
    NVRAM_set_byte(nvram,   0x34, boot_device);
    NVRAM_set_lword(nvram,  0x38, kernel_image);
    NVRAM_set_lword(nvram,  0x3C, kernel_size);
    if (cmdline) {
        /* XXX: put the cmdline in NVRAM too ? */
        pstrcpy_targphys("cmdline", CMDLINE_ADDR, RAM_size - CMDLINE_ADDR,
                         cmdline);
        NVRAM_set_lword(nvram,  0x40, CMDLINE_ADDR);
        NVRAM_set_lword(nvram,  0x44, strlen(cmdline));
    } else {
        NVRAM_set_lword(nvram,  0x40, 0);
        NVRAM_set_lword(nvram,  0x44, 0);
    }
    NVRAM_set_lword(nvram,  0x48, initrd_image);
    NVRAM_set_lword(nvram,  0x4C, initrd_size);
    NVRAM_set_lword(nvram,  0x50, NVRAM_image);

    NVRAM_set_word(nvram,   0x54, width);
    NVRAM_set_word(nvram,   0x56, height);
    NVRAM_set_word(nvram,   0x58, depth);
    crc = NVRAM_compute_crc(nvram, 0x00, 0xF8);
    NVRAM_set_word(nvram,   0xFC, crc);

    return 0;
}

static int prep_set_cmos_checksum(DeviceState *dev, void *opaque)
{
    uint16_t checksum = *(uint16_t *)opaque;
    ISADevice *rtc;

    if (object_dynamic_cast(OBJECT(dev), TYPE_MC146818_RTC)) {
        rtc = ISA_DEVICE(dev);
        rtc_set_memory(rtc, 0x2e, checksum & 0xff);
        rtc_set_memory(rtc, 0x3e, checksum & 0xff);
        rtc_set_memory(rtc, 0x2f, checksum >> 8);
        rtc_set_memory(rtc, 0x3f, checksum >> 8);

        object_property_add_alias(qdev_get_machine(), "rtc-time", OBJECT(rtc),
                                  "date");
    }
    return 0;
}

static void ibm_40p_init(MachineState *machine)
{
    CPUPPCState *env = NULL;
    uint16_t cmos_checksum;
    PowerPCCPU *cpu;
    DeviceState *dev, *i82378_dev;
    SysBusDevice *pcihost, *s;
    Nvram *m48t59 = NULL;
    PCIBus *pci_bus;
    ISADevice *isa_dev;
    ISABus *isa_bus;
    void *fw_cfg;
    int i;
    uint32_t kernel_base = 0, initrd_base = 0;
    long kernel_size = 0, initrd_size = 0;
    char boot_device;

    /* init CPU */
    cpu = POWERPC_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;
    if (PPC_INPUT(env) != PPC_FLAGS_INPUT_6xx) {
        error_report("only 6xx bus is supported on this machine");
        exit(1);
    }

    if (env->flags & POWERPC_FLAG_RTC_CLK) {
        /* POWER / PowerPC 601 RTC clock frequency is 7.8125 MHz */
        cpu_ppc_tb_init(env, 7812500UL);
    } else {
        /* Set time-base frequency to 100 Mhz */
        cpu_ppc_tb_init(env, 100UL * 1000UL * 1000UL);
    }
    qemu_register_reset(ppc_prep_reset, cpu);

    /* PCI host */
    dev = qdev_new("raven-pcihost");
    if (!bios_name) {
        bios_name = "openbios-ppc";
    }
    qdev_prop_set_string(dev, "bios-name", bios_name);
    qdev_prop_set_uint32(dev, "elf-machine", PPC_ELF_MACHINE);
    pcihost = SYS_BUS_DEVICE(dev);
    object_property_add_child(qdev_get_machine(), "raven", OBJECT(dev));
    sysbus_realize_and_unref(pcihost, &error_fatal);
    pci_bus = PCI_BUS(qdev_get_child_bus(dev, "pci.0"));
    if (!pci_bus) {
        error_report("could not create PCI host controller");
        exit(1);
    }

    /* PCI -> ISA bridge */
    i82378_dev = DEVICE(pci_create_simple(pci_bus, PCI_DEVFN(11, 0), "i82378"));
    qdev_connect_gpio_out(i82378_dev, 0,
                          cpu->env.irq_inputs[PPC6xx_INPUT_INT]);
    sysbus_connect_irq(pcihost, 0, qdev_get_gpio_in(i82378_dev, 15));
    isa_bus = ISA_BUS(qdev_get_child_bus(i82378_dev, "isa.0"));

    /* Memory controller */
    isa_dev = isa_new("rs6000-mc");
    dev = DEVICE(isa_dev);
    qdev_prop_set_uint32(dev, "ram-size", machine->ram_size);
    isa_realize_and_unref(isa_dev, isa_bus, &error_fatal);

    /* RTC */
    isa_dev = isa_new(TYPE_MC146818_RTC);
    dev = DEVICE(isa_dev);
    qdev_prop_set_int32(dev, "base_year", 1900);
    isa_realize_and_unref(isa_dev, isa_bus, &error_fatal);

    /* initialize CMOS checksums */
    cmos_checksum = 0x6aa9;
    qbus_walk_children(BUS(isa_bus), prep_set_cmos_checksum, NULL, NULL, NULL,
                       &cmos_checksum);

    /* add some more devices */
    if (defaults_enabled()) {
        m48t59 = NVRAM(isa_create_simple(isa_bus, "isa-m48t59"));

        isa_dev = isa_new("cs4231a");
        dev = DEVICE(isa_dev);
        qdev_prop_set_uint32(dev, "iobase", 0x830);
        qdev_prop_set_uint32(dev, "irq", 10);
        isa_realize_and_unref(isa_dev, isa_bus, &error_fatal);

        isa_dev = isa_new("pc87312");
        dev = DEVICE(isa_dev);
        qdev_prop_set_uint32(dev, "config", 12);
        isa_realize_and_unref(isa_dev, isa_bus, &error_fatal);

        isa_dev = isa_new("prep-systemio");
        dev = DEVICE(isa_dev);
        qdev_prop_set_uint32(dev, "ibm-planar-id", 0xfc);
        qdev_prop_set_uint32(dev, "equipment", 0xc0);
        isa_realize_and_unref(isa_dev, isa_bus, &error_fatal);

        dev = DEVICE(pci_create_simple(pci_bus, PCI_DEVFN(1, 0),
                                       "lsi53c810"));
        lsi53c8xx_handle_legacy_cmdline(dev);
        qdev_connect_gpio_out(dev, 0, qdev_get_gpio_in(i82378_dev, 13));

        /* XXX: s3-trio at PCI_DEVFN(2, 0) */
        pci_vga_init(pci_bus);

        for (i = 0; i < nb_nics; i++) {
            pci_nic_init_nofail(&nd_table[i], pci_bus, "pcnet",
                                i == 0 ? "3" : NULL);
        }
    }

    /* Prepare firmware configuration for OpenBIOS */
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

    if (machine->kernel_filename) {
        /* load kernel */
        kernel_base = KERNEL_LOAD_ADDR;
        kernel_size = load_image_targphys(machine->kernel_filename,
                                          kernel_base,
                                          machine->ram_size - kernel_base);
        if (kernel_size < 0) {
            error_report("could not load kernel '%s'",
                         machine->kernel_filename);
            exit(1);
        }
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_ADDR, kernel_base);
        fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_SIZE, kernel_size);
        /* load initrd */
        if (machine->initrd_filename) {
            initrd_base = INITRD_LOAD_ADDR;
            initrd_size = load_image_targphys(machine->initrd_filename,
                                              initrd_base,
                                              machine->ram_size - initrd_base);
            if (initrd_size < 0) {
                error_report("could not load initial ram disk '%s'",
                             machine->initrd_filename);
                exit(1);
            }
            fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_ADDR, initrd_base);
            fw_cfg_add_i32(fw_cfg, FW_CFG_INITRD_SIZE, initrd_size);
        }
        if (machine->kernel_cmdline && *machine->kernel_cmdline) {
            fw_cfg_add_i32(fw_cfg, FW_CFG_KERNEL_CMDLINE, CMDLINE_ADDR);
            pstrcpy_targphys("cmdline", CMDLINE_ADDR, TARGET_PAGE_SIZE,
                             machine->kernel_cmdline);
            fw_cfg_add_string(fw_cfg, FW_CFG_CMDLINE_DATA,
                              machine->kernel_cmdline);
            fw_cfg_add_i32(fw_cfg, FW_CFG_CMDLINE_SIZE,
                           strlen(machine->kernel_cmdline) + 1);
        }
        boot_device = 'm';
    } else {
        boot_device = machine->boot_order[0];
    }

    fw_cfg_add_i16(fw_cfg, FW_CFG_MAX_CPUS, (uint16_t)machine->smp.max_cpus);
    fw_cfg_add_i64(fw_cfg, FW_CFG_RAM_SIZE, (uint64_t)machine->ram_size);
    fw_cfg_add_i16(fw_cfg, FW_CFG_MACHINE_ID, ARCH_PREP);

    fw_cfg_add_i16(fw_cfg, FW_CFG_PPC_WIDTH, graphic_width);
    fw_cfg_add_i16(fw_cfg, FW_CFG_PPC_HEIGHT, graphic_height);
    fw_cfg_add_i16(fw_cfg, FW_CFG_PPC_DEPTH, graphic_depth);

    fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_IS_KVM, kvm_enabled());
    if (kvm_enabled()) {
        uint8_t *hypercall;

        fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_TBFREQ, kvmppc_get_tbfreq());
        hypercall = g_malloc(16);
        kvmppc_get_hypercall(env, hypercall, 16);
        fw_cfg_add_bytes(fw_cfg, FW_CFG_PPC_KVM_HC, hypercall, 16);
        fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_KVM_PID, getpid());
    } else {
        fw_cfg_add_i32(fw_cfg, FW_CFG_PPC_TBFREQ, NANOSECONDS_PER_SECOND);
    }
    fw_cfg_add_i16(fw_cfg, FW_CFG_BOOT_DEVICE, boot_device);
    qemu_register_boot_set(fw_cfg_boot_set, fw_cfg);

    /* Prepare firmware configuration for Open Hack'Ware */
    if (m48t59) {
        PPC_NVRAM_set_params(m48t59, NVRAM_SIZE, "PREP", ram_size,
                             boot_device,
                             kernel_base, kernel_size,
                             machine->kernel_cmdline,
                             initrd_base, initrd_size,
                             /* XXX: need an option to load a NVRAM image */
                             0,
                             graphic_width, graphic_height, graphic_depth);
    }
}

static void ibm_40p_machine_init(MachineClass *mc)
{
    mc->desc = "IBM RS/6000 7020 (40p)",
    mc->init = ibm_40p_init;
    mc->max_cpus = 1;
    mc->default_ram_size = 128 * MiB;
    mc->block_default_type = IF_SCSI;
    mc->default_boot_order = "c";
    mc->default_cpu_type = POWERPC_CPU_TYPE_NAME("604");
    mc->default_display = "std";
}

DEFINE_MACHINE("40p", ibm_40p_machine_init)
