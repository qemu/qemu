/*
 * SD Memory Card emulation (deprecated legacy API)
 *
 * Copyright (c) 2006 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef HW_SDCARD_LEGACY_H
#define HW_SDCARD_LEGACY_H

#include "hw/sd/sd.h"

/* Legacy functions to be used only by non-qdevified callers */
SDState *sd_init(BlockBackend *blk, bool is_spi);
int sd_do_command(SDState *card, SDRequest *request, uint8_t *response);
void sd_write_byte(SDState *card, uint8_t value);
uint8_t sd_read_byte(SDState *card);
void sd_set_cb(SDState *card, qemu_irq readonly, qemu_irq insert);

/* sd_enable should not be used -- it is only used on the nseries boards,
 * where it is part of a broken implementation of the MMC card slot switch
 * (there should be two card slots which are multiplexed to a single MMC
 * controller, but instead we model it with one card and controller and
 * disable the card when the second slot is selected, so it looks like the
 * second slot is always empty).
 */
void sd_enable(SDState *card, bool enable);

#endif /* HW_SDCARD_LEGACY_H */
