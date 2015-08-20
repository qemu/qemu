/* Chrontel 7xxx (7006 in particular) stub implementation.
 *
 * Copyright 2008 Daniel Silverstone <dsilvers@simtec.co.uk> and
 * Vincent Sanders <vince@simtec.co.uk>
 *
 * Copyright 2010, 2012 Stefan Weil
 *
 */

#include "qemu-common.h"
#include "hw/i2c/i2c.h"

//#define DBF(X...) fprintf(stderr, "QEMU:ch7xxx:" X)
#define DBF(X...)

typedef struct {
  I2CSlave i2c;
  int reg;
  int wridx;
} CH7xxxState;

static int
ch7xxx_rx(I2CSlave *i2c)
{
    CH7xxxState *s = (CH7xxxState *)i2c;
    DBF("RX?\n");

    switch (s->reg) {
    case 0x25:
      return 0x2A; /* CH7006 ID */
    }

    return 0x00;
}

static int
ch7xxx_tx(I2CSlave *i2c, uint8_t data)
{
  DBF("TX: %d\n", data);
  CH7xxxState *s = (CH7xxxState *)i2c;
  if (s->wridx == 0) {
    s->reg = data;
    s->wridx++;
    return 0;
  }

  return 0;
}

static void
ch7xxx_event(I2CSlave *i2c, enum i2c_event event)
{
  CH7xxxState *s = (CH7xxxState *)i2c;
  DBF("EV? %d\n", event);
  switch (event) {
  case I2C_START_RECV:
    break;
  case I2C_START_SEND:
    s->wridx = 0;
  case I2C_FINISH:
  case I2C_NACK:
      break;
  }
}

static int ch7xxx_init(I2CSlave *i2c)
{
    //~ StcPMUState *s = FROM_I2C_SLAVE(StcPMUState, i2c);
  //~ StcPMUState *s = (StcPMUState *)
    //~ i2c_slave_init(bus, addr, sizeof(StcPMUState));

  return 0;
}

static const VMStateDescription vmstate_ch7xxx = {
    .name = "ch7xxx",
    .version_id = 0,
    .minimum_version_id = 0,
    .minimum_version_id_old = 0,
    //~ .pre_save = menelaus_pre_save,
    //~ .post_load = menelaus_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_I2C_SLAVE(i2c, CH7xxxState),
        VMSTATE_END_OF_LIST()
    }
};

static void ch7xx_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);
    dc->vmsd = &vmstate_ch7xxx;
    k->init = ch7xxx_init;
    k->event = ch7xxx_event;
    k->recv = ch7xxx_rx;
    k->send = ch7xxx_tx;
}

static const TypeInfo ch7xxx_info = {
    .name ="ch7xxx",
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(CH7xxxState),
    .class_init = ch7xx_class_init,
};

static void ch7xxx_register_types(void)
{
    type_register_static(&ch7xxx_info);
}

type_init(ch7xxx_register_types)
