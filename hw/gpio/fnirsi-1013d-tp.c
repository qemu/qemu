/*
 * FNIRSI I2C Touch Pad emulation
 */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "ui/console.h"
#include "hw/gpio/allwinner-f1-pio.h"
#include "hw/gpio/fnirsi-1013d-pio.h"

// #define __DEBUG

//----------------------------------------------------------------------------------------------------------------------------------
// Port A defines for handling the touch panel
//----------------------------------------------------------------------------------------------------------------------------------
// Panel driver state machine
//----------------------------------------------------------------------------------------------------------------------------------
#define PANEL_WAIT_ADDRESS 0
#define PANEL_RCV_REG_HIGH 1
#define PANEL_RCV_REG_LOW  2
#define PANEL_RECEIVE_DATA 3
#define PANEL_SEND_DATA    4
//----------------------------------------------------------------------------------------------------------------------------------
// Panel Register Addresses
//----------------------------------------------------------------------------------------------------------------------------------
#define GT911_COMMAND_REG           0x8040
#define GT911_CFG_VERSION_REG       0x8047
#define GT911_XMAX_LO_REG           0x8048
#define GT911_YMAX_LO_REG           0x804A
#define GT911_VENDOR_ID_REG         0x814A
#define GT911_STATUS_REG            0x814E
//----------------------------------------------------------------------------------------------------------------------------------
#define GT911_STATUS_RDY          0x80
#define GT911_STATUS_LARGE_DET    0x40
#define GT911_STATUS_HAVE_KEY     0x10
#define GT911_STATUS_PT_NO_MASK   0x0F
//----------------------------------------------------------------------------------------------------------------------------------
#define GT911_PROXIMITY_DET_REG     0x814F
#define GT911_PT1_COORD_REGS        0x8150
#define GT911_PT2_COORD_REGS        0x8158
#define GT911_PT3_COORD_REGS        0x8160
#define GT911_PT4_COORD_REGS        0x8168
#define GT911_PT5_COORD_REGS        0x8170
//----------------------------------------------------------------------------------------------------------------------------------
#define GT911_PT_X_LO_OFS         0x00
#define GT911_PT_X_HI_OFS         0x01
#define GT911_PT_Y_LO_OFS         0x02
#define GT911_PT_Y_HI_OFS         0x03
#define GT911_PT_SIZE_LO_OFS      0x04
#define GT911_PT_TRK_ID_OFS       0x07
//----------------------------------------------------------------------------------------------------------------------------------
#define GT911_COMMAND_STATUS_LO_REG 0x81A8
#define GT911_COMMAND_STATUS_HI_REG 0x81A9
//----------------------------------------------------------------------------------------------------------------------------------
// I2C state machine states
//----------------------------------------------------------------------------------------------------------------------------------
#define I2C_STATE_IDLE     0
#define I2C_STATE_RCV_BITS 1
#define I2C_STATE_SND_BITS 2
#define I2C_STATE_RCV_ACK  3
#define I2C_STATE_SND_ACK  4
#define I2C_STATE_SKIP_ACK 5
#define I2C_NUM_BITS       8
#define I2C_MODE_MASK   0x01
#define I2C_MODE_WRITE  0x00
#define I2C_MODE_READ   0x01
#define I2C_RESET_PIN   0x01
#define I2C_INT_PIN     0x02
#define I2C_SDA_PIN     0x04
#define I2C_SCL_PIN     0x08
//----------------------------------------------------------------------------------------------------------------------------------
static const uint8_t gt911_config[] =
{
/* 0x8047 */                                            0xFF,
/* 0x8048 */  0x20, 0x03, 0xE0, 0x01, 0x0A, 0xFD, 0x00, 0x01,
/* 0x8050 */  0x08, 0x28, 0x08, 0x5A, 0x3C, 0x03, 0x05, 0x00,
/* 0x8058 */  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x1A,
/* 0x8060 */  0x1E, 0x14, 0x87, 0x29, 0x0A, 0x75, 0x77, 0xB2,
/* 0x8068 */  0x04, 0x00, 0x00, 0x00, 0x9A, 0x01, 0x11, 0x00,
/* 0x8070 */  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x8078 */  0x00, 0x00, 0x50, 0xA0, 0x94, 0xD5, 0x02, 0x08,
/* 0x8080 */  0x00, 0x00, 0x04, 0xA1, 0x55, 0x00, 0x8F, 0x62,
/* 0x8088 */  0x00, 0x7F, 0x71, 0x00, 0x73, 0x82, 0x00, 0x69,
/* 0x8090 */  0x95, 0x00, 0x69, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x8098 */  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x80A0 */  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x80A8 */  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x80B0 */  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
/* 0x80B8 */  0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x12,
/* 0x80C0 */  0x14, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
/* 0x80C8 */  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x80D0 */  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x04,
/* 0x80D8 */  0x06, 0x08, 0x0A, 0x0C, 0x1D, 0x1E, 0x1F, 0x20,
/* 0x80E0 */  0x21, 0x22, 0x24, 0x26, 0x28, 0xFF, 0xFF, 0xFF,
/* 0x80E8 */  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
/* 0x80F0 */  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 0x80F8 */  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
/* 0x8100 */  0x01
};
//----------------------------------------------------------------------------------------------------------------------------------
struct TPI2CState
{
    uint8_t   state;          // Process state for the panel state machine
    uint8_t   mode;           // Data direction mode for the current data stream
    uint16_t  address;        // Internal address for panel read and write actions
    uint8_t   data[0x200];    // Panel has a lot of registers. For easy implementation
                              // set to 0x200 (address range 0x8000 - 0x8200)
    uint8_t   i2c_state;
    uint8_t   i2c_byte;
    uint8_t   i2c_bit_no;
    
