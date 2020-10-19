/*
 * QEMU M48T59 and M48T08 NVRAM emulation
 *
 * Copyright (c) 2003-2005, 2007 Jocelyn Mayer
 * Copyright (c) 2013 Hervé Poussineau
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

#ifndef HW_RTC_M48T59_H
#define HW_RTC_M48T59_H

#include "exec/hwaddr.h"
#include "qom/object.h"

#define TYPE_NVRAM "nvram"

typedef struct NvramClass NvramClass;
DECLARE_CLASS_CHECKERS(NvramClass, NVRAM,
                       TYPE_NVRAM)
#define NVRAM(obj) \
    INTERFACE_CHECK(Nvram, (obj), TYPE_NVRAM)

typedef struct Nvram Nvram;

struct NvramClass {
    InterfaceClass parent;

    uint32_t (*read)(Nvram *obj, uint32_t addr);
    void (*write)(Nvram *obj, uint32_t addr, uint32_t val);
    void (*toggle_lock)(Nvram *obj, int lock);
};

#endif /* HW_M48T59_H */
