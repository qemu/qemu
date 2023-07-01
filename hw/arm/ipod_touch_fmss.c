#include "hw/arm/ipod_touch_fmss.h"

static void write_chip_info(IPodTouchFMSSState *s)
{
    uint32_t chipid[] = { 0xb614d5ad, 0xb614d5ad, 0xb614d5ad, 0xb614d5ad };
    cpu_physical_memory_write(s->reg_cinfo_target_addr, &chipid, 0x10);
}

static void read_nand_pages(IPodTouchFMSSState *s)
{
    // patch the FTL_Read method
    uint16_t *data = malloc(sizeof(uint16_t) * 9);
    data[0] = 0xB500; // push {lr}
    data[1] = 0x4B14; // ldr r3, [pc, #0x50]
    data[2] = 0x6018; // str r0, [r3]
    data[3] = 0x3304; // adds r3, #4
    data[4] = 0x6019; // str r1, [r3]
    data[5] = 0x3304; // adds r3, #4
    data[6] = 0x601A; // str r1, [r3] -> this will trigger the block loading
    data[7] = 0x2000; // movs r0, #0
    data[8] = 0xBD00; // pop {pc}
    cpu_physical_memory_write(0x0ff02d1c, (uint8_t *)data, 18);

    uint32_t *data2 = malloc(sizeof(uint32_t) * 1);
    data2[0] = 0x39400000;
    cpu_physical_memory_write(0x0ff02d1c + 0x54, (uint8_t *)data2, 4);

    // boot args
    char *boot_args = "boot-args=debug=0x8 kextlog=0xfff cpus=1 rd=disk0s1 serial=1 io=0xffff8fff";
    cpu_physical_memory_write(0x0ff2a584, boot_args, strlen(boot_args));

    for(int page_ind = 0; page_ind < s->reg_num_pages; page_ind++) {
        uint32_t page_nr = 0;
        uint32_t page_out_addr = 0;
        cpu_physical_memory_read(s->reg_pages_in_addr + page_ind, &page_nr, 0x4);
        cpu_physical_memory_read(s->reg_pages_out_addr + page_ind, &page_out_addr, 0x4);

        printf("Will read page %d into address 0x%08x and spare into address 0x%08x\n", page_nr, page_out_addr, s->reg_page_spare_out_addr);

        // prepare the page
        char filename[200];
        sprintf(filename, "/Users/martijndevos/Documents/generate_nand_it2g/nand/%d.page", page_nr);
        struct stat st = {0};
        if (stat(filename, &st) == -1) {
            printf("Will preparing empty page %d", page_nr);
            // page storage does not exist - initialize an empty buffer
            memset(s->page_buffer, 0, NAND_BYTES_PER_PAGE);
            memset(s->page_spare_buffer, 0, NAND_BYTES_PER_SPARE);
        }
        else {
            FILE *f = fopen(filename, "rb");
            if (f == NULL) { hw_error("Unable to read file!"); }
            fread(s->page_buffer, sizeof(char), NAND_BYTES_PER_PAGE, f);
            fread(s->page_spare_buffer, sizeof(char), NAND_BYTES_PER_SPARE, f);
            fclose(f);
        }

        cpu_physical_memory_write(page_out_addr, s->page_buffer, NAND_BYTES_PER_PAGE);
        cpu_physical_memory_write(s->reg_page_spare_out_addr, s->page_spare_buffer, NAND_BYTES_PER_SPARE);
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

    s->page_buffer = (uint8_t *)malloc(NAND_BYTES_PER_PAGE);
    s->page_spare_buffer = (uint8_t *)malloc(NAND_BYTES_PER_SPARE);
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