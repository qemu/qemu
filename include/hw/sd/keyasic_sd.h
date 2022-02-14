#ifndef KEYASIC_SD_H
#define KEYASIC_SD_H

#include "hw/sysbus.h"

struct KeyasicSdState {
    /*< private >*/
    SysBusDevice parent;

    /*< public >*/
    MemoryRegion iomem;

    // SD Card Block Set Register
    uint32_t scbsr;
    // SD Card Control Register
    uint32_t sccr;
    // SD Card Argument Register
    uint32_t scargr;
    // SD Card Address Register
    uint32_t csaddr;
    // SD Card Status Register
    uint32_t scsr;
    // SD Card Error Enable Register
    uint32_t sceer;
    // SD Card Response Register1
    uint32_t scrr1;
    // SD Card Response Register2
    uint32_t scrr2;
    // SD Card Response Register3
    uint32_t scrr3;
    // SD Card Response Register4
    uint32_t scrr4;
    // SD Card Buffer Transfer Response Register
    uint32_t scbtrr;
    // SD Card Buffer Transfer Control Register
    uint32_t scbtcr;
};

typedef struct KeyasicSdState KeyasicSdState;

#define TYPE_KEYASIC_SD "keyasic_sd"
#define KEYASIC_SD(obj) OBJECT_CHECK(KeyasicSdState, (obj), TYPE_KEYASIC_SD)

#endif /* KEYASIC_SD_H */
