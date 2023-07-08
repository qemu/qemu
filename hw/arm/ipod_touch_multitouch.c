#include "hw/arm/ipod_touch_multitouch.h"

static void prepare_interface_version_response(IPodTouchMultitouchState *s) {
    memset(s->out_buffer + 1, 0, 15);

    // set the interface version
    s->out_buffer[2] = MT_INTERFACE_VERSION;

    // set the max packet size
    s->out_buffer[3] = (MT_MAX_PACKET_SIZE & 0xFF);
    s->out_buffer[4] = (MT_MAX_PACKET_SIZE >> 8) & 0xFF;

    // compute and set the checksum
    uint32_t checksum = 0;
    for(int i = 0; i < 14; i++) {
        checksum += s->out_buffer[i];
    }

    s->out_buffer[14] = (checksum & 0xFF);
    s->out_buffer[15] = (checksum >> 8) & 0xFF;
}

static void prepare_cmd_status_response(IPodTouchMultitouchState *s) {
    memset(s->out_buffer + 1, 0, 15);

    // TODO we should probably set some CMD status here

    // compute and set the checksum
    uint32_t checksum = 0;
    for(int i = 0; i < 14; i++) {
        checksum += s->out_buffer[i];
    }

    s->out_buffer[14] = (checksum & 0xFF);
    s->out_buffer[15] = (checksum >> 8) & 0xFF;
}

static void prepare_report_info_response(IPodTouchMultitouchState *s, uint8_t report_id) {
    memset(s->out_buffer + 1, 0, 15);

    // set the error
    s->out_buffer[2] = 0;

    // set the report length
    uint32_t report_length = 0;
    if(report_id == MT_REPORT_UNKNOWN1) {
        report_length = MT_REPORT_UNKNOWN1_SIZE;
    }
    else if(report_id == MT_REPORT_FAMILY_ID) {
        report_length = MT_REPORT_FAMILY_ID_SIZE;
    }
    else if(report_id == MT_REPORT_SENSOR_INFO) {
        report_length = MT_REPORT_SENSOR_INFO_SIZE;
    }
    else if(report_id == MT_REPORT_SENSOR_REGION_DESC) {
        report_length = MT_REPORT_SENSOR_REGION_DESC_SIZE;
    }
    else if(report_id == MT_REPORT_SENSOR_REGION_PARAM) {
        report_length = MT_REPORT_SENSOR_REGION_PARAM_SIZE;
    }
    else if(report_id == MT_REPORT_SENSOR_DIMENSIONS) {
        report_length = MT_REPORT_SENSOR_DIMENSIONS_SIZE;
    }
    else {
        hw_error("Unknown report ID 0x%02x\n", report_id);
    }

    s->out_buffer[3] = (report_length & 0xFF);
    s->out_buffer[4] = (report_length >> 8) & 0xFF;

    // compute and set the checksum
    uint32_t checksum = 0;
    for(int i = 0; i < 14; i++) {
        checksum += s->out_buffer[i];
    }

    s->out_buffer[14] = (checksum & 0xFF);
    s->out_buffer[15] = (checksum >> 8) & 0xFF;
}

static void prepare_short_control_response(IPodTouchMultitouchState *s, uint8_t report_id) {
    memset(s->out_buffer + 1, 0, 15);

    if(report_id == MT_REPORT_FAMILY_ID) {
        s->out_buffer[3] = MT_FAMILY_ID;
    }
    else if(report_id == MT_REPORT_SENSOR_INFO) {
        s->out_buffer[3] = MT_ENDIANNESS;
        s->out_buffer[4] = MT_SENSOR_ROWS;
        s->out_buffer[5] = MT_SENSOR_COLUMNS;
        s->out_buffer[6] = (MT_BCD_VERSION & 0xFF);
        s->out_buffer[7] = (MT_BCD_VERSION >> 8) & 0xFF;
    }
    else if(report_id == MT_REPORT_SENSOR_REGION_DESC) {
        s->out_buffer[3] = MT_SENSOR_REGION_DESC;
    }
    else if(report_id == MT_REPORT_SENSOR_REGION_PARAM) {
        s->out_buffer[3] = MT_SENSOR_REGION_PARAM;
    }
    else if(report_id == MT_REPORT_SENSOR_DIMENSIONS) {
        uint32_t *ob_int32 = (uint32_t *)&s->out_buffer[3];
        ob_int32[0] = MT_SENSOR_SURFACE_WIDTH;
        ob_int32[1] = MT_SENSOR_SURFACE_HEIGHT;
    }
    else {
        hw_error("Unknown report ID 0x%02x\n", report_id);
    }

    // compute and set the checksum
    uint32_t checksum = 0;
    for(int i = 0; i < 14; i++) {
        checksum += s->out_buffer[i];
    }

    s->out_buffer[14] = (checksum & 0xFF);
    s->out_buffer[15] = (checksum >> 8) & 0xFF;
}

