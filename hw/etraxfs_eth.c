/*
 * QEMU ETRAX Ethernet Controller.
 *
 * Copyright (c) 2008 Edgar E. Iglesias, Axis Communications AB.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include "hw.h"
#include "net.h"

#include "etraxfs_dma.h"

#define D(x)

#define R_STAT            0x2c
#define RW_MGM_CTRL       0x28
#define FS_ETH_MAX_REGS   0x5c



struct qemu_phy
{
	uint32_t regs[32];

	unsigned int (*read)(struct qemu_phy *phy, unsigned int req);
	void (*write)(struct qemu_phy *phy, unsigned int req, unsigned int data);
};

static unsigned int tdk_read(struct qemu_phy *phy, unsigned int req)
{
	int regnum;
	unsigned r = 0;

	regnum = req & 0x1f;

	switch (regnum) {
		case 1:
			/* MR1.  */
			/* Speeds and modes.  */
			r |= (1 << 13) | (1 << 14);
			r |= (1 << 11) | (1 << 12);
			r |= (1 << 5); /* Autoneg complete.  */
			r |= (1 << 3); /* Autoneg able.  */
			r |= (1 << 2); /* Link.  */
			break;
		default:
			r = phy->regs[regnum];
			break;
	}
	D(printf("%s %x = reg[%d]\n", __func__, r, regnum));
	return r;
}

static void 
tdk_write(struct qemu_phy *phy, unsigned int req, unsigned int data)
{
	int regnum;

	regnum = req & 0x1f;
	D(printf("%s reg[%d] = %x\n", __func__, regnum, data));
	switch (regnum) {
		default:
			phy->regs[regnum] = data;
			break;
	}
}

static void 
tdk_init(struct qemu_phy *phy)
{
	phy->read = tdk_read;
	phy->write = tdk_write;
}

struct qemu_mdio
{
	/* bus.  */
	int mdc;
	int mdio;

	/* decoder.  */
	enum {
		PREAMBLE,
		SOF,
		OPC,
		ADDR,
		REQ,
		TURNAROUND,
		DATA
	} state;
	unsigned int drive;

	unsigned int cnt;
	unsigned int addr;
	unsigned int opc;
	unsigned int req;
	unsigned int data;

	struct qemu_phy *devs[32];
};

static void 
mdio_attach(struct qemu_mdio *bus, struct qemu_phy *phy, unsigned int addr)
{
	bus->devs[addr & 0x1f] = phy;
}

static void 
mdio_detach(struct qemu_mdio *bus, struct qemu_phy *phy, unsigned int addr)
{
	bus->devs[addr & 0x1f] = NULL;	
}

static void mdio_read_req(struct qemu_mdio *bus)
{
	struct qemu_phy *phy;

	phy = bus->devs[bus->addr];
	if (phy && phy->read)
		bus->data = phy->read(phy, bus->req);
	else 
		bus->data = 0xffff;
}

static void mdio_write_req(struct qemu_mdio *bus)
{
	struct qemu_phy *phy;

	phy = bus->devs[bus->addr];
	if (phy && phy->write)
		phy->write(phy, bus->req, bus->data);
}

static void mdio_cycle(struct qemu_mdio *bus)
{
	bus->cnt++;

	D(printf("mdc=%d mdio=%d state=%d cnt=%d drv=%d\n",
		bus->mdc, bus->mdio, bus->state, bus->cnt, bus->drive));
#if 0
	if (bus->mdc)
		printf("%d", bus->mdio);
#endif
	switch (bus->state)
	{
		case PREAMBLE:
			if (bus->mdc) {
				if (bus->cnt >= (32 * 2) && !bus->mdio) {
					bus->cnt = 0;
					bus->state = SOF;
					bus->data = 0;
				}
			}
			break;
		case SOF:
			if (bus->mdc) {
				if (bus->mdio != 1)
					printf("WARNING: no SOF\n");
				if (bus->cnt == 1*2) {
					bus->cnt = 0;
					bus->opc = 0;
					bus->state = OPC;
				}
			}
			break;
		case OPC:
			if (bus->mdc) {
				bus->opc <<= 1;
				bus->opc |= bus->mdio & 1;
				if (bus->cnt == 2*2) {
					bus->cnt = 0;
					bus->addr = 0;
					bus->state = ADDR;
				}
			}
			break;
		case ADDR:
			if (bus->mdc) {
				bus->addr <<= 1;
				bus->addr |= bus->mdio & 1;

				if (bus->cnt == 5*2) {
					bus->cnt = 0;
					bus->req = 0;
					bus->state = REQ;
				}
			}
			break;
		case REQ:
			if (bus->mdc) {
				bus->req <<= 1;
				bus->req |= bus->mdio & 1;
				if (bus->cnt == 5*2) {
					bus->cnt = 0;
					bus->state = TURNAROUND;
				}
			}
			break;
		case TURNAROUND:
			if (bus->mdc && bus->cnt == 2*2) {
				bus->mdio = 0;
				bus->cnt = 0;

				if (bus->opc == 2) {
					bus->drive = 1;
					mdio_read_req(bus);
					bus->mdio = bus->data & 1;
				}
				bus->state = DATA;
			}
			break;
		case DATA:			
			if (!bus->mdc) {
				if (bus->drive) {
					bus->mdio = bus->data & 1;
					bus->data >>= 1;
				}
			} else {
				if (!bus->drive) {
					bus->data <<= 1;
					bus->data |= bus->mdio;
				}
				if (bus->cnt == 16 * 2) {
					bus->cnt = 0;
					bus->state = PREAMBLE;
					mdio_write_req(bus);
				}
			}
			break;
		default:
			break;
	}
}


