#include "hw/arm/ipod_touch_block_device.h"
#include "hw/hw.h"

static uint64_t ipod_touch_block_device_read(void *opaque, hwaddr addr, unsigned size)
{
    fprintf(stderr, "%s: read from location 0x%08x\n", __func__, addr);

    IPodTouchBlockDeviceState *s = (IPodTouchBlockDeviceState *)opaque;
    switch(addr)
    {
        default:
            // hw_error("%s: read invalid location 0x%08x.\n", __func__, addr);
            break;
    }
    return 0;
}

static void ipod_touch_block_device_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IPodTouchBlockDeviceState *s = (IPodTouchBlockDeviceState *)opaque;
    fprintf(stderr, "%s: writing 0x%08x to 0x%08x\n", __func__, val, addr);

    switch(addr)
    {
        case 0x0:
            s->block_num_reg = val;
            break;
        case 0x4:
            s->num_blocks_reg = val;
            break;
        case 0x8:
            s->out_addr_reg = val;
            break;
        default:
            // hw_error("%s: read invalid location 0x%08x.\n", __func__, addr);
            break;
    }

    if(addr == 0x8) {
        // load the block
        printf("Will load block %d (count: %d) into address 0x%08x\n", s->block_num_reg, s->num_blocks_reg, s->out_addr_reg);

        for(int block_nr = 0; block_nr < s->num_blocks_reg; block_nr++) {
            char filename[200];
            int block_to_read = s->block_num_reg + block_nr;
            sprintf(filename, "/Users/martijndevos/Documents/generate_nand_it2g/blocks/%d.blk", block_to_read);
            struct stat st = {0};
            if (stat(filename, &st) == -1) {
                printf("Will preparing empty block %d", block_to_read);
                // page storage does not exist - initialize an empty buffer
                memset(s->block_buffer, 0, BYTES_PER_BLOCK);
            }
            else {
                FILE *f = fopen(filename, "rb");
                if (f == NULL) { hw_error("Unable to read file!"); }
                fread(s->block_buffer, sizeof(char), BYTES_PER_BLOCK, f);
                fclose(f);
            }

            cpu_physical_memory_write(s->out_addr_reg + block_nr * BYTES_PER_BLOCK, s->block_buffer, BYTES_PER_BLOCK);
        }
    }
}

static const MemoryRegionOps block_device_ops = {
    .read = ipod_touch_block_device_read,
    .write = ipod_touch_block_device_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_block_device_realize(DeviceState *dev, Error **errp)
{
    
}

static void ipod_touch_block_device_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    IPodTouchBlockDeviceState *s = IPOD_TOUCH_BLOCK_DEVICE(dev);

    memory_region_init_io(&s->iomem, obj, &block_device_ops, s, "block_device", 0x100);
    sysbus_init_mmio(sbd, &s->iomem);

    s->block_buffer = (uint8_t *)malloc(BYTES_PER_BLOCK);
}

static void ipod_touch_block_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ipod_touch_block_device_realize;
}

static const TypeInfo ipod_touch_block_device_info = {
    .name          = TYPE_IPOD_TOUCH_BLOCK_DEVICE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchBlockDeviceState),
    .instance_init = ipod_touch_block_device_init,
    .class_init    = ipod_touch_block_device_class_init,
};

static void ipod_touch_machine_types(void)
{
    type_register_static(&ipod_touch_block_device_info);
}

type_init(ipod_touch_machine_types)