static uint32_t ipod_touch_multitouch_transfer(SSIPeripheral *dev, uint32_t value)
{
    IPodTouchMultitouchState *s = IPOD_TOUCH_MULTITOUCH(dev);

    //printf("<MULTITOUCH> Got value: 0x%02x\n", value);

    if(s->cur_cmd == 0) {
        //printf("Starting command 0x%02x\n", value);
        // we're currently not in a command - start a new command
        s->cur_cmd = value;
        s->out_buffer = malloc(0x100);
        s->out_buffer[0] = value; // the response header
        s->buf_ind = 0;
        s->in_buffer = malloc(0x100);
        s->in_buffer_ind = 0;
        
        if(value == 0x18) { // filler packet??
            s->buf_size = 2;
            s->out_buffer[1] = 0xE1;
        }
        else if(value == 0x1A) { // HBPP ACK
            s->buf_size = 2;
            if(s->hbpp_atn_ack_response[0] == 0 && s->hbpp_atn_ack_response[1] == 0) {
                // return the default ACK response
                s->out_buffer[0] = 0x4B;
                s->out_buffer[1] = 0xC1;
            }
            else {
                s->out_buffer[0] = s->hbpp_atn_ack_response[0];
                s->out_buffer[1] = s->hbpp_atn_ack_response[1];
            }
             
        }
        else if(value == 0x1C) { // read register
            s->buf_size = 8;
            memset(s->out_buffer, 0, 8); // just return zeros
        }
        else if(value == 0x1D) { // execute
            s->buf_size = 12;
            memset(s->out_buffer, 0, 12); // just return zeros
        }
        else if(value == 0x1F) { // calibration
            s->buf_size = 2;
            s->out_buffer[1] = 0x0;
        }
        else if(value == 0x1E) { // write register
            s->buf_size = 16;
            memset(s->out_buffer, 0, 16); // just return zeros
        }
        else if(value == 0x1F) { // calibration
            s->buf_size = 2;
            s->out_buffer[1] = 0x0;
        }
        else if(value == MT_CMD_HBPP_DATA_PACKET) {
            s->buf_size = 20; // should be enough initially, until we get the packet length
            memset(s->out_buffer + 1, 0, 20 - 1); // just return zeros
        }
        else if(value == 0x47) { // unknown command, probably used to clear the interrupt
            s->buf_size = 2;
        }
        else if(value == MT_CMD_GET_CMD_STATUS) {
            s->buf_size = 16;
            prepare_cmd_status_response(s);
        }
        else if(value == MT_CMD_GET_INTERFACE_VERSION) {
            s->buf_size = 16;
            prepare_interface_version_response(s);
        }
        else if(value == MT_CMD_GET_REPORT_INFO) {
            s->buf_size = 16;
        }
        else if(value == MT_CMD_SHORT_CONTROL_WRITE) {
            s->buf_size = 16;
        }
        else if(value == MT_CMD_SHORT_CONTROL_READ) {
            s->buf_size = 16;
        }
        else if(value == MT_CMD_FRAME_READ) {
            printf("Will read frame!\n");
            s->buf_size = sizeof(MTFrame);
            free(s->out_buffer);
            s->out_buffer = (uint8_t *) s->next_frame;
        }
        else {
            hw_error("Unknown command 0x%02x!", value);
        }
    }

    s->in_buffer[s->in_buffer_ind] = value;
    s->in_buffer_ind++;

    if(s->cur_cmd == MT_CMD_HBPP_DATA_PACKET && s->in_buffer_ind == 10) {
        // verify the header checksum
        uint32_t checksum = 0;
        for(int i = 2; i < 8; i++) {
            checksum += s->in_buffer[i];
        }

        if(checksum != (s->in_buffer[8] << 8 | s->in_buffer[9])) {
            hw_error("HBPP data header checksum doesn't match!");
        }

        uint32_t data_len = (s->in_buffer[2] << 10) | (s->in_buffer[3] << 2) + 5;
        // extend the lengths of the in/out buffers
        free(s->in_buffer);
        s->in_buffer = malloc(data_len + 0x10);

        free(s->out_buffer);
        s->out_buffer = malloc(data_len);
        memset(s->out_buffer, 0, data_len);
        s->buf_size = data_len;
        s->buf_ind = 0;
    }
    else if(s->cur_cmd == MT_CMD_GET_REPORT_INFO && s->in_buffer_ind == 2) {
        prepare_report_info_response(s, s->in_buffer[1]);
    }
    else if(s->cur_cmd == MT_CMD_SHORT_CONTROL_WRITE && s->in_buffer_ind == 16) {
        // TODO we should persist the report here!
    }
    else if(s->cur_cmd == MT_CMD_SHORT_CONTROL_READ && s->in_buffer_ind == 2) {
        prepare_short_control_response(s, s->in_buffer[1]);
    }

    // TODO process register writes!

    uint8_t ret_val = s->out_buffer[s->buf_ind];
    s->buf_ind++;

    //printf("<MULTITOUCH> Got value: 0x%02x, returning 0x%02x (index: %d, buffer length: %d)\n", value, ret_val, s->buf_ind, s->buf_size);

    if(s->buf_ind == s->buf_size) {
        //printf("Finished command 0x%02x\n", s->cur_cmd);

        if(s->cur_cmd == 0x1E) {
            // make sure we return a success status on the next HBPP ACK
            s->hbpp_atn_ack_response[0] = 0x4A;
            s->hbpp_atn_ack_response[1] = 0xD1;
        }

        // we're done with the command
        s->cur_cmd = 0;
        s->buf_size = 0;
        //free(s->out_buffer);
        //free(s->in_buffer);
    }

    return ret_val;
}

