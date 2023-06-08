/* AUTHOR:       Matteo Vidali
 * AUTHOR EMAIL: mvidali@iu.edu - mmvidali@gmail.com
 * 
 * DESC:
 *   Written for the Indiana University Course E315 as an AUTOGRADER tool
 * This hardware device acts like the hardware on the PYNQ-Zynq7000 board
 * with a popcount bitstream.
 * It contains two memory regions (reset, popcount) which are responsible for 
 * computing popcount. It wiil be established in the arm-virt machine, as a
 * UIO device. There will be a corresponding kernel module present for this
 * device as well.
 *
 * For questions regarding design or maintenance of this software, contact
 * Matteo Vidali at the email address listed above, or Dr. Andrew Lukefahr at 
 * whatever email or phone number you can find.
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "chardev/char.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/popcount/popcount.h"

/* Read Callback for the popcount function */
static uint64_t pop_read(void *opaque, hwaddr addr, unsigned int size)
{
    /* Aquiring state */
    popState *s = opaque;

    /* Log guest error for debugging */
    qemu_log_mask(LOG_GUEST_ERROR, "%s: read: addr=0x%x size=%d\n",
                  __func__, (int)addr,size);

    /* Return the current bitcount */
    return s->bitcount;
}

/* Actual popcount computation */
static uint32_t popcount(uint32_t val){
    uint32_t count;
    for (count=0; val; count++){
        val &= val - 1;
    }
    return count; 
}

/* Write callback for main popcount function */
static void pop_write(void *opaque, hwaddr addr, uint64_t val64, unsigned int size)
{
    popState *s = opaque;
    uint32_t value = val64; /*this line is for full correctness - uint32_t*/
    (void)s;

    s->write_reg = value;
    s->bitcount += popcount(value);

    qemu_log_mask(LOG_GUEST_ERROR, "%s: write: addr=0x%x v=0x%x\n",
                  // __func__, (int)addr, (int)value);
}


/* Initializes the write register */
static void write_reg_init(popState *s){
    s->write_reg = 0;
    s->bitcount = 0;
}

/* Dummy function for reading the reset register - simply logs guest error*/
static uint64_t r_read(void *opaque, hwaddr addr, unsigned int size){
    qemu_log_mask(LOG_GUEST_ERROR, "READING RESET IS NOT USEFUL");
    return 0;
}

/* Reset Callback - resets the count and write register */
static void r_write(void *opaque, hwaddr addr, uint64_t val64, unsigned int size){
    popState *s = opaque;
    uint32_t value = val64;
    (void)s;

    if (value != 0){
        s->bitcount = 0;
        s->write_reg = 0;
    } 

}

/* Memory operation binding for popcount region */
static const MemoryRegionOps pop_ops = {
    .read = pop_read,
    .write = pop_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1*sizeof(int),
        .max_access_size = 2*sizeof(int)}
};

/* Memory operation binding for reset region */
static const MemoryRegionOps r_ops = {
    .read = r_read,
    .write = r_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4}
};

/* Initialization of the popcount hardware DEVICE_NATIVE_ENDIAN
 * May be desirable to make this of type void in the future to match 
 * QEMU hw device protocol more typically
 */
popState *popcount_create(MemoryRegion *address_space, hwaddr base)
{
    popState *s = g_malloc0(sizeof(popState));
    write_reg_init(s);
    memory_region_init_io(&s->reset, NULL, &r_ops, s, TYPE_POPCOUNT, 4);
    memory_region_init_io(&s->mmio, NULL, &pop_ops, s, TYPE_POPCOUNT, 32);
    memory_region_add_subregion(address_space, base, &s->reset);
    memory_region_add_subregion(address_space, base+4, &s->mmio);
    return s;
}

