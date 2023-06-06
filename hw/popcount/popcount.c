#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "chardev/char.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/popcount/popcount.h"

// These read and write functions are currently unused...
static uint64_t pop_read(void *opaque, hwaddr addr, unsigned int size)
{
    // Aquiring state
    popState *s = opaque;

    // Log guest error for debugging
    qemu_log_mask(LOG_GUEST_ERROR, "%s: read: addr=0x%x size=%d\n",
                  __func__, (int)addr,size);

    // Return the current bitcount
    return s->bitcount;
}

static uint32_t popcount(uint32_t val){
    uint32_t count;
    for (count=0; val; count++){
        val &= val - 1;
    }
    return count; 
}

static void pop_write(void *opaque, hwaddr addr, uint64_t val64, unsigned int size)
{
    /*dummy code for future development*/
    popState *s = opaque;
    uint32_t value = val64;
    unsigned char ch = value;
    (void)s;
    (void)ch;
    s->write_reg = value;

    s->bitcount += popcount(value);
    qemu_log_mask(LOG_GUEST_ERROR, "%s: write: addr=0x%x v=0x%x\n",
                  __func__, (int)addr, (int)value);
}

//static void reset_w(void *opaque, hwaddr, uint64_t val64, unsigned int size){
//    popState *s = opaque;
//    uint32_t value = val64;
//    s->reset_reg = value;
//    if (s->reset_reg == 1){
//        s->bitcount = 0;
//        s->write_reg = 0;
//        s->reset_reg = 0;
//    }
//}

// Initializes the write register
static void write_reg_init(popState *s){
    s->write_reg = 0;
    s->bitcount = 0;
}

static const MemoryRegionOps pop_ops = {
    .read = pop_read,
    .write = pop_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1*sizeof(int),
        .max_access_size = 2*sizeof(int)}
};

popState *popcount_create(MemoryRegion *address_space, hwaddr base)
{
    popState *s = g_malloc0(sizeof(popState));
    write_reg_init(s);
    memory_region_init_io(&s->mmio, NULL, &pop_ops, s, TYPE_POPCOUNT, 32);
    memory_region_add_subregion(address_space, base, &s->mmio);
    return s;
}

