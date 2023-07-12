#include "hw/arm/ipod_touch_fmss.h"

static uint32_t reverse_byte_order(uint32_t value) {
    return ((value & 0x000000FF) << 24) |
           ((value & 0x0000FF00) << 8) |
           ((value & 0x00FF0000) >> 8) |
           ((value & 0xFF000000) >> 24);
}

static void write_chip_info(IPodTouchFMSSState *s)
{
    uint32_t chipid[] = { 0xb614d5ad, 0xb614d5ad, 0xb614d5ad, 0xb614d5ad };
    cpu_physical_memory_write(s->reg_cinfo_target_addr, &chipid, 0x10);
}

static void read_nand_pages(IPodTouchFMSSState *s)
{
    // patch the FTL_Read method in iBoot
    uint16_t *data = malloc(sizeof(uint16_t) * 9);
    data[0] = 0xB500; // push {lr}
    data[1] = 0x4B14; // ldr r3, [pc, #0x50]
    data[2] = 0x6018; // str r0, [r3]
    data[3] = 0x3304; // adds r3, #4
    data[4] = 0x6019; // str r1, [r3]
    data[5] = 0x3304; // adds r3, #4
    data[6] = 0x601A; // str r2, [r3] -> this will trigger the block loading
    data[7] = 0x2000; // movs r0, #0
    data[8] = 0xBD00; // pop {pc}
    cpu_physical_memory_write(0x0ff02d1c, (uint8_t *)data, 18);

    uint32_t *block_driver_addr = malloc(sizeof(uint32_t) * 1);
    block_driver_addr[0] = 0x38A00F00;
    cpu_physical_memory_write(0x0ff02d1c + 0x54, (uint8_t *)block_driver_addr, 4);

    // patch the FTL_Read method in the kernel (which is not in thumb)
    uint32_t *data2 = malloc(sizeof(uint32_t) * 20);
    data2[0] = reverse_byte_order(0xf0402de9); // push on stack

    // store the block number and number of blocks in the block driver
    data2[1] = reverse_byte_order(0x50309FE5); // ldr r3, [pc, #0x50]
    data2[2] = reverse_byte_order(0x000083E5); // str r0, [r3]
    data2[3] = reverse_byte_order(0x043093E2); // adds r3, #4
    data2[4] = reverse_byte_order(0x001083E5); // str r1, [r3]
    data2[5] = reverse_byte_order(0x043093E2); // adds r3, #4

    // get the physical address of the current io_buffer of the NANDFTL
    data2[6] = reverse_byte_order(0x0500A0E1); // mov r0,r5 -> load the AppleNANDFTL object, which seems to reside in r5
    data2[7] = reverse_byte_order(0x001190E5); // ldr r1, [r0,#0x100] -> load the io_buffer object
    data2[8] = reverse_byte_order(0x002091E5); // ldr r2,[r1,#0x0] -> load the vtable of the io_buffer
    data2[9] = reverse_byte_order(0x0100A0E1); // mov r0,r1 -> make the io_buffer object the first parameter
    data2[10] = reverse_byte_order(0x0010A0E3); // mov r1,#0 -> set the second parameter to 0
    data2[11] = reverse_byte_order(0x0340A0E1); // mov r4,r3 -> make sure our block driver address is safe because r3 will be overridden
    data2[12] = reverse_byte_order(0x0FE0A0E1); // mov lr,pc
    data2[13] = reverse_byte_order(0x98F092E5); // ldr pc,[r2,#0x98] -> jump to the getPhysicalSegment function

    // store the address
    data2[14] = reverse_byte_order(0x000084E5); // str r0, [r4] -> this will trigger the block loading

    // return
    data2[15] = reverse_byte_order(0x0000B0E3); // movs r0, #0
    data2[16] = reverse_byte_order(0xf080bde8); // pop from stack

    cpu_physical_memory_write(0x858fdd4, (uint8_t *)data2, sizeof(uint32_t) * 17);

    // aux data
    uint32_t *aux_data = malloc(sizeof(uint32_t) * 20);
    aux_data[0] = 0xE366aF00; // virtual address of the block driver
    // aux_data[1] = 0xc0db0640; // NAND FTL object
    cpu_physical_memory_write(0x858fdd4 + 0x5C, (uint8_t *)aux_data, 4);

    // boot args
    char *boot_args = "kextlog=0xfff debug=0x8 cpus=1 rd=disk0s1 serial=1 io=0xffff8fff debug-usb=0xffffffff";
    cpu_physical_memory_write(0x0ff2a584, boot_args, strlen(boot_args));

    for(int page_ind = 0; page_ind < s->reg_num_pages; page_ind++) {
        uint32_t page_nr = 0;
        uint32_t page_out_addr = 0;
        cpu_physical_memory_read(s->reg_pages_in_addr + page_ind, &page_nr, 0x4);
        cpu_physical_memory_read(s->reg_pages_out_addr + page_ind, &page_out_addr, 0x4);

        //printf("Will read page %d into address 0x%08x and spare into address 0x%08x\n", page_nr, page_out_addr, s->reg_page_spare_out_addr);

        // prepare the page
        char filename[200];
        sprintf(filename, "/Users/martijndevos/Documents/generate_nand_it2g/nand/%d.page", page_nr);
        struct stat st = {0};
        if (stat(filename, &st) == -1) {
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
    fprintf(stderr, "%s: writing 0x%08x to 0x%08x\n", __func__, val, addr);

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