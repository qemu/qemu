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
#include "etraxfs.h"

#define D(x)

/* Advertisement control register. */
#define ADVERTISE_10HALF        0x0020  /* Try for 10mbps half-duplex  */
#define ADVERTISE_10FULL        0x0040  /* Try for 10mbps full-duplex  */
#define ADVERTISE_100HALF       0x0080  /* Try for 100mbps half-duplex */
#define ADVERTISE_100FULL       0x0100  /* Try for 100mbps full-duplex */

/* 
 * The MDIO extensions in the TDK PHY model were reversed engineered from the 
 * linux driver (PHYID and Diagnostics reg).
 * TODO: Add friendly names for the register nums.
 */
struct qemu_phy
{
	uint32_t regs[32];

	int link;

	unsigned int (*read)(struct qemu_phy *phy, unsigned int req);
	void (*write)(struct qemu_phy *phy, unsigned int req, 
		      unsigned int data);
};

static unsigned int tdk_read(struct qemu_phy *phy, unsigned int req)
{
	int regnum;
	unsigned r = 0;

	regnum = req & 0x1f;

	switch (regnum) {
		case 1:
			if (!phy->link)
				break;
			/* MR1.	 */
			/* Speeds and modes.  */
			r |= (1 << 13) | (1 << 14);
			r |= (1 << 11) | (1 << 12);
			r |= (1 << 5); /* Autoneg complete.  */
			r |= (1 << 3); /* Autoneg able.	 */
			r |= (1 << 2); /* link.	 */
			break;
		case 5:
			/* Link partner ability.
			   We are kind; always agree with whatever best mode
			   the guest advertises.  */
			r = 1 << 14; /* Success.  */
			/* Copy advertised modes.  */
			r |= phy->regs[4] & (15 << 5);
			/* Autoneg support.  */
			r |= 1;
			break;
		case 18:
		{
			/* Diagnostics reg.  */
			int duplex = 0;
			int speed_100 = 0;

			if (!phy->link)
				break;

			/* Are we advertising 100 half or 100 duplex ? */
			speed_100 = !!(phy->regs[4] & ADVERTISE_100HALF);
			speed_100 |= !!(phy->regs[4] & ADVERTISE_100FULL);

			/* Are we advertising 10 duplex or 100 duplex ? */
			duplex = !!(phy->regs[4] & ADVERTISE_100FULL);
			duplex |= !!(phy->regs[4] & ADVERTISE_10FULL);
			r = (speed_100 << 10) | (duplex << 11);
		}
		break;

		default:
			r = phy->regs[regnum];
			break;
	}
	D(printf("\n%s %x = reg[%d]\n", __func__, r, regnum));
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
	phy->regs[0] = 0x3100;
	/* PHY Id.  */
	phy->regs[2] = 0x0300;
	phy->regs[3] = 0xe400;
	/* Autonegotiation advertisement reg.  */
	phy->regs[4] = 0x01E1;
	phy->link = 1;

	phy->read = tdk_read;
	phy->write = tdk_write;
}

struct qemu_mdio
{
	/* bus.	 */
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

#ifdef USE_THIS_DEAD_CODE
static void 
mdio_detach(struct qemu_mdio *bus, struct qemu_phy *phy, unsigned int addr)
{
	bus->devs[addr & 0x1f] = NULL;	
}
#endif

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
					bus->mdio = !!(bus->data & (1 << 15));
					bus->data <<= 1;
				}
			} else {
				if (!bus->drive) {
					bus->data <<= 1;
					bus->data |= bus->mdio;
				}
				if (bus->cnt == 16 * 2) {
					bus->cnt = 0;
					bus->state = PREAMBLE;
					if (!bus->drive)
						mdio_write_req(bus);
					bus->drive = 0;
				}
			}
			break;
		default:
			break;
	}
}

/* ETRAX-FS Ethernet MAC block starts here.  */

#define RW_MA0_LO	  0x00
#define RW_MA0_HI	  0x01
#define RW_MA1_LO	  0x02
#define RW_MA1_HI	  0x03
#define RW_GA_LO	  0x04
#define RW_GA_HI	  0x05
#define RW_GEN_CTRL	  0x06
#define RW_REC_CTRL	  0x07
#define RW_TR_CTRL	  0x08
#define RW_CLR_ERR	  0x09
#define RW_MGM_CTRL	  0x0a
#define R_STAT		  0x0b
#define FS_ETH_MAX_REGS	  0x17

struct fs_eth
{
	NICState *nic;
	NICConf conf;
	int ethregs;

	/* Two addrs in the filter.  */
	uint8_t macaddr[2][6];
	uint32_t regs[FS_ETH_MAX_REGS];

	struct etraxfs_dma_client *dma_out;
	struct etraxfs_dma_client *dma_in;

	/* MDIO bus.  */
	struct qemu_mdio mdio_bus;
	unsigned int phyaddr;
	int duplex_mismatch;

