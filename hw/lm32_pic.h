#ifndef QEMU_HW_LM32_PIC_H
#define QEMU_HW_LM32_PIC_H

#include "qemu-common.h"

uint32_t lm32_pic_get_ip(DeviceState *d);
uint32_t lm32_pic_get_im(DeviceState *d);
void lm32_pic_set_ip(DeviceState *d, uint32_t ip);
void lm32_pic_set_im(DeviceState *d, uint32_t im);

void lm32_do_pic_info(Monitor *mon);
void lm32_irq_info(Monitor *mon);

#endif /* QEMU_HW_LM32_PIC_H */
