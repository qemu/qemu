/*
 * QEMU Motorla 680x0 Macintosh hardware System Emulator
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

#ifndef HW_Q800_H
#define HW_Q800_H

#include "hw/boards.h"
#include "qom/object.h"
#include "target/m68k/cpu-qom.h"
#include "system/memory.h"
#include "hw/m68k/q800-glue.h"
#include "hw/misc/mac_via.h"
#include "hw/net/dp8393x.h"
#include "hw/char/escc.h"
#include "hw/or-irq.h"
#include "hw/scsi/esp.h"
#include "hw/block/swim.h"
#include "hw/nubus/mac-nubus-bridge.h"
#include "hw/display/macfb.h"
#include "hw/misc/djmemc.h"
#include "hw/misc/iosb.h"
#include "hw/audio/asc.h"

/*
 * The main Q800 machine
 */

struct Q800MachineState {
    MachineState parent_obj;

    bool easc;
    M68kCPU cpu;
    MemoryRegion rom;
    MemoryRegion rom_alias;
    GLUEState glue;
    MOS6522Q800VIA1State via1;
    MOS6522Q800VIA2State via2;
    dp8393xState dp8393x;
    MemoryRegion dp8393x_prom;
    ESCCState escc;
    OrIRQState escc_orgate;
    SysBusESPState esp;
    Swim swim;
    MacNubusBridge mac_nubus_bridge;
    MacfbNubusState macfb;
    DJMEMCState djmemc;
    IOSBState iosb;
    ASCState asc;
    MemoryRegion ramio;
    MemoryRegion macio;
    MemoryRegion macio_alias;
    MemoryRegion machine_id;
    MemoryRegion escc_alias;
};

#define TYPE_Q800_MACHINE MACHINE_TYPE_NAME("q800")
OBJECT_DECLARE_SIMPLE_TYPE(Q800MachineState, Q800_MACHINE)

#endif