	/* PHY.	 */
	struct qemu_phy phy;
};

static void eth_validate_duplex(struct fs_eth *eth)
{
	struct qemu_phy *phy;
	unsigned int phy_duplex;
	unsigned int mac_duplex;
	int new_mm = 0;

	phy = eth->mdio_bus.devs[eth->phyaddr];
	phy_duplex = !!(phy->read(phy, 18) & (1 << 11));
	mac_duplex = !!(eth->regs[RW_REC_CTRL] & 128);

	if (mac_duplex != phy_duplex)
		new_mm = 1;

	if (eth->regs[RW_GEN_CTRL] & 1) {
		if (new_mm != eth->duplex_mismatch) {
			if (new_mm)
				printf("HW: WARNING "
				       "ETH duplex mismatch MAC=%d PHY=%d\n",
				       mac_duplex, phy_duplex);
			else
				printf("HW: ETH duplex ok.\n");
		}
		eth->duplex_mismatch = new_mm;
	}
}

static uint32_t eth_readl (void *opaque, target_phys_addr_t addr)
{
	struct fs_eth *eth = opaque;
	uint32_t r = 0;

	addr >>= 2;

	switch (addr) {
		case R_STAT:
			r = eth->mdio_bus.mdio & 1;
			break;
	default:
		r = eth->regs[addr];
		D(printf ("%s %x\n", __func__, addr * 4));
		break;
	}
	return r;
}

static void eth_update_ma(struct fs_eth *eth, int ma)
{
	int reg;
	int i = 0;

	ma &= 1;

	reg = RW_MA0_LO;
	if (ma)
		reg = RW_MA1_LO;

	eth->macaddr[ma][i++] = eth->regs[reg];
	eth->macaddr[ma][i++] = eth->regs[reg] >> 8;
	eth->macaddr[ma][i++] = eth->regs[reg] >> 16;
	eth->macaddr[ma][i++] = eth->regs[reg] >> 24;
	eth->macaddr[ma][i++] = eth->regs[reg + 1];
	eth->macaddr[ma][i] = eth->regs[reg + 1] >> 8;

	D(printf("set mac%d=%x.%x.%x.%x.%x.%x\n", ma,
		 eth->macaddr[ma][0], eth->macaddr[ma][1],
		 eth->macaddr[ma][2], eth->macaddr[ma][3],
		 eth->macaddr[ma][4], eth->macaddr[ma][5]));
}

static void
eth_writel (void *opaque, target_phys_addr_t addr, uint32_t value)
{
	struct fs_eth *eth = opaque;

	addr >>= 2;
	switch (addr)
	{
		case RW_MA0_LO:
		case RW_MA0_HI:
			eth->regs[addr] = value;
			eth_update_ma(eth, 0);
			break;
		case RW_MA1_LO:
		case RW_MA1_HI:
			eth->regs[addr] = value;
			eth_update_ma(eth, 1);
			break;

		case RW_MGM_CTRL:
			/* Attach an MDIO/PHY abstraction.  */
			if (value & 2)
				eth->mdio_bus.mdio = value & 1;
			if (eth->mdio_bus.mdc != (value & 4)) {
				mdio_cycle(&eth->mdio_bus);
				eth_validate_duplex(eth);
			}
			eth->mdio_bus.mdc = !!(value & 4);
			eth->regs[addr] = value;
			break;

		case RW_REC_CTRL:
			eth->regs[addr] = value;
			eth_validate_duplex(eth);
			break;

		default:
			eth->regs[addr] = value;
			D(printf ("%s %x %x\n",
				  __func__, addr, value));
			break;
	}
}

/* The ETRAX FS has a groupt address table (GAT) which works like a k=1 bloom
   filter dropping group addresses we have not joined.	The filter has 64
   bits (m). The has function is a simple nible xor of the group addr.	*/
static int eth_match_groupaddr(struct fs_eth *eth, const unsigned char *sa)
{
	unsigned int hsh;
	int m_individual = eth->regs[RW_REC_CTRL] & 4;
	int match;

	/* First bit on the wire of a MAC address signals multicast or
	   physical address.  */
	if (!m_individual && !(sa[0] & 1))
		return 0;

	/* Calculate the hash index for the GA registers. */
	hsh = 0;
	hsh ^= (*sa) & 0x3f;
	hsh ^= ((*sa) >> 6) & 0x03;
	++sa;
	hsh ^= ((*sa) << 2) & 0x03c;
	hsh ^= ((*sa) >> 4) & 0xf;
	++sa;
	hsh ^= ((*sa) << 4) & 0x30;
	hsh ^= ((*sa) >> 2) & 0x3f;
	++sa;
	hsh ^= (*sa) & 0x3f;
	hsh ^= ((*sa) >> 6) & 0x03;
	++sa;
	hsh ^= ((*sa) << 2) & 0x03c;
	hsh ^= ((*sa) >> 4) & 0xf;
	++sa;
	hsh ^= ((*sa) << 4) & 0x30;
	hsh ^= ((*sa) >> 2) & 0x3f;

	hsh &= 63;
	if (hsh > 31)
		match = eth->regs[RW_GA_HI] & (1 << (hsh - 32));
	else
		match = eth->regs[RW_GA_LO] & (1 << hsh);
	D(printf("hsh=%x ga=%x.%x mtch=%d\n", hsh,
		 eth->regs[RW_GA_HI], eth->regs[RW_GA_LO], match));
	return match;
}

