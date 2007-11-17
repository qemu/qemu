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
#include "sysemu.h"

#define VMPORT_CMD_GETVERSION 0x0a
#define VMPORT_CMD_GETRAMSIZE 0x14

#define VMPORT_ENTRIES 0x2c
#define VMPORT_MAGIC   0x564D5868

typedef struct _VMPortState
{
    CPUState *env;
    IOPortReadFunc *func[VMPORT_ENTRIES];
    void *opaque[VMPORT_ENTRIES];
} VMPortState;

static VMPortState port_state;

void vmport_register(unsigned char command, IOPortReadFunc *func, void *opaque)
{
    if (command >= VMPORT_ENTRIES)
        return;

    port_state.func[command] = func;
    port_state.opaque[command] = opaque;
}

static uint32_t vmport_ioport_read(void *opaque, uint32_t addr)
{
    VMPortState *s = opaque;
    unsigned char command;
    uint32_t eax;

    eax = s->env->regs[R_EAX];
    if (eax != VMPORT_MAGIC)
        return eax;

    command = s->env->regs[R_ECX];
    if (command >= VMPORT_ENTRIES)
        return eax;
    if (!s->func[command])
    {
        printf("vmport: unknown command %x\n", command);
        return eax;
    }

    return s->func[command](s->opaque[command], addr);
}

static uint32_t vmport_cmd_get_version(void *opaque, uint32_t addr)
{
    CPUState *env = opaque;
    env->regs[R_EBX] = VMPORT_MAGIC;
    return 6;
}

static uint32_t vmport_cmd_ram_size(void *opaque, uint32_t addr)
{
    CPUState *env = opaque;
    env->regs[R_EBX] = 0x1177;
    return ram_size;
}

void vmport_init(CPUState *env)
{
    port_state.env = env;

    register_ioport_read(0x5658, 1, 4, vmport_ioport_read, &port_state);

    /* Register some generic port commands */
    vmport_register(VMPORT_CMD_GETVERSION, vmport_cmd_get_version, env);
    vmport_register(VMPORT_CMD_GETRAMSIZE, vmport_cmd_ram_size, env);
}
