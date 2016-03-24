/*
 * CRIS image loading.
 *
 * Copyright (c) 2010 Edgar E. Iglesias, Axis Communications AB.
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
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/loader.h"
#include "elf.h"
#include "boot.h"
#include "qemu/cutils.h"

static void main_cpu_reset(void *opaque)
{
    CRISCPU *cpu = opaque;
    CPUCRISState *env = &cpu->env;
    struct cris_load_info *li;

    li = env->load_info;

    cpu_reset(CPU(cpu));

    if (!li) {
        /* nothing more to do.  */
        return;
    }

    env->pc = li->entry;

    if (li->image_filename) {
        env->regs[8] = 0x56902387; /* RAM boot magic.  */
        env->regs[9] = 0x40004000 + li->image_size;
    }

    if (li->cmdline) {
        /* Let the kernel know we are modifying the cmdline.  */
        env->regs[10] = 0x87109563;
        env->regs[11] = 0x40000000;
    }
}

static uint64_t translate_kernel_address(void *opaque, uint64_t addr)
{
    return addr - 0x80000000LL;
}

void cris_load_image(CRISCPU *cpu, struct cris_load_info *li)
{
    CPUCRISState *env = &cpu->env;
    uint64_t entry, high;
    int kcmdline_len;
    int image_size;

    env->load_info = li;
    /* Boots a kernel elf binary, os/linux-2.6/vmlinux from the axis 
       devboard SDK.  */
    image_size = load_elf(li->image_filename, translate_kernel_address, NULL,
                          &entry, NULL, &high, 0, EM_CRIS, 0, 0);
    li->entry = entry;
    if (image_size < 0) {
        /* Takes a kimage from the axis devboard SDK.  */
        image_size = load_image_targphys(li->image_filename, 0x40004000,
                                         ram_size);
        li->entry = 0x40004000;
    }

    if (image_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n",
                li->image_filename);
        exit(1);
    }

    if (li->cmdline && (kcmdline_len = strlen(li->cmdline))) {
        if (kcmdline_len > 256) {
            fprintf(stderr, "Too long CRIS kernel cmdline (max 256)\n");
            exit(1);
        }
        pstrcpy_targphys("cmdline", 0x40000000, 256, li->cmdline);
    }
    qemu_register_reset(main_cpu_reset, cpu);
}
