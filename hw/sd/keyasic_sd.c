/*
 * Keyasic SD Card 2.0 controller
 *
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "sysemu/dma.h"

#include "hw/sd/keyasic_sd.h"

// SPI-SDIO registers
#define REG_SDIO_ENABLE_OFFSET      0x300
#define REG_SDIO_CLK_DIV_OFFSET     0x304
#define REG_SDIO_INT_OFFSET         0x308
#define REG_SDIO_MASK_OFFSET        0x30c
#define REG_SDIO_CLK_POL_OFFSET     0x310

#define SDIO_ENABLED                (1<<0)
#define SDIO_CLK_DIV_MASK           0xff
#define SDIO_INT_MASK               0xff
#define SDIO_CLK_POLARITY           (1<<0)

#define SDIO_CARD_ERROR_INT         (1<<6)
#define SDIO_CMD_DONE_INT           (1<<5)
#define SDIO_TRAN_DONE_INT          (1<<4)
#define SDIO_DATA_BOUND_INT         (1<<3)
#define SDIO_BUF_TRAN_FINISH_INT    (1<<2)
#define SDIO_CH1_FINISH_TRAN_INT    (1<<1)
#define SDIO_CH0_FINISH_TRAN_INT    (1<<0)

// SD card registers
#define REG_SCBSR_OFFSET            0x0
#define REG_SCCR_OFFSET             0x4
#define REG_SCARGR_OFFSET           0x8
#define REG_CSADDR_OFFSET           0xc
#define REG_SCSR_OFFSET             0x10
#define REG_SCEER_OFFSET            0x14
#define REG_SCRR1_OFFSET            0x18
#define REG_SCRR2_OFFSET            0x1c
#define REG_SCRR3_OFFSET            0x20
#define REG_SCRR4_OFFSET            0x24

#define REG_DCCR0_OFFSET            0x28
#define REG_DCSSAR0_OFFSET          0x2c
#define REG_DCDSAR0_OFFSET          0x30
#define REG_DCDTR0_OFFSET           0x34

#define REG_DCCR1_OFFSET            0x38
#define REG_DCSSAR1_OFFSET          0x3c
#define REG_DCDSAR1_OFFSET          0x40
#define REG_DCDTR1_OFFSET           0x44

#define REG_SCBTRR_OFFSET           0x48
#define REG_SCBTCR_OFFSET           0x50

#define SCBSR_BLOCK_COUNT_MASK      0xff
#define SCBSR_BLOCK_SIZE_MASK       0xff00
#define SCBSR_BLOCK_512             0x0100
#define SCBSR_BLOCK_1K              0x0200
#define SCBSR_BLOCK_2K              0x0300
#define SCBSR_BLOCK_LENGTH_OFFSET   16
#define SCBSR_BLOCK_LENGTH_MASK     (0xff<<SCBSR_BLOCK_LENGTH_OFFSET)

#define SCCR_HARD_RESET             (1<<7)
#define SCCR_READ_DATA              (1<<8)
#define SCCR_AUTO_CMD12             (1<<9)
#define SCCR_RESP_TYPE_OFFSET       10
#define SCCR_RESP_TYPE_MASK         (0x3<<SCCR_RESP_TYPE_OFFSET)
#define SCCR_ENABLE_DATA_TRAN       (1<<14)
#define SCCR_CMD_OFFSET             16
#define SCCR_CMD_MASK               (0x3f<<SCCR_CMD_OFFSET)

#define CSADDR_BUF_NUM_OFFSET       2
#define CSADDR_BUF_NUM_MASK         (1<<CSADDR_BUF_NUM_OFFSET)

#define SCSR_CARD_EXIST             (1<<11)
#define SCSR_TIMEOUT                (1<<16)

#define SCERR_ENABLE_INT            (1<<8)

#define SCBTRR_BUF_FIFO_FINISH      (1<<0)
#define SCBTRR_TRAN_DONE_FINISH     (1<<1)
#define SCBTRR_CMD_DONE_FINISH      (1<<2)
#define SCBTRR_DATA_BOUND_FINISH    (1<<3)
#define SCBTRR_CARD_ERROR           (1<<4)

#define SCBTCR_BUF_IND              (1<<0)
#define SCBTCR_WRITE                (1<<1)
#define SCBTCR_TRANS_START          (1<<2)

#define DCCR_TRANS_START            (1<<0)
#define DCCR_CLEAR_INT              (1<<19)

static void keyasic_sd_update_irq(KeyasicSdState *s)
{
    if (!(s->sceer & SCERR_ENABLE_INT)) {
        goto lower;
    }

    if (!(s->sdio_int_status & s->sdio_int_mask)) {
        goto lower;
    }

    qemu_irq_raise(s->irq);
    return;

lower:
    qemu_irq_lower(s->irq);
}

static void keyasic_sd_card_transfer(KeyasicSdState *s, int buf_ind, int write_to_card)
{
    uint32_t block_size;

    switch (s->scbsr & SCBSR_BLOCK_SIZE_MASK) {
    case SCBSR_BLOCK_512: block_size = CARD_BLOCK_SIZE_512; break;
    case SCBSR_BLOCK_1K:  block_size = CARD_BLOCK_SIZE_1024; break;
    case SCBSR_BLOCK_2K:  block_size = CARD_BLOCK_SIZE_2048; break;

    default:
        block_size =
            (s->scbsr & SCBSR_BLOCK_LENGTH_MASK) >> SCBSR_BLOCK_LENGTH_OFFSET;
        break;
    }

    if (block_size > CARD_BLOCK_SIZE_512 &&
        block_size != CARD_BLOCK_SIZE_1024 &&
        block_size != CARD_BLOCK_SIZE_2048) {
        printf("KEYASIC_SD: Can't handle card size %u\n", block_size);
        // TODO: generate ERROR with no qemu crash
        abort();
        return;
    }

    if (write_to_card) {
        sdbus_write_data(&s->sdbus, s->internal_buffer[buf_ind], block_size);
    } else{
        sdbus_read_data(&s->sdbus, s->internal_buffer[buf_ind], block_size);
    }
}

static int keyasic_send_cmd(KeyasicSdState *s, uint8_t cmd, uint32_t arg)
{
    SDRequest request;
    uint8_t response[16];
    int rlen;

    request.cmd = cmd;
    request.arg = arg;

    rlen = sdbus_do_command(&s->sdbus, &request, response);
    if (rlen != 0 && rlen != 4 && rlen != 16) {
        return -1;
    }

    if (rlen == 4) {
        s->scrr[0] = ldl_be_p(&response[0]);
        s->scrr[1] = s->scrr[2] = s->scrr[3] = 0;
    } else if (rlen == 16) {
        s->scrr[3] = ldl_be_p(&response[0]);
        s->scrr[2] = ldl_be_p(&response[4]);
        s->scrr[1] = ldl_be_p(&response[8]);
        s->scrr[0] = ldl_be_p(&response[12]);
    }

    return 0;
}

static void keyasic_sd_do_cmd(KeyasicSdState *s)
{
    uint8_t cmd = (s->sccr & SCCR_CMD_MASK) >> SCCR_CMD_OFFSET;

    if (keyasic_send_cmd(s, cmd, s->scargr)) {
        s->scsr |= SCSR_TIMEOUT;
        s->sdio_int_status |= SDIO_CARD_ERROR_INT;
        return;
    }

    // TODO: реализация команд SDIO 5, 52, 53, 54 (их не поддерживает qemu)
    // команда sdio (5) спрашивает вольтаж, пробуем сами отвечать
    // if (request.cmd == 5) {
    //     // если 0, то выдаем поддерживаемые, иначе показываем, что мы busy
    //     s->scrr1 = s->scargr ? 0x80000000 : 0xff0000;
    // }

    switch (s->sccr & SCCR_RESP_TYPE_MASK) {
    case 0:
        break;

    default:
        // TODO: shouldn't we somehow check response (R1, R1b and so on)
        break;
    }

    s->sdio_int_status |= SDIO_CMD_DONE_INT;

    // if no transmition requested - return
    if (!(s->sccr & SCCR_ENABLE_DATA_TRAN)) {
        return;
    }

    int buf_ind = (s->csaddr &CSADDR_BUF_NUM_MASK) >> CSADDR_BUF_NUM_OFFSET;

    keyasic_sd_card_transfer(s, buf_ind, !(s->sccr & SCCR_READ_DATA));

    if (cmd == 18 || cmd == 25) {
        s->multi_cmd_in_progress = cmd;
        s->multi_transfer_count = (s->scbsr & SCBSR_BLOCK_COUNT_MASK) - 1;

        s->sdio_int_status |= SDIO_DATA_BOUND_INT;
    } else {
        s->sdio_int_status |= SDIO_TRAN_DONE_INT;
    }
}

static void keyasic_sd_multi_transfer_cont(KeyasicSdState *s)
{
    int buf_ind = (s->csaddr &CSADDR_BUF_NUM_MASK) >> CSADDR_BUF_NUM_OFFSET;

    keyasic_sd_card_transfer(s, buf_ind, !(s->sccr & SCCR_READ_DATA));

    s->multi_transfer_count--;

    if (s->multi_transfer_count) {
        s->sdio_int_status |= SDIO_DATA_BOUND_INT;
    } else {
        if (keyasic_send_cmd(s, 12, 0)) {
            printf("Auto-cmd 12 error\n");
            abort();
        }

        s->sdio_int_status |= SDIO_TRAN_DONE_INT;
        s->sdio_int_status |= SDIO_CMD_DONE_INT;
    }
}

static void keyasic_sd_mem_transfer(KeyasicSdState *s)
{
    if (!(s->scbtcr & SCBTCR_TRANS_START)) {
        return;
    }

    uint32_t is_write = s->scbtcr & SCBTCR_WRITE;
    int buf_ind = s->scbtcr & SCBTCR_BUF_IND;

    for (int i = 0; i < CARD_BUFFER_COUNT; i++) {
        if (!(s->dccr[i] & DCCR_TRANS_START)) {
            continue;
        }

        MemTxResult res;
        dma_addr_t address = is_write ? s->dcssar[i] : s->dcdsar[i];

        if (is_write) {
            res = dma_memory_read(s->addr_space, address, s->internal_buffer[buf_ind],
                                  s->dcdtr[i]);
        } else {
            res = dma_memory_write(s->addr_space, address, s->internal_buffer[buf_ind],
                                  s->dcdtr[i]);
        }

        if (res) {
            printf("Couldn't write from %s with error %x\n",
                is_write ? "memory to SD" : "SD to memory", res);
            abort();
        }

        s->sdio_int_status |= i ? SDIO_CH1_FINISH_TRAN_INT : SDIO_CH0_FINISH_TRAN_INT;
        s->sdio_int_status |= SDIO_BUF_TRAN_FINISH_INT;
    }
}

static void keyasic_sd_hard_reset(KeyasicSdState *s)
{
    s->scbsr = 0;
    s->sccr = 0;
    s->scargr = 0;
    s->csaddr = 0;
    s->scsr = 0;
    s->sceer = 0;
    s->scbtrr = 0;
    s->scbtcr = 0;

    memset(s->scrr, 0, sizeof(s->scrr));
    memset(s->dccr, 0, sizeof(s->dccr));
    memset(s->dcssar, 0, sizeof(s->dcssar));
    memset(s->dcdsar, 0, sizeof(s->dcdsar));
    memset(s->dcdtr, 0, sizeof(s->dcdtr));

    s->multi_transfer_count = 0;
    s->multi_cmd_in_progress = 0;

    keyasic_sd_update_irq(s);
}

static uint64_t keyasic_sd_read(void *opaque, hwaddr offset, unsigned int size)
{
    KeyasicSdState *s = KEYASIC_SD(opaque);
    uint64_t val = 0;

    if (!(s->sdio_en & SDIO_ENABLED)) {
        goto skip_sd_card;
    }

    switch (offset) {
    case REG_SCBSR_OFFSET:
        val = s->scbsr;
        break;

    case REG_SCCR_OFFSET:
        val = s->sccr;
        break;

    case REG_SCARGR_OFFSET:
        val = s->scargr;
        break;

    case REG_CSADDR_OFFSET:
        val = s->csaddr;
        break;

    case REG_SCSR_OFFSET:
        val = s->scsr;
        break;

    case REG_SCEER_OFFSET:
        val = s->sceer;
        break;

    case REG_SCRR1_OFFSET:
        val = s->scrr[0];
        break;

    case REG_SCRR2_OFFSET:
        val = s->scrr[1];
        break;

    case REG_SCRR3_OFFSET:
        val = s->scrr[2];
        break;

    case REG_SCRR4_OFFSET:
        val = s->scrr[3];
        break;

    case REG_SCBTRR_OFFSET:
        val = s->scbtrr;
        break;

    default: break;
    }

skip_sd_card:
    switch (offset) {
    case REG_SDIO_ENABLE_OFFSET:
        val = s->sdio_en;
        break;

    case REG_SDIO_CLK_DIV_OFFSET:
        val = s->sdio_clk_div;
        break;

    case REG_SDIO_INT_OFFSET:
        val = s->sdio_int_status;
        break;

    case REG_SDIO_MASK_OFFSET:
        val = s->sdio_int_mask;
        break;

    case REG_SDIO_CLK_POL_OFFSET:
        val = s->sdio_clk_polarity;
        break;

    default: break;
    }

    return val;
}

static void keyasic_sd_write(void *opaque, hwaddr offset, uint64_t val,
                                unsigned int size)
{
    KeyasicSdState *s = KEYASIC_SD(opaque);

    if (!(s->sdio_en & SDIO_ENABLED)) {
        goto skip_sd_card;
    }

    switch (offset) {
    case REG_SCBSR_OFFSET:
        s->scbsr = val;
        break;

    case REG_SCCR_OFFSET:
        s->sccr = val;

        if (val & SCCR_HARD_RESET) {
            keyasic_sd_hard_reset(s);
            break;
        }

        keyasic_sd_do_cmd(s);
        keyasic_sd_update_irq(s);
        break;

    case REG_SCARGR_OFFSET:
        s->scargr = val;
        break;

    case REG_CSADDR_OFFSET:
        s->csaddr = val;

        if (s->multi_transfer_count) {
            keyasic_sd_multi_transfer_cont(s);
            keyasic_sd_update_irq(s);
        }
        break;

    case REG_SCSR_OFFSET:
        // FIXME: сюда нельзя писать?
        break;

    case REG_SCEER_OFFSET:
        s->sceer = val;
        keyasic_sd_update_irq(s);
        break;

    case REG_SCRR1_OFFSET:
        s->scrr[0] = val;
        break;

    case REG_SCRR2_OFFSET:
        s->scrr[1] = val;
        break;

    case REG_SCRR3_OFFSET:
        s->scrr[2] = val;
        break;

    case REG_SCRR4_OFFSET:
        s->scrr[3] = val;
        break;

    case REG_SCBTRR_OFFSET:
        if (val & SCBTRR_TRAN_DONE_FINISH) {
            s->sdio_int_status &= ~SDIO_TRAN_DONE_INT;
        }

        if (val & SCBTRR_DATA_BOUND_FINISH) {
            s->sdio_int_status &= ~SDIO_DATA_BOUND_INT;
        }

        if (val & SCBTRR_CMD_DONE_FINISH) {
            s->sdio_int_status &= ~SDIO_CMD_DONE_INT;
        }

        if (val & SCBTRR_BUF_FIFO_FINISH) {
            s->sdio_int_status &= ~SDIO_BUF_TRAN_FINISH_INT;
        }

        if (val & SCBTRR_CARD_ERROR) {
            s->sdio_int_status &= ~SDIO_CARD_ERROR_INT;
        }
        break;

    case REG_SCBTCR_OFFSET:
        s->scbtcr = val;

        if (s->scbtcr & SCBTCR_TRANS_START) {
            keyasic_sd_mem_transfer(s);
            keyasic_sd_update_irq(s);
        }
        break;

    case REG_DCCR0_OFFSET:
        if (val & DCCR_CLEAR_INT) {
            val &= ~DCCR_CLEAR_INT;
            s->sdio_int_status &= ~SDIO_CH0_FINISH_TRAN_INT;
        }

        s->dccr[0] = val;

        if (s->dccr[0] & DCCR_TRANS_START) {
            keyasic_sd_mem_transfer(s);
            keyasic_sd_update_irq(s);
        }
        break;

    case REG_DCSSAR0_OFFSET:
        s->dcssar[0] = val;
        break;

    case REG_DCDSAR0_OFFSET:
        s->dcdsar[0] = val;
        break;

    case REG_DCDTR0_OFFSET:
        s->dcdtr[0] = val;
        break;

    case REG_DCCR1_OFFSET:
        if (val & DCCR_CLEAR_INT) {
            val &= ~DCCR_CLEAR_INT;
            s->sdio_int_status &= ~SDIO_CH1_FINISH_TRAN_INT;
        }

        s->dccr[1] = val;

        if (s->dccr[1] & DCCR_TRANS_START) {
            keyasic_sd_mem_transfer(s);
            keyasic_sd_update_irq(s);
        }
        break;

    case REG_DCSSAR1_OFFSET:
        s->dcssar[1] = val;
        break;

    case REG_DCDSAR1_OFFSET:
        s->dcdsar[1] = val;
        break;

    case REG_DCDTR1_OFFSET:
        s->dcdtr[1] = val;
        break;


    default: break;
    }

skip_sd_card:
    switch (offset) {
    case REG_SDIO_ENABLE_OFFSET:
        s->sdio_en = val & SDIO_ENABLED;
        break;

    case REG_SDIO_CLK_DIV_OFFSET:
        s->sdio_clk_div = val & SDIO_CLK_DIV_MASK;
        break;

    case REG_SDIO_MASK_OFFSET:
        s->sdio_int_mask = val & SDIO_INT_MASK;
        keyasic_sd_update_irq(s);
        break;

    case REG_SDIO_CLK_POL_OFFSET:
        s->sdio_clk_polarity = val & SDIO_CLK_POLARITY;
        break;

    default: break;
    }
}

static const MemoryRegionOps keyasic_sd_ops = {
    .read = keyasic_sd_read,
    .write = keyasic_sd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void keyasic_sd_realize(DeviceState *dev, Error **errp)
{
    KeyasicSdState *s = KEYASIC_SD(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, OBJECT(dev), &keyasic_sd_ops, s, "keyasic_sd", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    // set default address space
    if (s->addr_space == NULL) {
        s->addr_space = &address_space_memory;
    }
}

void keyasic_sd_change_address_space(KeyasicSdState *s, AddressSpace *addr_space, Error **errp)
{
    if (object_property_get_bool(OBJECT(s), "realized", errp)) {
        error_setg(errp, "Can't change address_space of realized device\n");
    }

    s->addr_space = addr_space;
}

static void keyasic_sd_set_readonly(DeviceState *dev, bool level)
{}

static void keyasic_sd_set_inserted(DeviceState *dev, bool level)
{
    KeyasicSdState *s = (KeyasicSdState *)dev;

    if (level) {
        s->scsr |= SCSR_CARD_EXIST;
    } else {
        s->scsr &= ~SCSR_CARD_EXIST;
    }

    // negate the inserted state
    qemu_set_irq(s->card_inserted, !level);
}

static void keyasic_sd_reset(DeviceState *dev)
{
    KeyasicSdState *s = KEYASIC_SD(dev);

    keyasic_sd_hard_reset(s);

    s->sdio_en = 0;
    s->sdio_clk_div = 0;
    s->sdio_int_status = 0;
    s->sdio_int_mask = 0;
    s->sdio_clk_polarity = 0;

    keyasic_sd_set_inserted(DEVICE(s), sdbus_get_inserted(&s->sdbus));
}

static void keyasic_sd_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    KeyasicSdState *s = KEYASIC_SD(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    sysbus_init_irq(sbd, &s->irq);

    qdev_init_gpio_out_named(dev, &s->card_inserted, "card-inserted", 1);

    qbus_init(&s->sdbus, sizeof(s->sdbus), TYPE_KEYASIC_SD_BUS, dev, "sd-bus");
}

static void keyasic_sd_class_init(ObjectClass *classp, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(classp);

    dc->desc = "Keyasic SD card 2.0 controller";
    dc->realize = keyasic_sd_realize;
    dc->reset = keyasic_sd_reset;
}

static TypeInfo keyasic_sd_info = {
    .name = TYPE_KEYASIC_SD,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(KeyasicSdState),
    .class_init = keyasic_sd_class_init,
    .instance_init = keyasic_sd_init,
};

static void keyasic_sd_bus_class_init(ObjectClass *klass, void *data)
{
    SDBusClass *sbc = SD_BUS_CLASS(klass);

    sbc->set_inserted = keyasic_sd_set_inserted;
    sbc->set_readonly = keyasic_sd_set_readonly;
}

static const TypeInfo keyasic_sd_bus_info = {
    .name = TYPE_KEYASIC_SD_BUS,
    .parent = TYPE_SD_BUS,
    .instance_size = sizeof(SDBus),
    .class_init = keyasic_sd_bus_class_init,
};

static void keyasic_sd_register_types(void)
{
    type_register_static(&keyasic_sd_info);
    type_register_static(&keyasic_sd_bus_info);
}

type_init(keyasic_sd_register_types)
