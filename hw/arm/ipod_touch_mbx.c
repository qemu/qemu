#include "hw/arm/ipod_touch_mbx.h"

static uint32_t reverse_byte_order(uint32_t value) {
    return ((value & 0x000000FF) << 24) |
           ((value & 0x0000FF00) << 8) |
           ((value & 0x00FF0000) >> 8) |
           ((value & 0xFF000000) >> 24);
}

static uint64_t ipod_touch_mbx1_read(void *opaque, hwaddr addr, unsigned size)
{
    printf("%s: read from location 0x%08x\n", __func__, addr);
    switch(addr)
    {
        case 0x12c:
            return 0x40;
        case 0xf00:
            return (2 << 0x10) | (1 << 0x18); // seems to be some kind of identifier
        case 0x1020:
            return 0x10000;
        default:
            break;
    }
    return 0;
}

static void ipod_touch_mbx1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IPodTouchMBXState *s = (IPodTouchMBXState *)opaque;
    fprintf(stderr, "%s: writing 0x%08x to 0x%08x\n", __func__, val, addr);
}

static void patch_kernel()
{
    // patch the loading of the AppleBCM4325 driver
    char *bcm4325_vars = "test";

    // write the pointer to our custom subroutine
    uint32_t *data = malloc(sizeof(uint32_t) * 200);
    data[0] = 0xC0460000;
    cpu_physical_memory_write(0x8324aa8, (uint8_t *)data, sizeof(uint32_t) * 1);

    // create the call to the subroutine
    data = malloc(sizeof(uint32_t) * 200);
    data[0] = reverse_byte_order(0x0640A0E1); // mov r4, r6
    data[1] = reverse_byte_order(0x9C309FE5); // ldr r3, [pc, #0x9c]
    data[2] = reverse_byte_order(0x33FF2FE1); // blx r3
    cpu_physical_memory_write(0x8324a00, (uint8_t *)data, sizeof(uint32_t) * 3);

    // fill in the driver load subroutine
    data = malloc(sizeof(uint32_t) * 200);
    data[0] = reverse_byte_order(0xFE402DE9); // push on stack

    // create the OSData object containing the BCM4325Vars string
    data[1] = reverse_byte_order(0xF4009FE5); // ldr r0, [pc, #0xf4]
    data[2] = reverse_byte_order(0x2010B0E3); // movs r1, #0x20
    data[3] = reverse_byte_order(0xF0309FE5); // ldr r3, [pc, #0xf0]
    data[4] = reverse_byte_order(0x33FF2FE1); // blx r3 <- calling OSData::withBytes
    data[5] = reverse_byte_order(0x0050A0E1); // mov r5, r0 (to save the created object)

    // create the OSDictionary object that we will pass to AppleBCM4325::init
    data[6] = reverse_byte_order(0x0100B0E3); // movs r0, #0x1
    data[7] = reverse_byte_order(0xE4309FE5); // ldr r3, [pc, #0xe4]
    data[8] = reverse_byte_order(0x33FF2FE1); // blx r3 <- calling OSDictionary::withCapacity
    data[9] = reverse_byte_order(0x0060A0E1); // mov r6, r0 (to save the created object)

    // call OSDictionary::setObject
    data[10] = reverse_byte_order(0x00C096E5); // ldr r12, [r6, #0x0] (get the vtable of the OSDictionary)
    data[11] = reverse_byte_order(0xD8109FE5); // ldr r1, [pc, #0xd8]
    data[12] = reverse_byte_order(0x0520A0E1); // mov r2, r5
    data[13] = reverse_byte_order(0x0FE0A0E1); // mov lr, pc
    data[14] = reverse_byte_order(0x98F09CE5); // ldr pc, [r12, #0x98]

    // create the AppleBCM4325 object
    data[15] = reverse_byte_order(0x010AB0E3); // movs r0, #0x1000
    data[16] = reverse_byte_order(0xC8309FE5); // ldr r3, [pc, #0xc8]
    data[17] = reverse_byte_order(0x33FF2FE1); // blx r3 <- calling OSObject::operator.new
    data[18] = reverse_byte_order(0x0050A0E1); // mov r5, r0 (to save the new object somewhere)

    // call AppleBCM4325 object initialization
    data[19] = reverse_byte_order(0xC0309FE5); // ldr r3, [pc, #0xc0]
    data[20] = reverse_byte_order(0x33FF2FE1); // blx r3

    // call the IONetworkController metaclass initialization
    data[21] = reverse_byte_order(0x0100B0E3); // movs r0, #0x1
    data[22] = reverse_byte_order(0xB8109FE5); // ldr r1, [pc, #0xb8]
    data[23] = reverse_byte_order(0xB8209FE5); // ldr r2, [pc, #0xb8]
    data[24] = reverse_byte_order(0x32FF2FE1); // blx r2

    // load the "com.apple.driver.AppleBCM4325" kext
    data[25] = reverse_byte_order(0xB4009FE5); // ldr r0, [pc, #0xb4]
    data[26] = reverse_byte_order(0x0110B0E3); // movs r1, #0x1
    data[27] = reverse_byte_order(0xB0209FE5); // ldr r2, [pc, #0xd8]
    data[28] = reverse_byte_order(0x32FF2FE1); // blx r2

    // call AppleBCM4325::init
    data[29] = reverse_byte_order(0x00C095E5); // ldr r12, [r5, #0x0] (get the vtable)
    data[30] = reverse_byte_order(0x0500A0E1); // mov r0, r5
    data[31] = reverse_byte_order(0x0610A0E1); // mov r1, r6
    data[32] = reverse_byte_order(0x0FE0A0E1); // mov lr, pc
    data[33] = reverse_byte_order(0x6CF09CE5); // ldr pc, [r12, #0x6c]

    // call AppleBCM4325::start
    data[34] = reverse_byte_order(0x00C095E5); // ldr r12, [r5, #0x0] (get the vtable)
    data[35] = reverse_byte_order(0x0500A0E1); // mov r0, r5
    data[36] = reverse_byte_order(0x0410A0E1); // mov r1, r4
    data[37] = reverse_byte_order(0x0FE0A0E1); // mov lr, pc
    data[38] = reverse_byte_order(0x78F19CE5); // ldr pc, [r12, #0x178]

    data[39] = reverse_byte_order(0xFE80BDE8); // pop from stack

    cpu_physical_memory_write(0x8460000, (uint8_t *)data, sizeof(uint32_t) * 50);

    // write the data section of the driver load subroutine (0x100 items from the start of the subroutine)
    data = malloc(sizeof(uint32_t) * 200);
    data[0] = 0xc0460200; // the address of the BCM4325Vars string
    data[1] = 0xc013c373; // the address of OSData::withBytes
    data[2] = 0xc013cc3d; // the address of OSDictionary::withCapacity
    data[3] = 0xc03467bc; // the "BCM4325Vars" string
    data[4] = 0xc013ad8d; // the address of OSObject::operator.new
    data[5] = 0xc032c294; // the object initialization method of AppleBCM4325
    data[6] = 0xffff; // the 2nd parameter for the call to the IONetworkController metaclass initialization
    data[7] = 0xc02f94f9; // the initialization method of the IONetworkController metaclass
    data[8] = 0xc038a320; // the "com.apple.driver.AppleBCM4325" string
    data[9] = 0xc015de01; // the kmod_load_request method

    cpu_physical_memory_write(0x8460100, (uint8_t *)data, sizeof(uint32_t) * 10);

    // write the BCM4325Vars string
    cpu_physical_memory_write(0x8460200, (uint8_t *)bcm4325_vars, strlen(bcm4325_vars));
}

