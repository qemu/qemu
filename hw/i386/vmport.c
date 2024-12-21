/*
 * QEMU VMPort emulation
 *
 * Copyright (C) 2007 Hervé Poussineau
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

/*
 * Guest code that interacts with this virtual device can be found
 * in VMware open-vm-tools open-source project:
 * https://github.com/vmware/open-vm-tools
 */

#include "qemu/osdep.h"
#include "hw/isa/isa.h"
#include "hw/i386/vmport.h"
#include "hw/qdev-properties.h"
#include "hw/boards.h"
#include "system/system.h"
#include "system/hw_accel.h"
#include "system/qtest.h"
#include "qemu/log.h"
#include "trace.h"
#include "qom/object.h"

#define VMPORT_MAGIC   0x564D5868

/* Compatibility flags for migration */
#define VMPORT_COMPAT_READ_SET_EAX_BIT              0
#define VMPORT_COMPAT_SIGNAL_UNSUPPORTED_CMD_BIT    1
#define VMPORT_COMPAT_REPORT_VMX_TYPE_BIT           2
#define VMPORT_COMPAT_CMDS_V2_BIT                   3
#define VMPORT_COMPAT_READ_SET_EAX              \
    (1 << VMPORT_COMPAT_READ_SET_EAX_BIT)
#define VMPORT_COMPAT_SIGNAL_UNSUPPORTED_CMD    \
    (1 << VMPORT_COMPAT_SIGNAL_UNSUPPORTED_CMD_BIT)
#define VMPORT_COMPAT_REPORT_VMX_TYPE           \
    (1 << VMPORT_COMPAT_REPORT_VMX_TYPE_BIT)
#define VMPORT_COMPAT_CMDS_V2                   \
    (1 << VMPORT_COMPAT_CMDS_V2_BIT)

/* vCPU features reported by CMD_GET_VCPU_INFO */
#define VCPU_INFO_SLC64_BIT             0
#define VCPU_INFO_SYNC_VTSCS_BIT        1
#define VCPU_INFO_HV_REPLAY_OK_BIT      2
#define VCPU_INFO_LEGACY_X2APIC_BIT     3
#define VCPU_INFO_RESERVED_BIT          31

OBJECT_DECLARE_SIMPLE_TYPE(VMPortState, VMPORT)

struct VMPortState {
    ISADevice parent_obj;

    MemoryRegion io;
    VMPortReadFunc *func[VMPORT_ENTRIES];
    void *opaque[VMPORT_ENTRIES];

    uint32_t vmware_vmx_version;
    uint8_t vmware_vmx_type;

    uint32_t compat_flags;
};

static VMPortState *port_state;

void vmport_register(VMPortCommand command, VMPortReadFunc *func, void *opaque)
{
    assert(command < VMPORT_ENTRIES);
    assert(port_state);

    trace_vmport_register(command, func, opaque);
    port_state->func[command] = func;
    port_state->opaque[command] = opaque;
}

static uint64_t vmport_ioport_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    VMPortState *s = opaque;
    CPUState *cs = current_cpu;
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env;
    unsigned char command;
    uint32_t eax;

    if (qtest_enabled()) {
        return -1;
    }
    env = &cpu->env;
    cpu_synchronize_state(cs);

    eax = env->regs[R_EAX];
    if (eax != VMPORT_MAGIC) {
        goto err;
    }

    command = env->regs[R_ECX];
    trace_vmport_command(command);
    if (command >= VMPORT_ENTRIES || !s->func[command]) {
        qemu_log_mask(LOG_UNIMP, "vmport: unknown command %x\n", command);
        goto err;
    }

    eax = s->func[command](s->opaque[command], addr);
    goto out;

err:
    if (s->compat_flags & VMPORT_COMPAT_SIGNAL_UNSUPPORTED_CMD) {
        eax = UINT32_MAX;
    }

out:
    /*
     * The call above to cpu_synchronize_state() gets vCPU registers values
     * to QEMU but also cause QEMU to write QEMU vCPU registers values to
     * vCPU implementation (e.g. Accelerator such as KVM) just before
     * resuming guest.
     *
     * Therefore, in order to make IOPort return value propagate to
     * guest EAX, we need to explicitly update QEMU EAX register value.
     */
    if (s->compat_flags & VMPORT_COMPAT_READ_SET_EAX) {
        cpu->env.regs[R_EAX] = eax;
    }

    return eax;
}

static void vmport_ioport_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    X86CPU *cpu = X86_CPU(current_cpu);

    if (qtest_enabled()) {
        return;
    }
    cpu->env.regs[R_EAX] = vmport_ioport_read(opaque, addr, 4);
}

static uint32_t vmport_cmd_get_version(void *opaque, uint32_t addr)
{
    X86CPU *cpu = X86_CPU(current_cpu);

    if (qtest_enabled()) {
        return -1;
    }
    cpu->env.regs[R_EBX] = VMPORT_MAGIC;
    if (port_state->compat_flags & VMPORT_COMPAT_REPORT_VMX_TYPE) {
        cpu->env.regs[R_ECX] = port_state->vmware_vmx_type;
    }
    return port_state->vmware_vmx_version;
}