static MTFrame *get_frame(IPodTouchMultitouchState *s, uint8_t event, float x, float y, uint16_t radius1, uint16_t radius2, uint16_t radius3, uint16_t contactDensity) {
    MTFrame *frame = calloc(sizeof(MTFrame), sizeof(uint8_t *));

    uint16_t data_len = sizeof(MTFrameHeader) + sizeof(FingerData) + 2;

    /// create the frame length packet
    frame->frame_length.cmd = MT_CMD_FRAME_READ;
    frame->frame_length.length1 = (data_len & 0xFF);
    frame->frame_length.length2 = (data_len >> 8) & 0xFF;

    uint16_t checksum = 0;
    for(int i = 0; i < 14; i++) {
        checksum += ((uint8_t *) &frame->frame_length)[i];
    }
    frame->frame_length.checksum1 = (checksum & 0xFF);
    frame->frame_length.checksum2 = (checksum >> 8) & 0xFF;

    // create the frame packet
    frame->frame_packet.cmd = MT_CMD_FRAME_READ;
    frame->frame_packet.length1 = (data_len & 0xFF);
    frame->frame_packet.length2 = (data_len >> 8) & 0xFF;

    checksum = 0;
    for(int i = 0; i < 4; i++) {
        checksum += ((uint8_t *) &frame->frame_length)[i];
    }

    // the first five bytes have to sum up to 0.
    frame->frame_packet.checksum_pad = 0xFF - (checksum & 0xFF) + 1;

    frame->frame_packet.header.type = MT_FRAME_TYPE_PATH;
    frame->frame_packet.header.frameNum = s->frame_counter;
    frame->frame_packet.header.headerLen = sizeof(MTFrameHeader);
    uint64_t elapsed_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 1000000;
    frame->frame_packet.header.timestamp = elapsed_ns;
    frame->frame_packet.header.numFingers = 1;
    frame->frame_packet.header.fingerDataLen = sizeof(FingerData);

    // create the finger data
    frame->finger_data.id = 1;
    frame->finger_data.event = event;
    frame->finger_data.unk_2 = 2;
    frame->finger_data.unk_3 = 1;

    // compute the velocity
    int diff_x = (int)((x - s->prev_touch_x) * MT_INTERNAL_SENSOR_SURFACE_WIDTH);
    int diff_y = (int)((x - s->prev_touch_y) * MT_INTERNAL_SENSOR_SURFACE_HEIGHT);
    frame->finger_data.velX = diff_x / (elapsed_ns - s->last_frame_timestamp) * 1000;
    frame->finger_data.velY = diff_y / (elapsed_ns - s->last_frame_timestamp) * 1000;

    frame->finger_data.x = (int)(x * MT_INTERNAL_SENSOR_SURFACE_WIDTH);
    frame->finger_data.y = (int)(y * MT_INTERNAL_SENSOR_SURFACE_HEIGHT);
    frame->finger_data.radius1 = radius1;
    frame->finger_data.radius2 = radius2;
    frame->finger_data.radius3 = radius3;
    frame->finger_data.angle = 19317;
    frame->finger_data.contactDensity = contactDensity; // seems to be a medium press

    // compute the checksum over the frame data.
    checksum = 0;
    for(int i = 0; i < data_len - 2; i++) {
        checksum += ((uint8_t *) &frame->frame_packet.header)[i];
    }
    frame->checksum1 = (checksum & 0xFF);
    frame->checksum2 = (checksum >> 8) & 0xFF;

    s->last_frame_timestamp = elapsed_ns;
    s->frame_counter += 1;

    return frame;
}