static uint64_t ipod_touch_mbx2_read(void *opaque, hwaddr addr, unsigned size)
{
    printf("%s: read from location 0x%08x\n", __func__, addr);
    switch(addr)
    {
        case 0xC:
            patch_kernel();
        default:
            break;
    }
    return 0;
}

static void ipod_touch_mbx2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IPodTouchMBXState *s = (IPodTouchMBXState *)opaque;
    fprintf(stderr, "%s: writing 0x%08x to 0x%08x\n", __func__, val, addr);
}

static const MemoryRegionOps ipod_touch_mbx1_ops = {
    .read = ipod_touch_mbx1_read,
    .write = ipod_touch_mbx1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps ipod_touch_mbx2_ops = {
    .read = ipod_touch_mbx2_read,
    .write = ipod_touch_mbx2_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_mbx_init(Object *obj)
{
    IPodTouchMBXState *s = IPOD_TOUCH_MBX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem1, obj, &ipod_touch_mbx1_ops, s, TYPE_IPOD_TOUCH_MBX, 0x1000000);
    sysbus_init_mmio(sbd, &s->iomem1);
    memory_region_init_io(&s->iomem2, obj, &ipod_touch_mbx2_ops, s, TYPE_IPOD_TOUCH_MBX, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem2);
}

static void ipod_touch_mbx_class_init(ObjectClass *klass, void *data)
{
    
}

static const TypeInfo ipod_touch_mbx_type_info = {
    .name = TYPE_IPOD_TOUCH_MBX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchMBXState),
    .instance_init = ipod_touch_mbx_init,
    .class_init = ipod_touch_mbx_class_init,
};

static void ipod_touch_mbx_register_types(void)
{
    type_register_static(&ipod_touch_mbx_type_info);
}

type_init(ipod_touch_mbx_register_types)