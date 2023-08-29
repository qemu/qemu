#include "hw/arm/ipod_touch_sdio.h"

void sdio_exec_cmd(IPodTouchSDIOState *s)
{
    uint32_t cmd_type = s->cmd & 0x3f;
    uint32_t addr = (s->arg >> 9) & 0x1ffff;
    printf("SDIO CMD: %d, ADDR: %d\n", cmd_type, addr);
    if(cmd_type == 0x3) {
        // RCA request - ignore
    }
    else if(cmd_type == 0x5) {
        if(addr == 0) {
            // reading slot 0 - make sure there is a device here
            s->resp0 = (1 << 31) /* indicate ready */ | (BCM4325_FUNCTIONS << CMD5_FUNC_OFFSET) /* number of functions */;
        }
    }
    else if(cmd_type == 0x7) {
        // select card - ignore
    }
    else if(cmd_type == 0x34) {
        // CMD52 - read/write from a register
        bool is_write = (s->arg >> 31) != 0;
        if(is_write) {
            uint8_t data = s->arg & 0xFF;
            s->registers[addr] = data;
            if(addr == 0x2) { s->registers[0x3] = data; } // if we write to register 2, we also write the same result to register 3 (this is the enabled functions register)
            printf("Writing %d to register %d\n", data, addr);
        } else {
            s->resp0 = s->registers[addr];
        }
    }
    else {
        hw_error("Unknown SDIO command %d", cmd_type);
    }
}

static void ipod_touch_sdio_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
    printf("%s: writing 0x%08x to 0x%08x\n", __func__, value, addr);
    
    IPodTouchSDIOState *s = (struct IPodTouchSDIOState *) opaque;

    switch(addr) {
        case SDIO_CMD:
            s->cmd = value;
            if(value & (1 << 31)) { // execute bit is set
                sdio_exec_cmd(s);
            }
            break;
        case SDIO_ARGU:
            s->arg = value;
            break;
        case SDIO_STATE:
            s->state = value;
            break;
        case SDIO_STAC:
            s->stac = value;
            break;
        case SDIO_CSR:
            s->csr = value;
            break;
        case SDIO_IRQMASK:
            s->irq_mask = value;
            break;
        default:
            break;
    }
}

static uint64_t ipod_touch_sdio_read(void *opaque, hwaddr addr, unsigned size)
{
    printf("%s: offset = 0x%08x\n", __func__, addr);

    IPodTouchSDIOState *s = (struct IPodTouchSDIOState *) opaque;

    switch (addr) {
        case SDIO_CMD:
            return s->cmd;
        case SDIO_ARGU:
            return s->arg;
        case SDIO_STATE:
            return s->state;
        case SDIO_STAC:
            return s->stac;
        case SDIO_DSTA:
            return (1 << 0) | (1 << 4) ; // 0x1 indicates that the SDIO is ready for a CMD, (1 << 4) that the command is complete
        case SDIO_RESP0:
            return s->resp0;
        case SDIO_RESP1:
            return s->resp1;
        case SDIO_RESP2:
            return s->resp2;
        case SDIO_RESP3:
            return s->resp3;
        case SDIO_CSR:
            return s->csr;
        case SDIO_IRQMASK:
            return s->irq_mask;
        default:
            break;
    }

    return 0;
}

static const MemoryRegionOps ipod_touch_sdio_ops = {
    .read = ipod_touch_sdio_read,
    .write = ipod_touch_sdio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_sdio_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    IPodTouchSDIOState *s = IPOD_TOUCH_SDIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->registers[0x9] = CIS_OFFSET; // registers 0x9 - 0xB contain the relative address offset, which we set to 0xC8 (200)
    s->registers[19] = 0x1; // enable support for high speed mode

    // set the vendor information
    s->registers[CIS_OFFSET] = CIS_MANUFACTURER_ID;
    s->registers[CIS_OFFSET + 1] = 0x4;
    s->registers[CIS_OFFSET + 2] = BCM4325_MANUFACTURER & 0xFF;
    s->registers[CIS_OFFSET + 3] = (BCM4325_MANUFACTURER >> 8) & 0xFF;
    s->registers[CIS_OFFSET + 4] = BCM4325_PRODUCT_ID & 0xFF;
    s->registers[CIS_OFFSET + 5] = (BCM4325_PRODUCT_ID >> 8) & 0xFF;

    // set the MAC address
    s->registers[CIS_OFFSET + 6] = CIS_FUNCTION_EXTENSION;
    s->registers[CIS_OFFSET + 8] = 0x4; // unknown
    s->registers[CIS_OFFSET + 9] = 0x6; // the length of the MAC address

    for(int i = 0; i < 6; i++) { // TODO should be fixed
        s->registers[CIS_OFFSET + 10 + i] = 0x42;
    }    

    memory_region_init_io(&s->iomem, obj, &ipod_touch_sdio_ops, s, TYPE_IPOD_TOUCH_SDIO, 4096);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void ipod_touch_sdio_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
}

static const TypeInfo ipod_touch_sdio_type_info = {
    .name = TYPE_IPOD_TOUCH_SDIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchSDIOState),
    .instance_init = ipod_touch_sdio_init,
    .class_init = ipod_touch_sdio_class_init,
};

static void ipod_touch_sdio_register_types(void)
{
    type_register_static(&ipod_touch_sdio_type_info);
}

type_init(ipod_touch_sdio_register_types)