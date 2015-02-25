#ifndef QEMU_HW_LM32_PIC_H
#define QEMU_HW_LM32_PIC_H

#include "qemu-common.h"

uint32_t lm32_pic_get_ip(DeviceState *d);
uint32_t lm32_pic_get_im(DeviceState *d);
void lm32_pic_set_ip(DeviceState *d, uint32_t ip);
void lm32_pic_set_im(DeviceState *d, uint32_t im);

void lm32_hmp_info_pic(Monitor *mon, const QDict *qdict);
void lm32_hmp_info_irq(Monitor *mon, const QDict *qdict);

#endif /* QEMU_HW_LM32_PIC_H */
