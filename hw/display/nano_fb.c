/*
 * Copyright (c) 2020 Nanosonics
 *
 * Nanosonics IMX6UL LCDIF emulation.
 *
 *
 * This code is licensed under the GPL, version 2 or later.
 * See the file `COPYING' in the top level directory.
 *
 * It (partially) emulates nanosonics platform with a Freescale
 * i.MX6ul SoC
 */


#include "qemu/osdep.h"
#include "hw/display/nano_fb.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "ui/pixel_ops.h"
#include "hw/display/framebuffer.h"
#include "util/nano_utils.h"
#include "hw/gpio/imx_gpio.h"

#define NANO_CTRL 0x0
#define NANO_CTRL_SET 0x4
#define NANO_CTRL_CLR 0x8
#define NANO_CTRL1 0x10
#define NANO_CTRL1_SET 0x14
#define NANO_CTRL1_CLR 0x18
#define NANO_TRANSFER_COUNT 0x30
#define NANO_CUR_BUF 0x40
#define NANO_TIMING 0x60


#define LCDIF_CTRL_RUN_MASK                      (0x1U)
#define LCDIF_CTRL1_CUR_FRAME_DONE_IRQ_MASK      (0x200U)
#define LCDIF_CTRL_DATA_SELECT_MASK              (0x10000U)

typedef struct keypad_key_desc {
	unsigned int x;
	unsigned int y;
	unsigned int w;
	unsigned int h;
    const char*  btn_name;
    unsigned int group;
    unsigned int pin;
    qemu_irq     btn_irq;
}keypad_key_desc;


static keypad_key_desc nano_keypad[] = {
	{63,  121,  74,  74, "start", 5, 9,  NULL}, //start key -- index 0
	{486,  84,  42,  42, "up",    1, 23, NULL}, //up    key -- index 1
	{426, 144,  42,  42, "left",  1, 20, NULL}, //left  key -- index 2
	{546, 144,  42,  42, "right", 3, 3,  NULL}, //right key -- index 3
	{486, 204,  42,  42, "down",  1, 21, NULL}, //down  key -- index 4
    {486, 144,  42,  42, "enter", 1, 22, NULL}, //enter key -- index 5
    {},
};

typedef struct led_param {
	int x;
	int y;
	int w;
	int h;
}led_param;

static led_param nano_led_params[] = {
    {220, 253, 159,  5},  //light
    {63,  121,  74, 74},  //start button
};

static T_PixelDatas board_mem_pixels;
static T_PixelDatas startBtn_on_mem_pixels;
static T_PixelDatas startBtn_off_mem_pixels;
static T_PixelDatas indicator_off_mem_pixels;
static T_PixelDatas indicator_red_mem_pixels;
static T_PixelDatas indicator_green_mem_pixels;

#define NANO_LCD_BUFF_SIZE 15360
static uint8_t s_dataBuf[NANO_LCD_BUFF_SIZE] __attribute__((aligned(64)));

static void nanofb_lcdif_update(void *opaque)
{
    NANOFbState *s = NANOFB(opaque);
    do
    {
        if(s->w*s->h == NANO_LCD_BUFF_SIZE)
        {
            SysBusDevice *sbd = SYS_BUS_DEVICE(s);
            uint8_t *src;
            ram_addr_t addr;
            MemoryRegion *mem;
            framebuffer_update_memory_section(&s->fbsection, sysbus_address_space(sbd), s->cur_buf,
                                            s->h, s->w);
            mem = (&(s->fbsection))->mr;
            if (!mem) {
                break;
            }
            assert(s->w*s->h == NANO_LCD_BUFF_SIZE);
            addr = (&(s->fbsection))->offset_within_region;
            src = memory_region_get_ram_ptr(mem) + addr;
            memcpy(s_dataBuf, src, s->w*s->h);
            s->invalidate = 1;
        }
    } while (false);
    s->ctrl &= (~LCDIF_CTRL_RUN_MASK);
    s->ctrl1 |= LCDIF_CTRL1_CUR_FRAME_DONE_IRQ_MASK;
    qemu_irq_raise(s->elcdif_irq);
}

static uint64_t nano_lcdif_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    NANOFbState *s = NANOFB(opaque);

    switch (addr) 
    {
    case NANO_CTRL:	
        return s->ctrl;

    case NANO_CTRL_SET:	
        return s->ctrl_set;

    case NANO_CTRL_CLR:	
        return s->ctrl_clr;

    case NANO_CTRL1:
        return s->ctrl1;

    case NANO_CTRL1_SET:	
        return s->ctrl1_set;

    case NANO_CTRL1_CLR:
        return s->ctrl1_clr;

    case NANO_TIMING:
        return s->timing;

    default:
        break;
    }
    return 0;
}

