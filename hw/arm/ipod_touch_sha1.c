#include "hw/arm/ipod_touch_sha1.h"

static uint64_t swapLong(void *X) {
    uint64_t x = (uint64_t) X;
    x = (x & 0x00000000FFFFFFFF) << 32 | (x & 0xFFFFFFFF00000000) >> 32;
    x = (x & 0x0000FFFF0000FFFF) << 16 | (x & 0xFFFF0000FFFF0000) >> 16;
    x = (x & 0x00FF00FF00FF00FF) << 8  | (x & 0xFF00FF00FF00FF00) >> 8;
    return x;
}

static void flush_hw_buffer(IPodTouchSHA1State *s) {
    // Flush the hardware buffer to the state buffer and clear the buffer.
    memcpy(s->buffer + s->buffer_ind, (uint8_t *)s->hw_buffer, 0x40);
    memset(s->hw_buffer, 0, 0x40);
    s->hw_buffer_dirty = false;
    s->buffer_ind += 0x40;
}

static void sha1_reset(IPodTouchSHA1State *s)
{
	s->config = 0;
	s->memory_start = 0;
	s->memory_mode = 0;
	s->insize = 0;
	memset(&s->buffer, 0, SHA1_BUFFER_SIZE);
	memset(&s->hw_buffer, 0, 0x10 * sizeof(uint32_t));
	s->buffer_ind = 0;
	memset(&s->hashout, 0, 0x14);
	s->hw_buffer_dirty = false;
	s->hash_computed = false;
}

static uint64_t ipod_touch_sha1_read(void *opaque, hwaddr offset, unsigned size)
{
	IPodTouchSHA1State *s = (IPodTouchSHA1State *)opaque;

    //fprintf(stderr, "%s: offset 0x%08x\n", __FUNCTION__, offset);

	switch(offset) {
		case SHA_CONFIG:
			return s->config;
		case SHA_RESET:
			return 0;
		case SHA_MEMORY_START:
			return s->memory_start;
		case SHA_MEMORY_MODE:
			return s->memory_mode;
		case SHA_INSIZE:
			return s->insize;
		/* Hash result ouput */
		case 0x20 ... 0x34:
			//fprintf(stderr, "Hash out %08x\n",  *(uint32_t *)&s->hashout[offset - 0x20]);
            if(!s->hash_computed) {
                // lazy compute the final hash by inspecting the last eight bytes of the buffer, which contains the length of the input data.
                uint64_t data_length = swapLong(((uint64_t *)s->buffer)[s->buffer_ind / 8 - 1]) / 8;

                SHA_CTX ctx;
                SHA1_Init(&ctx);
                SHA1_Update(&ctx, s->buffer, data_length);
                SHA1_Final(s->hashout, &ctx);
                s->hash_computed = true;
            }

			return *(uint32_t *)&s->hashout[offset - 0x20];
	}

    return 0;
}

static void ipod_touch_sha1_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    IPodTouchSHA1State *s = (IPodTouchSHA1State *)opaque;

    //fprintf(stderr, "%s: offset 0x%08x value 0x%08x\n", __FUNCTION__, offset, value);

	switch(offset) {
		case SHA_CONFIG:
			if(value == 0x2 || value == 0xa)
			{
                if(s->hw_buffer_dirty) {
                    flush_hw_buffer(s);
                }

				if(s->memory_mode)
				{
					// we are in memory mode - gradually add the memory to the buffer
					for(int i = 0; i < s->insize / 0x40; i++) {
						cpu_physical_memory_read(s->memory_start + i * 0x40, s->buffer + s->buffer_ind, 0x40);
						s->buffer_ind += 0x40;
					}
				}
			} else {
				s->config = value;
			}
			break;
		case SHA_RESET:
			sha1_reset(s);
			break;
		case SHA_MEMORY_START:
			s->memory_start = value;
			break;
		case SHA_MEMORY_MODE:
			s->memory_mode = value;
			break;
		case SHA_INSIZE:
            assert(value <= SHA1_BUFFER_SIZE);
			s->insize = value;
			break;
		case 0x40 ... 0x7c:
            // write to the hardware buffer
            s->hw_buffer[(offset - 0x40) / 4] |= value;
            s->hw_buffer_dirty = true;
			break;
	}
}

static const MemoryRegionOps sha1_ops = {
    .read = ipod_touch_sha1_read,
    .write = ipod_touch_sha1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_touch_sha1_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    IPodTouchSHA1State *s = IPOD_TOUCH_SHA1(dev);

    memory_region_init_io(&s->iomem, obj, &sha1_ops, s, "sha1", 0x100);
    sysbus_init_mmio(sbd, &s->iomem);

    sha1_reset(s);
}

static void ipod_touch_sha1_class_init(ObjectClass *klass, void *data)
{

}

static const TypeInfo ipod_touch_sha1_info = {
    .name          = TYPE_IPOD_TOUCH_SHA1,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodTouchSHA1State),
    .instance_init = ipod_touch_sha1_init,
    .class_init    = ipod_touch_sha1_class_init,
};

static void ipod_touch_machine_types(void)
{
    type_register_static(&ipod_touch_sha1_info);
}

type_init(ipod_touch_machine_types)