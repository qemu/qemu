/*
 * PCMCIA emulation
 *
 * Copyright 2013 SUSE LINUX Products GmbH
 */

#include "qemu-common.h"
#include "hw/hw.h"
#include "hw/pcmcia.h"

static const TypeInfo pcmcia_card_type_info = {
    .name = TYPE_PCMCIA_CARD,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(PCMCIACardState),
    .abstract = true,
    .class_size = sizeof(PCMCIACardClass),
};

static void pcmcia_register_types(void)
{
    type_register_static(&pcmcia_card_type_info);
}

type_init(pcmcia_register_types)