static void ipod_touch_multitouch_inform_frame_ready(IPodTouchMultitouchState *s) {
    s->sysic->gpio_int_status[4] |= (1 << 27); // the multitouch interrupt bit is in group 4 (32 interrupts per group), and the 27th of the 4th group
    qemu_irq_raise(s->sysic->gpio_irqs[4]);
}

void ipod_touch_multitouch_on_touch(IPodTouchMultitouchState *s) {
    s->touch_down = true;

    s->next_frame = get_frame(s, MT_EVENT_TOUCH_START, s->touch_x, s->touch_y, 100, 660, 580, 150);
    ipod_touch_multitouch_inform_frame_ready(s);

    timer_mod(s->touch_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + NANOSECONDS_PER_SECOND / 10);
}

void ipod_touch_multitouch_on_release(IPodTouchMultitouchState *s) {
    s->next_frame = get_frame(s, MT_EVENT_TOUCH_ENDED, s->touch_x, s->touch_y, 0, 0, 0, 0);
    s->touch_down = false;
    ipod_touch_multitouch_inform_frame_ready(s);

    timer_del(s->touch_timer);
    timer_mod(s->touch_end_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + NANOSECONDS_PER_SECOND / 10);
}

static void touch_timer_tick(void *opaque)
{
    IPodTouchMultitouchState *s = (IPodTouchMultitouchState *)opaque;

    s->next_frame = get_frame(s, MT_EVENT_TOUCH_MOVED, s->touch_x, s->touch_y, 100, 660, 580, 150);
    ipod_touch_multitouch_inform_frame_ready(s);

    if(s->touch_down) {
        // reschedule the timer
        timer_mod(s->touch_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + NANOSECONDS_PER_SECOND / 10);
    }
}

static void touch_end_timer_tick(void *opaque)
{
    IPodTouchMultitouchState *s = (IPodTouchMultitouchState *)opaque;
    s->next_frame = get_frame(s, MT_EVENT_TOUCH_FULL_END, s->touch_x, s->touch_y, 0, 0, 0, 0);
    s->touch_down = false;
    ipod_touch_multitouch_inform_frame_ready(s);
}

static void ipod_touch_multitouch_realize(SSIPeripheral *d, Error **errp)
{
    IPodTouchMultitouchState *s = IPOD_TOUCH_MULTITOUCH(d);
    memset(s->hbpp_atn_ack_response, 0, 2);
    s->touch_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, touch_timer_tick, s);
    s->touch_end_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, touch_end_timer_tick, s);

    s->prev_touch_x = 0;
    s->prev_touch_y = 0;
    s->last_frame_timestamp = 0;
}

static void ipod_touch_multitouch_class_init(ObjectClass *klass, void *data)
{
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);
    k->realize = ipod_touch_multitouch_realize;
    k->transfer = ipod_touch_multitouch_transfer;
}

static const TypeInfo ipod_touch_multitouch_type_info = {
    .name = TYPE_IPOD_TOUCH_MULTITOUCH,
    .parent = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(IPodTouchMultitouchState),
    .class_init = ipod_touch_multitouch_class_init,
};

static void ipod_touch_multitouch_register_types(void)
{
    type_register_static(&ipod_touch_multitouch_type_info);
}

type_init(ipod_touch_multitouch_register_types)
