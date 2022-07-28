/*
 * FNIRSI FPGA emulation
 */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/gpio/allwinner-f1-pio.h"
#include "hw/gpio/fnirsi-1013d-pio.h"

//Port E defines for handling the FPGA

//FPGA control pins located in the second byte

#define FPGA_CLOCK_PIN         0x0100
#define FPGA_READ_WRITE_PIN    0x0200
#define FPGA_DATA_COMMAND_PIN  0x0400

//Mask for separating the two control lines
#define FPGA_CONTROL_MASK      (FPGA_DATA_COMMAND_PIN | FPGA_READ_WRITE_PIN)

#define FPGA_COMMAND_WRITE     0x0600
#define FPGA_COMMAND_READ      0x0400
#define FPGA_DATA_WRITE        0x0200
#define FPGA_DATA_READ         0x0000

#define MODE_DATA_READ         0x00
#define MODE_DATA_WRITE        0x01
#define MODE_COMMAND_WRITE     0x02


uint8_t version[2] = {0x14, 0x32};
uint8_t cmd0x14[2] = {0x07, 0xd5};
uint8_t cmd0x21[2];
uint8_t brightness[2] = {0x00, 0x00};
uint8_t tp_coords[2];
    
uint8_t chip[256] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00, 0xb8, 0x00, 0x00,
  0x00, 0x20, 0x00, 0x00, 0x00, 0x21, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xea, 0x60, 0x00, 0x00,
  0x00, 0xdb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x7e, 0xaf, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00    
};

uint8_t *adc1_data = 0;
uint8_t *adc2_data = 0;  

struct FpgaState
{
    uint8_t     mode;
    uint8_t     cmd;
    uint16_t    cnt;
    uint8_t   * ptr;
    
    // Data for parameter storage system
    uint8_t     crypt;
    uint8_t     ofs;
    uint8_t     data[8];
    uint8_t     flag;      
} fpga;

#define REG_INDEX(offset)    ((offset) / sizeof(uint32_t))