static uint32_t vmport_cmd_get_bios_uuid(void *opaque, uint32_t addr)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    uint32_t *uuid_parts = (uint32_t *)(qemu_uuid.data);

    cpu->env.regs[R_EAX] = le32_to_cpu(uuid_parts[0]);
    cpu->env.regs[R_EBX] = le32_to_cpu(uuid_parts[1]);
    cpu->env.regs[R_ECX] = le32_to_cpu(uuid_parts[2]);
    cpu->env.regs[R_EDX] = le32_to_cpu(uuid_parts[3]);
    return cpu->env.regs[R_EAX];
}

static uint32_t vmport_cmd_ram_size(void *opaque, uint32_t addr)
{
    X86CPU *cpu = X86_CPU(current_cpu);

    if (qtest_enabled()) {
        return -1;
    }
    cpu->env.regs[R_EBX] = 0x1177;
    return current_machine->ram_size;
}

static uint32_t vmport_cmd_get_hz(void *opaque, uint32_t addr)
{
    X86CPU *cpu = X86_CPU(current_cpu);

    if (cpu->env.tsc_khz && cpu->env.apic_bus_freq) {
        uint64_t tsc_freq = (uint64_t)cpu->env.tsc_khz * 1000;

        cpu->env.regs[R_ECX] = cpu->env.apic_bus_freq;
        cpu->env.regs[R_EBX] = (uint32_t)(tsc_freq >> 32);
        cpu->env.regs[R_EAX] = (uint32_t)tsc_freq;
    } else {
        /* Signal cmd as not supported */
        cpu->env.regs[R_EBX] = UINT32_MAX;
    }

    return cpu->env.regs[R_EAX];
}

static uint32_t vmport_cmd_get_vcpu_info(void *opaque, uint32_t addr)
{
    X86CPU *cpu = X86_CPU(current_cpu);
    uint32_t ret = 0;

    if (cpu->env.features[FEAT_1_ECX] & CPUID_EXT_X2APIC) {
        ret |= 1 << VCPU_INFO_LEGACY_X2APIC_BIT;
    }

    return ret;
}

static const MemoryRegionOps vmport_ops = {
    .read = vmport_ioport_read,
    .write = vmport_ioport_write,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void vmport_realizefn(DeviceState *dev, Error **errp)
{
    ISADevice *isadev = ISA_DEVICE(dev);
    VMPortState *s = VMPORT(dev);

    memory_region_init_io(&s->io, OBJECT(s), &vmport_ops, s, "vmport", 1);
    isa_register_ioport(isadev, &s->io, 0x5658);

    port_state = s;

    /* Register some generic port commands */
    vmport_register(VMPORT_CMD_GETVERSION, vmport_cmd_get_version, NULL);
    vmport_register(VMPORT_CMD_GETRAMSIZE, vmport_cmd_ram_size, NULL);
    if (s->compat_flags & VMPORT_COMPAT_CMDS_V2) {
        vmport_register(VMPORT_CMD_GETBIOSUUID, vmport_cmd_get_bios_uuid, NULL);
        vmport_register(VMPORT_CMD_GETHZ, vmport_cmd_get_hz, NULL);
        vmport_register(VMPORT_CMD_GET_VCPU_INFO, vmport_cmd_get_vcpu_info,
                        NULL);
    }
}

static const Property vmport_properties[] = {
    /* Used to enforce compatibility for migration */
    DEFINE_PROP_BIT("x-read-set-eax", VMPortState, compat_flags,
                    VMPORT_COMPAT_READ_SET_EAX_BIT, true),
    DEFINE_PROP_BIT("x-signal-unsupported-cmd", VMPortState, compat_flags,
                    VMPORT_COMPAT_SIGNAL_UNSUPPORTED_CMD_BIT, true),
    DEFINE_PROP_BIT("x-report-vmx-type", VMPortState, compat_flags,
                    VMPORT_COMPAT_REPORT_VMX_TYPE_BIT, true),
    DEFINE_PROP_BIT("x-cmds-v2", VMPortState, compat_flags,
                    VMPORT_COMPAT_CMDS_V2_BIT, true),

    /* Default value taken from open-vm-tools code VERSION_MAGIC definition */
    DEFINE_PROP_UINT32("vmware-vmx-version", VMPortState,
                       vmware_vmx_version, 6),
    /*
     * Value determines which VMware product type host report itself to guest.
     *
     * Most guests are fine with exposing host as VMware ESX server.
     * Some legacy/proprietary guests hard-code a given type.
     *
     * For a complete list of values, refer to enum VMXType at open-vm-tools
     * project (Defined at lib/include/vm_vmx_type.h).
     *
     * Reasonable options:
     * 0 - Unset
     * 1 - VMware Express (deprecated)
     * 2 - VMware ESX Server
     * 3 - VMware Server (Deprecated)
     * 4 - VMware Workstation
     * 5 - ACE 1.x (Deprecated)
     */
    DEFINE_PROP_UINT8("vmware-vmx-type", VMPortState, vmware_vmx_type, 2),
};

static void vmport_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = vmport_realizefn;
    /* Reason: realize sets global port_state */
    dc->user_creatable = false;
    device_class_set_props(dc, vmport_properties);
}

static const TypeInfo vmport_info = {
    .name          = TYPE_VMPORT,
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(VMPortState),
    .class_init    = vmport_class_initfn,
};

static void vmport_register_types(void)
{
    type_register_static(&vmport_info);
}

type_init(vmport_register_types)
