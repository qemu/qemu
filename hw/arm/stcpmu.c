/* Simtec PMU implementation.
 *
 * Copyright 2008 Daniel Silverstone <dsilvers@simtec.co.uk> and
 * Vincent Sanders <vince@simtec.co.uk>
 *
 * Copyright 2010, 2012 Stefan Weil
 *
 */

#include "qemu-common.h"
#include "hw/i2c/i2c.h"
#include "stcpmu.h"

//#define DBF(X...) fprintf(stderr, "QEMU: stcpmu: " X)
#define DBF(X...)

static const char stcpmu_ident[] = "SBPM";
static const char stcpmu_uniqueid[] = "\0\0QEMU";


typedef struct {
    I2CSlave i2c;
    int reg;
    int rdidx;
    int wridx;
} StcPMUState;

static int
stcpmu_rx(I2CSlave *i2c)
{
    StcPMUState *s = FROM_I2C_SLAVE(StcPMUState, i2c);
    int ret = 0;

    DBF("Read from reg %d byte %d\n", s->reg, s->rdidx);

    switch (s->reg) {

    case IICREG_IDENT:
        if (s->rdidx >= 4) {
            ret = 0;
        } else {
            ret = stcpmu_ident[s->rdidx];
        }
        s->rdidx++;
        break;

    case IICREG_VER:
        ret = STCPMU_VCURR;
        break;

    case IICREG_IRQEN:
        ret = 0x02;
        break;

    case IICREG_UNQID:
        if (s->rdidx >= 6) {
            ret = 0;
        } else {
            ret = stcpmu_uniqueid[s->rdidx];
        }
        s->rdidx++;
        break;

    case IICREG_GPIO_PRESENT:
        ret = 0;
        s->rdidx++;
    }

    DBF("Result 0x%02x\n", ret);

    return ret;
}

static int
stcpmu_tx(I2CSlave *i2c, uint8_t data)
{
    StcPMUState *s = FROM_I2C_SLAVE(StcPMUState, i2c);
    DBF("Write : %d\n", data);
    if (s->wridx == 0) {
        s->reg = data;
        s->wridx++;
    }

    return 0;
}

static void
stcpmu_event(I2CSlave *i2c, enum i2c_event event)
{
    StcPMUState *s = FROM_I2C_SLAVE(StcPMUState, i2c);
    DBF("EV? %d\n", event);
    switch (event) {
    case I2C_START_RECV:
        s->rdidx = 0;
        break;
    case I2C_START_SEND:
        s->wridx = 0;
        break;
    case I2C_FINISH:
    case I2C_NACK:
        break;
    }
}

static int stcpmu_init(I2CSlave *i2c)
{
    //~ StcPMUState *s = FROM_I2C_SLAVE(StcPMUState, i2c);
  //~ StcPMUState *s = (StcPMUState *)
    //~ i2c_slave_init(bus, addr, sizeof(StcPMUState));

  return 0;
}

static const VMStateDescription vmstate_stcpmu = {
    .name = "stcpmu",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    //~ .pre_save = menelaus_pre_save,
    //~ .post_load = menelaus_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_I2C_SLAVE(i2c, StcPMUState),
        VMSTATE_END_OF_LIST()
    }
};

static void stcpmu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    dc->vmsd = &vmstate_stcpmu;
    k->init = stcpmu_init;
    k->event = stcpmu_event;
    k->recv = stcpmu_rx;
    k->send = stcpmu_tx;
}

static const TypeInfo stcpmu_info = {
    .name ="stcpmu",
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(StcPMUState),
    .class_init = stcpmu_class_init
};

static void stcpmu_register_types(void)
{
    type_register_static(&stcpmu_info);
}

type_init(stcpmu_register_types)
