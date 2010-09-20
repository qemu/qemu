/* Chrontel 7xxx (7006 in particular) stub implementation.
 *
 * Copyright 2008 Daniel Silverstone <dsilvers@simtec.co.uk> and
 * Vincent Sanders <vince@simtec.co.uk>
 *
 */

#include "qemu-common.h"
#include "i2c.h"
#include <stdio.h>

//#define DBF(X...) fprintf(stderr, "QEMU:ch7xxx:" X)
#define DBF(X...)

typedef struct ch7xxx_state_s {
  i2c_slave i2c;
  int reg;
  int wridx;
} ch7xxx_state_t;

static int
ch7xxx_rx(i2c_slave *i2c)
{
    ch7xxx_state_t *s = (ch7xxx_state_t *)i2c;
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
  ch7xxx_state_t *s = (ch7xxx_state_t *)i2c;
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
  ch7xxx_state_t *s = (ch7xxx_state_t *)i2c;
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

i2c_slave *
ch7xxx_init(i2c_bus *bus, int addr)
{
  ch7xxx_state_t *s = (ch7xxx_state_t *)
    i2c_slave_init(bus, addr, sizeof(ch7xxx_state_t));

  s->i2c.event = ch7xxx_event;
  s->i2c.recv = ch7xxx_rx;
  s->i2c.send = ch7xxx_tx;

  return &s->i2c;
}
