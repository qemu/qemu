/*
 * w1-ds18s20.c
 *
 * Copyright: Michel Pollet <buserror@gmail.com>
 *
 * QEMU Licence
 */

/*
 * This device will behave like a DS18S20 onewire temperature sensor,
 * Linux's w1-gpio driver will be fooled into talking to it.
 *
 * w1-gpio (and other onewire masters?) attempts to fudge the bit timing
 * to try to adapt to 'bad' wires, most real hardware sensors must have a
 * PLL of sort are seems to be able to adapt, this implementation doesn't
 * and 'sometime' drops from the bus for a short time, in a way it's
 * rather nice as it simulates a moderately bad wire.
 *
 * To instantiate this driver, you just need one IRQ in and out, there
 * is a second input IRQ to set the temperature. A nice 'todo' would
 * possibly to have a monitor command to do so.
 *
 * Another nice todo would possibly be to handle a proper qemu 'bus'
 * and have a way to specify the hardware ID of the device.
 *
 * Example instantiation for this device:
    {
        DeviceState * dev = sysbus_create_simple("ds18s20", -1, 0);

        qdev_connect_gpio_out(gpio, GPIO_W1, qdev_get_gpio_in(dev, 0));
        qdev_connect_gpio_out(dev, 0, qdev_get_gpio_in(gpio, GPIO_W1));
    }
 * Test case (assuming your w1-gpio knows it's GPIO from a .dts):
    / # modprobe w1-therm
    / # modprobe w1-gpio
    / # cat /sys/bus/w1/devices/28-deadbeeff00d/w1_slave
    50 05 8d e0 ff fd 03 40 14 : crc=cb NO
    00 00 00 00 00 00 00 00 00 t=85000
    / # cat /sys/bus/w1/devices/28-deadbeeff00d/w1_slave
    50 05 0d f0 7f ff 00 10 45 : crc=45 YES
    50 05 0d f0 7f ff 00 10 45 t=85000
 *
 */
#include "hw/sysbus.h"


#define D(w)

typedef struct OneWireDevice {
    SysBusDevice busdev;
    MemoryRegion dummy_iomem;

    qemu_irq    out;

    int         current_temp_mc;    // in millicelcius
    uint64_t    w1_id;              // full w1 ID, including CRC
    uint64_t    w1_id_received;     // for comparisons
    int         muted;              // set to 1 when 'offline' awaiting start
    int         addr_bit;           // current address bit sent/received

    int64_t     stamp;              // timestamp of last low edge

    uint8_t     write_buffer;       // incoming bits from master
    int         write_count;
    uint64_t    read_buffer;        // outgoing bits to master
    int         read_count;
    uint8_t     read_crc;           // CRC, for scratchpad
    uint8_t     command;            // current command
} OneWireDevice;

enum {
    W1_CMD_SEARCH_ROM = 0xf0,
    W1_CMD_MATCH_ROM = 0x55,
    W1_CMD_SKIP_ROM = 0xcc,
    W1_CMD_READ_PSU = 0xb4,
    W1_CMD_CONVERT_TEMP = 0x44,
    W1_CMD_READ_SCRATCHPAD = 0xbe,
    // MISSING "write scratchpad", unused in linux
};

/* CRC bits here were nicked from linux's */
static uint8_t w1_crc8_table[] = {
    0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65,
    157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220,
    35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
    190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255,
    70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7,
    219, 133, 103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
    101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36,
    248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185,
    140, 210, 48, 110, 237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
    17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49, 178, 236, 14, 80,
    175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82, 176, 238,
    50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
    202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139,
    87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22,
    233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
    116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53
};

static uint8_t w1_calc_crc8(uint8_t * data, int len)
{
    uint8_t crc = 0;
    while (len--)
        crc = w1_crc8_table[crc ^ *data++];
    return crc;
}

static uint8_t w1_calc_crc_le(uint64_t n, int len)
{
    int i;
    uint8_t w1_id[8];
    for (i = 0; i < len; i++) {
        w1_id[i] = n >> (i * 8);
    }
    return w1_calc_crc8(w1_id, len);
}

static void w1_make_id(OneWireDevice * w, uint64 unique)
{
    uint8_t crc = w1_calc_crc_le(unique, 7);
    w->w1_id = (unique & ~0xff00000000000000) | ((uint64_t)crc << 56);
    D(printf("%s start id %016llx\n", __func__,
            (long long int)w->w1_id);)
}

