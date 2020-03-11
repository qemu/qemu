/*
 * QEMU PC System Emulator
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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
#include "hw/i386/x86.h"
#include "hw/i386/pc.h"
#include "hw/char/serial.h"
#include "hw/char/parallel.h"
#include "hw/i386/apic.h"
#include "hw/i386/topology.h"
#include "hw/i386/fw_cfg.h"
#include "sysemu/cpus.h"
#include "hw/block/fdc.h"
#include "hw/ide.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/timer/hpet.h"
#include "hw/firmware/smbios.h"
#include "hw/loader.h"
#include "elf.h"
#include "migration/vmstate.h"
#include "multiboot.h"
#include "hw/rtc/mc146818rtc.h"
#include "hw/intc/i8259.h"
#include "hw/dma/i8257.h"
#include "hw/timer/i8254.h"
#include "hw/input/i8042.h"
#include "hw/irq.h"
#include "hw/audio/pcspk.h"
#include "hw/pci/msi.h"
#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "sysemu/tcg.h"
#include "sysemu/numa.h"
#include "sysemu/kvm.h"
#include "sysemu/qtest.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "kvm_i386.h"
#include "hw/xen/xen.h"
#include "hw/xen/start_info.h"
#include "ui/qemu-spice.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/arch_init.h"
#include "qemu/bitmap.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/option.h"
#include "qemu/cutils.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/cpu_hotplug.h"
#include "hw/boards.h"
#include "acpi-build.h"
#include "hw/mem/pc-dimm.h"
#include "hw/mem/nvdimm.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-common.h"
#include "qapi/visitor.h"
#include "hw/core/cpu.h"
#include "hw/usb.h"
#include "hw/i386/intel_iommu.h"
#include "hw/net/ne2000-isa.h"
#include "standard-headers/asm-x86/bootparam.h"
#include "hw/virtio/virtio-pmem-pci.h"
#include "hw/mem/memory-device.h"
#include "sysemu/replay.h"
#include "qapi/qmp/qerror.h"
#include "config-devices.h"
#include "e820_memory_layout.h"
#include "fw_cfg.h"
#include "trace.h"

GlobalProperty pc_compat_4_2[] = {
    { "mch", "smbase-smram", "off" },
};
const size_t pc_compat_4_2_len = G_N_ELEMENTS(pc_compat_4_2);

GlobalProperty pc_compat_4_1[] = {};
const size_t pc_compat_4_1_len = G_N_ELEMENTS(pc_compat_4_1);

GlobalProperty pc_compat_4_0[] = {};
const size_t pc_compat_4_0_len = G_N_ELEMENTS(pc_compat_4_0);

GlobalProperty pc_compat_3_1[] = {
    { "intel-iommu", "dma-drain", "off" },
    { "Opteron_G3" "-" TYPE_X86_CPU, "rdtscp", "off" },
    { "Opteron_G4" "-" TYPE_X86_CPU, "rdtscp", "off" },
    { "Opteron_G4" "-" TYPE_X86_CPU, "npt", "off" },
    { "Opteron_G4" "-" TYPE_X86_CPU, "nrip-save", "off" },
    { "Opteron_G5" "-" TYPE_X86_CPU, "rdtscp", "off" },
    { "Opteron_G5" "-" TYPE_X86_CPU, "npt", "off" },
    { "Opteron_G5" "-" TYPE_X86_CPU, "nrip-save", "off" },
    { "EPYC" "-" TYPE_X86_CPU, "npt", "off" },
    { "EPYC" "-" TYPE_X86_CPU, "nrip-save", "off" },
    { "EPYC-IBPB" "-" TYPE_X86_CPU, "npt", "off" },
    { "EPYC-IBPB" "-" TYPE_X86_CPU, "nrip-save", "off" },
    { "Skylake-Client" "-" TYPE_X86_CPU,      "mpx", "on" },
    { "Skylake-Client-IBRS" "-" TYPE_X86_CPU, "mpx", "on" },
    { "Skylake-Server" "-" TYPE_X86_CPU,      "mpx", "on" },
    { "Skylake-Server-IBRS" "-" TYPE_X86_CPU, "mpx", "on" },
    { "Cascadelake-Server" "-" TYPE_X86_CPU,  "mpx", "on" },
    { "Icelake-Client" "-" TYPE_X86_CPU,      "mpx", "on" },
    { "Icelake-Server" "-" TYPE_X86_CPU,      "mpx", "on" },
    { "Cascadelake-Server" "-" TYPE_X86_CPU, "stepping", "5" },
    { TYPE_X86_CPU, "x-intel-pt-auto-level", "off" },
};
const size_t pc_compat_3_1_len = G_N_ELEMENTS(pc_compat_3_1);

GlobalProperty pc_compat_3_0[] = {
    { TYPE_X86_CPU, "x-hv-synic-kvm-only", "on" },
    { "Skylake-Server" "-" TYPE_X86_CPU, "pku", "off" },
    { "Skylake-Server-IBRS" "-" TYPE_X86_CPU, "pku", "off" },
};
const size_t pc_compat_3_0_len = G_N_ELEMENTS(pc_compat_3_0);

GlobalProperty pc_compat_2_12[] = {
    { TYPE_X86_CPU, "legacy-cache", "on" },
    { TYPE_X86_CPU, "topoext", "off" },
    { "EPYC-" TYPE_X86_CPU, "xlevel", "0x8000000a" },
    { "EPYC-IBPB-" TYPE_X86_CPU, "xlevel", "0x8000000a" },
};
const size_t pc_compat_2_12_len = G_N_ELEMENTS(pc_compat_2_12);

GlobalProperty pc_compat_2_11[] = {
    { TYPE_X86_CPU, "x-migrate-smi-count", "off" },
    { "Skylake-Server" "-" TYPE_X86_CPU, "clflushopt", "off" },
};
const size_t pc_compat_2_11_len = G_N_ELEMENTS(pc_compat_2_11);

GlobalProperty pc_compat_2_10[] = {
    { TYPE_X86_CPU, "x-hv-max-vps", "0x40" },
    { "i440FX-pcihost", "x-pci-hole64-fix", "off" },
    { "q35-pcihost", "x-pci-hole64-fix", "off" },
};
const size_t pc_compat_2_10_len = G_N_ELEMENTS(pc_compat_2_10);

GlobalProperty pc_compat_2_9[] = {
    { "mch", "extended-tseg-mbytes", "0" },
};
const size_t pc_compat_2_9_len = G_N_ELEMENTS(pc_compat_2_9);

GlobalProperty pc_compat_2_8[] = {
    { TYPE_X86_CPU, "tcg-cpuid", "off" },
    { "kvmclock", "x-mach-use-reliable-get-clock", "off" },
    { "ICH9-LPC", "x-smi-broadcast", "off" },
    { TYPE_X86_CPU, "vmware-cpuid-freq", "off" },
    { "Haswell-" TYPE_X86_CPU, "stepping", "1" },
};
const size_t pc_compat_2_8_len = G_N_ELEMENTS(pc_compat_2_8);

GlobalProperty pc_compat_2_7[] = {
    { TYPE_X86_CPU, "l3-cache", "off" },
    { TYPE_X86_CPU, "full-cpuid-auto-level", "off" },
    { "Opteron_G3" "-" TYPE_X86_CPU, "family", "15" },
    { "Opteron_G3" "-" TYPE_X86_CPU, "model", "6" },
    { "Opteron_G3" "-" TYPE_X86_CPU, "stepping", "1" },
    { "isa-pcspk", "migrate", "off" },
};
const size_t pc_compat_2_7_len = G_N_ELEMENTS(pc_compat_2_7);

GlobalProperty pc_compat_2_6[] = {
    { TYPE_X86_CPU, "cpuid-0xb", "off" },
    { "vmxnet3", "romfile", "" },
    { TYPE_X86_CPU, "fill-mtrr-mask", "off" },
    { "apic-common", "legacy-instance-id", "on", }
};
const size_t pc_compat_2_6_len = G_N_ELEMENTS(pc_compat_2_6);

GlobalProperty pc_compat_2_5[] = {};
const size_t pc_compat_2_5_len = G_N_ELEMENTS(pc_compat_2_5);

GlobalProperty pc_compat_2_4[] = {
    PC_CPU_MODEL_IDS("2.4.0")
    { "Haswell-" TYPE_X86_CPU, "abm", "off" },
    { "Haswell-noTSX-" TYPE_X86_CPU, "abm", "off" },
    { "Broadwell-" TYPE_X86_CPU, "abm", "off" },
    { "Broadwell-noTSX-" TYPE_X86_CPU, "abm", "off" },
    { "host" "-" TYPE_X86_CPU, "host-cache-info", "on" },
    { TYPE_X86_CPU, "check", "off" },
    { "qemu64" "-" TYPE_X86_CPU, "sse4a", "on" },
    { "qemu64" "-" TYPE_X86_CPU, "abm", "on" },
    { "qemu64" "-" TYPE_X86_CPU, "popcnt", "on" },
    { "qemu32" "-" TYPE_X86_CPU, "popcnt", "on" },
    { "Opteron_G2" "-" TYPE_X86_CPU, "rdtscp", "on" },
    { "Opteron_G3" "-" TYPE_X86_CPU, "rdtscp", "on" },
    { "Opteron_G4" "-" TYPE_X86_CPU, "rdtscp", "on" },
    { "Opteron_G5" "-" TYPE_X86_CPU, "rdtscp", "on", }
};
const size_t pc_compat_2_4_len = G_N_ELEMENTS(pc_compat_2_4);

GlobalProperty pc_compat_2_3[] = {
    PC_CPU_MODEL_IDS("2.3.0")
    { TYPE_X86_CPU, "arat", "off" },
    { "qemu64" "-" TYPE_X86_CPU, "min-level", "4" },
    { "kvm64" "-" TYPE_X86_CPU, "min-level", "5" },
    { "pentium3" "-" TYPE_X86_CPU, "min-level", "2" },
    { "n270" "-" TYPE_X86_CPU, "min-level", "5" },
    { "Conroe" "-" TYPE_X86_CPU, "min-level", "4" },
    { "Penryn" "-" TYPE_X86_CPU, "min-level", "4" },
    { "Nehalem" "-" TYPE_X86_CPU, "min-level", "4" },
    { "n270" "-" TYPE_X86_CPU, "min-xlevel", "0x8000000a" },
    { "Penryn" "-" TYPE_X86_CPU, "min-xlevel", "0x8000000a" },
    { "Conroe" "-" TYPE_X86_CPU, "min-xlevel", "0x8000000a" },
    { "Nehalem" "-" TYPE_X86_CPU, "min-xlevel", "0x8000000a" },
    { "Westmere" "-" TYPE_X86_CPU, "min-xlevel", "0x8000000a" },
    { "SandyBridge" "-" TYPE_X86_CPU, "min-xlevel", "0x8000000a" },
    { "IvyBridge" "-" TYPE_X86_CPU, "min-xlevel", "0x8000000a" },
    { "Haswell" "-" TYPE_X86_CPU, "min-xlevel", "0x8000000a" },
    { "Haswell-noTSX" "-" TYPE_X86_CPU, "min-xlevel", "0x8000000a" },
    { "Broadwell" "-" TYPE_X86_CPU, "min-xlevel", "0x8000000a" },
    { "Broadwell-noTSX" "-" TYPE_X86_CPU, "min-xlevel", "0x8000000a" },
    { TYPE_X86_CPU, "kvm-no-smi-migration", "on" },
};
const size_t pc_compat_2_3_len = G_N_ELEMENTS(pc_compat_2_3);

GlobalProperty pc_compat_2_2[] = {
    PC_CPU_MODEL_IDS("2.2.0")
    { "kvm64" "-" TYPE_X86_CPU, "vme", "off" },
    { "kvm32" "-" TYPE_X86_CPU, "vme", "off" },
    { "Conroe" "-" TYPE_X86_CPU, "vme", "off" },
    { "Penryn" "-" TYPE_X86_CPU, "vme", "off" },
    { "Nehalem" "-" TYPE_X86_CPU, "vme", "off" },
    { "Westmere" "-" TYPE_X86_CPU, "vme", "off" },
    { "SandyBridge" "-" TYPE_X86_CPU, "vme", "off" },
    { "Haswell" "-" TYPE_X86_CPU, "vme", "off" },
    { "Broadwell" "-" TYPE_X86_CPU, "vme", "off" },
    { "Opteron_G1" "-" TYPE_X86_CPU, "vme", "off" },
    { "Opteron_G2" "-" TYPE_X86_CPU, "vme", "off" },
    { "Opteron_G3" "-" TYPE_X86_CPU, "vme", "off" },
    { "Opteron_G4" "-" TYPE_X86_CPU, "vme", "off" },
    { "Opteron_G5" "-" TYPE_X86_CPU, "vme", "off" },
    { "Haswell" "-" TYPE_X86_CPU, "f16c", "off" },
    { "Haswell" "-" TYPE_X86_CPU, "rdrand", "off" },
    { "Broadwell" "-" TYPE_X86_CPU, "f16c", "off" },
    { "Broadwell" "-" TYPE_X86_CPU, "rdrand", "off" },
};
const size_t pc_compat_2_2_len = G_N_ELEMENTS(pc_compat_2_2);

GlobalProperty pc_compat_2_1[] = {
    PC_CPU_MODEL_IDS("2.1.0")
    { "coreduo" "-" TYPE_X86_CPU, "vmx", "on" },
    { "core2duo" "-" TYPE_X86_CPU, "vmx", "on" },
};
const size_t pc_compat_2_1_len = G_N_ELEMENTS(pc_compat_2_1);

GlobalProperty pc_compat_2_0[] = {
    PC_CPU_MODEL_IDS("2.0.0")
    { "virtio-scsi-pci", "any_layout", "off" },
    { "PIIX4_PM", "memory-hotplug-support", "off" },
    { "apic", "version", "0x11" },
    { "nec-usb-xhci", "superspeed-ports-first", "off" },
    { "nec-usb-xhci", "force-pcie-endcap", "on" },
    { "pci-serial", "prog_if", "0" },
    { "pci-serial-2x", "prog_if", "0" },
    { "pci-serial-4x", "prog_if", "0" },
    { "virtio-net-pci", "guest_announce", "off" },
    { "ICH9-LPC", "memory-hotplug-support", "off" },
    { "xio3130-downstream", COMPAT_PROP_PCP, "off" },
    { "ioh3420", COMPAT_PROP_PCP, "off" },
};
const size_t pc_compat_2_0_len = G_N_ELEMENTS(pc_compat_2_0);

GlobalProperty pc_compat_1_7[] = {
    PC_CPU_MODEL_IDS("1.7.0")
    { TYPE_USB_DEVICE, "msos-desc", "no" },
    { "PIIX4_PM", "acpi-pci-hotplug-with-bridge-support", "off" },
    { "hpet", HPET_INTCAP, "4" },
};
const size_t pc_compat_1_7_len = G_N_ELEMENTS(pc_compat_1_7);

GlobalProperty pc_compat_1_6[] = {
    PC_CPU_MODEL_IDS("1.6.0")
    { "e1000", "mitigation", "off" },
    { "qemu64-" TYPE_X86_CPU, "model", "2" },
    { "qemu32-" TYPE_X86_CPU, "model", "3" },
    { "i440FX-pcihost", "short_root_bus", "1" },
    { "q35-pcihost", "short_root_bus", "1" },
};
const size_t pc_compat_1_6_len = G_N_ELEMENTS(pc_compat_1_6);

GlobalProperty pc_compat_1_5[] = {
    PC_CPU_MODEL_IDS("1.5.0")
    { "Conroe-" TYPE_X86_CPU, "model", "2" },
    { "Conroe-" TYPE_X86_CPU, "min-level", "2" },
    { "Penryn-" TYPE_X86_CPU, "model", "2" },
    { "Penryn-" TYPE_X86_CPU, "min-level", "2" },
    { "Nehalem-" TYPE_X86_CPU, "model", "2" },
    { "Nehalem-" TYPE_X86_CPU, "min-level", "2" },
    { "virtio-net-pci", "any_layout", "off" },
    { TYPE_X86_CPU, "pmu", "on" },
    { "i440FX-pcihost", "short_root_bus", "0" },
    { "q35-pcihost", "short_root_bus", "0" },
};
const size_t pc_compat_1_5_len = G_N_ELEMENTS(pc_compat_1_5);

GlobalProperty pc_compat_1_4[] = {
    PC_CPU_MODEL_IDS("1.4.0")
    { "scsi-hd", "discard_granularity", "0" },
    { "scsi-cd", "discard_granularity", "0" },
    { "scsi-disk", "discard_granularity", "0" },
    { "ide-hd", "discard_granularity", "0" },
    { "ide-cd", "discard_granularity", "0" },
    { "ide-drive", "discard_granularity", "0" },
    { "virtio-blk-pci", "discard_granularity", "0" },
    /* DEV_NVECTORS_UNSPECIFIED as a uint32_t string: */
    { "virtio-serial-pci", "vectors", "0xFFFFFFFF" },
    { "virtio-net-pci", "ctrl_guest_offloads", "off" },
    { "e1000", "romfile", "pxe-e1000.rom" },
    { "ne2k_pci", "romfile", "pxe-ne2k_pci.rom" },
    { "pcnet", "romfile", "pxe-pcnet.rom" },
    { "rtl8139", "romfile", "pxe-rtl8139.rom" },
    { "virtio-net-pci", "romfile", "pxe-virtio.rom" },
    { "486-" TYPE_X86_CPU, "model", "0" },
    { "n270" "-" TYPE_X86_CPU, "movbe", "off" },
    { "Westmere" "-" TYPE_X86_CPU, "pclmulqdq", "off" },
};
const size_t pc_compat_1_4_len = G_N_ELEMENTS(pc_compat_1_4);

