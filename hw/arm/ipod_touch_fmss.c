#include "hw/arm/ipod_touch_fmss.h"

static uint32_t reverse_byte_order(uint32_t value) {
    return ((value & 0x000000FF) << 24) |
           ((value & 0x0000FF00) << 8) |
           ((value & 0x00FF0000) >> 8) |
           ((value & 0xFF000000) >> 24);
}

static uint8_t find_bit_index(uint8_t num) {
    int index = 0;
    while (num > 1) {
        num >>= 1;
        index++;
    }
    return index;
}

static void write_chip_info(IPodTouchFMSSState *s)
{
    uint32_t chipid[] = { 0xb614d5ad, 0xb614d5ad, 0xb614d5ad, 0xb614d5ad };
    cpu_physical_memory_write(s->reg_cinfo_target_addr, &chipid, 0x10);
}

static void dump_registers(IPodTouchFMSSState *s) {
    printf("FMSS_PAGES_IN_ADDR: 0x%08x\n", s->reg_pages_in_addr);
    printf("FMSS_CS_BUF_ADDR: 0x%08x\n", s->reg_cs_buf_addr);
    printf("FMSS_NUM_PAGES: 0x%08x\n", s->reg_num_pages);
}

static void read_nand_pages(IPodTouchFMSSState *s)
{
    // boot args
    const char *boot_args = "kextlog=0xfff debug=0x8 cpus=1 rd=disk0s1 serial=1 pmu-debug=0x1 io=0xffff8fff debug-usb=0xffffffff"; // if not const then overwritten
    cpu_physical_memory_write(0x0ff2a584, boot_args, strlen(boot_args));

    int page_out_buf_ind = 0;
    //dump_registers(s);
    //printf("Start CMD...\n");
    for(int page_ind = 0; page_ind < s->reg_num_pages; page_ind++) {
        uint32_t page_nr = 0;
        uint32_t page_out_addr = 0;
        uint32_t cs = 0;
        cpu_physical_memory_read(s->reg_pages_in_addr + (page_ind * sizeof(uint32_t)), &page_nr, sizeof(uint32_t));
        cpu_physical_memory_read(s->reg_cs_buf_addr + (page_ind * sizeof(uint32_t)), &cs, sizeof(uint32_t));
        uint32_t og_cs = cs;
        cs = find_bit_index(cs);

        if(cs > 3) {
            printf("CS %d invalid! (og CS: %d, reading page %d)\n", cs, og_cs, page_nr);
            dump_registers(s);
            hw_error("CS %d invalid!", cs);
        }

        // prepare the page
        char filename[200];
        sprintf(filename, "/Users/martijndevos/Documents/generate_nand_it2g/nand/cs%d/%d.page", cs, page_nr);
        struct stat st = {0};
        if (stat(filename, &st) == -1) {
            // page storage does not exist - initialize an empty buffer
            memset(s->page_buffer, 0, NAND_BYTES_PER_PAGE);
            memset(s->page_spare_buffer, 0, NAND_BYTES_PER_SPARE);

            uint32_t *buf_cst = (uint32_t *) s->page_spare_buffer;
            buf_cst[2] = 0x00FF00FF;
        }
        else {
            FILE *f = fopen(filename, "rb");
            if (f == NULL) { hw_error("Unable to read file!"); }
            fread(s->page_buffer, sizeof(char), NAND_BYTES_PER_PAGE, f);
            fread(s->page_spare_buffer, sizeof(char), NAND_BYTES_PER_SPARE, f);
            fclose(f);
        }

        // we write away the page in two parts, 2048 bytes first and then the other 2048 bytes.
        int write_buf_size = NAND_BYTES_PER_PAGE / 2;
        for(int i = 0; i < 2; i++) {
            cpu_physical_memory_read(s->reg_pages_out_addr + (page_out_buf_ind * sizeof(uint32_t)), &page_out_addr, sizeof(uint32_t));
            cpu_physical_memory_write(page_out_addr, s->page_buffer + i * write_buf_size, write_buf_size);
            //printf("Will read page %d @ cs %d into address 0x%08x and spare into address 0x%08x\n", page_nr, cs, page_out_addr, s->reg_page_spare_out_addr);
            page_out_buf_ind++;

        }

        // finally, write the spare
        cpu_physical_memory_write(s->reg_page_spare_out_addr + page_ind * 0xc, s->page_spare_buffer, NAND_BYTES_PER_SPARE);
    }
}

static uint64_t ipod_touch_fmss_read(void *opaque, hwaddr addr, unsigned size)
{
    fprintf(stderr, "%s: read from location 0x%08x\n", __func__, addr);

    IPodTouchFMSSState *s = (IPodTouchFMSSState *)opaque;
    switch(addr)
    {
        case FMSS__CS_BUF_RST_OK:
            return 0x1;
        case FMSS__CS_IRQ:
            return s->reg_cs_irq_bit;
        case FMSS__CS_IRQMASK:
            return 0x1;
        case FMSS__FMCTRL1:
            return (0x1 << 30);
        case 0xD00:
            return 42;
        default:
            // hw_error("%s: read invalid location 0x%08x.\n", __func__, addr);
            break;
    }
    return 0;
}

static void ipod_touch_fmss_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IPodTouchFMSSState *s = (IPodTouchFMSSState *)opaque;
    //printf("%s: writing 0x%08x to 0x%08x\n", __func__, val, addr);

    switch(addr) {
        case 0xC00:
            if(val == 0x0000ffb5) { s->reg_cs_irq_bit = 1; } // TODO ugly and hard-coded
            if(val == 0xfff5) { s->reg_cs_irq_bit = 1; qemu_set_irq(s->irq, 1); }
            break;
        case FMSS__CS_IRQ:
            s->reg_cs_irq_bit = 0;
            qemu_set_irq(s->irq, 0);
            break;
        case FMSS_CINFO_TARGET_ADDR:
            s->reg_cinfo_target_addr = val;
            write_chip_info(s);
            break;
        case FMSS_PAGES_IN_ADDR:
            s->reg_pages_in_addr = val;
            break;
        case FMSS_CS_BUF_ADDR:
            s->reg_cs_buf_addr = val;
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
        case FMSS_CSGENRC:
            s->reg_csgenrc = val;
            break;
        case 0xD38:
            if(s->reg_csgenrc == 0xa01) { read_nand_pages(s); }
            else { printf("NAND write not suported yet!\n"); }
            
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

    memory_region_init_io(&s->iomem, obj, &fmss_ops, s, "fmss", 0xF00);
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