static void nano_lcdif_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    NANOFbState *s = NANOFB(opaque);

    switch (addr) 
    {
    case NANO_CTRL:	
        s->ctrl = value;
        break;

    case NANO_CTRL_SET:	
        s->ctrl_set = value;
        s->ctrl |= value;
        if(value == LCDIF_CTRL_RUN_MASK) {
            nanofb_lcdif_update(opaque);
        }
        break;

    case NANO_CTRL_CLR:	
        s->ctrl_clr = value;
        s->ctrl &= (~value);
        break;

    case NANO_CTRL1_SET:	
        s->ctrl1_set = value;
        s->ctrl1 |= value;
        break;

    case NANO_CTRL1_CLR:
        s->ctrl1_clr = value;
        s->ctrl1 &= (~value);
        if(value == LCDIF_CTRL1_CUR_FRAME_DONE_IRQ_MASK) {
            qemu_irq_lower(s->elcdif_irq);
        }
        break;

    case NANO_TRANSFER_COUNT:
        s->h = value & 0x0000FFFF;
        s->w = ((value & 0xFFFF0000) >> 16);
        break;

    case NANO_CUR_BUF:
        s->cur_buf = value;
        break;

    case NANO_TIMING:
        s->timing = value;
        break;

    default:
        break;
    }

}

