#ifndef QEMU_HW_CHAR_LM32_JUART_H
#define QEMU_HW_CHAR_LM32_JUART_H

#include "hw/qdev.h"

#define TYPE_LM32_JUART "lm32-juart"

uint32_t lm32_juart_get_jtx(DeviceState *d);
uint32_t lm32_juart_get_jrx(DeviceState *d);
void lm32_juart_set_jtx(DeviceState *d, uint32_t jtx);
void lm32_juart_set_jrx(DeviceState *d, uint32_t jrx);

#endif /* QEMU_HW_CHAR_LM32_JUART_H */
