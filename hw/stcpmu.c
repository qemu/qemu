/* Chrontel 7xxx (7006 in particular) stub implementation.
 *
 * Copyright 2008 Daniel Silverstone <dsilvers@simtec.co.uk> and
 * Vincent Sanders <vince@simtec.co.uk>
 *
 */

#include "qemu-common.h"
#include "i2c.h"
#include <stdio.h>
#include "stcpmu.h"

//#define DBF(X...) fprintf(stderr, "QEMU: stcpmu: " X)
#define DBF(X...)

unsigned char stcpmu_ident[] = "SBPM";
unsigned char stcpmu_uniqueid[] = "\0\0QEMU";


typedef struct stcpmu_state_s {
  i2c_slave i2c;
  int reg;
  int rdidx;
  int wridx;
} stcpmu_state_t;

static int
stcpmu_rx(i2c_slave *i2c)
{
    stcpmu_state_t *s = (stcpmu_state_t *)i2c;
    int ret = 0;

    DBF("Read from reg %d byte %d\n", s->reg, s->rdidx);

    switch (s->reg) {

    case IICREG_IDENT:
        if (s->rdidx >= 4)
            ret = 0;
        else
            ret = stcpmu_ident[s->rdidx];
        s->rdidx++;
        break;

    case IICREG_VER:
        ret = STCPMU_VCURR;
        break;

    case IICREG_IRQEN:
        ret = 0x02;
        break;

    case IICREG_UNQID:
        if (s->rdidx >= 6)
            ret = 0;
        else
            ret = stcpmu_uniqueid[s->rdidx];
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
stcpmu_tx(i2c_slave *i2c, uint8_t data)
{
  DBF("Write : %d\n", data);
  stcpmu_state_t *s = (stcpmu_state_t *)i2c;
  if (s->wridx == 0) {
    s->reg = data;
    s->wridx++;
    return 0;
  }

  return 0;
}

static void
stcpmu_event(i2c_slave *i2c, enum i2c_event event)
{
  stcpmu_state_t *s = (stcpmu_state_t *)i2c;
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

i2c_slave *
stcpmu_init(i2c_bus *bus, int addr)
{
  stcpmu_state_t *s = (stcpmu_state_t *)
    i2c_slave_init(bus, addr, sizeof(stcpmu_state_t));

  s->i2c.event = stcpmu_event;
  s->i2c.recv = stcpmu_rx;
  s->i2c.send = stcpmu_tx;

  return &s->i2c;
}
