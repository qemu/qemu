#include "hw/arm/ipod_touch_fmss.h"

static void write_chip_info(IPodTouchFMSSState *s)
{
    uint32_t chipid[] = { 0xb614d5ad, 0xb614d5ad, 0xb614d5ad, 0xb614d5ad };
    cpu_physical_memory_write(s->reg_cinfo_target_addr, &chipid, 0x10);
}

static void read_nand_pages(IPodTouchFMSSState *s)
{
    for(int page_ind = 0; page_ind < s->reg_num_pages; page_ind++) {
        uint32_t page_nr = 0;
        uint32_t page_out_addr = 0;
        uint32_t page_spare_out_addr = 0;
        cpu_physical_memory_read(s->reg_pages_in_addr + page_ind, &page_nr, 0x4);
        cpu_physical_memory_read(s->reg_pages_out_addr + page_ind, &page_out_addr, 0x4);
        if(page_nr == 524160) {
            // NAND signature page
            uint8_t *page = calloc(0x100, sizeof(uint8_t));
            char *magic = "NANDDRIVERSIGN";
            memcpy(page, magic, strlen(magic));
            page[0x34] = 0x4; // length of the info

            // signature (0x43313131)
            page[0x38] = 0x31;
            page[0x39] = 0x31;
            page[0x3A] = 0x31;
            page[0x3B] = 0x43;
            printf("Will read page %d into address 0x%08x\n", page_nr, page_out_addr);
            cpu_physical_memory_write(page_out_addr, page, 0x100);
        }
        else if(page_nr == 524161) {
            // BBT page
            uint8_t *page = calloc(0x100, sizeof(uint8_t));
            char *magic = "DEVICEINFOBBT";
            memcpy(page, magic, strlen(magic));
            cpu_physical_memory_write(page_out_addr, page, 0x100);
        }

        // write the spare
        printf("Writing spare to 0x%08x\n", s->reg_page_spare_out_addr);
        uint8_t *spare = calloc(0x10, sizeof(uint8_t));
        spare[9] = 0x80;
        cpu_physical_memory_write(s->reg_page_spare_out_addr, spare, 0x10);
    }
}

static uint64_t ipod_touch_fmss_read(void *opaque, hwaddr addr, unsigned size)
{
    //fprintf(stderr, "%s: read from location 0x%08x\n", __func__, addr);

    IPodTouchFMSSState *s = (IPodTouchFMSSState *)opaque;
    switch(addr)
    {
        case FMSS__CS_BUF_RST_OK:
            return 0x1;
        case FMSS__CS_IRQ:
            return s->reg_cs_irq_bit;
        case FMSS__FMCTRL1:
            return (0x1 << 30);
        default:
            // hw_error("%s: read invalid location 0x%08x.\n", __func__, addr);
            break;
    }
    return 0;
}

static void ipod_touch_fmss_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IPodTouchFMSSState *s = (IPodTouchFMSSState *)opaque;
    fprintf(stderr, "%s: writing 0x%08x to 0x%08x\n", __func__, val, addr);

    switch(addr) {
        case 0xC00:
            if(val == 0x0000ffb5) { s->reg_cs_irq_bit = 1; } // TODO ugly and hard-coded
            break;
        case FMSS__CS_IRQ:
            if(val == 0xD) { s->reg_cs_irq_bit = 0; } // clear interrupt bit
            break;
        case FMSS_CINFO_TARGET_ADDR:
            s->reg_cinfo_target_addr = val;
            write_chip_info(s);
            break;
        case FMSS_PAGES_IN_ADDR:
            s->reg_pages_in_addr = val;
            break;
        case FMSS_NUM_PAGES:
            s->reg_num_pages = val;
            break;
        case FMSS_PAGE_SPARE_OUT_ADDR:
            s->reg_page_spare_out_addr = val;
            break;
        case FMSS_PAGES_OUT_ADDR:
            s->reg_pages_out_addr = val;
            break;
        case 0xD38:
            read_nand_pages(s);
    }
}

static const MemoryRegionOps fmss_ops = {
    .read = ipod_touch_fmss_read,
    .write = ipod_touch_fmss_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_fmss_realize(DeviceState *dev, Error **errp)
{
    
}

static void ipod_touch_fmss_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    IPodTouchFMSSState *s = IPOD_TOUCH_FMSS(dev);

    memory_region_init_io(&s->iomem, obj, &fmss_ops, s, "fmss", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void ipod_touch_fmss_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ipod_touch_fmss_realize;
}

static const TypeInfo ipod_touch_fmss_info = {
    .name          = TYPE_IPOD_TOUCH_FMSS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchFMSSState),
    .instance_init = ipod_touch_fmss_init,
    .class_init    = ipod_touch_fmss_class_init,
};

static void ipod_touch_machine_types(void)
{
    type_register_static(&ipod_touch_fmss_info);
}

type_init(ipod_touch_machine_types)