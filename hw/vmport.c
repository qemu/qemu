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
#include "hw.h"
#include "isa.h"
#include "pc.h"
#include "kvm.h"
#include "qdev.h"

//#define VMPORT_DEBUG

#define VMPORT_CMD_GETVERSION 0x0a
#define VMPORT_CMD_GETRAMSIZE 0x14

#define VMPORT_ENTRIES 0x2c
#define VMPORT_MAGIC   0x564D5868

typedef struct _VMPortState
{
    ISADevice dev;
    MemoryRegion io;
    IOPortReadFunc *func[VMPORT_ENTRIES];
    void *opaque[VMPORT_ENTRIES];
} VMPortState;

static VMPortState *port_state;

void vmport_register(unsigned char command, IOPortReadFunc *func, void *opaque)
{
    if (command >= VMPORT_ENTRIES)
        return;

    port_state->func[command] = func;
    port_state->opaque[command] = opaque;
}

static uint32_t vmport_ioport_read(void *opaque, uint32_t addr)
{
    VMPortState *s = opaque;
    CPUX86State *env = cpu_single_env;
    unsigned char command;
    uint32_t eax;

    cpu_synchronize_state(env);

    eax = env->regs[R_EAX];
    if (eax != VMPORT_MAGIC)
        return eax;

    command = env->regs[R_ECX];
    if (command >= VMPORT_ENTRIES)
        return eax;
    if (!s->func[command])
    {
#ifdef VMPORT_DEBUG
        fprintf(stderr, "vmport: unknown command %x\n", command);
#endif
        return eax;
    }

    return s->func[command](s->opaque[command], addr);
}

static void vmport_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    CPUX86State *env = cpu_single_env;

    env->regs[R_EAX] = vmport_ioport_read(opaque, addr);
}

static uint32_t vmport_cmd_get_version(void *opaque, uint32_t addr)
{
    CPUX86State *env = cpu_single_env;
    env->regs[R_EBX] = VMPORT_MAGIC;
    return 6;
}

static uint32_t vmport_cmd_ram_size(void *opaque, uint32_t addr)
{
    CPUX86State *env = cpu_single_env;
    env->regs[R_EBX] = 0x1177;
    return ram_size;
}

/* vmmouse helpers */
void vmmouse_get_data(uint32_t *data)
{
    CPUX86State *env = cpu_single_env;

    data[0] = env->regs[R_EAX]; data[1] = env->regs[R_EBX];
    data[2] = env->regs[R_ECX]; data[3] = env->regs[R_EDX];
    data[4] = env->regs[R_ESI]; data[5] = env->regs[R_EDI];
}

void vmmouse_set_data(const uint32_t *data)
{
    CPUX86State *env = cpu_single_env;

    env->regs[R_EAX] = data[0]; env->regs[R_EBX] = data[1];
    env->regs[R_ECX] = data[2]; env->regs[R_EDX] = data[3];
    env->regs[R_ESI] = data[4]; env->regs[R_EDI] = data[5];
}

static const MemoryRegionPortio vmport_portio[] = {
    {0, 1, 4, .read = vmport_ioport_read, .write = vmport_ioport_write },
    PORTIO_END_OF_LIST(),
};

static const MemoryRegionOps vmport_ops = {
    .old_portio = vmport_portio
};

static int vmport_initfn(ISADevice *dev)
{
    VMPortState *s = DO_UPCAST(VMPortState, dev, dev);

    memory_region_init_io(&s->io, &vmport_ops, s, "vmport", 1);
    isa_register_ioport(dev, &s->io, 0x5658);

    port_state = s;
    /* Register some generic port commands */
    vmport_register(VMPORT_CMD_GETVERSION, vmport_cmd_get_version, NULL);
    vmport_register(VMPORT_CMD_GETRAMSIZE, vmport_cmd_ram_size, NULL);
    return 0;
}

static void vmport_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ISADeviceClass *ic = ISA_DEVICE_CLASS(klass);
    ic->init = vmport_initfn;
    dc->no_user = 1;
}

static TypeInfo vmport_info = {
    .name          = "vmport",
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(VMPortState),
    .class_init    = vmport_class_initfn,
};

static void vmport_register_types(void)
{
    type_register_static(&vmport_info);
}

type_init(vmport_register_types)