GSIState *pc_gsi_create(qemu_irq **irqs, bool pci_enabled)
{
    GSIState *s;

    s = g_new0(GSIState, 1);
    if (kvm_ioapic_in_kernel()) {
        kvm_pc_setup_irq_routing(pci_enabled);
    }
    *irqs = qemu_allocate_irqs(gsi_handler, s, GSI_NUM_PINS);

    return s;
}

static void ioport80_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
}

static uint64_t ioport80_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0xffffffffffffffffULL;
}

/* MSDOS compatibility mode FPU exception support */
static void ioportF0_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size)
{
    if (tcg_enabled()) {
        cpu_set_ignne();
    }
}

static uint64_t ioportF0_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0xffffffffffffffffULL;
}

/* PC cmos mappings */

#define REG_EQUIPMENT_BYTE          0x14

int cmos_get_fd_drive_type(FloppyDriveType fd0)
{
    int val;

    switch (fd0) {
    case FLOPPY_DRIVE_TYPE_144:
        /* 1.44 Mb 3"5 drive */
        val = 4;
        break;
    case FLOPPY_DRIVE_TYPE_288:
        /* 2.88 Mb 3"5 drive */
        val = 5;
        break;
    case FLOPPY_DRIVE_TYPE_120:
        /* 1.2 Mb 5"5 drive */
        val = 2;
        break;
    case FLOPPY_DRIVE_TYPE_NONE:
    default:
        val = 0;
        break;
    }
    return val;
}