    QEMUPutMouseEntry *eh_entry;
    int       x;
    int       y;
    int       dz;
    int       buttons_state;
} tp;
//----------------------------------------------------------------------------------------------------------------------------------
#define REG_INDEX(offset)    ((offset) / sizeof(uint32_t))
//----------------------------------------------------------------------------------------------------------------------------------
#define GT111_ADDRESS_VALID(A) (((A) >= GT911_COMMAND_REG) && ((A) <= GT911_COMMAND_STATUS_HI_REG))
//----------------------------------------------------------------------------------------------------------------------------------
static inline uint8_t gt911_read(struct TPI2CState *s, uint16_t address)
{
    if (GT111_ADDRESS_VALID(address)) { 
        int ofs;
        ofs = address & 0x01ff;    
        return s->data[ofs];
    }
    return 0;
}
//----------------------------------------------------------------------------------------------------------------------------------
static inline void gt911_write(struct TPI2CState *s, uint16_t address, uint8_t value)
{
    if (GT111_ADDRESS_VALID(address)) {
        int ofs;
        ofs = address & 0x01ff;    
        s->data[ofs] = value;
    }
}
//----------------------------------------------------------------------------------------------------------------------------------
static void gt911_save_touch_point(struct TPI2CState *s, uint16_t x, uint16_t y)
{
        gt911_write(s, GT911_PT1_COORD_REGS + GT911_PT_X_LO_OFS, x & 0xff);             
        gt911_write(s, GT911_PT1_COORD_REGS + GT911_PT_X_HI_OFS, x >> 8);             
        gt911_write(s, GT911_PT1_COORD_REGS + GT911_PT_Y_LO_OFS, y & 0xff);             
        gt911_write(s, GT911_PT1_COORD_REGS + GT911_PT_Y_HI_OFS, y >> 8);             
        gt911_write(s, GT911_STATUS_REG,
                       GT911_STATUS_RDY | GT911_STATUS_HAVE_KEY | 1);
}
//----------------------------------------------------------------------------------------------------------------------------------
static uint32_t fnirsi_tp_write(void *opaque, uint32_t *regs, uint32_t ofs, uint32_t value)
{
    struct TPI2CState *s = (struct TPI2CState *)opaque;
    const uint32_t idx = REG_INDEX(ofs);    
    const uint32_t reg = regs[idx];
    uint8_t address;
    
    if (ofs == REG_PIO_DATA) {
    	// Only when device not being reset
        if (reg & I2C_RESET_PIN) {
    		//On rising edge of the SCL pin a data bit needs to be read
            if (((reg & I2C_SCL_PIN) == 0) && (value & I2C_SCL_PIN)) {
    			// Handle according to the current i2c state
                switch (s->i2c_state) {
                case I2C_STATE_SND_BITS:
    				// Set SDA with the bit to send (MSB first)
                    if (s->i2c_byte & 0x80) {
    					// Set SDA
                        value |= I2C_SDA_PIN;
                    } else {
    					// Clear SDA
                        value &= ~I2C_SDA_PIN;
                    }
    				// Select next bit
                    s->i2c_byte <<= 1;
    				//Signal another bit is send
                    s->i2c_bit_no++;
    				// Check if it was the last bit
                    if (s->i2c_bit_no >= I2C_NUM_BITS) {
    					//If so switch to receiving the ack
                        s->i2c_state = I2C_STATE_RCV_ACK;
    					// Select the next byte to send    					
                        s->i2c_byte = gt911_read(s, s->address);
                        s->i2c_bit_no = 0;
    					// Point to next register for next read
                        s->address++;
                    }
                    break;
    
                case I2C_STATE_RCV_BITS:
    				// Get the bit from SDA and put it in the current byte
                    s->i2c_byte = (s->i2c_byte << 1) | ((value & I2C_SDA_PIN)? 1 : 0);
    
    				//Signal another bit is received
                    s->i2c_bit_no++;
    
    				// Check if it was the last bit
                    if (s->i2c_bit_no >= I2C_NUM_BITS) {
    					// Check on process state here
                        switch (s->state) {
                        case PANEL_WAIT_ADDRESS:
							// Check if received byte matches the device address
                            address = s->i2c_byte >> 1;
                            if ((address == 0x14) || (address == 0x5B)) {
								// Check on the data mode for further action
                                s->mode = s->i2c_byte & 0x01;

                                if (s->mode == I2C_MODE_WRITE) {
									// For write the next two bytes are the register address
                                    s->state = PANEL_RCV_REG_HIGH;
                                } else {
									// For a read data needs to be returned
                                    s->state = PANEL_SEND_DATA;
                                }
								// Switch to set ack state to show the device is present (The scope code does not use this)
                                s->i2c_state = I2C_STATE_SND_ACK;
                            } else {
								// Switch to skip ack state since the data is not fur this device
                                s->i2c_state = I2C_STATE_SKIP_ACK;
                            }
                            break;

                        case PANEL_RCV_REG_HIGH:
							// Received the high part of the address,
							// so set it in the panel address.
                            s->address = (s->i2c_byte << 8);
							// Switch to receiving low part of address
                            s->state = PANEL_RCV_REG_LOW;
							// Switch to set ack state to show the byte is received well (The scope code does not use this)
                            s->i2c_state = I2C_STATE_SND_ACK;
                            break;

                        case PANEL_RCV_REG_LOW:
							// Received the high part of the address so set it in the panel address
                            s->address |= s->i2c_byte;
							// Switch to receiving data
                            s->state = PANEL_RECEIVE_DATA;
							// Switch to set ack state to show the byte is received well (The scope code does not use this)
                            s->i2c_state = I2C_STATE_SND_ACK;
                            break;

                        case PANEL_RECEIVE_DATA:
#ifdef __DEBUG
if ((s->address != GT911_STATUS_REG) || s->i2c_byte) {
    fprintf(stderr, "WR %02x=>[%04x]\n", s->i2c_byte, s->address);
    fflush(stderr);
}
#endif
							// Just keep receiving data and increment the internal address
                            gt911_write(s, s->address, s->i2c_byte);
                            switch (s->address) {                        
                            case GT911_COMMAND_REG:
                            case GT911_CFG_VERSION_REG:
                            case GT911_XMAX_LO_REG:
                                break;
                            case GT911_STATUS_REG:
                                if (s->buttons_state) {
                                    // Mouse wasn't released, report touch event 
                                    gt911_save_touch_point(s, s->x, s->y);
                                } else {
                                    gt911_write(s, GT911_STATUS_REG, gt911_read(s, GT911_STATUS_REG) | GT911_STATUS_RDY);
                                }
                                break;
                            case GT911_PT1_COORD_REGS ... GT911_PT1_COORD_REGS + GT911_PT_TRK_ID_OFS:
                                break;
                            }                            
							// Next register address for write
                            s->address++;
							// Switch to set ack state to show the byte is received well (The scope code does not use this)
                            s->i2c_state = I2C_STATE_SND_ACK;
                            break;
                        }
    
    					// Clear the data byte and bit indicator
                        s->i2c_byte = 0;
                        s->i2c_bit_no = 0;
                    }
                    break;
    
                case I2C_STATE_SND_ACK:
    				// Need to send an ack bit on the next port read
                    value |= I2C_SDA_PIN;
    
    				// Check on the panel state for deciding on next state
                    if (s->state == PANEL_SEND_DATA) {
       					// Upper seven bits of the address are not used
                        switch (s->address) {                        
                        case GT911_COMMAND_REG:
                        case GT911_CFG_VERSION_REG:
                        case GT911_XMAX_LO_REG:
                            break;
                        case GT911_STATUS_REG:
							//Signal there is touch
							//gt911_write(s,  GT911_STATUS_REG, 0x81);
                            break;
                        case GT911_PT1_COORD_REGS ... GT911_PT1_COORD_REGS + GT911_PT_TRK_ID_OFS:
                            break;
                        }
    					// Load the current byte with the data to send
                        s->i2c_byte = gt911_read(s, s->address);
#ifdef __DEBUG
if ((s->address != GT911_STATUS_REG) || (s->i2c_byte & ~GT911_STATUS_RDY)) {
    fprintf(stderr, "RD [%04x]=>%02x(%d)\n", s->address, s->i2c_byte, s->i2c_bit_no);
    fflush(stderr);    					
}
#endif    					
    					// Next register address for read
                        s->address++;
    					// Switch to send bits state
                        s->i2c_state = I2C_STATE_SND_BITS;
                    } else {
    					// Switch to receive bits state
                        s->i2c_state = I2C_STATE_RCV_BITS;
                    }
                    break;
    
                case I2C_STATE_RCV_ACK:
    				// Get the acknowledgment state from SDA and set the correct state based on it
    				// Not really needed but nice to know the data was received well
    
    				// Switch to get bits state
                    s->i2c_state = I2C_STATE_SND_BITS;
                    break;
    
                default:
    				//Switch to idle state
                    s->i2c_state = I2C_STATE_IDLE;
                    break;
                }
            } else if ((value & I2C_SDA_PIN) != (reg & I2C_SDA_PIN)) {
    		    // On rising or falling edge of the SDA pin
    			// and SCL high there is a start or stop condition
                if (value & I2C_SCL_PIN) {
    				//Get the status of the SDA pin to see which condition has been received
                    if (value & I2C_SDA_PIN) {
    					//On SDA high a stop condition is received so switch to idle state
                        s->i2c_state = I2C_STATE_IDLE;
                    } else {
    					//On SDA low a start condition is received so clear the data byte and bit indicator
                        s->i2c_byte = 0;
                        s->i2c_bit_no = 0;
    					//Switch to get bit state
                        s->i2c_state = I2C_STATE_RCV_BITS;
    					//After a start the panel needs to be re addressed.
                        s->state = PANEL_WAIT_ADDRESS;
                    }
                }
            }
        }
    }
    return value;
}
//----------------------------------------------------------------------------------------------------------------------------------
static inline int int_clamp(int val, int vmin, int vmax)
{
    if (val < vmin)
        return vmin;
    else if (val > vmax)
        return vmax;
    else
        return val;
}
//----------------------------------------------------------------------------------------------------------------------------------
static void mouse_event(void *opaque,
                        int x1, int y1, int dz1, int buttons_state)
{
    struct TPI2CState *s = opaque;
    uint16_t x, y;

    // TODO: Avoid hardcoded screen resolution        
    x = x1 * 800 / 0x7FFF;
    y = y1 * 480 / 0x7FFF;

    s->x   = x;
    s->y   = y;
    s->dz += dz1;
    s->buttons_state = buttons_state;
    
    if ((buttons_state & 0x0F) && 
        !(gt911_read(s, GT911_STATUS_REG) & GT911_STATUS_HAVE_KEY)) {
#ifdef __DEBUG
fprintf(stderr, "x=%d(%d) y=%d(%d) dz=%d buttons=0x%x\n", x, x1, y, y1, dz1, buttons_state);
fflush(stderr);
#endif
        gt911_save_touch_point(s, x, y);
    }
}
//----------------------------------------------------------------------------------------------------------------------------------
void fnirsi_tp_init(struct AwPIOState *s)
{
    tp.state = PANEL_WAIT_ADDRESS;
    tp.mode = 0;
    tp.address = 0;
    memset(tp.data, 0x00, sizeof(tp.data));
    memcpy(&tp.data[0x47], gt911_config, sizeof(gt911_config));
    gt911_write(&tp, GT911_STATUS_REG, GT911_STATUS_RDY);

    tp.i2c_state = I2C_STATE_IDLE;
    tp.i2c_byte = 0;
    tp.i2c_bit_no = 0;
      
    tp.x = 0;
    tp.y = 0;
    tp.dz = 0;
    tp.buttons_state = 0;
    
    allwinner_set_pio_port_cb(s, PIO_A, &tp, 0, fnirsi_tp_write);
    tp.eh_entry = qemu_add_mouse_event_handler(mouse_event, &tp, 1, "FNIRSI-1013D TouchPad");
    qemu_activate_mouse_event_handler(tp.eh_entry);
}
