#include "hw/arm/ipod_touch_lis302dl.h"

static int lis302dl_event(I2CSlave *i2c, enum i2c_event event)
{
    return 0;
}

static uint8_t lis302dl_recv(I2CSlave *i2c)
{
    LIS302DLState *s = LIS302DL(i2c);
    printf("Reading accelerometer register %d\n", s->cmd);

    switch(s->cmd) {
        case ACCEL_WHOAMI:
            return ACCEL_WHOAMI_VALUE; // whoami value
        default:
            return 0;
    }
}

static int lis302dl_send(I2CSlave *i2c, uint8_t data)
{
    LIS302DLState *s = LIS302DL(i2c);
    s->cmd = data;
    return 0;
}

static void lis302dl_init(Object *obj)
{

}

static void lis302dl_class_init(ObjectClass *klass, void *data)
{
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = lis302dl_event;
    k->recv = lis302dl_recv;
    k->send = lis302dl_send;
}

static const TypeInfo lis302dl_info = {
    .name          = TYPE_LIS302DL,
    .parent        = TYPE_I2C_SLAVE,
    .instance_init = lis302dl_init,
    .instance_size = sizeof(LIS302DLState),
    .class_init    = lis302dl_class_init,
};

static void lis302dl_register_types(void)
{
    type_register_static(&lis302dl_info);
}

type_init(lis302dl_register_types)