static void cmos_init_hd(ISADevice *s, int type_ofs, int info_ofs,
                         int16_t cylinders, int8_t heads, int8_t sectors)
{
    rtc_set_memory(s, type_ofs, 47);
    rtc_set_memory(s, info_ofs, cylinders);
    rtc_set_memory(s, info_ofs + 1, cylinders >> 8);
    rtc_set_memory(s, info_ofs + 2, heads);
    rtc_set_memory(s, info_ofs + 3, 0xff);
    rtc_set_memory(s, info_ofs + 4, 0xff);
    rtc_set_memory(s, info_ofs + 5, 0xc0 | ((heads > 8) << 3));
    rtc_set_memory(s, info_ofs + 6, cylinders);
    rtc_set_memory(s, info_ofs + 7, cylinders >> 8);
    rtc_set_memory(s, info_ofs + 8, sectors);
}

/* convert boot_device letter to something recognizable by the bios */
static int boot_device2nibble(char boot_device)
{
    switch(boot_device) {
    case 'a':
    case 'b':
        return 0x01; /* floppy boot */
    case 'c':
        return 0x02; /* hard drive boot */
    case 'd':
        return 0x03; /* CD-ROM boot */
    case 'n':
        return 0x04; /* Network boot */
    }
    return 0;
}

static void set_boot_dev(ISADevice *s, const char *boot_device, Error **errp)
{
#define PC_MAX_BOOT_DEVICES 3
    int nbds, bds[3] = { 0, };
    int i;

    nbds = strlen(boot_device);
    if (nbds > PC_MAX_BOOT_DEVICES) {
        error_setg(errp, "Too many boot devices for PC");
        return;
    }
    for (i = 0; i < nbds; i++) {
        bds[i] = boot_device2nibble(boot_device[i]);
        if (bds[i] == 0) {
            error_setg(errp, "Invalid boot device for PC: '%c'",
                       boot_device[i]);
            return;
        }
    }
    rtc_set_memory(s, 0x3d, (bds[1] << 4) | bds[0]);
    rtc_set_memory(s, 0x38, (bds[2] << 4) | (fd_bootchk ? 0x0 : 0x1));
}

static void pc_boot_set(void *opaque, const char *boot_device, Error **errp)
{
    set_boot_dev(opaque, boot_device, errp);
}

static void pc_cmos_init_floppy(ISADevice *rtc_state, ISADevice *floppy)
{
    int val, nb, i;
    FloppyDriveType fd_type[2] = { FLOPPY_DRIVE_TYPE_NONE,
                                   FLOPPY_DRIVE_TYPE_NONE };

    /* floppy type */
    if (floppy) {
        for (i = 0; i < 2; i++) {
            fd_type[i] = isa_fdc_get_drive_type(floppy, i);
        }
    }
    val = (cmos_get_fd_drive_type(fd_type[0]) << 4) |
        cmos_get_fd_drive_type(fd_type[1]);
    rtc_set_memory(rtc_state, 0x10, val);

    val = rtc_get_memory(rtc_state, REG_EQUIPMENT_BYTE);
    nb = 0;
    if (fd_type[0] != FLOPPY_DRIVE_TYPE_NONE) {
        nb++;
    }
    if (fd_type[1] != FLOPPY_DRIVE_TYPE_NONE) {
        nb++;
    }
    switch (nb) {
    case 0:
        break;
    case 1:
        val |= 0x01; /* 1 drive, ready for boot */
        break;
    case 2:
        val |= 0x41; /* 2 drives, ready for boot */
        break;
    }
    rtc_set_memory(rtc_state, REG_EQUIPMENT_BYTE, val);
}

typedef struct pc_cmos_init_late_arg {
    ISADevice *rtc_state;
    BusState *idebus[2];
} pc_cmos_init_late_arg;

typedef struct check_fdc_state {
    ISADevice *floppy;
    bool multiple;
} CheckFdcState;

static int check_fdc(Object *obj, void *opaque)
{
    CheckFdcState *state = opaque;
    Object *fdc;
    uint32_t iobase;
    Error *local_err = NULL;

    fdc = object_dynamic_cast(obj, TYPE_ISA_FDC);
    if (!fdc) {
        return 0;
    }

    iobase = object_property_get_uint(obj, "iobase", &local_err);
    if (local_err || iobase != 0x3f0) {
        error_free(local_err);
        return 0;
    }

    if (state->floppy) {
        state->multiple = true;
    } else {
        state->floppy = ISA_DEVICE(obj);
    }
    return 0;
}

static const char * const fdc_container_path[] = {
    "/unattached", "/peripheral", "/peripheral-anon"
};

/*
 * Locate the FDC at IO address 0x3f0, in order to configure the CMOS registers
 * and ACPI objects.
 */
ISADevice *pc_find_fdc0(void)
{
    int i;
    Object *container;
    CheckFdcState state = { 0 };

    for (i = 0; i < ARRAY_SIZE(fdc_container_path); i++) {
        container = container_get(qdev_get_machine(), fdc_container_path[i]);
        object_child_foreach(container, check_fdc, &state);
    }

    if (state.multiple) {
        warn_report("multiple floppy disk controllers with "
                    "iobase=0x3f0 have been found");
        error_printf("the one being picked for CMOS setup might not reflect "
                     "your intent");
    }

    return state.floppy;
}

static void pc_cmos_init_late(void *opaque)
{
    pc_cmos_init_late_arg *arg = opaque;
    ISADevice *s = arg->rtc_state;
    int16_t cylinders;
    int8_t heads, sectors;
    int val;
    int i, trans;

    val = 0;
    if (arg->idebus[0] && ide_get_geometry(arg->idebus[0], 0,
                                           &cylinders, &heads, &sectors) >= 0) {
        cmos_init_hd(s, 0x19, 0x1b, cylinders, heads, sectors);
        val |= 0xf0;
    }
    if (arg->idebus[0] && ide_get_geometry(arg->idebus[0], 1,
                                           &cylinders, &heads, &sectors) >= 0) {
        cmos_init_hd(s, 0x1a, 0x24, cylinders, heads, sectors);
        val |= 0x0f;
    }
    rtc_set_memory(s, 0x12, val);

    val = 0;
    for (i = 0; i < 4; i++) {
        /* NOTE: ide_get_geometry() returns the physical
           geometry.  It is always such that: 1 <= sects <= 63, 1
           <= heads <= 16, 1 <= cylinders <= 16383. The BIOS
           geometry can be different if a translation is done. */
        if (arg->idebus[i / 2] &&
            ide_get_geometry(arg->idebus[i / 2], i % 2,
                             &cylinders, &heads, &sectors) >= 0) {
            trans = ide_get_bios_chs_trans(arg->idebus[i / 2], i % 2) - 1;
            assert((trans & ~3) == 0);
            val |= trans << (i * 2);
        }
    }
    rtc_set_memory(s, 0x39, val);

    pc_cmos_init_floppy(s, pc_find_fdc0());

    qemu_unregister_reset(pc_cmos_init_late, opaque);
}

void pc_cmos_init(PCMachineState *pcms,
                  BusState *idebus0, BusState *idebus1,
                  ISADevice *s)
{
    int val;
    static pc_cmos_init_late_arg arg;
    X86MachineState *x86ms = X86_MACHINE(pcms);

    /* various important CMOS locations needed by PC/Bochs bios */

    /* memory size */
    /* base memory (first MiB) */
    val = MIN(x86ms->below_4g_mem_size / KiB, 640);
    rtc_set_memory(s, 0x15, val);
    rtc_set_memory(s, 0x16, val >> 8);
    /* extended memory (next 64MiB) */
    if (x86ms->below_4g_mem_size > 1 * MiB) {
        val = (x86ms->below_4g_mem_size - 1 * MiB) / KiB;
    } else {
        val = 0;
    }
    if (val > 65535)
        val = 65535;
    rtc_set_memory(s, 0x17, val);
    rtc_set_memory(s, 0x18, val >> 8);
    rtc_set_memory(s, 0x30, val);
    rtc_set_memory(s, 0x31, val >> 8);
    /* memory between 16MiB and 4GiB */
    if (x86ms->below_4g_mem_size > 16 * MiB) {
        val = (x86ms->below_4g_mem_size - 16 * MiB) / (64 * KiB);
    } else {
        val = 0;
    }
    if (val > 65535)
        val = 65535;
    rtc_set_memory(s, 0x34, val);
    rtc_set_memory(s, 0x35, val >> 8);
    /* memory above 4GiB */
    val = x86ms->above_4g_mem_size / 65536;
    rtc_set_memory(s, 0x5b, val);
    rtc_set_memory(s, 0x5c, val >> 8);
    rtc_set_memory(s, 0x5d, val >> 16);

    object_property_add_link(OBJECT(pcms), "rtc_state",
                             TYPE_ISA_DEVICE,
                             (Object **)&x86ms->rtc,
                             object_property_allow_set_link,
                             OBJ_PROP_LINK_STRONG, &error_abort);
    object_property_set_link(OBJECT(pcms), OBJECT(s),
                             "rtc_state", &error_abort);

    set_boot_dev(s, MACHINE(pcms)->boot_order, &error_fatal);

    val = 0;
    val |= 0x02; /* FPU is there */
    val |= 0x04; /* PS/2 mouse installed */
    rtc_set_memory(s, REG_EQUIPMENT_BYTE, val);

    /* hard drives and FDC */
    arg.rtc_state = s;
    arg.idebus[0] = idebus0;
    arg.idebus[1] = idebus1;
    qemu_register_reset(pc_cmos_init_late, &arg);
}

