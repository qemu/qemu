#ifndef GRETH_H
#define GRETH_H

#include "hw/sysbus.h"
#include "net/net.h"

struct GRETHState {
    /*< private >*/
    SysBusDevice parent;

    /* Address space for internal DMA that can be changed during board init */
    AddressSpace *addr_space;

    /*< public >*/
    MemoryRegion iomem;

    NICConf conf;
    NICState *nic;

    uint32_t ctrl;
    uint32_t status;
    uint32_t mac_msb;
    uint32_t mac_lsb;
    uint32_t send_desc;
    uint32_t recv_desc;
    uint32_t mdio;

    uint16_t phy_ctrl;

    qemu_irq irq;
};

typedef struct GRETHState GRETHState;

#define TYPE_GRETH "greth"
#define GRETH(obj) OBJECT_CHECK(GRETHState, (obj), TYPE_GRETH)

void greth_change_address_space(GRETHState *s, AddressSpace *addr_space, Error **errp);

#endif /* GRETH_H */