static int eth_can_receive(VLANClientState *nc)
{
	return 1;
}

static ssize_t eth_receive(VLANClientState *nc, const uint8_t *buf, size_t size)
{
	unsigned char sa_bcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	struct fs_eth *eth = DO_UPCAST(NICState, nc, nc)->opaque;
	int use_ma0 = eth->regs[RW_REC_CTRL] & 1;
	int use_ma1 = eth->regs[RW_REC_CTRL] & 2;
	int r_bcast = eth->regs[RW_REC_CTRL] & 8;

	if (size < 12)
		return -1;

	D(printf("%x.%x.%x.%x.%x.%x ma=%d %d bc=%d\n",
		 buf[0], buf[1], buf[2], buf[3], buf[4], buf[5],
		 use_ma0, use_ma1, r_bcast));
	       
	/* Does the frame get through the address filters?  */
	if ((!use_ma0 || memcmp(buf, eth->macaddr[0], 6))
	    && (!use_ma1 || memcmp(buf, eth->macaddr[1], 6))
	    && (!r_bcast || memcmp(buf, sa_bcast, 6))
	    && !eth_match_groupaddr(eth, buf))
		return size;

	/* FIXME: Find another way to pass on the fake csum.  */
	etraxfs_dmac_input(eth->dma_in, (void *)buf, size + 4, 1);

        return size;
}

static int eth_tx_push(void *opaque, unsigned char *buf, int len)
{
	struct fs_eth *eth = opaque;

	D(printf("%s buf=%p len=%d\n", __func__, buf, len));
	qemu_send_packet(&eth->nic->nc, buf, len);
	return len;
}

static void eth_set_link(VLANClientState *nc)
{
	struct fs_eth *eth = DO_UPCAST(NICState, nc, nc)->opaque;
	D(printf("%s %d\n", __func__, nc->link_down));
	eth->phy.link = !nc->link_down;
}

static CPUReadMemoryFunc * const eth_read[] = {
	NULL, NULL,
	&eth_readl,
};

static CPUWriteMemoryFunc * const eth_write[] = {
	NULL, NULL,
	&eth_writel,
};

static void eth_cleanup(VLANClientState *nc)
{
	struct fs_eth *eth = DO_UPCAST(NICState, nc, nc)->opaque;

        cpu_unregister_io_memory(eth->ethregs);

	/* Disconnect the client.  */
	eth->dma_out->client.push = NULL;
	eth->dma_out->client.opaque = NULL;
	eth->dma_in->client.opaque = NULL;
	eth->dma_in->client.pull = NULL;
        qemu_free(eth);
}

static NetClientInfo net_etraxfs_info = {
	.type = NET_CLIENT_TYPE_NIC,
	.size = sizeof(NICState),
	.can_receive = eth_can_receive,
	.receive = eth_receive,
	.cleanup = eth_cleanup,
	.link_status_changed = eth_set_link,
};

void etraxfs_eth_init(NICInfo *nd, target_phys_addr_t base, int phyaddr,
                       struct etraxfs_dma_client *dma_out,
                       struct etraxfs_dma_client *dma_in)
{
	struct fs_eth *eth = NULL;

	qemu_check_nic_model(nd, "fseth");

	eth = qemu_mallocz(sizeof *eth);

	dma_out->client.push = eth_tx_push;
	dma_out->client.opaque = eth;
	dma_in->client.opaque = eth;
	dma_in->client.pull = NULL;

	eth->dma_out = dma_out;
	eth->dma_in = dma_in;

	/* Connect the phy.  */
	eth->phyaddr = phyaddr & 0x1f;
	tdk_init(&eth->phy);
	mdio_attach(&eth->mdio_bus, &eth->phy, eth->phyaddr);

	eth->ethregs = cpu_register_io_memory(eth_read, eth_write, eth,
                                              DEVICE_NATIVE_ENDIAN);
	cpu_register_physical_memory (base, 0x5c, eth->ethregs);

	eth->conf.macaddr = nd->macaddr;
	eth->conf.vlan = nd->vlan;
	eth->conf.peer = nd->netdev;

	eth->nic = qemu_new_nic(&net_etraxfs_info, &eth->conf,
				nd->model, nd->name, eth);
}
