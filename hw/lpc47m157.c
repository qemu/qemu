/*
 * QEMU SMSC LPC47M157 (Super I/O)
 *
 * Copyright (c) 2013 espes
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/isa/isa.h"


#define ENTER_CONFIG_KEY    0x55
#define EXIT_CONFIG_KEY     0xAA

#define MAX_CONFIG_REG 0x30
#define CONFIG_DEVICE_NUMBER    0x07
#define CONFIG_PORT_LOW         0x26
#define CONFIG_PORT_HIGH        0x27

#define DEVICE_FDD              0x0
#define DEVICE_PARALLEL_PORT    0x3
#define DEVICE_SERIAL_PORT_1    0x4
#define DEVICE_SERIAL_PORT_2    0x5
#define DEVICE_KEYBOARD         0x7
#define DEVICE_GAME_PORT        0x9
#define DEVICE_PME              0xA
#define DEVICE_MPU_401          0xB

#define CONFIG_DEVICE_BASE_ADDRESS_HIGH     0x60
#define CONFIG_DEVICE_BASE_ADDRESS_LOW      0x61

#define DEBUG_LPC47M157

typedef struct LPC47M157State {
    ISADevice dev;

    MemoryRegion io;

    bool configuration_mode;
    uint32_t selected_reg;

    uint8_t config_regs[MAX_CONFIG_REG];
} LPC47M157State;

#define LPC47M157_DEVICE(obj) \
    OBJECT_CHECK(LPC47M157State, (obj), "lpc47m157")

static void lpc47m157_io_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned int size)
{
    LPC47M157State *s = opaque;

#ifdef DEBUG_LPC47M157
    printf("lpc47m157 io write 0x%llx = 0x%llx\n", addr, val);
#endif

    if (addr == 0) { //INDEX_PORT
        if (val == ENTER_CONFIG_KEY) {
            assert(!s->configuration_mode);
            s->configuration_mode = true;
        } else if (val == EXIT_CONFIG_KEY) {
            assert(s->configuration_mode);
            s->configuration_mode = false;
        } else {
            s->selected_reg = val;
        }
    } else if (addr == 1) { //DATA_PORT
        if (s->selected_reg < MAX_CONFIG_REG) {
            //global configuration register
            s->config_regs[s->selected_reg] = val;
        } else {
            if (s->config_regs[CONFIG_DEVICE_NUMBER] == DEVICE_SERIAL_PORT_1) {

            } else {
                assert(false);
            }
        }
    } else {
        assert(false);
    }
}

static uint64_t lpc47m157_io_read(void *opaque, hwaddr addr, unsigned int size)
{
    LPC47M157State *s = opaque;
    uint32_t val = 0;

    if (addr == 0) { //INDEX_PORT

    } else if (addr == 1) { //DATA_PORT
        if (s->selected_reg < MAX_CONFIG_REG) {
            val = s->config_regs[s->selected_reg];
        } else {

        }
    } else {
        assert(false);
    }

#ifdef DEBUG_LPC47M157
    printf("lpc47m157 io read 0x%llx -> 0x%x\n", addr, val);
#endif

    return val;
}

static const MemoryRegionOps lpc47m157_io_ops = {
    .read  = lpc47m157_io_read,
    .write = lpc47m157_io_write,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void lpc47m157_realize(DeviceState *dev, Error **errp)
{
    LPC47M157State *s = LPC47M157_DEVICE(dev);
    ISADevice *isa = ISA_DEVICE(dev);

    const uint32_t iobase = 0x2e; //0x4e if SYSOPT pin, make it a property 
    s->config_regs[CONFIG_PORT_LOW] = iobase & 0xFF;
    s->config_regs[CONFIG_PORT_HIGH] = iobase >> 8;

    memory_region_init_io(&s->io, OBJECT(s),
                          &lpc47m157_io_ops, s, "lpc47m157", 2);
    isa_register_ioport(isa, &s->io, iobase);
}

static void lpc47m157_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = lpc47m157_realize;
    //dc->reset = pc87312_reset;
    //dc->vmsd = &vmstate_pc87312;
    //dc->props = pc87312_properties;
}

static const TypeInfo lpc47m157_type_info = {
    .name          = "lpc47m157",
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(LPC47M157State),
    .class_init    = lpc47m157_class_init,
};

static void lpc47m157_register_types(void)
{
    type_register_static(&lpc47m157_type_info);
}

type_init(lpc47m157_register_types)