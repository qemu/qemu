/*
 * Keyasic SD Card 2.0 controller
 *
 */

#include "qemu/osdep.h"
#include "hw/sd/keyasic_sd.h"

#define REG_SCBSR_OFFSET        0x0
#define REG_SCCR_OFFSET         0x4
#define REG_SCARGR_OFFSET       0x8
#define REG_CSADDR_OFFSET       0xc
#define REG_SCSR_OFFSET         0x10
#define REG_SCEER_OFFSET        0x14
#define REG_SCRR1_OFFSET        0x18
#define REG_SCRR2_OFFSET        0x1c
#define REG_SCRR3_OFFSET        0x20
#define REG_SCRR4_OFFSET        0x24
#define REG_SCBTRR_OFFSET       0x48
#define REG_SCBTCR_OFFSET       0x48

#define SCCR_HARD_RESET         (1<<7)

#define SCSR_CARD_EXIST         (1<<11)

static void keyasic_sd_hard_reset(KeyasicSdState *s)
{
    s->scbsr = 0;
    s->sccr = 0;
    s->scargr = 0;
    s->csaddr = 0;
    s->scsr = 0;
    s->sceer = 0;
    s->scrr1 = 0;
    s->scrr2 = 0;
    s->scrr3 = 0;
    s->scrr4 = 0;
    s->scbtrr = 0;
    s->scbtcr = 0;
}

static uint64_t keyasic_sd_read(void *opaque, hwaddr offset, unsigned int size)
{
    KeyasicSdState *s = KEYASIC_SD(opaque);
    uint64_t val = 0;

    switch (offset) {
    case REG_SCBSR_OFFSET:
        val = s->scbsr;
        break;

    case REG_SCCR_OFFSET:
        val = s->sccr;
        break;

    case REG_SCARGR_OFFSET:
        val = s->scargr;
        break;

    case REG_CSADDR_OFFSET:
        val = s->csaddr;
        break;

    case REG_SCSR_OFFSET:
        val = s->scsr;
        break;

    case REG_SCEER_OFFSET:
        val = s->sceer;
        break;

    case REG_SCRR1_OFFSET:
        val = s->scrr1;
        break;

    case REG_SCRR2_OFFSET:
        val = s->scrr2;
        break;

    case REG_SCRR3_OFFSET:
        val = s->scrr3;
        break;

    case REG_SCRR4_OFFSET:
        val = s->scrr4;
        break;

    case REG_SCBTRR_OFFSET:
        val = s->scbtrr;
        break;

    default: break;
    }

    printf("sd READ(0x%02lx): %lx\n", offset, val);

    return val;
}

static void keyasic_sd_write(void *opaque, hwaddr offset, uint64_t val,
                                unsigned int size)
{
    KeyasicSdState *s = KEYASIC_SD(opaque);

    switch (offset) {
    case REG_SCBSR_OFFSET:
        s->scbsr = val;
        break;

    case REG_SCCR_OFFSET:
        s->sccr = val;

        if (val & SCCR_HARD_RESET) {
            keyasic_sd_hard_reset(s);
        }
        break;

    case REG_SCARGR_OFFSET:
        s->scargr = val;
        break;

    case REG_CSADDR_OFFSET:
        s->csaddr = val;
        break;

    case REG_SCSR_OFFSET:
        // FIXME: for now we say that we don't have any SD CARD
        s->scsr = val & ~SCSR_CARD_EXIST;
        break;

    case REG_SCEER_OFFSET:
        s->sceer = val;
        break;

    case REG_SCRR1_OFFSET:
        s->scrr1 = val;
        break;

    case REG_SCRR2_OFFSET:
        s->scrr2 = val;
        break;

    case REG_SCRR3_OFFSET:
        s->scrr3 = val;
        break;

    case REG_SCRR4_OFFSET:
        s->scrr4 = val;
        break;

    case REG_SCBTCR_OFFSET:
        s->scbtcr = val;
        break;

    default: break;
    }

    printf("sd WRITE(0x%02lx): %lx\n", offset, val);
}

static const MemoryRegionOps keyasic_sd_ops = {
    .read = keyasic_sd_read,
    .write = keyasic_sd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void keyasic_sd_realize(DeviceState *dev, Error **errp)
{
    KeyasicSdState *s = KEYASIC_SD(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &keyasic_sd_ops, s, "keyasic_sd", 0x100);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void keyasic_sd_reset(DeviceState *dev)
{
    KeyasicSdState *s = KEYASIC_SD(dev);

    keyasic_sd_hard_reset(s);
}

static void keyasic_sd_class_init(ObjectClass *classp, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(classp);

    dc->desc = "Keyasic SD card 2.0 controller";
    dc->realize = keyasic_sd_realize;
    dc->reset = keyasic_sd_reset;
}

static TypeInfo keyasic_sd_info = {
    .name = TYPE_KEYASIC_SD,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(KeyasicSdState),
    .class_init = keyasic_sd_class_init,
};

static void keyasic_sd_register_types(void)
{
    type_register_static(&keyasic_sd_info);
}

type_init(keyasic_sd_register_types)
