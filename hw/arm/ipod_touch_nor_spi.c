#include "hw/arm/ipod_touch_nor_spi.h"

static uint32_t ipod_touch_nor_spi_transfer(SSIPeripheral *dev, uint32_t value)
{
    IPodTouchNORSPIState *s = IPOD_TOUCH_NOR_SPI(dev);

    //printf("NOR SPI received value 0x%08x\n", value);

    if(!s->cur_cmd && (value == 0x5)) {
        // this is a command -> set it
        s->cur_cmd = value;
        return 0x0;
    }

    if(s->cur_cmd) {
        uint32_t res = 0;
        switch(s->cur_cmd) {
        case 0x5:
            res = 0x0;
            break;
        default:
            break;
        }

        s->cur_cmd = 0;
        return res;
    }
    
    return 0x0;
}

static void ipod_touch_nor_spi_realize(SSIPeripheral *d, Error **errp)
{

}

static void ipod_touch_nor_spi_class_init(ObjectClass *klass, void *data)
{
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);
    k->realize = ipod_touch_nor_spi_realize;
    k->transfer = ipod_touch_nor_spi_transfer;
}

static const TypeInfo ipod_touch_nor_spi_type_info = {
    .name = TYPE_IPOD_TOUCH_NOR_SPI,
    .parent = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(IPodTouchNORSPIState),
    .class_init = ipod_touch_nor_spi_class_init,
};

static void ipod_touch_nor_spi_register_types(void)
{
    type_register_static(&ipod_touch_nor_spi_type_info);
}

type_init(ipod_touch_nor_spi_register_types)