static const MemoryRegionOps nano_lcdif_ops = {
    .read = nano_lcdif_read,
    .write = nano_lcdif_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void nano_fb_update(void *opaque)
{
    NANOFbState *s = NANOFB(opaque);
    uint8_t *dest;
    uint8_t *src;
    int dest_row_pitch;
    int bpp;
    char path[100];
    DisplaySurface *surface = qemu_console_surface(s->con);
    if(s->con_inited) {
        if(s->invalidate == 0) {
            return;
        }

        bpp = surface_bits_per_pixel(surface);
        src = surface_data(surface);
        dest_row_pitch = surface_stride(surface);
        for(int row = 95;row < (128+95); row++)
        {
            dest = src + row*dest_row_pitch;
            dest = dest + 179*4;
            for(int col = 0; col < 120; col++)
            {
                int i = (row-95)*120 + col;
                uint8_t v = s_dataBuf[(NANO_LCD_BUFF_SIZE-i) - 1];
                uint8_t c1 = v & 0xF;
                uint8_t c2 = (v >> 4) & 0xF;
                uint32_t rgb888;
                uint8_t r, g, b;
                r = 255 * c1 / 0xF;
                g = 255 * c1 / 0xF;
                b = 255 * c1 / 0xF;

                switch (bpp) {
                case 15:
                    *(uint16_t *)dest = rgb_to_pixel15(r, g, b);
                    dest += 2;
                    break;
                case 16:
                    *(uint16_t *)dest = rgb_to_pixel16(r, g, b);
                    dest += 2;
                    break;
                case 24:
                    rgb888 = rgb_to_pixel24(r, g, b);
                    *dest++ = rgb888 & 0xff;
                    *dest++ = (rgb888 >> 8) & 0xff;
                    *dest++ = (rgb888 >> 16) & 0xff;
                    break;
                case 32:
                    *(uint32_t *)dest = rgb_to_pixel32(r, g, b);
                    dest += 4;
                    break;
                default:
                    assert(false);
                }

                r = 255 * c2 / 0xF;
                g = 255 * c2 / 0xF;
                b = 255 * c2 / 0xF;
                switch (bpp) {
                case 15:
                    *(uint16_t *)dest = rgb_to_pixel15(r, g, b);
                    dest += 2;
                    break;
                case 16:
                    *(uint16_t *)dest = rgb_to_pixel16(r, g, b);
                    dest += 2;
                    break;
                case 24:
                    rgb888 = rgb_to_pixel24(r, g, b);
                    *dest++ = rgb888 & 0xff;
                    *dest++ = (rgb888 >> 8) & 0xff;
                    *dest++ = (rgb888 >> 16) & 0xff;
                    break;
                case 32:
                    *(uint32_t *)dest = rgb_to_pixel32(r, g, b);
                    dest += 4;
                    break;
                default:
                    assert(false);
                }
            }
        }
        dpy_gfx_update(s->con, 179, 95, 240, 128);
    } 
    else {  
        src = board_mem_pixels.aucPixelDatas;

        dest_row_pitch = surface_stride(surface);
        dest = surface_data(surface);

        for (int i = 0; i < 320; i++) {
            memcpy(dest, src, dest_row_pitch);
            
            src  += board_mem_pixels.iLineBytes;
            dest += dest_row_pitch;
        }
        dpy_gfx_update(s->con, 0, 0, 600, 320);
        s->con_inited = 1;
        
        //init keypad button irq and reset keypad pin to high
        for(int buttonIndex = 0; buttonIndex < 6; ++buttonIndex)
        {
            int group = nano_keypad[buttonIndex].group;
            int pin = nano_keypad[buttonIndex].pin;
            sprintf(path, "/machine/soc/gpio%d", group-1);
            IMXGPIOState * s = IMX_GPIO(object_resolve_path(path, NULL));
            if(s)
            {
                nano_keypad[buttonIndex].btn_irq = qdev_get_gpio_in(DEVICE(s), pin);
                qemu_set_irq(nano_keypad[buttonIndex].btn_irq, 1);
            }
            else
            {
                assert(false);
            }
        }
    }
    
    s->invalidate = 0;
    
}

static void updateRegion(void *opaque, const T_PixelDatas* data, led_param* param)
{
    NANOFbState *s = NANOFB(opaque);
    uint8_t *dest;
    const uint8_t *src;
    int dest_row_pitch;
    DisplaySurface *surface = qemu_console_surface(s->con);
    dest_row_pitch = surface_stride(surface);
    dest = surface_data(surface);
    dest = dest + param->y*dest_row_pitch;
    dest = dest + param->x*4;
    src = data->aucPixelDatas;
    for(int i = 0; i < param->h; ++i)
    {
        memcpy(dest, src, dest_row_pitch);      
        src  += data->iLineBytes;
        dest += dest_row_pitch;
    }
    dpy_gfx_update(s->con, param->x, param->y, param->w, param->h);

}

void updateRGBLedStatus(IndicatorLedStatus ledStatus)
{
    static IndicatorLedStatus lastLedStatus = eOff;
    char path[100];
	sprintf(path, "/machine/soc/%s", NANO_LCD_DEV_NAME);
	NANOFbState * s = NANOFB(object_resolve_path(path, NULL));
    if(s == NULL)
    {
        DBG_PRINTF("nano lcd not found\n");
        return;
    }
    
    if(!s->con_inited){
        DBG_PRINTF("nano lcd console not inited\n");
        return;
    }
    if(lastLedStatus == ledStatus) {
        return;
    }
    lastLedStatus = ledStatus;
    updateRegion(s, (ledStatus == eOff) ? &indicator_off_mem_pixels : (ledStatus == eRed) ? &indicator_red_mem_pixels : &indicator_green_mem_pixels, 
                &nano_led_params[0]);
}

void updateStartButtonLedStatus(bool bOn)
{
    static bool last_bOn = false;
    char path[100];
	sprintf(path, "/machine/soc/%s", NANO_LCD_DEV_NAME);
	NANOFbState * s = NANOFB(object_resolve_path(path, NULL));
    if(s == NULL)
    {
        DBG_PRINTF("nano lcd not found\n");
        return;
    }
    
    if(!s->con_inited){
        DBG_PRINTF("nano lcd console not inited\n");
        return;
    }
    if(last_bOn == bOn) {
        return;
    }
    last_bOn = bOn;
    updateRegion(s, bOn ? &startBtn_on_mem_pixels : &startBtn_off_mem_pixels, &nano_led_params[1]);
}

static int nano_board_ui_backgroud_prepare(void)
{
	int err;

	char *cur_app_abs_dir = get_cur_app_abs_dir();
	
	PT_PicFileParser pBMPParser = GetBMPParserInit();
	T_FileMap tFileMap;

	sprintf(tFileMap.strFileName, "%s/%s", cur_app_abs_dir, "p5_panel.bmp");
	DBG_PRINTF("=====p5 panel bitmap path is (%s)=====\n", tFileMap.strFileName);
	err =MapFile(&tFileMap);

	if (err)
	{
		DBG_PRINTF("=====map file (%s) error=====\n", tFileMap.strFileName);
		return -1;
	}
	board_mem_pixels.iBpp  = 32;
	err = pBMPParser->GetPixelDatas(&tFileMap,  &board_mem_pixels);
	
	UnMapFile(&tFileMap);
	
	return err;
}

static int nano_startBtn_on_prepare(void)
{
	int err;

	char *cur_app_abs_dir = get_cur_app_abs_dir();
	
	PT_PicFileParser pBMPParser = GetBMPParserInit();
	T_FileMap tFileMap;

	sprintf(tFileMap.strFileName, "%s/%s", cur_app_abs_dir, "start_button_on.bmp");
	DBG_PRINTF("=====p5 panel bitmap path is (%s)=====\n", tFileMap.strFileName);
	err =MapFile(&tFileMap);

	if (err)
	{
		DBG_PRINTF("=====map file (%s) error=====\n", tFileMap.strFileName);
		return -1;
	}
	startBtn_on_mem_pixels.iBpp  = 32;
	err = pBMPParser->GetPixelDatas(&tFileMap,  &startBtn_on_mem_pixels);
	
	UnMapFile(&tFileMap);
	
	return err;
}

static int nano_startBtn_off_prepare(void)
{
	int err;

	char *cur_app_abs_dir = get_cur_app_abs_dir();
	
	PT_PicFileParser pBMPParser = GetBMPParserInit();
	T_FileMap tFileMap;

	sprintf(tFileMap.strFileName, "%s/%s", cur_app_abs_dir, "start_button_off.bmp");
	DBG_PRINTF("=====p5 panel bitmap path is (%s)=====\n", tFileMap.strFileName);
	err =MapFile(&tFileMap);

	if (err)
	{
		DBG_PRINTF("=====map file (%s) error=====\n", tFileMap.strFileName);
		return -1;
	}
	startBtn_off_mem_pixels.iBpp  = 32;
	err = pBMPParser->GetPixelDatas(&tFileMap,  &startBtn_off_mem_pixels);
	
	UnMapFile(&tFileMap);
	
	return err;
}

static int nano_indicator_off_prepare(void)
{
	int err;

	char *cur_app_abs_dir = get_cur_app_abs_dir();
	
	PT_PicFileParser pBMPParser = GetBMPParserInit();
	T_FileMap tFileMap;

	sprintf(tFileMap.strFileName, "%s/%s", cur_app_abs_dir, "light_off.bmp");
	DBG_PRINTF("=====p5 panel bitmap path is (%s)=====\n", tFileMap.strFileName);
	err =MapFile(&tFileMap);

	if (err)
	{
		DBG_PRINTF("=====map file (%s) error=====\n", tFileMap.strFileName);
		return -1;
	}
	indicator_off_mem_pixels.iBpp  = 32;
	err = pBMPParser->GetPixelDatas(&tFileMap,  &indicator_off_mem_pixels);
	
	UnMapFile(&tFileMap);
	
	return err;
}

static int nano_indicator_red_prepare(void)
{
	int err;

	char *cur_app_abs_dir = get_cur_app_abs_dir();
	
	PT_PicFileParser pBMPParser = GetBMPParserInit();
	T_FileMap tFileMap;

	sprintf(tFileMap.strFileName, "%s/%s", cur_app_abs_dir, "light_red.bmp");
	DBG_PRINTF("=====p5 panel bitmap path is (%s)=====\n", tFileMap.strFileName);
	err =MapFile(&tFileMap);

	if (err)
	{
		DBG_PRINTF("=====map file (%s) error=====\n", tFileMap.strFileName);
		return -1;
	}
	indicator_red_mem_pixels.iBpp  = 32;
	err = pBMPParser->GetPixelDatas(&tFileMap,  &indicator_red_mem_pixels);
	
	UnMapFile(&tFileMap);
	
	return err;
}

static int nano_indicator_green_prepare(void)
{
	int err;

	char *cur_app_abs_dir = get_cur_app_abs_dir();
	
	PT_PicFileParser pBMPParser = GetBMPParserInit();
	T_FileMap tFileMap;

	sprintf(tFileMap.strFileName, "%s/%s", cur_app_abs_dir, "light_green.bmp");
	DBG_PRINTF("=====p5 panel bitmap path is (%s)=====\n", tFileMap.strFileName);
	err =MapFile(&tFileMap);

	if (err)
	{
		DBG_PRINTF("=====map file (%s) error=====\n", tFileMap.strFileName);
		return -1;
	}
	indicator_green_mem_pixels.iBpp  = 32;
	err = pBMPParser->GetPixelDatas(&tFileMap,  &indicator_green_mem_pixels);
	
	UnMapFile(&tFileMap);
	
	return err;
}

static int get_key_index_by_xy(int x, int y)
{
	int i;
	for (i = 0; nano_keypad[i].x; i++)
	{
		if ((x >= nano_keypad[i].x) && (x < nano_keypad[i].x + nano_keypad[i].w))
			if ((y >= nano_keypad[i].y) && (y < nano_keypad[i].y + nano_keypad[i].h))
				return i;
	}
	
	return -1;
}

static void handle_mouse_input_event(int x, int y, bool btn_down)
{
	int index = get_key_index_by_xy(x, y);

	if (index == -1)
    {
    	DBG_PRINTF("=====no key pressed {x=%d,y=%d}=====\n", x, y);
    	return;
	}

    DBG_PRINTF("=====%s key %s {x=%d,y=%d}=====\n", nano_keypad[index].btn_name, btn_down ? "pressed" : "released",x, y);
    qemu_set_irq(nano_keypad[index].btn_irq, btn_down ? 0 : 1);
}

static void keypad_mouse_input_event(DeviceState *dev, QemuConsole *src,
                                InputEvent *evt)
{
    NANOFbState *s = NANOFB(dev);
    InputMoveEvent *move;
    InputBtnEvent *btn;
    DisplaySurface *surface;
    int scale;

    switch (evt->type) {
    case INPUT_EVENT_KIND_ABS:
		move = evt->u.abs.data;

		if (!src) {
			return;
		}
		surface = qemu_console_surface(src);
		switch (move->axis) {
			case INPUT_AXIS_X:
				scale = surface_width(surface) - 1;
				break;
			case INPUT_AXIS_Y:
				scale = surface_height(surface) - 1;
				break;
			default:
				scale = 0x8000;
				break;
		}
        s->axis[move->axis] = move->value * scale / 0x7fff;

        break;

    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
		
		switch (btn->button) {
	        case INPUT_BUTTON_LEFT:
				if (!btn->down)
				{
            		handle_mouse_input_event(s->axis[INPUT_AXIS_X], s->axis[INPUT_AXIS_Y], false);
				}
                else
                {
                    handle_mouse_input_event(s->axis[INPUT_AXIS_X], s->axis[INPUT_AXIS_Y], true);
                }
                
	            break;
				
	        default:
	            break;
				
		}
        break;

    default:
        break;
    }
}


static QemuInputHandler keypad_mouse_handler = {
	.name  = "nano_keypad_board",
	.mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
	.event = keypad_mouse_input_event,
	//.sync  = hid_pointer_sync,
};

static void nano_fb_invalidate(void *opaque)
{
    NANOFbState *s = NANOFB(opaque);
    s->invalidate = 1;
}

static const GraphicHwOps nanofb_ops = {
    .invalidate  = nano_fb_invalidate,
    .gfx_update  = nano_fb_update,
};

static void nano_fb_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice* sbd = SYS_BUS_DEVICE(dev);
    NANOFbState *s = NANOFB(dev);

    if (nano_board_ui_backgroud_prepare() ||
        nano_startBtn_on_prepare() ||
        nano_startBtn_off_prepare() ||
        nano_indicator_off_prepare() ||
        nano_indicator_red_prepare() ||
        nano_indicator_green_prepare())
		return;

    for(int i = 0; i < NANO_LCD_BUFF_SIZE; i++)
    {
        s_dataBuf[i] = 0xFF;
    }
    
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->elcdif_irq);
    dev->id = "nano_keypad_board";
    s->invalidate = 1;
    s->ctrl = 0;
	s->ctrl_set = 0;
	s->ctrl_clr = 0;
    s->ctrl1 = 0;
	s->ctrl1_set = 0;
	s->ctrl1_clr = 0;
	s->w = 0;
    s->h = 0;
    s->cur_buf= 0;
    s->timing = 0;
    s->invalidate = 1;
    s->con = graphic_console_init(dev, 0, &nanofb_ops, s);
    s->con_inited = 0;
    qemu_console_resize(s->con, 600, 320);

    s->input = qemu_input_handler_register(dev, &keypad_mouse_handler);
    // can not bind all input handler, or : qemu_input_is_absolute is false
	//qemu_input_handler_bind(s->input, dev->id, 0, NULL);
	qemu_input_handler_activate(s->input);
}

static void nano_fb_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->realize = nano_fb_realize; 
    dc->desc = "nano fb graphic console";
}

static void nano_fb_init(Object *obj)
{
    NANOFbState *s = NANOFB(obj);
    memory_region_init_io(&s->iomem, obj, &nano_lcdif_ops, s, TYPE_NANOFB, 612);// 218 is the size of struct LCDIF_Type
}

static const TypeInfo nano_fb_info = {
    .name          = TYPE_NANOFB,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(NANOFbState),
    .class_init    = nano_fb_class_init,
    .instance_init = nano_fb_init,      
    
};

static void nano_fb_register_types(void)
{
    type_register_static(&nano_fb_info);
}

type_init(nano_fb_register_types)
