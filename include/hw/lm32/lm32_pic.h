#ifndef QEMU_HW_LM32_PIC_H
#define QEMU_HW_LM32_PIC_H


uint32_t lm32_pic_get_ip(DeviceState *d);
uint32_t lm32_pic_get_im(DeviceState *d);
void lm32_pic_set_ip(DeviceState *d, uint32_t ip);
void lm32_pic_set_im(DeviceState *d, uint32_t im);

#endif /* QEMU_HW_LM32_PIC_H */
