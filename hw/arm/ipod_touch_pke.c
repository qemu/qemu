#include "hw/arm/ipod_touch_pke.h"

uint8_t LLB_IMG3_HASH[34] = { 0x30, 0x21, 0x30, 0x09,
                  0x06, 0x05, 0x2b, 0x0e,
                  0x03, 0x02, 0x1a, 0x05,
                  0x00, 0x04, 0x14, 0xc0,
                  0x67, 0x68, 0x03, 0xe2,
                  0x53, 0xd0, 0xf2, 0x8f,
                  0x35, 0xba, 0xde, 0x08,
                  0xea, 0xdc, 0x3a, 0xb3,
                  0xd3, 0x49 };

uint8_t LLB_IMG3_HASH_ALGO_ID = 0xCD;

static uint64_t ipod_touch_pke_read(void *opaque, hwaddr offset, unsigned size)
{
    IPodTouchPKEState *s = (IPodTouchPKEState *)opaque;

    printf("%s: offset 0x%08x\n", __FUNCTION__, offset);

    switch(offset) {
        case 0x900 ... 0x9FC:
        {
            uint32_t *res = (uint32_t *)s->pmod_result;
            return res[(offset - 0x900) / 4];
        }
        default:
            break;
    }

    return 0;
}

static void ipod_touch_pke_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    IPodTouchPKEState *s = (IPodTouchPKEState *)opaque;

    printf("%s: offset 0x%08x value 0x%08x\n", __FUNCTION__, offset, value);
}

static const MemoryRegionOps pke_ops = {
    .read = ipod_touch_pke_read,
    .write = ipod_touch_pke_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_pke_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    IPodTouchPKEState *s = IPOD_TOUCH_PKE(dev);

    memory_region_init_io(&s->iomem, obj, &pke_ops, s, "pke", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    // TODO prepare the hard-coded pmod result
    for(int i = 0; i < 254; i++) { s->pmod_result[i] = 0xFF; }
    s->pmod_result[254] = 0x1;
    s->pmod_result[255] = 0x0;
    s->pmod_result[35] = 0x0;
    for(int i = 0; i < 34; i++) { s->pmod_result[34 - i] = LLB_IMG3_HASH[i]; }
    s->pmod_result[0] = LLB_IMG3_HASH_ALGO_ID;
}

static void ipod_touch_pke_class_init(ObjectClass *klass, void *data)
{

}

static const TypeInfo ipod_touch_pke_info = {
    .name          = TYPE_IPOD_TOUCH_PKE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchPKEState),
    .instance_init = ipod_touch_pke_init,
    .class_init    = ipod_touch_pke_class_init,
};

static void ipod_touch_machine_types(void)
{
    type_register_static(&ipod_touch_pke_info);
}

type_init(ipod_touch_machine_types)