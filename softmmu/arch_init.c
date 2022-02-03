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
#include "qemu/module.h"
#include "sysemu/arch_init.h"

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

const uint32_t arch_type = QEMU_ARCH;

void qemu_init_arch_modules(void)
{
#ifdef CONFIG_MODULES
    module_init_info(qemu_modinfo);
    module_allow_arch(TARGET_NAME);
#endif
}