struct fs_eth
{
        CPUState *env;
	qemu_irq *irq;
        target_phys_addr_t base;
	VLANClientState *vc;
	uint8_t macaddr[6];
	int ethregs;

	uint32_t regs[FS_ETH_MAX_REGS];

	unsigned char rx_fifo[1536];
	int rx_fifo_len;
	int rx_fifo_pos;

	struct etraxfs_dma_client *dma_out;
	struct etraxfs_dma_client *dma_in;

	/* MDIO bus.  */
	struct qemu_mdio mdio_bus;
	/* PHY.  */
	struct qemu_phy phy;
};

static uint32_t eth_rinvalid (void *opaque, target_phys_addr_t addr)
{
        struct fs_eth *eth = opaque;
        CPUState *env = eth->env;
        cpu_abort(env, "Unsupported short access. reg=%x pc=%x.\n", 
                  addr, env->pc);
        return 0;
}

static uint32_t eth_readl (void *opaque, target_phys_addr_t addr)
{
        struct fs_eth *eth = opaque;
        D(CPUState *env = eth->env);
        uint32_t r = 0;

        /* Make addr relative to this instances base.  */
        addr -= eth->base;
        switch (addr) {
		case R_STAT:
			/* Attach an MDIO/PHY abstraction.  */
			r = eth->mdio_bus.mdio & 1;
			break;
        default:
		r = eth->regs[addr];
                D(printf ("%s %x p=%x\n", __func__, addr, env->pc));
                break;
        }
        return r;
}

static void
eth_winvalid (void *opaque, target_phys_addr_t addr, uint32_t value)
{
        struct fs_eth *eth = opaque;
        CPUState *env = eth->env;
        cpu_abort(env, "Unsupported short access. reg=%x pc=%x.\n", 
                  addr, env->pc);
}

static void
eth_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
        struct fs_eth *eth = opaque;
        CPUState *env = eth->env;

        /* Make addr relative to this instances base.  */
        addr -= eth->base;
        switch (addr)
        {
		case RW_MGM_CTRL:
			/* Attach an MDIO/PHY abstraction.  */
			if (value & 2)
				eth->mdio_bus.mdio = value & 1;
			if (eth->mdio_bus.mdc != (value & 4))
				mdio_cycle(&eth->mdio_bus);
			eth->mdio_bus.mdc = !!(value & 4);
			break;

                default:
                        printf ("%s %x %x pc=%x\n",
                                __func__, addr, value, env->pc);
                        break;
        }
}

static int eth_can_receive(void *opaque)
{
	struct fs_eth *eth = opaque;
	int r;

	r = eth->rx_fifo_len == 0;
	if (!r) {
		/* TODO: signal fifo overrun.  */
		printf("PACKET LOSS!\n");
	}
	return r;
}

static void eth_receive(void *opaque, const uint8_t *buf, int size)
{
	struct fs_eth *eth = opaque;
	if (size > sizeof(eth->rx_fifo)) {
		/* TODO: signal error.  */
	} else {
		memcpy(eth->rx_fifo, buf, size);
		/* +4, HW passes the CRC to sw.  */
		eth->rx_fifo_len = size + 4;
		eth->rx_fifo_pos = 0;
	}
}

static void eth_rx_pull(void *opaque)
{
	struct fs_eth *eth = opaque;
	int len;
	if (eth->rx_fifo_len) {		
		D(printf("%s %d\n", __func__, eth->rx_fifo_len));
#if 0
		{
			int i;
			for (i = 0; i < 32; i++)
				printf("%2.2x", eth->rx_fifo[i]);
			printf("\n");
		}
#endif
		len = etraxfs_dmac_input(eth->dma_in,
					 eth->rx_fifo + eth->rx_fifo_pos, 
					 eth->rx_fifo_len, 1);
		eth->rx_fifo_len -= len;
		eth->rx_fifo_pos += len;
	}
}

static int eth_tx_push(void *opaque, unsigned char *buf, int len)
{
	struct fs_eth *eth = opaque;

	D(printf("%s buf=%p len=%d\n", __func__, buf, len));
	qemu_send_packet(eth->vc, buf, len);
	return len;
}

static CPUReadMemoryFunc *eth_read[] = {
    &eth_rinvalid,
    &eth_rinvalid,
    &eth_readl,
};

static CPUWriteMemoryFunc *eth_write[] = {
    &eth_winvalid,
    &eth_winvalid,
    &eth_writel,
};

void *etraxfs_eth_init(NICInfo *nd, CPUState *env, 
		       qemu_irq *irq, target_phys_addr_t base)
{
	struct etraxfs_dma_client *dma = NULL;	
	struct fs_eth *eth = NULL;

	dma = qemu_mallocz(sizeof *dma * 2);
	if (!dma)
		return NULL;

	eth = qemu_mallocz(sizeof *eth);
	if (!eth)
		goto err;

	dma[0].client.push = eth_tx_push;
	dma[0].client.opaque = eth;
	dma[1].client.opaque = eth;
	dma[1].client.pull = eth_rx_pull;

	eth->env = env;
	eth->base = base;
	eth->irq = irq;
	eth->dma_out = dma;
	eth->dma_in = dma + 1;
	memcpy(eth->macaddr, nd->macaddr, 6);

	/* Connect the phy.  */
	tdk_init(&eth->phy);
	mdio_attach(&eth->mdio_bus, &eth->phy, 0x1);

	eth->ethregs = cpu_register_io_memory(0, eth_read, eth_write, eth);
	cpu_register_physical_memory (base, 0x5c, eth->ethregs);

	eth->vc = qemu_new_vlan_client(nd->vlan, 
				       eth_receive, eth_can_receive, eth);

	return dma;
  err:
	qemu_free(eth);
	qemu_free(dma);
	return NULL;
}
