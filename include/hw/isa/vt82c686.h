#ifndef HW_VT82C686_H
#define HW_VT82C686_H

#include "hw/pci/pci_device.h"
#include "audio/audio.h"

#define TYPE_VT82C686B_ISA "vt82c686b-isa"
#define TYPE_VT82C686B_USB_UHCI "vt82c686b-usb-uhci"
#define TYPE_VT8231_ISA "vt8231-isa"
#define TYPE_VIA_AC97 "via-ac97"
#define TYPE_VIA_IDE "via-ide"
#define TYPE_VIA_MC97 "via-mc97"

typedef struct {
    uint8_t stat;
    uint8_t type;
    uint32_t base;
    uint32_t curr;
    uint32_t addr;
    uint32_t clen;
} ViaAC97SGDChannel;

OBJECT_DECLARE_SIMPLE_TYPE(ViaAC97State, VIA_AC97);

struct ViaAC97State {
    PCIDevice dev;
    QEMUSoundCard card;
    MemoryRegion sgd;
    MemoryRegion fm;
    MemoryRegion midi;
    SWVoiceOut *vo;
    ViaAC97SGDChannel aur;
    uint16_t codec_regs[128];
    uint32_t ac97_cmd;
};

#endif
