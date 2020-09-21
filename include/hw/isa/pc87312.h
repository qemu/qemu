/*
 * QEMU National Semiconductor PC87312 (Super I/O)
 *
 * Copyright (c) 2010-2012 Herve Poussineau
 * Copyright (c) 2011-2012 Andreas Färber
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
#ifndef QEMU_PC87312_H
#define QEMU_PC87312_H

#include "hw/isa/superio.h"
#include "qom/object.h"


#define TYPE_PC87312 "pc87312"
OBJECT_DECLARE_SIMPLE_TYPE(PC87312State, PC87312)

struct PC87312State {
    /*< private >*/
    ISASuperIODevice parent_dev;
    /*< public >*/

    uint16_t iobase;
    uint8_t config; /* initial configuration */

    struct {
        ISADevice *dev;
    } ide;

    MemoryRegion io;

    uint8_t read_id_step;
    uint8_t selected_index;

    uint8_t regs[3];
};


#endif
