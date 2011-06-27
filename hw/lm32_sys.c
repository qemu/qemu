/*
 *  QEMU model of the LatticeMico32 system control block.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * This model is mainly intended for testing purposes and doesn't fit to any
 * real hardware. On the one hand it provides a control register (R_CTRL) on
 * the other hand it supports the lm32 tests.
 *
 * A write to the control register causes a system shutdown.
 * Tests first write the pointer to a test name to the test name register
 * (R_TESTNAME) and then write a zero to the pass/fail register (R_PASSFAIL) if
 * the test is passed or any non-zero value to it if the test is failed.
 */

#include "hw.h"
#include "sysbus.h"
#include "trace.h"
#include "qemu-log.h"
#include "qemu-error.h"
#include "sysemu.h"
#include "qemu-log.h"

enum {
    R_CTRL = 0,
    R_PASSFAIL,
    R_TESTNAME,
    R_MAX
};

#define MAX_TESTNAME_LEN 16

struct LM32SysState {
    SysBusDevice busdev;
    uint32_t base;
    uint32_t regs[R_MAX];
    uint8_t testname[MAX_TESTNAME_LEN];
};
typedef struct LM32SysState LM32SysState;

static void copy_testname(LM32SysState *s)
{
    cpu_physical_memory_read(s->regs[R_TESTNAME], s->testname,
            MAX_TESTNAME_LEN);
    s->testname[MAX_TESTNAME_LEN - 1] = '\0';
}

static void sys_write(void *opaque, target_phys_addr_t addr, uint32_t value)
{
    LM32SysState *s = opaque;
    char *testname;

    trace_lm32_sys_memory_write(addr, value);

    addr >>= 2;
    switch (addr) {
    case R_CTRL:
        qemu_system_shutdown_request();
        break;
    case R_PASSFAIL:
        s->regs[addr] = value;
        testname = (char *)s->testname;
        qemu_log("TC  %-16s %s\n", testname, (value) ? "FAILED" : "OK");
        break;
    case R_TESTNAME:
        s->regs[addr] = value;
        copy_testname(s);
        break;

    default:
        error_report("lm32_sys: write access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }
}

static CPUReadMemoryFunc * const sys_read_fn[] = {
    NULL,
    NULL,
    NULL,
};

static CPUWriteMemoryFunc * const sys_write_fn[] = {
    NULL,
    NULL,
    &sys_write,
};

static void sys_reset(DeviceState *d)
{
    LM32SysState *s = container_of(d, LM32SysState, busdev.qdev);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }
    memset(s->testname, 0, MAX_TESTNAME_LEN);
}

static int lm32_sys_init(SysBusDevice *dev)
{
    LM32SysState *s = FROM_SYSBUS(typeof(*s), dev);
    int sys_regs;

    sys_regs = cpu_register_io_memory(sys_read_fn, sys_write_fn, s,
            DEVICE_NATIVE_ENDIAN);
    sysbus_init_mmio(dev, R_MAX * 4, sys_regs);

    /* Note: This device is not created in the board initialization,
     * instead it has to be added with the -device parameter. Therefore,
     * the device maps itself. */
    sysbus_mmio_map(dev, 0, s->base);

    return 0;
}

static const VMStateDescription vmstate_lm32_sys = {
    .name = "lm32-sys",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, LM32SysState, R_MAX),
        VMSTATE_BUFFER(testname, LM32SysState),
        VMSTATE_END_OF_LIST()
    }
};

static SysBusDeviceInfo lm32_sys_info = {
    .init = lm32_sys_init,
    .qdev.name  = "lm32-sys",
    .qdev.size  = sizeof(LM32SysState),
    .qdev.vmsd  = &vmstate_lm32_sys,
    .qdev.reset = sys_reset,
    .qdev.props = (Property[]) {
        DEFINE_PROP_UINT32("base", LM32SysState, base, 0xffff0000),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void lm32_sys_register(void)
{
    sysbus_register_withprop(&lm32_sys_info);
}

device_init(lm32_sys_register)
