#include "hw/arm/ipod_touch_pcf50633_pmu.h"

static int pcf50633_event(I2CSlave *i2c, enum i2c_event event)
{
    return 0;
}

static int int_to_bcd(int value) {
    int shift = 0;
    int res = 0;
    while (value > 0) {
      res |= (value % 10) << (shift++ << 2);
      value /= 10;
   }
   return res;
}

static uint8_t pcf50633_recv(I2CSlave *i2c)
{
    Pcf50633State *s = PCF50633(i2c);
    printf("Reading PMU register %d\n", s->cmd);

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    int res = 0;

    switch(s->cmd) {
        case PMU_MBCS1:
            res = 0; // battery power source
            break;
        case PMU_ADCC1:
            res = 0; // battery charge voltage
            break;
        case PMU_RTCSC:  // seconds
            res = int_to_bcd(tm.tm_sec);
            break;
        case PMU_RTCMN:  // minutes
            res = int_to_bcd(tm.tm_min);
            break;
        case PMU_RTCHR:  // hours
            res = int_to_bcd(tm.tm_hour);
            break;
        case PMU_RTCDT:  // days
            res = int_to_bcd(tm.tm_mday);
            break;
        case PMU_RTCMT:  // month
            res = int_to_bcd(tm.tm_mon + 1);
            break;
        case PMU_RTCYR:  // year
            res = int_to_bcd(tm.tm_year - 100); // the year counts from 1900
            break;
        case 0x67:
            res = 1; // whether we should enable debug UARTS
            break;
        case 0x69:
            res = 0; // boot count error/panic
            break;
        case 0x76:
            res = 0; // unknown register
            break;
        default:
            res = 0;
    }

    s->cmd += 1;
    return res;
}

static int pcf50633_send(I2CSlave *i2c, uint8_t data)
{
    Pcf50633State *s = PCF50633(i2c);
    s->cmd = data;
    return 0;
}

static void pcf50633_init(Object *obj)
{

}

static void pcf50633_class_init(ObjectClass *klass, void *data)
{
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    k->event = pcf50633_event;
    k->recv = pcf50633_recv;
    k->send = pcf50633_send;
}

static const TypeInfo pcf50633_info = {
    .name          = TYPE_PCF50633,
    .parent        = TYPE_I2C_SLAVE,
    .instance_init = pcf50633_init,
    .instance_size = sizeof(Pcf50633State),
    .class_init    = pcf50633_class_init,
};

static void pcf50633_register_types(void)
{
    type_register_static(&pcf50633_info);
}

type_init(pcf50633_register_types)