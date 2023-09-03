#include "hw/arm/ipod_touch_sdio.h"

static void trigger_irq(void *opaque)
{
    IPodTouchSDIOState *s = (IPodTouchSDIOState *)opaque;
    s->irq_reg = 0x3;
    qemu_irq_raise(s->irq);
}

void sdio_exec_cmd(IPodTouchSDIOState *s)
{
    uint32_t cmd_type = s->cmd & 0x3f;
    uint32_t addr = (s->arg >> 9) & 0x1ffff;
    uint32_t func = (s->arg >> 28) & 0x7;
    printf("SDIO CMD: %d, ADDR: %d, FUNC: %d\n", cmd_type, addr, func);
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
            printf("SDIO: Executing cmd52 by writing 0x%02x to register 0x%02x\n", data, addr);
        } else {
            if(addr == 0x1000e) {
                // misc register
                s->resp0 = (1 << 6) /* enable ALP clock */ | (1 << 7); /* enable HT clock */
            }
            else {
                printf("Loading as response reg 0x%02x 0x%02x\n", s->registers[addr], addr);
                s->resp0 = s->registers[addr];
            }
        }
    }
    else if(cmd_type == 0x35) {
        // CMD53 - block transfer
        addr = addr & 0x7fff;
        bool is_write = (s->arg >> 31) != 0;
        printf("SDIO: Executing cmd53 with block size %d and %d blocks (reg address: 0x%08x, destination address: 0x%08x, write? %d)\n", s->blklen, s->numblk, addr, s->baddr, is_write);
        
        if(is_write) {
            if(func == 0x1) {
                cpu_physical_memory_read(s->baddr, &s->registers[addr], s->blklen * s->numblk);
            }
            else if(func == 0x2) {
                // this is a BCM4325 command - schedule the IRQ request to indicate that the command has been completed
                s->irq_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, trigger_irq, s);
                timer_mod(s->irq_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 50);
                printf("INT SCHED\n");
            }
        } else {
            if(addr == 0x0) {
                // chip ID register
                uint32_t chipid[] = { 0x5 << 0x10 };
                cpu_physical_memory_write(s->baddr, &chipid, 0x4);
            }
        }

        // toggle IRQ register
        s->irq_reg = 0x1;
        qemu_irq_raise(s->irq);
        printf("Raised IRQ\n");
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
        case SDIO_IRQ:
            qemu_irq_lower(s->irq);
            printf("Lowered IRQ\n");
            break;
        case SDIO_IRQMASK:
            s->irq_mask = value;
            break;
        case SDIO_BADDR:
            s->baddr = value;
            break;
        case SDIO_BLKLEN:
            s->blklen = value;
            break;
        case SDIO_NUMBLK:
            s->numblk = value;
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
        case SDIO_IRQ:
            return s->irq_reg;
        case SDIO_IRQMASK:
            return s->irq_mask;
        case SDIO_BADDR:
            return s->baddr;
        case SDIO_BLKLEN:
            return s->blklen;
        case SDIO_NUMBLK:
            return s->numblk;
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
    sysbus_init_irq(sbd, &s->irq);
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