static void handle_a20_line_change(void *opaque, int irq, int level)
{
    X86CPU *cpu = opaque;

    /* XXX: send to all CPUs ? */
    /* XXX: add logic to handle multiple A20 line sources */
    x86_cpu_set_a20(cpu, level);
}

#define NE2000_NB_MAX 6

static const int ne2000_io[NE2000_NB_MAX] = { 0x300, 0x320, 0x340, 0x360,
                                              0x280, 0x380 };
static const int ne2000_irq[NE2000_NB_MAX] = { 9, 10, 11, 3, 4, 5 };

void pc_init_ne2k_isa(ISABus *bus, NICInfo *nd)
{
    static int nb_ne2k = 0;

    if (nb_ne2k == NE2000_NB_MAX)
        return;
    isa_ne2000_init(bus, ne2000_io[nb_ne2k],
                    ne2000_irq[nb_ne2k], nd);
    nb_ne2k++;
}

void pc_acpi_smi_interrupt(void *opaque, int irq, int level)
{
    X86CPU *cpu = opaque;

    if (level) {
        cpu_interrupt(CPU(cpu), CPU_INTERRUPT_SMI);
    }
}

/*
 * This function is very similar to smp_parse()
 * in hw/core/machine.c but includes CPU die support.
 */
void pc_smp_parse(MachineState *ms, QemuOpts *opts)
{
    X86MachineState *x86ms = X86_MACHINE(ms);

    if (opts) {
        unsigned cpus    = qemu_opt_get_number(opts, "cpus", 0);
        unsigned sockets = qemu_opt_get_number(opts, "sockets", 0);
        unsigned dies = qemu_opt_get_number(opts, "dies", 1);
        unsigned cores   = qemu_opt_get_number(opts, "cores", 0);
        unsigned threads = qemu_opt_get_number(opts, "threads", 0);

        /* compute missing values, prefer sockets over cores over threads */
        if (cpus == 0 || sockets == 0) {
            cores = cores > 0 ? cores : 1;
            threads = threads > 0 ? threads : 1;
            if (cpus == 0) {
                sockets = sockets > 0 ? sockets : 1;
                cpus = cores * threads * dies * sockets;
            } else {
                ms->smp.max_cpus =
                        qemu_opt_get_number(opts, "maxcpus", cpus);
                sockets = ms->smp.max_cpus / (cores * threads * dies);
            }
        } else if (cores == 0) {
            threads = threads > 0 ? threads : 1;
            cores = cpus / (sockets * dies * threads);
            cores = cores > 0 ? cores : 1;
        } else if (threads == 0) {
            threads = cpus / (cores * dies * sockets);
            threads = threads > 0 ? threads : 1;
        } else if (sockets * dies * cores * threads < cpus) {
            error_report("cpu topology: "
                         "sockets (%u) * dies (%u) * cores (%u) * threads (%u) < "
                         "smp_cpus (%u)",
                         sockets, dies, cores, threads, cpus);
            exit(1);
        }

        ms->smp.max_cpus =
                qemu_opt_get_number(opts, "maxcpus", cpus);

        if (ms->smp.max_cpus < cpus) {
            error_report("maxcpus must be equal to or greater than smp");
            exit(1);
        }

        if (sockets * dies * cores * threads > ms->smp.max_cpus) {
            error_report("cpu topology: "
                         "sockets (%u) * dies (%u) * cores (%u) * threads (%u) > "
                         "maxcpus (%u)",
                         sockets, dies, cores, threads,
                         ms->smp.max_cpus);
            exit(1);
        }

        if (sockets * dies * cores * threads != ms->smp.max_cpus) {
            warn_report("Invalid CPU topology deprecated: "
                        "sockets (%u) * dies (%u) * cores (%u) * threads (%u) "
                        "!= maxcpus (%u)",
                        sockets, dies, cores, threads,
                        ms->smp.max_cpus);
        }

        ms->smp.cpus = cpus;
        ms->smp.cores = cores;
        ms->smp.threads = threads;
        ms->smp.sockets = sockets;
        x86ms->smp_dies = dies;
    }

    if (ms->smp.cpus > 1) {
        Error *blocker = NULL;
        error_setg(&blocker, QERR_REPLAY_NOT_SUPPORTED, "smp");
        replay_add_blocker(blocker);
    }
}

void pc_hot_add_cpu(MachineState *ms, const int64_t id, Error **errp)
{
    X86MachineState *x86ms = X86_MACHINE(ms);
    int64_t apic_id = x86_cpu_apic_id_from_index(x86ms, id);
    Error *local_err = NULL;

    if (id < 0) {
        error_setg(errp, "Invalid CPU id: %" PRIi64, id);
        return;
    }

    if (apic_id >= ACPI_CPU_HOTPLUG_ID_LIMIT) {
        error_setg(errp, "Unable to add CPU: %" PRIi64
                   ", resulting APIC ID (%" PRIi64 ") is too large",
                   id, apic_id);
        return;
    }


    x86_cpu_new(X86_MACHINE(ms), apic_id, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
}

static void rtc_set_cpus_count(ISADevice *rtc, uint16_t cpus_count)
{
    if (cpus_count > 0xff) {
        /* If the number of CPUs can't be represented in 8 bits, the
         * BIOS must use "FW_CFG_NB_CPUS". Set RTC field to 0 just
         * to make old BIOSes fail more predictably.
         */
        rtc_set_memory(rtc, 0x5f, 0);
    } else {
        rtc_set_memory(rtc, 0x5f, cpus_count - 1);
    }
}

static
void pc_machine_done(Notifier *notifier, void *data)
{
    PCMachineState *pcms = container_of(notifier,
                                        PCMachineState, machine_done);
    X86MachineState *x86ms = X86_MACHINE(pcms);
    PCIBus *bus = pcms->bus;

    /* set the number of CPUs */
    rtc_set_cpus_count(x86ms->rtc, x86ms->boot_cpus);

    if (bus) {
        int extra_hosts = 0;

        QLIST_FOREACH(bus, &bus->child, sibling) {
            /* look for expander root buses */
            if (pci_bus_is_root(bus)) {
                extra_hosts++;
            }
        }
        if (extra_hosts && x86ms->fw_cfg) {
            uint64_t *val = g_malloc(sizeof(*val));
            *val = cpu_to_le64(extra_hosts);
            fw_cfg_add_file(x86ms->fw_cfg,
                    "etc/extra-pci-roots", val, sizeof(*val));
        }
    }

    acpi_setup();
    if (x86ms->fw_cfg) {
        fw_cfg_build_smbios(MACHINE(pcms), x86ms->fw_cfg);
        fw_cfg_build_feature_control(MACHINE(pcms), x86ms->fw_cfg);
        /* update FW_CFG_NB_CPUS to account for -device added CPUs */
        fw_cfg_modify_i16(x86ms->fw_cfg, FW_CFG_NB_CPUS, x86ms->boot_cpus);
    }

    if (x86ms->apic_id_limit > 255 && !xen_enabled()) {
        IntelIOMMUState *iommu = INTEL_IOMMU_DEVICE(x86_iommu_get_default());

        if (!iommu || !x86_iommu_ir_supported(X86_IOMMU_DEVICE(iommu)) ||
            iommu->intr_eim != ON_OFF_AUTO_ON) {
            error_report("current -smp configuration requires "
                         "Extended Interrupt Mode enabled. "
                         "You can add an IOMMU using: "
                         "-device intel-iommu,intremap=on,eim=on");
            exit(EXIT_FAILURE);
        }
    }
}

void pc_guest_info_init(PCMachineState *pcms)
{
    int i;
    MachineState *ms = MACHINE(pcms);
    X86MachineState *x86ms = X86_MACHINE(pcms);

    x86ms->apic_xrupt_override = kvm_allows_irq0_override();
    pcms->numa_nodes = ms->numa_state->num_nodes;
    pcms->node_mem = g_malloc0(pcms->numa_nodes *
                                    sizeof *pcms->node_mem);
    for (i = 0; i < ms->numa_state->num_nodes; i++) {
        pcms->node_mem[i] = ms->numa_state->nodes[i].node_mem;
    }

    pcms->machine_done.notify = pc_machine_done;
    qemu_add_machine_init_done_notifier(&pcms->machine_done);
}

/* setup pci memory address space mapping into system address space */
void pc_pci_as_mapping_init(Object *owner, MemoryRegion *system_memory,
                            MemoryRegion *pci_address_space)
{
    /* Set to lower priority than RAM */
    memory_region_add_subregion_overlap(system_memory, 0x0,
                                        pci_address_space, -1);
}

void xen_load_linux(PCMachineState *pcms)
{
    int i;
    FWCfgState *fw_cfg;
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    X86MachineState *x86ms = X86_MACHINE(pcms);

    assert(MACHINE(pcms)->kernel_filename != NULL);

    fw_cfg = fw_cfg_init_io(FW_CFG_IO_BASE);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, x86ms->boot_cpus);
    rom_set_fw(fw_cfg);

    x86_load_linux(x86ms, fw_cfg, pcmc->acpi_data_size,
                   pcmc->pvh_enabled, pcmc->linuxboot_dma_enabled);
    for (i = 0; i < nb_option_roms; i++) {
        assert(!strcmp(option_rom[i].name, "linuxboot.bin") ||
               !strcmp(option_rom[i].name, "linuxboot_dma.bin") ||
               !strcmp(option_rom[i].name, "pvh.bin") ||
               !strcmp(option_rom[i].name, "multiboot.bin"));
        rom_add_option(option_rom[i].name, option_rom[i].bootindex);
    }
    x86ms->fw_cfg = fw_cfg;
}