static void w1_receive(void *opaque, int irq, int level)
{
    OneWireDevice * w = (OneWireDevice *)opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t duration = (now - w->stamp) / SCALE_US;

    /* the IRQ 0 is for signalling, the second IRQ is to allow external
     * code to set the temperature the sensor will return */
    if (irq == 1) {
        w->current_temp_mc = level;
        return;
    }
    if (level == 0) {
        w->stamp = now;
        qemu_irq_raise(w->out);
        return;
    }

    /*
     * First detect whether this is a reset, a read 0/1 or a write 0/1
     * These timings are straight from the datasheet, however, the
     * master driver has a tendency to vary the timings, presumably
     * to allow bad wires and so on.
     */
    if (duration >= 480) {              // init sequence
        D(printf("%s init pulse %d us\n", __func__, (int)duration);)
        w->command = 0;
        qemu_irq_lower(w->out);
        w->write_count = w->read_count = 0;
        w->write_buffer = w->read_buffer = 0;
        w->muted = 0;
    } else if (!w->muted) {
        if (duration > 40) {         // write 0 slot
            w->write_buffer >>= 1;
            w->write_count++;
        } else if (duration > 1) {
            if (w->read_count) {            // read bit slot
                qemu_set_irq(w->out, w->read_buffer & 1);
                w->read_buffer >>= 1;
                w->read_count--;
            } else {                        // write 1 slot
                w->write_buffer = (w->write_buffer >> 1) | 0x80;
                w->write_count++;
            }
        }
    }

    /*
     * If we've received 8 bits, check to see if we're in command
     * mode, and start that command processing.
     */
    if (w->write_count == 8) {
    //    printf("%s BYTE %02x\n", __func__, w->write_buffer);
        w->command = w->write_buffer;
        w->write_count = 0;
        switch (w->command) {
            case W1_CMD_SEARCH_ROM:
                w->addr_bit = 0;
                w->read_count = 0;
                w->w1_id_received = 0;
            //    printf("%s SEARCH_ROM start id %016llx\n", __func__,
            //            (long long int)w->w1_id);
                break;
            case W1_CMD_MATCH_ROM:
                w->addr_bit = 0;
                w->read_count = 0;
                w->w1_id_received = 0;
                D(printf("%s MATCH_ROM start id %016llx\n", __func__,
                        (long long int)w->w1_id);)
                break;
            case W1_CMD_SKIP_ROM:   // it's like we match. w00t
                w->w1_id_received = w->w1_id;
                w->muted = 0;
                D(printf("%s SKIP_ROM\n", __func__);)
                break;
            case W1_CMD_READ_PSU:
                w->read_count = 1;
                w->read_buffer = 1; // 0: parasite power, 1: power pin
                D(printf("%s READ_PSU\n", __func__);)
                break;
            case W1_CMD_CONVERT_TEMP:
                w->read_count = 3;
                w->read_buffer = 0x4; // send 2 busy and 1 'done' bit, for a laugh
                D(printf("%s CONVERT_TEMP\n", __func__);)
                break;
            case W1_CMD_READ_SCRATCHPAD:
                w->read_count = 64;
                w->read_buffer = 0x1000ff7ff00d0000 +
                        (((w->current_temp_mc * 0x7d0) / 125000) & 0xffff);
                w->read_crc = w1_calc_crc_le(w->read_buffer, 8);
                D(printf("%s READ_SCRATCHPAD %016llx crc %02x\n", __func__,
                        (long long int)w->read_buffer, w->read_crc);)
                break;
            default:
                D(printf("%s unknown w1 command code %02x\n", __func__, w->command);)
                break;
        }
        w->write_buffer = 0;
    }
    /*
     * Ongoing commands are processed here
     */
    switch (w->command) {
        case W1_CMD_SEARCH_ROM:
            // still transmitting an address bit
            if (w->read_count > 0) {
                break;
            }
            // if we have received the ack bit, OR it is the first bit,
            // then try to send another one until we're done
            if (w->write_count == 1 || w->addr_bit == 0) {
                if (w->write_count) {
                    w->w1_id_received = (w->w1_id_received >> 1) |
                            (((uint64_t)w->write_buffer >> 7) << 63);
                    if ((w->write_buffer >> 7) !=
                            (int)((w->w1_id >> (w->addr_bit-1)) & 1)) {
                     //   printf("%s NOT MATCHED bit %d muting\n", __func__, w->addr_bit-1);
                        w->command = 0;
                        w->muted = 1;
                        break;
                    }
                    w->write_buffer = 0;
                    w->write_count = 0;
                    if (w->addr_bit == 64) {
                        D(printf("%s SEARCH_ROM done %016llx / %016llx\n", __func__,
                                (long long int)w->w1_id_received,
                                (long long int)w->w1_id);)
                        w->command = 0;
                        break;
                    }
                }
                uint8_t bit = (w->w1_id >> w->addr_bit) & 1;
                w->read_buffer = bit | (!bit << 1);
                w->read_count = 2;
                w->addr_bit++;
            }
            break;
        case W1_CMD_MATCH_ROM:
            if (w->write_count) {
                w->w1_id_received = (w->w1_id_received >> 1) |
                        (((uint64_t)w->write_buffer >> 7) << 63);
                w->write_buffer = 0;
                w->write_count = 0;
                if (w->addr_bit == 64) {
                    D(printf("%s W1_CMD_MATCH_ROM done %016llx / %016llx\n", __func__,
                            (long long int)w->w1_id_received,
                            (long long int)w->w1_id);)
                    w->command = 0;
                    w->muted = w->w1_id_received != w->w1_id;
                    break;
                }
            }
            break;
        case W1_CMD_READ_SCRATCHPAD:
            if (w->read_count != 0) {
                break;
            }
            D(printf("%s READ_SCRATCHPAD send CRC\n", __func__);)
            w->read_count = 8;
            w->read_buffer = w->read_crc;
            w->command = 0;
            break;
    }
}

static int
w1_device_init(SysBusDevice *dev)
{
    OneWireDevice * w = OBJECT_CHECK(OneWireDevice, dev, "ds18s20");
    DeviceState *qdev = DEVICE(dev);

    memory_region_init(&w->dummy_iomem, OBJECT(w), "w1_device", 0);
    sysbus_init_mmio(dev, &w->dummy_iomem);

    qdev_init_gpio_in(qdev, w1_receive, 2);
    qdev_init_gpio_out(qdev, &w->out, 1);

    /* The 0x28 there is the important bit, it's for thermal sensor family */
    w1_make_id(w, 0x00deadbeeff00d28);
    w->current_temp_mc = 85000; // reset value
    return 0;
}


static void w1_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = w1_device_init;
    dc->desc = "Virtual 1-Wire DS18S20 Thermal Sensor";
}

static TypeInfo w1_device_info = {
    .name          = "ds18s20",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(OneWireDevice),
    .class_init    = w1_device_class_init,
};

static void w1_device_register_type(void)
{
    type_register_static(&w1_device_info);
}

type_init(w1_device_register_type)
