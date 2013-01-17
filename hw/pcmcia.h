#ifndef HW_PCMCIA_H
#define HW_PCMCIA_H 1

/* PCMCIA/Cardbus */

#include "qemu-common.h"

typedef struct {
    qemu_irq irq;
    int attached;
    const char *slot_string;
    const char *card_string;
} PCMCIASocket;

void pcmcia_socket_register(PCMCIASocket *socket);
void pcmcia_socket_unregister(PCMCIASocket *socket);
void pcmcia_info(Monitor *mon, const QDict *qdict);

struct PCMCIACardState {
    void *state;
    PCMCIASocket *slot;
    int (*attach)(void *state);
    int (*detach)(void *state);
    const uint8_t *cis;
    int cis_len;

    /* Only valid if attached */
    uint8_t (*attr_read)(void *state, uint32_t address);
    void (*attr_write)(void *state, uint32_t address, uint8_t value);
    uint16_t (*common_read)(void *state, uint32_t address);
    void (*common_write)(void *state, uint32_t address, uint16_t value);
    uint16_t (*io_read)(void *state, uint32_t address);
    void (*io_write)(void *state, uint32_t address, uint16_t value);
};

#define CISTPL_DEVICE		0x01	/* 5V Device Information Tuple */
#define CISTPL_NO_LINK		0x14	/* No Link Tuple */
#define CISTPL_VERS_1		0x15	/* Level 1 Version Tuple */
#define CISTPL_JEDEC_C		0x18	/* JEDEC ID Tuple */
#define CISTPL_JEDEC_A		0x19	/* JEDEC ID Tuple */
#define CISTPL_CONFIG		0x1a	/* Configuration Tuple */
#define CISTPL_CFTABLE_ENTRY	0x1b	/* 16-bit PCCard Configuration */
#define CISTPL_DEVICE_OC	0x1c	/* Additional Device Information */
#define CISTPL_DEVICE_OA	0x1d	/* Additional Device Information */
#define CISTPL_DEVICE_GEO	0x1e	/* Additional Device Information */
#define CISTPL_DEVICE_GEO_A	0x1f	/* Additional Device Information */
#define CISTPL_MANFID		0x20	/* Manufacture ID Tuple */
#define CISTPL_FUNCID		0x21	/* Function ID Tuple */
#define CISTPL_FUNCE		0x22	/* Function Extension Tuple */
#define CISTPL_END		0xff	/* Tuple End */
#define CISTPL_ENDMARK		0xff

/* dscm1xxxx.c */
PCMCIACardState *dscm1xxxx_init(DriveInfo *bdrv);

#endif