static uint32_t fnirsi_fpga_write(void *opaque, uint32_t *regs, uint32_t ofs, uint32_t value)
{
    struct FpgaState *s = (struct FpgaState *)opaque;
    const uint32_t idx = REG_INDEX(ofs);    
    const uint32_t reg = regs[idx];
    uint32_t i;
    
    if (ofs == REG_PIO_DATA) {

       	//For testing charge indicator. Setting this bit changes state to not charging
       	//registers->data |= 0x10;
    
    	//On rising edge of the clock pin data is being transfered
        if (((reg & FPGA_CLOCK_PIN) == 0) && (value & FPGA_CLOCK_PIN))
        {
    		//Check on command or data and read or write
            switch (value & FPGA_CONTROL_MASK) {
            case FPGA_COMMAND_WRITE:
                //trace_fpga(s, value);
                
				// set mode to command write
                s->mode = MODE_COMMAND_WRITE;
				//Set the new command as current
                
                s->ptr = 0;

				// Decide which action to take
                switch (value & 0xff) {
                case 0x06:
					//pd->print_command = 0;
                    s->ptr = version;
                    s->cnt = 2;
                    break;
                case 0x14:
					//pd->print_command = 0;
                    s->ptr = cmd0x14;
                    s->cnt = 2;
                    break;

                case 0x20:	// Channel 1
					//pd->print_command = 0;
                    s->ptr = adc1_data;
                    s->cnt = 1500;
                    break;

                case 0x21:	//Channel 2
					//pd->print_command = 0;
					value = (value & ~0xFF) | 0x70;
                    break;

                case 0x22:
                    s->ptr = adc2_data;
                    s->cnt = 1500;
                    break;

                case 0x23:
					value = (value & ~0xFF) | 0x20;
                    break;

                case 0x38:
					//pd->print_command = 0;
					//Set brightness
					s->ptr = brightness;
                    s->cnt = 1500;
                    break;

                case 0x41:
					// Read touch panel coordinates register address
					s->ptr = tp_coords;
                    s->cnt = 2;
                    break;

                case 0x64:
					//pd->print_command = 0;
                    // Read Parameters from the chip
                    break;

                case 0x65:
					//pd->print_command = 0;
                    // Write Parameters to the chip
                    break;

                case 0x66:
					//pd->print_command = 0;
					//Start the process based on the mode
                    if (s->cmd == 0x64) {
						// Check if this is the first read after start up
						if (s->flag == 0x00) {
                            s->crypt = 0x00;
                            s->ofs = 0;
                            // Put crypt value for reading
                            s->data[3] = s->crypt;                            						    
						} else {
						
                            memcpy(&s->data[3], &chip[s->ofs], 4);
    						// Add the crypt byte to the data
                            s->data[0] = s->crypt;
    						// Invert it for the crypting
                            s->crypt = ~s->crypt;
    						// Decide what size descriptor needs to be returned
                            if (s->data[3]) {
    							// More than 24 bits used then use 0xAA
                                s->data[1] = 0xAA;
                            } else if (s->data[4]) {
    							// More than 16 bits but less than 24 bits used then use 0xA5
                                s->data[1] = 0xA5;
                            } else if (s->data[5]) {
    							// More than 8 bits but less than 16 bits used then use 0x5A
                                s->data[1] = 0x5A;
                            } else {
    							// 8 bits or less used then use 0x55
                                s->data[1] = 0x55;
                            }
    						//Calculate the checksum
                            s->data[2] = s->crypt + s->data[1] + s->data[3] + s->data[4] + s->data[5] + s->data[6];
    
    						//Crypt the data
                            for (i = 1; i < 7; i++) {
                                s->data[i] ^= s->crypt;
                            }
                        }
                    } else if (s->cmd == 0x65) {
						//For write store the data in the file but the question is when??
						//Get the parameter id for file handling
                        s->ofs = s->data[1] ^ s->crypt;
                        // trace_fpga_crypt();
                    }
					s->flag = 0x01;
					// Need a flag to keep track of first read.
					// With this read the last crypt byte needs to be returned in pd->param_data[3]
					// The following write mangles this byte after processing the data and sends it in pd->param_data[0]
					// The following read decrypts the data but inverts the crypt byte before doing so
					// Use the data from 0x69 from previous write session to get the intended parameter. 0x00 (no write done before)
					// Needs to return the last crypt byte. Use 0 to avoid crypto???
					// Depending on data type the id + count byte (0x69) needs to be set to either 0x55, 0x5A, 0xA5 or 0xAA
					// one byte, two bytes, three bytes or four bytes of data
                    break;

                case 0x0A:
                case 0x67:
					//pd->print_command = 0;
                    //For this one the software tests against one and continues if so,
                    // else it waits for touch
					// Since the process is synchronous just respond with ready status
                    if (s->cmd == 0x66) {
                        s->ptr = &s->flag;
                        s->cnt = 1;
                    }
                    break;

                case 0x68:
                case 0x69:
                case 0x6A:
                case 0x6B:
                case 0x6C:
                case 0x6D:
                case 0x6E:
					//pd->print_command = 0;
                    if (s->cmd == 0x67) {
                        s->ptr = &s->data[s->cmd & 0x7];
                        s->cnt = 1;
                    }
                    break;
                }
                s->cmd = value & 0xff;
                break;

            case FPGA_DATA_WRITE:
				// Check if previous was a data read
                if (s->mode == MODE_DATA_READ) {
                    // Error
                } else {
					// When previous action was a command write switch to data write
                    s->mode = MODE_DATA_WRITE;

					// Store the data in the target register if needed
                    if (s->ptr) {
                        *s->ptr = value & 0xff;
                        if (s->cnt) {
                            if (--s->cnt) s->ptr++;
                            else          s->ptr = 0;                   
                        }
                    }
                }
                break;

            case FPGA_DATA_READ:
				// When previous action was a data write
                if (s->mode == MODE_DATA_WRITE) {
					// Check if previous was a write
                    value |= 0xFF;
                } else {
                    s->mode = MODE_DATA_READ;

                    if (s->ptr) {
                    
                        value = (value & ~0xFF) | *s->ptr;
    
                        if (s->cnt) {
                            if (--s->cnt) s->ptr++;
                            else          s->ptr = 0;                   
                        }
                    }
                }    
                break;                
            }
        }
    }
    return value;
}

void fnirsi_fpga_init(AwPIOState *s)
{
    fpga.cmd   = 0;
    fpga.cnt   = 0;
    fpga.ptr   = 0;
    fpga.crypt = 0;
    fpga.ofs   = 0;
    memset(fpga.data, 0, 8);
    fpga.flag = 0x00;
    
    allwinner_set_pio_port_cb(s, PIO_E, &fpga, 0, fnirsi_fpga_write);    
}
