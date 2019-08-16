/*
 * CBUS three-pin bus and the Retu / Betty / Tahvo / Vilma / Avilma /
 * Hinku / Vinku / Ahne / Pihi chips used in various Nokia platforms.
 * Based on reverse-engineering of a linux driver.
 *
 * Copyright (C) 2008 Nokia Corporation
 * Written by Andrzej Zaborowski
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_MISC_CBUS_H
#define HW_MISC_CBUS_H


typedef struct {
    qemu_irq clk;
    qemu_irq dat;
    qemu_irq sel;
} CBus;

CBus *cbus_init(qemu_irq dat_out);
void cbus_attach(CBus *bus, void *slave_opaque);

void *retu_init(qemu_irq irq, int vilma);
void *tahvo_init(qemu_irq irq, int betty);

void retu_key_event(void *retu, int state);

#endif
