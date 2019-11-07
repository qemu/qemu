/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "sysemu/sysemu.h"
#include "sysemu/arch_init.h"
#include "hw/pci/pci.h"
#include "hw/audio/soundhw.h"
#include "qapi/qapi-commands-misc.h"
#include "qapi/error.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "hw/acpi/acpi.h"
#include "qemu/help_option.h"

#ifdef TARGET_SPARC
int graphic_width = 1024;
int graphic_height = 768;
int graphic_depth = 8;
#elif defined(TARGET_M68K)
int graphic_width = 800;
int graphic_height = 600;
int graphic_depth = 8;
#else
int graphic_width = 800;
int graphic_height = 600;
int graphic_depth = 32;
#endif


#if defined(TARGET_ALPHA)
#define QEMU_ARCH QEMU_ARCH_ALPHA
#elif defined(TARGET_ARM)
#define QEMU_ARCH QEMU_ARCH_ARM
#elif defined(TARGET_CRIS)
#define QEMU_ARCH QEMU_ARCH_CRIS
#elif defined(TARGET_HPPA)
#define QEMU_ARCH QEMU_ARCH_HPPA
#elif defined(TARGET_I386)
#define QEMU_ARCH QEMU_ARCH_I386
#elif defined(TARGET_LM32)
#define QEMU_ARCH QEMU_ARCH_LM32
#elif defined(TARGET_M68K)
#define QEMU_ARCH QEMU_ARCH_M68K
#elif defined(TARGET_MICROBLAZE)
#define QEMU_ARCH QEMU_ARCH_MICROBLAZE
#elif defined(TARGET_MIPS)
#define QEMU_ARCH QEMU_ARCH_MIPS
#elif defined(TARGET_MOXIE)
#define QEMU_ARCH QEMU_ARCH_MOXIE
#elif defined(TARGET_NIOS2)
#define QEMU_ARCH QEMU_ARCH_NIOS2
#elif defined(TARGET_OPENRISC)
#define QEMU_ARCH QEMU_ARCH_OPENRISC
#elif defined(TARGET_PPC)
#define QEMU_ARCH QEMU_ARCH_PPC
#elif defined(TARGET_RISCV)
#define QEMU_ARCH QEMU_ARCH_RISCV
#elif defined(TARGET_S390X)
#define QEMU_ARCH QEMU_ARCH_S390X
#elif defined(TARGET_SH4)
#define QEMU_ARCH QEMU_ARCH_SH4
#elif defined(TARGET_SPARC)
#define QEMU_ARCH QEMU_ARCH_SPARC
#elif defined(TARGET_TRICORE)
#define QEMU_ARCH QEMU_ARCH_TRICORE
#elif defined(TARGET_UNICORE32)
#define QEMU_ARCH QEMU_ARCH_UNICORE32
#elif defined(TARGET_XTENSA)
#define QEMU_ARCH QEMU_ARCH_XTENSA
#endif

const uint32_t arch_type = QEMU_ARCH;

int kvm_available(void)
{
#ifdef CONFIG_KVM
    return 1;
#else
    return 0;
#endif
}

int xen_available(void)
{
#ifdef CONFIG_XEN
    return 1;
#else
    return 0;
#endif
}
