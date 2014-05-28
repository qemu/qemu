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
#include "hw/char/serial.h"
#include "sysemu/sysemu.h"
#include "sysemu/char.h"
#include "qapi/qmp/qerror.h"

#define MAX_DEVICE 0xC
#define DEVICE_FDD              0x0
#define DEVICE_PARALLEL_PORT    0x3
#define DEVICE_SERIAL_PORT_1    0x4
#define DEVICE_SERIAL_PORT_2    0x5
#define DEVICE_KEYBOARD         0x7
#define DEVICE_GAME_PORT        0x9
#define DEVICE_PME              0xA
#define DEVICE_MPU_401          0xB

#define ENTER_CONFIG_KEY    0x55
#define EXIT_CONFIG_KEY     0xAA

#define MAX_CONFIG_REG  0x30
#define MAX_DEVICE_REGS 0xFF

#define CONFIG_DEVICE_NUMBER    0x07
#define CONFIG_PORT_LOW         0x26
#define CONFIG_PORT_HIGH        0x27

#define CONFIG_DEVICE_ACTIVATE              0x30
#define CONFIG_DEVICE_BASE_ADDRESS_HIGH     0x60
#define CONFIG_DEVICE_BASE_ADDRESS_LOW      0x61
#define CONFIG_DEVICE_INETRRUPT             0x70

#define DEBUG_LPC47M157

typedef struct LPC47M157State {
    ISADevice dev;

    MemoryRegion io;

    bool configuration_mode;
    uint32_t selected_reg;

    uint8_t config_regs[MAX_CONFIG_REG];
    uint8_t device_regs[MAX_DEVICE][MAX_DEVICE_REGS];

    struct {
        bool active;
        SerialState state;
    } serial[2];
} LPC47M157State;

#define LPC47M157_DEVICE(obj) \
    OBJECT_CHECK(LPC47M157State, (obj), "lpc47m157")

static void update_devices(LPC47M157State *s)
{
    ISADevice *isadev = ISA_DEVICE(s);
    
    /* init serial devices */
    int i;
    for (i=0; i<2; i++) {
        uint8_t *dev = s->device_regs[DEVICE_SERIAL_PORT_1 + i];
        if (dev[CONFIG_DEVICE_ACTIVATE] && !s->serial[i].active) {
            
            uint32_t iobase = (dev[CONFIG_DEVICE_BASE_ADDRESS_HIGH] << 8)
                                | dev[CONFIG_DEVICE_BASE_ADDRESS_LOW];
            uint32_t irq = dev[CONFIG_DEVICE_INETRRUPT];

            SerialState *ss = &s->serial[i].state;
            if (irq != 0) {
                isa_init_irq(isadev, &ss->irq, irq);
            }
            isa_register_ioport(isadev, &ss->io, iobase);

            s->serial[i].active = true;
        }
    }
}

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

            update_devices(s);
        } else {
            s->selected_reg = val;
        }
    } else if (addr == 1) { //DATA_PORT
        if (s->selected_reg < MAX_CONFIG_REG) {
            /* global configuration register */
            s->config_regs[s->selected_reg] = val;
        } else {
            /* device register */
            assert(s->config_regs[CONFIG_DEVICE_NUMBER] < MAX_DEVICE);
            uint8_t* dev = s->device_regs[s->config_regs[CONFIG_DEVICE_NUMBER]];
            dev[s->selected_reg] = val;
#ifdef DEBUG_LPC47M157
            printf("lpc47m157 dev %x . %x = %llx\n", s->config_regs[CONFIG_DEVICE_NUMBER], s->selected_reg, val);
#endif
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
            assert(s->config_regs[CONFIG_DEVICE_NUMBER] < MAX_DEVICE);
            uint8_t* dev = s->device_regs[s->config_regs[CONFIG_DEVICE_NUMBER]];
            val = dev[s->selected_reg];
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

    /* init serial cores */
    int i;
    for (i=0; i<2; i++) {
        CharDriverState *chr = serial_hds[i];
        if (chr == NULL) {
            char name[5];
            snprintf(name, sizeof(name), "ser%d", i);
            chr = qemu_chr_new(name, "null", NULL);
        }

        SerialState *ss = &s->serial[i].state;
        ss->chr = chr;
        ss->baudbase = 115200;

        Error *err = NULL;
        serial_realize_core(ss, &err);
        if (err != NULL) {
            qerror_report_err(err);
            error_free(err);
            exit(1);
        }

        memory_region_init_io(&ss->io, OBJECT(s),
                              &serial_io_ops, ss, "serial", 8);
    }
}

static const VMStateDescription vmstate_lpc47m157= {
    .name = "lpc47m157",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(serial[0].state, LPC47M157State, 0,
                       vmstate_serial, SerialState),
        VMSTATE_STRUCT(serial[1].state, LPC47M157State, 0,
                       vmstate_serial, SerialState),
        VMSTATE_END_OF_LIST()
    }
};

static void lpc47m157_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = lpc47m157_realize;
    dc->vmsd = &vmstate_lpc47m157;
    //dc->reset = pc87312_reset;
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