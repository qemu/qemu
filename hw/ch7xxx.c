/* Chrontel 7xxx (7006 in particular) stub implementation.
 *
 * Copyright 2008 Daniel Silverstone <dsilvers@simtec.co.uk> and
 * Vincent Sanders <vince@simtec.co.uk>
 *
 */

#include "qemu-common.h"
#include "i2c.h"

//#define DBF(X...) fprintf(stderr, "QEMU:ch7xxx:" X)
#define DBF(X...)

typedef struct {
  i2c_slave i2c;
  int reg;
  int wridx;
} CH7xxxState;

static int
ch7xxx_rx(i2c_slave *i2c)
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
ch7xxx_tx(i2c_slave *i2c, uint8_t data)
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
ch7xxx_event(i2c_slave *i2c, enum i2c_event event)
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

static int ch7xxx_init(i2c_slave *i2c)
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

static I2CSlaveInfo ch7xxx_info = {
    .qdev.name ="ch7xxx",
    .qdev.size = sizeof(CH7xxxState),
    .qdev.vmsd = &vmstate_ch7xxx,
    .init = ch7xxx_init,
    .event = ch7xxx_event,
    .recv = ch7xxx_rx,
    .send = ch7xxx_tx
};

static void ch7xxx_register_devices(void)
{
    i2c_register_slave(&ch7xxx_info);
}

device_init(ch7xxx_register_devices)