void pc_memory_init(PCMachineState *pcms,
                    MemoryRegion *system_memory,
                    MemoryRegion *rom_memory,
                    MemoryRegion **ram_memory)
{
    int linux_boot, i;
    MemoryRegion *option_rom_mr;
    MemoryRegion *ram_below_4g, *ram_above_4g;
    FWCfgState *fw_cfg;
    MachineState *machine = MACHINE(pcms);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    X86MachineState *x86ms = X86_MACHINE(pcms);

    assert(machine->ram_size == x86ms->below_4g_mem_size +
                                x86ms->above_4g_mem_size);

    linux_boot = (machine->kernel_filename != NULL);

    /*
     * Split single memory region and use aliases to address portions of it,
     * done for backwards compatibility with older qemus.
     */
    *ram_memory = machine->ram;
    ram_below_4g = g_malloc(sizeof(*ram_below_4g));
    memory_region_init_alias(ram_below_4g, NULL, "ram-below-4g", machine->ram,
                             0, x86ms->below_4g_mem_size);
    memory_region_add_subregion(system_memory, 0, ram_below_4g);
    e820_add_entry(0, x86ms->below_4g_mem_size, E820_RAM);
    if (x86ms->above_4g_mem_size > 0) {
        ram_above_4g = g_malloc(sizeof(*ram_above_4g));
        memory_region_init_alias(ram_above_4g, NULL, "ram-above-4g",
                                 machine->ram,
                                 x86ms->below_4g_mem_size,
                                 x86ms->above_4g_mem_size);
        memory_region_add_subregion(system_memory, 0x100000000ULL,
                                    ram_above_4g);
        e820_add_entry(0x100000000ULL, x86ms->above_4g_mem_size, E820_RAM);
    }

    if (!pcmc->has_reserved_memory &&
        (machine->ram_slots ||
         (machine->maxram_size > machine->ram_size))) {

        error_report("\"-memory 'slots|maxmem'\" is not supported by: %s",
                     mc->name);
        exit(EXIT_FAILURE);
    }

    /* always allocate the device memory information */
    machine->device_memory = g_malloc0(sizeof(*machine->device_memory));

    /* initialize device memory address space */
    if (pcmc->has_reserved_memory &&
        (machine->ram_size < machine->maxram_size)) {
        ram_addr_t device_mem_size = machine->maxram_size - machine->ram_size;

        if (machine->ram_slots > ACPI_MAX_RAM_SLOTS) {
            error_report("unsupported amount of memory slots: %"PRIu64,
                         machine->ram_slots);
            exit(EXIT_FAILURE);
        }

        if (QEMU_ALIGN_UP(machine->maxram_size,
                          TARGET_PAGE_SIZE) != machine->maxram_size) {
            error_report("maximum memory size must by aligned to multiple of "
                         "%d bytes", TARGET_PAGE_SIZE);
            exit(EXIT_FAILURE);
        }

        machine->device_memory->base =
            ROUND_UP(0x100000000ULL + x86ms->above_4g_mem_size, 1 * GiB);

        if (pcmc->enforce_aligned_dimm) {
            /* size device region assuming 1G page max alignment per slot */
            device_mem_size += (1 * GiB) * machine->ram_slots;
        }

        if ((machine->device_memory->base + device_mem_size) <
            device_mem_size) {
            error_report("unsupported amount of maximum memory: " RAM_ADDR_FMT,
                         machine->maxram_size);
            exit(EXIT_FAILURE);
        }

        memory_region_init(&machine->device_memory->mr, OBJECT(pcms),
                           "device-memory", device_mem_size);
        memory_region_add_subregion(system_memory, machine->device_memory->base,
                                    &machine->device_memory->mr);
    }

    /* Initialize PC system firmware */
    pc_system_firmware_init(pcms, rom_memory);

    option_rom_mr = g_malloc(sizeof(*option_rom_mr));
    memory_region_init_ram(option_rom_mr, NULL, "pc.rom", PC_ROM_SIZE,
                           &error_fatal);
    if (pcmc->pci_enabled) {
        memory_region_set_readonly(option_rom_mr, true);
    }
    memory_region_add_subregion_overlap(rom_memory,
                                        PC_ROM_MIN_VGA,
                                        option_rom_mr,
                                        1);

    fw_cfg = fw_cfg_arch_create(machine,
                                x86ms->boot_cpus, x86ms->apic_id_limit);

    rom_set_fw(fw_cfg);

    if (pcmc->has_reserved_memory && machine->device_memory->base) {
        uint64_t *val = g_malloc(sizeof(*val));
        PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
        uint64_t res_mem_end = machine->device_memory->base;

        if (!pcmc->broken_reserved_end) {
            res_mem_end += memory_region_size(&machine->device_memory->mr);
        }
        *val = cpu_to_le64(ROUND_UP(res_mem_end, 1 * GiB));
        fw_cfg_add_file(fw_cfg, "etc/reserved-memory-end", val, sizeof(*val));
    }

    if (linux_boot) {
        x86_load_linux(x86ms, fw_cfg, pcmc->acpi_data_size,
                       pcmc->pvh_enabled, pcmc->linuxboot_dma_enabled);
    }

    for (i = 0; i < nb_option_roms; i++) {
        rom_add_option(option_rom[i].name, option_rom[i].bootindex);
    }
    x86ms->fw_cfg = fw_cfg;

    /* Init default IOAPIC address space */
    x86ms->ioapic_as = &address_space_memory;

    /* Init ACPI memory hotplug IO base address */
    pcms->memhp_io_base = ACPI_MEMORY_HOTPLUG_BASE;
}

/*
 * The 64bit pci hole starts after "above 4G RAM" and
 * potentially the space reserved for memory hotplug.
 */
uint64_t pc_pci_hole64_start(void)
{
    PCMachineState *pcms = PC_MACHINE(qdev_get_machine());
    PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    MachineState *ms = MACHINE(pcms);
    X86MachineState *x86ms = X86_MACHINE(pcms);
    uint64_t hole64_start = 0;

    if (pcmc->has_reserved_memory && ms->device_memory->base) {
        hole64_start = ms->device_memory->base;
        if (!pcmc->broken_reserved_end) {
            hole64_start += memory_region_size(&ms->device_memory->mr);
        }
    } else {
        hole64_start = 0x100000000ULL + x86ms->above_4g_mem_size;
    }

    return ROUND_UP(hole64_start, 1 * GiB);
}

DeviceState *pc_vga_init(ISABus *isa_bus, PCIBus *pci_bus)
{
    DeviceState *dev = NULL;

    rom_set_order_override(FW_CFG_ORDER_OVERRIDE_VGA);
    if (pci_bus) {
        PCIDevice *pcidev = pci_vga_init(pci_bus);
        dev = pcidev ? &pcidev->qdev : NULL;
    } else if (isa_bus) {
        ISADevice *isadev = isa_vga_init(isa_bus);
        dev = isadev ? DEVICE(isadev) : NULL;
    }
    rom_reset_order_override();
    return dev;
}

