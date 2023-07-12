#include "hw/arm/ipod_touch_nor_spi.h"

static void initialize_nor(IPodTouchNORSPIState *s)
{
    unsigned long fsize;
    // TODO still hardcoded, string copy not working...
    if (g_file_get_contents("/Users/martijndevos/Documents/generate_nor_it2g/nor.bin", (char **)&s->nor_data, &fsize, NULL)) {
        s->nor_initialized = true;
    }
}

static uint32_t ipod_touch_nor_spi_transfer(SSIPeripheral *dev, uint32_t value)
{
    IPodTouchNORSPIState *s = IPOD_TOUCH_NOR_SPI(dev);

    if(s->cur_cmd == NOR_READ_DATA_CMD && s->in_buf_cur_ind == s->in_buf_size && value != 0xFF) {
        // if we are currently reading from the NOR data and we receive a value that's not the sentinel, reset the current command.
        s->cur_cmd = 0;
    }

    if(s->cur_cmd == 0) {
        // this is a new command -> set it
        s->cur_cmd = value;
        printf("NEW CMD %d\n", s->cur_cmd);
        s->out_buf = malloc(0x1000);
        s->in_buf = malloc(0x1000);
        s->in_buf[0] = value;
        s->in_buf_size = 0;
        s->in_buf_cur_ind = 1;
        s->out_buf_cur_ind = 0;

        if(value == NOR_WRITE_TO_STATUS_REG) {
            s->in_buf_size = 1;
            s->out_buf_size = 1;
            s->out_buf[0] = 0;
        }
        else if(value == NOR_GET_STATUS_CMD) {
            printf("GET STATUS\n");
            s->in_buf_size = 1;
            s->out_buf_size = 1;
            s->out_buf[0] = 0;
        }
        else if(value == NOR_WRITE_DATA_CMD) {
            s->in_buf_size = 4;
            s->out_buf_size = 256;
            for(int i = 0; i < 256; i++) { s->out_buf[i] = 0; }; // TODO we ignore this command for now
        }
        else if(value == NOR_READ_DATA_CMD) {
            printf("Received read command!\n");
            s->in_buf_size = 4;
            s->out_buf_size = 4096;
        }
        else if(value == NOR_ERASE_BLOCK) {
            s->in_buf_size = 1;
            s->out_buf_size = 3;
            for(int i = 0; i < 3; i++) { s->out_buf[i] = 0x0; } // TODO we ignore this command for now
        }
        else if(value == NOR_ENABLE_WRITE) {
            s->write_enabled = 1;
            s->cur_cmd = 0;
        }
        else if(value == NOR_DISABLE_WRITE) {
            s->write_enabled = 0;
            s->cur_cmd = 0;
        }
        else if(value == NOR_GET_JEDECID) {
            s->in_buf_size = 1;
            s->out_buf_size = 3;
            s->out_buf[0] = 0x1F; // vendor: atmel, device: 0x02 -> AT25DF081A
            s->out_buf[1] = 0x45;
            s->out_buf[2] = 0x02;
        }
        else {
            printf("Unknown command %d!\n", value);
            hw_error("Unknown command 0x%02x!", value);
        }

        return 0x0;
    }
    else if(s->cur_cmd != 0 && s->in_buf_cur_ind < s->in_buf_size) {
        // we're reading the command
        s->in_buf[s->in_buf_cur_ind] = value;
        s->in_buf_cur_ind++;

        if(s->cur_cmd == NOR_GET_STATUS_CMD && s->in_buf_cur_ind == s->in_buf_size) {
            s->out_buf[0] = 0x0;  // indicates that the NOR is ready
        }
        else if(s->cur_cmd == NOR_READ_DATA_CMD && s->in_buf_cur_ind == s->in_buf_size) {
            if(!s->nor_initialized) { initialize_nor(s); }
            s->nor_read_ind = (s->in_buf[1] << 16) | (s->in_buf[2] << 8) | s->in_buf[3];
            printf("Setting NOR read index to: %d\n", s->nor_read_ind);
        }
        return 0x0;
    }
    else {
        uint8_t ret_val;
        // otherwise, we're outputting the response
        if(s->cur_cmd == NOR_READ_DATA_CMD) {
            uint8_t ret_val = s->nor_data[s->nor_read_ind];
            s->nor_read_ind++;
            return ret_val;
        }
        else {
            ret_val = s->out_buf[s->out_buf_cur_ind];
            s->out_buf_cur_ind++;

            if(s->cur_cmd != 0 && (s->out_buf_cur_ind == s->out_buf_size)) {
                // the command is done - clean up
                s->cur_cmd = 0;
                free(s->in_buf);
                free(s->out_buf);
            }
        }
        return ret_val;
    }
}

static void ipod_touch_nor_spi_realize(SSIPeripheral *d, Error **errp)
{
    IPodTouchNORSPIState *s = IPOD_TOUCH_NOR_SPI(d);
    s->nor_initialized = 0;
}

static void ipod_touch_nor_spi_class_init(ObjectClass *klass, void *data)
{
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);
    k->realize = ipod_touch_nor_spi_realize;
    k->transfer = ipod_touch_nor_spi_transfer;
}

static const TypeInfo ipod_touch_nor_spi_type_info = {
    .name = TYPE_IPOD_TOUCH_NOR_SPI,
    .parent = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(IPodTouchNORSPIState),
    .class_init = ipod_touch_nor_spi_class_init,
};

static void ipod_touch_nor_spi_register_types(void)
{
    type_register_static(&ipod_touch_nor_spi_type_info);
}

type_init(ipod_touch_nor_spi_register_types)
