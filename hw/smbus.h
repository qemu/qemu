/*
 * QEMU SMBus API
 * 
 * Copyright (c) 2007 Arastra, Inc.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

typedef struct SMBusDevice SMBusDevice;

struct SMBusDevice {
    uint8_t addr;
    void (*quick_cmd)(SMBusDevice *dev, uint8_t read);
    void (*send_byte)(SMBusDevice *dev, uint8_t val);
    uint8_t (*receive_byte)(SMBusDevice *dev);
    void (*write_byte)(SMBusDevice *dev, uint8_t cmd, uint8_t val);
    uint8_t (*read_byte)(SMBusDevice *dev, uint8_t cmd);
    void (*write_word)(SMBusDevice *dev, uint8_t cmd, uint16_t val);
    uint16_t (*read_word)(SMBusDevice *dev, uint8_t cmd);
    void (*write_block)(SMBusDevice *dev, uint8_t cmd, uint8_t len, uint8_t *buf);
    uint8_t (*read_block)(SMBusDevice *dev, uint8_t cmd, uint8_t *buf);
};