static const MemoryRegionOps ioport80_io_ops = {
    .write = ioport80_write,
    .read = ioport80_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static const MemoryRegionOps ioportF0_io_ops = {
    .write = ioportF0_write,
    .read = ioportF0_read,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void pc_superio_init(ISABus *isa_bus, bool create_fdctrl, bool no_vmport)
{
    int i;
    DriveInfo *fd[MAX_FD];
    qemu_irq *a20_line;
    ISADevice *i8042, *port92, *vmmouse;

    serial_hds_isa_init(isa_bus, 0, MAX_ISA_SERIAL_PORTS);
    parallel_hds_isa_init(isa_bus, MAX_PARALLEL_PORTS);

    for (i = 0; i < MAX_FD; i++) {
        fd[i] = drive_get(IF_FLOPPY, 0, i);
        create_fdctrl |= !!fd[i];
    }
    if (create_fdctrl) {
        fdctrl_init_isa(isa_bus, fd);
    }

    i8042 = isa_create_simple(isa_bus, "i8042");
    if (!no_vmport) {
        vmport_init(isa_bus);
        vmmouse = isa_try_create(isa_bus, "vmmouse");
    } else {
        vmmouse = NULL;
    }
    if (vmmouse) {
        object_property_set_link(OBJECT(vmmouse), OBJECT(i8042),
                                 "i8042", &error_abort);
        qdev_init_nofail(DEVICE(vmmouse));
    }
    port92 = isa_create_simple(isa_bus, TYPE_PORT92);

    a20_line = qemu_allocate_irqs(handle_a20_line_change, first_cpu, 2);
    i8042_setup_a20_line(i8042, a20_line[0]);
    qdev_connect_gpio_out_named(DEVICE(port92),
                                PORT92_A20_LINE, 0, a20_line[1]);
    g_free(a20_line);
}

void pc_basic_device_init(ISABus *isa_bus, qemu_irq *gsi,
                          ISADevice **rtc_state,
                          bool create_fdctrl,
                          bool no_vmport,
                          bool has_pit,
                          uint32_t hpet_irqs)
{
    int i;
    DeviceState *hpet = NULL;
    int pit_isa_irq = 0;
    qemu_irq pit_alt_irq = NULL;
    qemu_irq rtc_irq = NULL;
    ISADevice *pit = NULL;
    MemoryRegion *ioport80_io = g_new(MemoryRegion, 1);
    MemoryRegion *ioportF0_io = g_new(MemoryRegion, 1);

    memory_region_init_io(ioport80_io, NULL, &ioport80_io_ops, NULL, "ioport80", 1);
    memory_region_add_subregion(isa_bus->address_space_io, 0x80, ioport80_io);

    memory_region_init_io(ioportF0_io, NULL, &ioportF0_io_ops, NULL, "ioportF0", 1);
    memory_region_add_subregion(isa_bus->address_space_io, 0xf0, ioportF0_io);

    /*
     * Check if an HPET shall be created.
     *
     * Without KVM_CAP_PIT_STATE2, we cannot switch off the in-kernel PIT
     * when the HPET wants to take over. Thus we have to disable the latter.
     */
    if (!no_hpet && (!kvm_irqchip_in_kernel() || kvm_has_pit_state2())) {
        hpet = qdev_try_create(NULL, TYPE_HPET);
        if (hpet) {
            /* For pc-piix-*, hpet's intcap is always IRQ2. For pc-q35-1.7
             * and earlier, use IRQ2 for compat. Otherwise, use IRQ16~23,
             * IRQ8 and IRQ2.
             */
            uint8_t compat = object_property_get_uint(OBJECT(hpet),
                    HPET_INTCAP, NULL);
            if (!compat) {
                qdev_prop_set_uint32(hpet, HPET_INTCAP, hpet_irqs);
            }
            qdev_init_nofail(hpet);
            sysbus_mmio_map(SYS_BUS_DEVICE(hpet), 0, HPET_BASE);

            for (i = 0; i < GSI_NUM_PINS; i++) {
                sysbus_connect_irq(SYS_BUS_DEVICE(hpet), i, gsi[i]);
            }
            pit_isa_irq = -1;
            pit_alt_irq = qdev_get_gpio_in(hpet, HPET_LEGACY_PIT_INT);
            rtc_irq = qdev_get_gpio_in(hpet, HPET_LEGACY_RTC_INT);
        }
    }
    *rtc_state = mc146818_rtc_init(isa_bus, 2000, rtc_irq);

    qemu_register_boot_set(pc_boot_set, *rtc_state);

    if (!xen_enabled() && has_pit) {
        if (kvm_pit_in_kernel()) {
            pit = kvm_pit_init(isa_bus, 0x40);
        } else {
            pit = i8254_pit_init(isa_bus, 0x40, pit_isa_irq, pit_alt_irq);
        }
        if (hpet) {
            /* connect PIT to output control line of the HPET */
            qdev_connect_gpio_out(hpet, 0, qdev_get_gpio_in(DEVICE(pit), 0));
        }
        pcspk_init(isa_bus, pit);
    }

    i8257_dma_init(isa_bus, 0);

    /* Super I/O */
    pc_superio_init(isa_bus, create_fdctrl, no_vmport);
}

void pc_nic_init(PCMachineClass *pcmc, ISABus *isa_bus, PCIBus *pci_bus)
{
    int i;

    rom_set_order_override(FW_CFG_ORDER_OVERRIDE_NIC);
    for (i = 0; i < nb_nics; i++) {
        NICInfo *nd = &nd_table[i];
        const char *model = nd->model ? nd->model : pcmc->default_nic_model;

        if (g_str_equal(model, "ne2k_isa")) {
            pc_init_ne2k_isa(isa_bus, nd);
        } else {
            pci_nic_init_nofail(nd, pci_bus, model, NULL);
        }
    }
    rom_reset_order_override();
}

void pc_i8259_create(ISABus *isa_bus, qemu_irq *i8259_irqs)
{
    qemu_irq *i8259;

    if (kvm_pic_in_kernel()) {
        i8259 = kvm_i8259_init(isa_bus);
    } else if (xen_enabled()) {
        i8259 = xen_interrupt_controller_init();
    } else {
        i8259 = i8259_init(isa_bus, x86_allocate_cpu_irq());
    }

    for (size_t i = 0; i < ISA_NUM_IRQS; i++) {
        i8259_irqs[i] = i8259[i];
    }

    g_free(i8259);
}

static void pc_memory_pre_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                               Error **errp)
{
    const PCMachineState *pcms = PC_MACHINE(hotplug_dev);
    const PCMachineClass *pcmc = PC_MACHINE_GET_CLASS(pcms);
    const MachineState *ms = MACHINE(hotplug_dev);
    const bool is_nvdimm = object_dynamic_cast(OBJECT(dev), TYPE_NVDIMM);
    const uint64_t legacy_align = TARGET_PAGE_SIZE;
    Error *local_err = NULL;

    /*
     * When -no-acpi is used with Q35 machine type, no ACPI is built,
     * but pcms->acpi_dev is still created. Check !acpi_enabled in
     * addition to cover this case.
     */
    if (!pcms->acpi_dev || !x86_machine_is_acpi_enabled(X86_MACHINE(pcms))) {
        error_setg(errp,
                   "memory hotplug is not enabled: missing acpi device or acpi disabled");
        return;
    }

    if (is_nvdimm && !ms->nvdimms_state->is_enabled) {
        error_setg(errp, "nvdimm is not enabled: missing 'nvdimm' in '-M'");
        return;
    }

    hotplug_handler_pre_plug(pcms->acpi_dev, dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    pc_dimm_pre_plug(PC_DIMM(dev), MACHINE(hotplug_dev),
                     pcmc->enforce_aligned_dimm ? NULL : &legacy_align, errp);
}

static void pc_memory_plug(HotplugHandler *hotplug_dev,
                           DeviceState *dev, Error **errp)
{
    Error *local_err = NULL;
    PCMachineState *pcms = PC_MACHINE(hotplug_dev);
    MachineState *ms = MACHINE(hotplug_dev);
    bool is_nvdimm = object_dynamic_cast(OBJECT(dev), TYPE_NVDIMM);

    pc_dimm_plug(PC_DIMM(dev), MACHINE(pcms), &local_err);
    if (local_err) {
        goto out;
    }

    if (is_nvdimm) {
        nvdimm_plug(ms->nvdimms_state);
    }

    hotplug_handler_plug(HOTPLUG_HANDLER(pcms->acpi_dev), dev, &error_abort);
out:
    error_propagate(errp, local_err);
}

static void pc_memory_unplug_request(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    Error *local_err = NULL;
    PCMachineState *pcms = PC_MACHINE(hotplug_dev);

    /*
     * When -no-acpi is used with Q35 machine type, no ACPI is built,
     * but pcms->acpi_dev is still created. Check !acpi_enabled in
     * addition to cover this case.
     */
    if (!pcms->acpi_dev || !x86_machine_is_acpi_enabled(X86_MACHINE(pcms))) {
        error_setg(&local_err,
                   "memory hotplug is not enabled: missing acpi device or acpi disabled");
        goto out;
    }

    if (object_dynamic_cast(OBJECT(dev), TYPE_NVDIMM)) {
        error_setg(&local_err,
                   "nvdimm device hot unplug is not supported yet.");
        goto out;
    }

    hotplug_handler_unplug_request(HOTPLUG_HANDLER(pcms->acpi_dev), dev,
                                   &local_err);
out:
    error_propagate(errp, local_err);
}

static void pc_memory_unplug(HotplugHandler *hotplug_dev,
                             DeviceState *dev, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(hotplug_dev);
    Error *local_err = NULL;

    hotplug_handler_unplug(HOTPLUG_HANDLER(pcms->acpi_dev), dev, &local_err);
    if (local_err) {
        goto out;
    }

    pc_dimm_unplug(PC_DIMM(dev), MACHINE(pcms));
    object_property_set_bool(OBJECT(dev), false, "realized", NULL);
 out:
    error_propagate(errp, local_err);
}

static int pc_apic_cmp(const void *a, const void *b)
{
   CPUArchId *apic_a = (CPUArchId *)a;
   CPUArchId *apic_b = (CPUArchId *)b;

   return apic_a->arch_id - apic_b->arch_id;
}

/* returns pointer to CPUArchId descriptor that matches CPU's apic_id
 * in ms->possible_cpus->cpus, if ms->possible_cpus->cpus has no
 * entry corresponding to CPU's apic_id returns NULL.
 */
static CPUArchId *pc_find_cpu_slot(MachineState *ms, uint32_t id, int *idx)
{
    CPUArchId apic_id, *found_cpu;

    apic_id.arch_id = id;
    found_cpu = bsearch(&apic_id, ms->possible_cpus->cpus,
        ms->possible_cpus->len, sizeof(*ms->possible_cpus->cpus),
        pc_apic_cmp);
    if (found_cpu && idx) {
        *idx = found_cpu - ms->possible_cpus->cpus;
    }
    return found_cpu;
}

static void pc_cpu_plug(HotplugHandler *hotplug_dev,
                        DeviceState *dev, Error **errp)
{
    CPUArchId *found_cpu;
    Error *local_err = NULL;
    X86CPU *cpu = X86_CPU(dev);
    PCMachineState *pcms = PC_MACHINE(hotplug_dev);
    X86MachineState *x86ms = X86_MACHINE(pcms);

    if (pcms->acpi_dev) {
        hotplug_handler_plug(HOTPLUG_HANDLER(pcms->acpi_dev), dev, &local_err);
        if (local_err) {
            goto out;
        }
    }

    /* increment the number of CPUs */
    x86ms->boot_cpus++;
    if (x86ms->rtc) {
        rtc_set_cpus_count(x86ms->rtc, x86ms->boot_cpus);
    }
    if (x86ms->fw_cfg) {
        fw_cfg_modify_i16(x86ms->fw_cfg, FW_CFG_NB_CPUS, x86ms->boot_cpus);
    }

    found_cpu = pc_find_cpu_slot(MACHINE(pcms), cpu->apic_id, NULL);
    found_cpu->cpu = OBJECT(dev);
out:
    error_propagate(errp, local_err);
}
static void pc_cpu_unplug_request_cb(HotplugHandler *hotplug_dev,
                                     DeviceState *dev, Error **errp)
{
    int idx = -1;
    Error *local_err = NULL;
    X86CPU *cpu = X86_CPU(dev);
    PCMachineState *pcms = PC_MACHINE(hotplug_dev);

    if (!pcms->acpi_dev) {
        error_setg(&local_err, "CPU hot unplug not supported without ACPI");
        goto out;
    }

    pc_find_cpu_slot(MACHINE(pcms), cpu->apic_id, &idx);
    assert(idx != -1);
    if (idx == 0) {
        error_setg(&local_err, "Boot CPU is unpluggable");
        goto out;
    }

    hotplug_handler_unplug_request(HOTPLUG_HANDLER(pcms->acpi_dev), dev,
                                   &local_err);
    if (local_err) {
        goto out;
    }

 out:
    error_propagate(errp, local_err);

}

static void pc_cpu_unplug_cb(HotplugHandler *hotplug_dev,
                             DeviceState *dev, Error **errp)
{
    CPUArchId *found_cpu;
    Error *local_err = NULL;
    X86CPU *cpu = X86_CPU(dev);
    PCMachineState *pcms = PC_MACHINE(hotplug_dev);
    X86MachineState *x86ms = X86_MACHINE(pcms);

    hotplug_handler_unplug(HOTPLUG_HANDLER(pcms->acpi_dev), dev, &local_err);
    if (local_err) {
        goto out;
    }

    found_cpu = pc_find_cpu_slot(MACHINE(pcms), cpu->apic_id, NULL);
    found_cpu->cpu = NULL;
    object_property_set_bool(OBJECT(dev), false, "realized", NULL);

    /* decrement the number of CPUs */
    x86ms->boot_cpus--;
    /* Update the number of CPUs in CMOS */
    rtc_set_cpus_count(x86ms->rtc, x86ms->boot_cpus);
    fw_cfg_modify_i16(x86ms->fw_cfg, FW_CFG_NB_CPUS, x86ms->boot_cpus);
 out:
    error_propagate(errp, local_err);
}

static void pc_cpu_pre_plug(HotplugHandler *hotplug_dev,
                            DeviceState *dev, Error **errp)
{
    int idx;
    CPUState *cs;
    CPUArchId *cpu_slot;
    X86CPUTopoIDs topo_ids;
    X86CPU *cpu = X86_CPU(dev);
    CPUX86State *env = &cpu->env;
    MachineState *ms = MACHINE(hotplug_dev);
    PCMachineState *pcms = PC_MACHINE(hotplug_dev);
    X86MachineState *x86ms = X86_MACHINE(pcms);
    unsigned int smp_cores = ms->smp.cores;
    unsigned int smp_threads = ms->smp.threads;
    X86CPUTopoInfo topo_info;

    if(!object_dynamic_cast(OBJECT(cpu), ms->cpu_type)) {
        error_setg(errp, "Invalid CPU type, expected cpu type: '%s'",
                   ms->cpu_type);
        return;
    }

    init_topo_info(&topo_info, x86ms);

    env->nr_dies = x86ms->smp_dies;
    env->nr_nodes = topo_info.nodes_per_pkg;
    env->pkg_offset = x86ms->apicid_pkg_offset(&topo_info);

    /*
     * If APIC ID is not set,
     * set it based on socket/die/core/thread properties.
     */
    if (cpu->apic_id == UNASSIGNED_APIC_ID) {
        int max_socket = (ms->smp.max_cpus - 1) /
                                smp_threads / smp_cores / x86ms->smp_dies;

        /*
         * die-id was optional in QEMU 4.0 and older, so keep it optional
         * if there's only one die per socket.
         */
        if (cpu->die_id < 0 && x86ms->smp_dies == 1) {
            cpu->die_id = 0;
        }

        if (cpu->socket_id < 0) {
            error_setg(errp, "CPU socket-id is not set");
            return;
        } else if (cpu->socket_id > max_socket) {
            error_setg(errp, "Invalid CPU socket-id: %u must be in range 0:%u",
                       cpu->socket_id, max_socket);
            return;
        }
        if (cpu->die_id < 0) {
            error_setg(errp, "CPU die-id is not set");
            return;
        } else if (cpu->die_id > x86ms->smp_dies - 1) {
            error_setg(errp, "Invalid CPU die-id: %u must be in range 0:%u",
                       cpu->die_id, x86ms->smp_dies - 1);
            return;
        }
        if (cpu->core_id < 0) {
            error_setg(errp, "CPU core-id is not set");
            return;
        } else if (cpu->core_id > (smp_cores - 1)) {
            error_setg(errp, "Invalid CPU core-id: %u must be in range 0:%u",
                       cpu->core_id, smp_cores - 1);
            return;
        }
        if (cpu->thread_id < 0) {
            error_setg(errp, "CPU thread-id is not set");
            return;
        } else if (cpu->thread_id > (smp_threads - 1)) {
            error_setg(errp, "Invalid CPU thread-id: %u must be in range 0:%u",
                       cpu->thread_id, smp_threads - 1);
            return;
        }

        topo_ids.pkg_id = cpu->socket_id;
        topo_ids.die_id = cpu->die_id;
        topo_ids.core_id = cpu->core_id;
        topo_ids.smt_id = cpu->thread_id;
        cpu->apic_id = x86ms->apicid_from_topo_ids(&topo_info, &topo_ids);
    }

    cpu_slot = pc_find_cpu_slot(MACHINE(pcms), cpu->apic_id, &idx);
    if (!cpu_slot) {
        MachineState *ms = MACHINE(pcms);

        x86ms->topo_ids_from_apicid(cpu->apic_id, &topo_info, &topo_ids);
        error_setg(errp,
            "Invalid CPU [socket: %u, die: %u, core: %u, thread: %u] with"
            " APIC ID %" PRIu32 ", valid index range 0:%d",
            topo_ids.pkg_id, topo_ids.die_id, topo_ids.core_id, topo_ids.smt_id,
            cpu->apic_id, ms->possible_cpus->len - 1);
        return;
    }

    if (cpu_slot->cpu) {
        error_setg(errp, "CPU[%d] with APIC ID %" PRIu32 " exists",
                   idx, cpu->apic_id);
        return;
    }

    /* if 'address' properties socket-id/core-id/thread-id are not set, set them
     * so that machine_query_hotpluggable_cpus would show correct values
     */
    /* TODO: move socket_id/core_id/thread_id checks into x86_cpu_realizefn()
     * once -smp refactoring is complete and there will be CPU private
     * CPUState::nr_cores and CPUState::nr_threads fields instead of globals */
    x86ms->topo_ids_from_apicid(cpu->apic_id, &topo_info, &topo_ids);
    if (cpu->socket_id != -1 && cpu->socket_id != topo_ids.pkg_id) {
        error_setg(errp, "property socket-id: %u doesn't match set apic-id:"
            " 0x%x (socket-id: %u)", cpu->socket_id, cpu->apic_id,
            topo_ids.pkg_id);
        return;
    }
    cpu->socket_id = topo_ids.pkg_id;

    if (cpu->die_id != -1 && cpu->die_id != topo_ids.die_id) {
        error_setg(errp, "property die-id: %u doesn't match set apic-id:"
            " 0x%x (die-id: %u)", cpu->die_id, cpu->apic_id, topo_ids.die_id);
        return;
    }
    cpu->die_id = topo_ids.die_id;

    if (cpu->core_id != -1 && cpu->core_id != topo_ids.core_id) {
        error_setg(errp, "property core-id: %u doesn't match set apic-id:"
            " 0x%x (core-id: %u)", cpu->core_id, cpu->apic_id,
            topo_ids.core_id);
        return;
    }
    cpu->core_id = topo_ids.core_id;

    if (cpu->thread_id != -1 && cpu->thread_id != topo_ids.smt_id) {
        error_setg(errp, "property thread-id: %u doesn't match set apic-id:"
            " 0x%x (thread-id: %u)", cpu->thread_id, cpu->apic_id,
            topo_ids.smt_id);
        return;
    }
    cpu->thread_id = topo_ids.smt_id;

    if (hyperv_feat_enabled(cpu, HYPERV_FEAT_VPINDEX) &&
        !kvm_hv_vpindex_settable()) {
        error_setg(errp, "kernel doesn't allow setting HyperV VP_INDEX");
        return;
    }

    cs = CPU(cpu);
    cs->cpu_index = idx;

    numa_cpu_pre_plug(cpu_slot, dev, errp);
}

static void pc_virtio_pmem_pci_pre_plug(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    HotplugHandler *hotplug_dev2 = qdev_get_bus_hotplug_handler(dev);
    Error *local_err = NULL;

    if (!hotplug_dev2) {
        /*
         * Without a bus hotplug handler, we cannot control the plug/unplug
         * order. This should never be the case on x86, however better add
         * a safety net.
         */
        error_setg(errp, "virtio-pmem-pci not supported on this bus.");
        return;
    }
    /*
     * First, see if we can plug this memory device at all. If that
     * succeeds, branch of to the actual hotplug handler.
     */
    memory_device_pre_plug(MEMORY_DEVICE(dev), MACHINE(hotplug_dev), NULL,
                           &local_err);
    if (!local_err) {
        hotplug_handler_pre_plug(hotplug_dev2, dev, &local_err);
    }
    error_propagate(errp, local_err);
}

static void pc_virtio_pmem_pci_plug(HotplugHandler *hotplug_dev,
                                    DeviceState *dev, Error **errp)
{
    HotplugHandler *hotplug_dev2 = qdev_get_bus_hotplug_handler(dev);
    Error *local_err = NULL;

    /*
     * Plug the memory device first and then branch off to the actual
     * hotplug handler. If that one fails, we can easily undo the memory
     * device bits.
     */
    memory_device_plug(MEMORY_DEVICE(dev), MACHINE(hotplug_dev));
    hotplug_handler_plug(hotplug_dev2, dev, &local_err);
    if (local_err) {
        memory_device_unplug(MEMORY_DEVICE(dev), MACHINE(hotplug_dev));
    }
    error_propagate(errp, local_err);
}

static void pc_virtio_pmem_pci_unplug_request(HotplugHandler *hotplug_dev,
                                              DeviceState *dev, Error **errp)
{
    /* We don't support virtio pmem hot unplug */
    error_setg(errp, "virtio pmem device unplug not supported.");
}

static void pc_virtio_pmem_pci_unplug(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp)
{
    /* We don't support virtio pmem hot unplug */
}

static void pc_machine_device_pre_plug_cb(HotplugHandler *hotplug_dev,
                                          DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        pc_memory_pre_plug(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        pc_cpu_pre_plug(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_PMEM_PCI)) {
        pc_virtio_pmem_pci_pre_plug(hotplug_dev, dev, errp);
    }
}

static void pc_machine_device_plug_cb(HotplugHandler *hotplug_dev,
                                      DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        pc_memory_plug(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        pc_cpu_plug(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_PMEM_PCI)) {
        pc_virtio_pmem_pci_plug(hotplug_dev, dev, errp);
    }
}

static void pc_machine_device_unplug_request_cb(HotplugHandler *hotplug_dev,
                                                DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        pc_memory_unplug_request(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        pc_cpu_unplug_request_cb(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_PMEM_PCI)) {
        pc_virtio_pmem_pci_unplug_request(hotplug_dev, dev, errp);
    } else {
        error_setg(errp, "acpi: device unplug request for not supported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static void pc_machine_device_unplug_cb(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        pc_memory_unplug(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        pc_cpu_unplug_cb(hotplug_dev, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_PMEM_PCI)) {
        pc_virtio_pmem_pci_unplug(hotplug_dev, dev, errp);
    } else {
        error_setg(errp, "acpi: device unplug for not supported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static HotplugHandler *pc_get_hotplug_handler(MachineState *machine,
                                             DeviceState *dev)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM) ||
        object_dynamic_cast(OBJECT(dev), TYPE_CPU) ||
        object_dynamic_cast(OBJECT(dev), TYPE_VIRTIO_PMEM_PCI)) {
        return HOTPLUG_HANDLER(machine);
    }

    return NULL;
}

static void
pc_machine_get_device_memory_region_size(Object *obj, Visitor *v,
                                         const char *name, void *opaque,
                                         Error **errp)
{
    MachineState *ms = MACHINE(obj);
    int64_t value = 0;

    if (ms->device_memory) {
        value = memory_region_size(&ms->device_memory->mr);
    }

    visit_type_int(v, name, &value, errp);
}

static void pc_machine_get_vmport(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);
    OnOffAuto vmport = pcms->vmport;

    visit_type_OnOffAuto(v, name, &vmport, errp);
}

static void pc_machine_set_vmport(Object *obj, Visitor *v, const char *name,
                                  void *opaque, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    visit_type_OnOffAuto(v, name, &pcms->vmport, errp);
}

static bool pc_machine_get_smbus(Object *obj, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    return pcms->smbus_enabled;
}

static void pc_machine_set_smbus(Object *obj, bool value, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    pcms->smbus_enabled = value;
}

static bool pc_machine_get_sata(Object *obj, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    return pcms->sata_enabled;
}

static void pc_machine_set_sata(Object *obj, bool value, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    pcms->sata_enabled = value;
}

static bool pc_machine_get_pit(Object *obj, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    return pcms->pit_enabled;
}

static void pc_machine_set_pit(Object *obj, bool value, Error **errp)
{
    PCMachineState *pcms = PC_MACHINE(obj);

    pcms->pit_enabled = value;
}

static void pc_machine_initfn(Object *obj)
{
    PCMachineState *pcms = PC_MACHINE(obj);

#ifdef CONFIG_VMPORT
    pcms->vmport = ON_OFF_AUTO_AUTO;
#else
    pcms->vmport = ON_OFF_AUTO_OFF;
#endif /* CONFIG_VMPORT */
    /* acpi build is enabled by default if machine supports it */
    pcms->acpi_build_enabled = PC_MACHINE_GET_CLASS(pcms)->has_acpi_build;
    pcms->smbus_enabled = true;
    pcms->sata_enabled = true;
    pcms->pit_enabled = true;

    pc_system_flash_create(pcms);
}

static void pc_machine_reset(MachineState *machine)
{
    CPUState *cs;
    X86CPU *cpu;

    qemu_devices_reset();

    /* Reset APIC after devices have been reset to cancel
     * any changes that qemu_devices_reset() might have done.
     */
    CPU_FOREACH(cs) {
        cpu = X86_CPU(cs);

        if (cpu->apic_state) {
            device_legacy_reset(cpu->apic_state);
        }
    }
}

static void pc_machine_wakeup(MachineState *machine)
{
    cpu_synchronize_all_states();
    pc_machine_reset(machine);
    cpu_synchronize_all_post_reset();
}

static bool pc_hotplug_allowed(MachineState *ms, DeviceState *dev, Error **errp)
{
    X86IOMMUState *iommu = x86_iommu_get_default();
    IntelIOMMUState *intel_iommu;

    if (iommu &&
        object_dynamic_cast((Object *)iommu, TYPE_INTEL_IOMMU_DEVICE) &&
        object_dynamic_cast((Object *)dev, "vfio-pci")) {
        intel_iommu = INTEL_IOMMU_DEVICE(iommu);
        if (!intel_iommu->caching_mode) {
            error_setg(errp, "Device assignment is not allowed without "
                       "enabling caching-mode=on for Intel IOMMU.");
            return false;
        }
    }

    return true;
}

static void pc_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    PCMachineClass *pcmc = PC_MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    pcmc->pci_enabled = true;
    pcmc->has_acpi_build = true;
    pcmc->rsdp_in_ram = true;
    pcmc->smbios_defaults = true;
    pcmc->smbios_uuid_encoded = true;
    pcmc->gigabyte_align = true;
    pcmc->has_reserved_memory = true;
    pcmc->kvmclock_enabled = true;
    pcmc->enforce_aligned_dimm = true;
    /* BIOS ACPI tables: 128K. Other BIOS datastructures: less than 4K reported
     * to be used at the moment, 32K should be enough for a while.  */
    pcmc->acpi_data_size = 0x20000 + 0x8000;
    pcmc->linuxboot_dma_enabled = true;
    pcmc->pvh_enabled = true;
    assert(!mc->get_hotplug_handler);
    mc->get_hotplug_handler = pc_get_hotplug_handler;
    mc->hotplug_allowed = pc_hotplug_allowed;
    mc->cpu_index_to_instance_props = x86_cpu_index_to_props;
    mc->get_default_cpu_node_id = x86_get_default_cpu_node_id;
    mc->possible_cpu_arch_ids = x86_possible_cpu_arch_ids;
    mc->auto_enable_numa_with_memhp = true;
    mc->has_hotpluggable_cpus = true;
    mc->default_boot_order = "cad";
    mc->hot_add_cpu = pc_hot_add_cpu;
    mc->smp_parse = pc_smp_parse;
    mc->block_default_type = IF_IDE;
    mc->max_cpus = 255;
    mc->reset = pc_machine_reset;
    mc->wakeup = pc_machine_wakeup;
    hc->pre_plug = pc_machine_device_pre_plug_cb;
    hc->plug = pc_machine_device_plug_cb;
    hc->unplug_request = pc_machine_device_unplug_request_cb;
    hc->unplug = pc_machine_device_unplug_cb;
    mc->default_cpu_type = TARGET_DEFAULT_CPU_TYPE;
    mc->nvdimm_supported = true;
    mc->numa_mem_supported = true;
    mc->default_ram_id = "pc.ram";

    object_class_property_add(oc, PC_MACHINE_DEVMEM_REGION_SIZE, "int",
        pc_machine_get_device_memory_region_size, NULL,
        NULL, NULL, &error_abort);

    object_class_property_add(oc, PC_MACHINE_VMPORT, "OnOffAuto",
        pc_machine_get_vmport, pc_machine_set_vmport,
        NULL, NULL, &error_abort);
    object_class_property_set_description(oc, PC_MACHINE_VMPORT,
        "Enable vmport (pc & q35)", &error_abort);

    object_class_property_add_bool(oc, PC_MACHINE_SMBUS,
        pc_machine_get_smbus, pc_machine_set_smbus, &error_abort);

    object_class_property_add_bool(oc, PC_MACHINE_SATA,
        pc_machine_get_sata, pc_machine_set_sata, &error_abort);

    object_class_property_add_bool(oc, PC_MACHINE_PIT,
        pc_machine_get_pit, pc_machine_set_pit, &error_abort);
}

static const TypeInfo pc_machine_info = {
    .name = TYPE_PC_MACHINE,
    .parent = TYPE_X86_MACHINE,
    .abstract = true,
    .instance_size = sizeof(PCMachineState),
    .instance_init = pc_machine_initfn,
    .class_size = sizeof(PCMachineClass),
    .class_init = pc_machine_class_init,
    .interfaces = (InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },
         { }
    },
};

static void pc_machine_register_types(void)
{
    type_register_static(&pc_machine_info);
}

type_init(pc_machine_register_types)
