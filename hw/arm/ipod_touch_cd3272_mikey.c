#include "hw/arm/ipod_touch_cd3272_mikey.h"

static int cd3272_mikey_event(I2CSlave *i2c, enum i2c_event event)
{
    return 0;
}

static uint8_t cd3272_mikey_recv(I2CSlave *i2c)
{
    CD3272MikeyState *s = CD3272MIKEY(i2c);
    printf("Reading mikey register %d\n", s->cmd);

    int res = 0;

    switch(s->cmd) {
        default:
            res = 0;
    }

    s->cmd += 1;
    return res;
}

static int cd3272_mikey_send(I2CSlave *i2c, uint8_t data)
{
    CD3272MikeyState *s = CD3272MIKEY(i2c);
    s->cmd = data;
    return 0;
}

static void cd3272_mikey_init(Object *obj)
{

}

static void cd3272_mikey_class_init(ObjectClass *klass, void *data)
{
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = cd3272_mikey_event;
    k->recv = cd3272_mikey_recv;
    k->send = cd3272_mikey_send;
}

static const TypeInfo cd3272_mikey_info = {
    .name          = TYPE_CD3272MIKEY,
    .parent        = TYPE_I2C_SLAVE,
    .instance_init = cd3272_mikey_init,
    .instance_size = sizeof(CD3272MikeyState),
    .class_init    = cd3272_mikey_class_init,
};

static void cd3272_mikey_register_types(void)
{
    type_register_static(&cd3272_mikey_info);
}

type_init(cd3272_mikey_register_types)