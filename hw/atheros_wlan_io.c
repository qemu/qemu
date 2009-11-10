/**
 * QEMU WLAN device emulation
 *
 * Copyright (c) 2008 Clemens Kolbitsch
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
 *
 * Modifications:
 *  2008-February-24  Clemens Kolbitsch :
 *                                  New implementation based on ne2000.c
 *
 */


#include "config-host.h"

#if defined(CONFIG_WIN32)
#warning("not compiled for Windows host")
#else

#include "hw.h"
#include "pci.h"
#include "pc.h"
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <signal.h>

#include <time.h>
#include <sys/time.h>

#include "hw/atheros_wlan.h"
#include "hw/atheros_wlan_ap.h"
#include "hw/atheros_wlan_io.h"

/*
 * MadWifi OPENHAL atheros constants
 */
#include "hw/ath5k.h"
#include "hw/ath5k_hw.h"
#include "hw/ath5kreg.h"


static const struct Atheros_WLAN_frequency Atheros_WLAN_frequency_data[] =
	{
		{ 20689, 3077, 2412 },		// channel 1
		{ 20715, 3078, 2417 },		// channel 2
		{ 20689, 3079, 2422 },		// channel 3
		{ 20715, 3079, 2427 },		// channel 4
		{ 20529, 3076, 2432 },		// channel 5
		{ 20507, 3078, 2437 },		// channel 6
		{ 20529, 3078, 2442 },		// channel 7
		{ 20507, 3079, 2447 },		// channel 8
		{ 20529, 3077, 2452 },		// channel 9
		{ 20635, 3078, 2457 },		// channel 10
		{ 20529, 3079, 2462 },		// channel 11
		{ 20635, 3079, 2467 },		// channel 12
		{ 20657, 3076, 2472 },		// channel 13
		{ 20529, 1029, 2484 }		// channel 14
	};

/*
 * NOTE: By using this function instead
 * of accessing the array directly through
 * an index, we can leave out parts of the
 * EEPROM data!!
 */
static int get_eeprom_data(Atheros_WLANState *s, uint32_t addr, uint32_t *val)
{
	if (val == NULL)
	{
		return 1;
	}

	// why?? but seems necessary...
	addr--;

	if ((addr < 0) || (addr > s->eeprom_size))
	{
		return 2;
	}

	*val = s->eeprom_data[addr];
	return 0;
}






static void updateFrequency(Atheros_WLANState *s)
{
	int i;
	u_int32_t new_frequency = 0;
	for (i=0; i < sizeof(Atheros_WLAN_frequency_data) / sizeof(Atheros_WLAN_frequency_data[0]); i++)
	{
		if (Atheros_WLAN_frequency_data[i].value1 != s->current_frequency_partial_data[0])
			continue;

		if (Atheros_WLAN_frequency_data[i].value2 != s->current_frequency_partial_data[1])
			continue;

		new_frequency = Atheros_WLAN_frequency_data[i].frequency;
		break;
	}

	if (new_frequency)
	{
		s->current_frequency = new_frequency;
	}
}



static uint32_t mm_readl(Atheros_WLANState *s, target_phys_addr_t addr);
static void mm_writel(Atheros_WLANState *s, target_phys_addr_t addr, uint32_t val);

static void Atheros_WLAN_mmio_writeb(void *opaque, target_phys_addr_t addr, uint32_t val)
{
	DEBUG_PRINT(("!!! DEBUG INIMPLEMENTED !!!\n"));
	DEBUG_PRINT(("mmio_writeb %x val %x\n", addr, val));
	DEBUG_PRINT(("!!! DEBUG INIMPLEMENTED !!!\n"));
}

static void Atheros_WLAN_mmio_writew(void *opaque, target_phys_addr_t addr, uint32_t val)
{
	DEBUG_PRINT(("!!! DEBUG INIMPLEMENTED !!!\n"));
	DEBUG_PRINT(("mmio_writew %x val %x\n", addr, val));
	DEBUG_PRINT(("!!! DEBUG INIMPLEMENTED !!!\n"));
}

static void Atheros_WLAN_mmio_writel(void *opaque, target_phys_addr_t addr, uint32_t val)
{
	mm_writel((Atheros_WLANState *)opaque, Atheros_WLAN_MEM_SANITIZE(addr), val);
	DEBUG_PRINT(("  through call: mmio_writel 0x%x (%u) val 0x%x (%u)\n", Atheros_WLAN_MEM_SANITIZE(addr), Atheros_WLAN_MEM_SANITIZE(addr), val, val));
}

static uint32_t Atheros_WLAN_mmio_readb(void *opaque, target_phys_addr_t addr)
{
	DEBUG_PRINT(("!!! DEBUG INIMPLEMENTED !!!\n"));
	DEBUG_PRINT(("mmio_readb %u\n", addr));
	DEBUG_PRINT(("!!! DEBUG INIMPLEMENTED !!!\n"));

	return 0;
}

static uint32_t Atheros_WLAN_mmio_readw(void *opaque, target_phys_addr_t addr)
{
	DEBUG_PRINT(("!!! DEBUG INIMPLEMENTED !!!\n"));
	DEBUG_PRINT(("mmio_readw %u\n", addr));
	DEBUG_PRINT(("!!! DEBUG INIMPLEMENTED !!!\n"));

	return 0;
}

static uint32_t Atheros_WLAN_mmio_readl(void *opaque, target_phys_addr_t addr)
{
	uint32_t val;
	val = mm_readl((Atheros_WLANState *)opaque, Atheros_WLAN_MEM_SANITIZE(addr));

	DEBUG_PRINT(("   mmio_readl 0x%x (%u) = 0x%x (%u)\n", Atheros_WLAN_MEM_SANITIZE(addr), Atheros_WLAN_MEM_SANITIZE(addr), val, val));
	return val;
}


static void Atheros_WLAN_mmio_map(PCIDevice *pci_dev, int region_num,
                                  pcibus_t addr, pcibus_t size, int type)
{
	DEBUG_PRINT(("mmio_map\n"));
	PCIAtheros_WLANState *d = (PCIAtheros_WLANState *)pci_dev;
	Atheros_WLANState *s = &d->Atheros_WLAN;

    DEBUG_PRINT(("cpu_register_physical_memory(0x%08" FMT_PCIBUS ", %u, %p)\n",
                 addr, Atheros_WLAN_MEM_SIZE, (unsigned long*)s->Atheros_WLAN_mmio_io_addr));

	cpu_register_physical_memory(addr + 0, Atheros_WLAN_MEM_SIZE, s->Atheros_WLAN_mmio_io_addr);
}

static CPUReadMemoryFunc *Atheros_WLAN_mmio_read[3] = {
    Atheros_WLAN_mmio_readb,
    Atheros_WLAN_mmio_readw,
    Atheros_WLAN_mmio_readl,
};

static CPUWriteMemoryFunc *Atheros_WLAN_mmio_write[3] = {
    Atheros_WLAN_mmio_writeb,
    Atheros_WLAN_mmio_writew,
    Atheros_WLAN_mmio_writel,
};

void Atheros_WLAN_setup_io(PCIAtheros_WLANState *d)
{
    Atheros_WLANState *s;
    s = &d->Atheros_WLAN;

    /* I/O handler for memory-mapped I/O */
    s->Atheros_WLAN_mmio_io_addr =
        cpu_register_io_memory(Atheros_WLAN_mmio_read,
                               Atheros_WLAN_mmio_write, s);
    pci_register_bar(&d->dev, 0, Atheros_WLAN_MEM_SIZE,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, Atheros_WLAN_mmio_map);
}
















#define FASTBINLOG(x)							\
		(x == 1)    ?  0 :					\
		(x == 2)    ?  1 :					\
		(x == 4)    ?  2 :					\
		(x == 8)    ?  3 : 					\
		(x == 16)   ?  4 :					\
		(x == 32)   ?  5 :					\
		(x == 64)   ?  6 :					\
		(x == 128)  ?  7 :					\
		(x == 256)  ?  8 :					\
		(x == 512)  ?  9 :					\
		(x == 1024) ? 10 :					\
		(x == 2048) ? 11 :					\
		(x == 4096) ? 12 : 13



static uint32_t mm_readl(Atheros_WLANState *s, target_phys_addr_t addr)
{
	uint32_t val = GET_MEM_L(s->mem, addr);
	switch (addr)
	{
		case ATH_HW_IRQ_PENDING:
			/*
			 * This indicates that the interrupt
			 * routine has been called. reset interrupt
			 * status and put the interrupt-status
			 * number at the correct memory-location
			 *
			 * In case multiple interrupts are pending
			 * this memory-location is checked multiple
			 * times... each time, we put another interrupt
			 * status into memory until no more interrupts
			 * have to be handled
			 */
			Atheros_WLAN_disable_irq(s);

			DEBUG_PRINT((">> irq pending? ... 0x%x\n", val));
			SET_MEM_L(s->mem, 0x0080, 0x0);
			SET_MEM_L(s->mem, 0x80ec, 0x0001c680);
			SET_MEM_L(s->mem, 0x80f0, 0x000055dc);
			SET_MEM_L(s->mem, 0x80f8, 0x0015f6fc);
			SET_MEM_L(s->mem, 0x9850, 0x0de8b0da);
			break;

		/*
		 * The following registers are Read-and-Clear
		 * registers --> must be reset after a read!!
		 *
		 * However, it does not work when using linux!!
		 */
		case AR5K_PISR:
			if (s->device_driver_type == WINXP_DRIVER)
			{
				addr = AR5K_RAC_PISR;
				// fall through...
			}
			else
			{
				break;
			}

		case AR5K_RAC_PISR:
			Atheros_WLAN_update_irq(s);
			val = GET_MEM_L(s->mem, addr);
			SET_MEM_L(s->mem, addr, 0);
			SET_MEM_L(s->mem, AR5K_PCICFG, 0x34);

			DEBUG_PRINT((">> irq status 0x%x\n", val));
			break;

		case AR5K_RAC_SISR0:
		case AR5K_RAC_SISR1:
		case AR5K_RAC_SISR2:
		case AR5K_RAC_SISR3:
		case AR5K_RAC_SISR4:
			val = 0;
			SET_MEM_L(s->mem, addr, 0);
			DEBUG_PRINT(("secondary irq status\n"));
			break;


		/*
		 * According to the openHAL source
		 * documentation this is also read-and-clear
		 * but if it is made so, the WinDriver does
		 * not work any more...
		 */
		case AR5K_RXDP:
			//SET_MEM_L(s->mem, addr, 0);
			break;

		default:
			break;
	}
	return val;
}

static void mm_writel(Atheros_WLANState *s, target_phys_addr_t addr, uint32_t val)
{
	uint32_t h;
	switch (addr)
	{

/******************************************************************
 *
 * ath5k_hw_init ---> ath5k_hw_nic_wakeup
 *
 ******************************************************************/

		case AR5K_RESET_CTL:

			if (val == (AR5K_RESET_CTL_CHIP | AR5K_RESET_CTL_PCI))
			{
				/* ath5k_hw.c: 613 */
				DEBUG_PRINT(("reset device (MAC + PCI)\n"));

				/*
				 * claim device is inited
				 */
				SET_MEM_L(s->mem, AR5K_STA_ID1, 0);
				SET_MEM_L(s->mem, AR5K_RESET_CTL, 3);
			}
			else if (val & (AR5K_RESET_CTL_CHIP | AR5K_RESET_CTL_PCI))
			{
				/* ath5k_hw.c: 613 */
				DEBUG_PRINT(("reset device (MAC + PCI + ?)\n"));

				/*
				 * claim device is inited
				 */
				SET_MEM_L(s->mem, AR5K_STA_ID1, 0);
				SET_MEM_L(s->mem, AR5K_RESET_CTL, 3);
			}
			else
			{
				/* ath5k_hw.c: 626 */
				DEBUG_PRINT(("reset device (generic)\n"));

				/*
				 * warm-start device
				 */
				SET_MEM_L(s->mem, AR5K_RESET_CTL, 0);
			}
			break;


/******************************************************************
 *
 * interrupt handling
 *
 ******************************************************************/

		case AR5K_IER:
			if (val == AR5K_IER_DISABLE)
			{
				/* ath5k_hw.c: 1636 */
				DEBUG_PRINT(("disabling interrupts\n"));
				SET_MEM_L(s->mem, AR5K_GPIODO, 0x0);
				SET_MEM_L(s->mem, AR5K_GPIODI, 0x0);

				s->interrupt_enabled = 0;
			}
			else if (val == AR5K_IER_ENABLE)
			{
				/* ath5k_hw.c: 1674 */
				DEBUG_PRINT(("enabling interrupts\n"));
				SET_MEM_L(s->mem, AR5K_GPIODO, 0x2);
				SET_MEM_L(s->mem, AR5K_GPIODI, 0x3);	// new

				s->interrupt_enabled = 1;
			}
			else
			{
				DEBUG_PRINT(("setting interrupt-enable to undefined value!!\n"));
			}
			break;

		case AR5K_GPIODO:
			if (val == 0x2)
			{
				SET_MEM_L(s->mem, AR5K_GPIODI, 0x3);
			}
			break;

		case AR5K_GPIODI:	// new
			if (val == 0x2)
			{
				SET_MEM_L(s->mem, AR5K_GPIODO, 0x3);
			}
			break;

		case AR5K_PIMR:
			/* ath5k_hw.c: 1668 */
			DEBUG_PRINT(("setting primary interrupt-mask to 0x%x (%u)\n", val, val));
			s->interrupt_p_mask = val;

			SET_MEM_L(s->mem, addr, val);
			break;

		case AR5K_SIMR0:
			DEBUG_PRINT(("setting secondary interrupt-mask 0 to 0x%x (%u)\n", val, val));
			s->interrupt_s_mask[0] = val;
			break;
		case AR5K_SIMR1:
			DEBUG_PRINT(("setting secondary interrupt-mask 1 to 0x%x (%u)\n", val, val));
			s->interrupt_s_mask[1] = val;
			break;
		case AR5K_SIMR2:
			DEBUG_PRINT(("setting secondary interrupt-mask 2 to 0x%x (%u)\n", val, val));
			s->interrupt_s_mask[2] = val;
			break;
		case AR5K_SIMR3:
			DEBUG_PRINT(("setting secondary interrupt-mask 3 to 0x%x (%u)\n", val, val));
			s->interrupt_s_mask[3] = val;
			break;
		case AR5K_SIMR4:
			DEBUG_PRINT(("setting secondary interrupt-mask 4 to 0x%x (%u)\n", val, val));
			s->interrupt_s_mask[4] = val;
			break;

/******************************************************************
 *
 * ath5k queuing (for transmit and receive buffers)
 *
 ******************************************************************/

		case AR5K_QCU_TXE:
			/* ath5k_hw.c: 1423ff */

			/* enable specified queue (nr in val) */
			val = FASTBINLOG(val);

			DEBUG_PRINT(("queue %u enabled\n", val));
			if ((val >= 0) && (val < 16))
			{
				s->transmit_queue_enabled[val] = 1;
				Atheros_WLAN_handleTxBuffer(s, val);
			}
			else
			{
				DEBUG_PRINT(("unknown queue 0x%x (%u)\n", val, val));
			}
			break;

		case AR5K_QCU_TXD:
			/* ath5k_hw.c: 1423ff */

			/* disable specified queue (nr in val) */
			val = FASTBINLOG(val);

			DEBUG_PRINT(("queue %u disabled\n", val));
			if ((val >= 0) && (val < 16))
			{
				s->transmit_queue_enabled[val] = 0;
			}
			else
			{
				DEBUG_PRINT(("unknown queue 0x%x (%u)\n", val, val));
			}
			break;

		case AR5K_IFS0:
		case AR5K_IFS1:
			DEBUG_PRINT(("TODO: find out what inter frame spacing registers are used for...\n"));
			break;

		case AR5K_CFG:

			if (val == AR5K_INIT_CFG)
			{
				/* ath5k_hw.c: 1261 */
				DEBUG_PRINT(("Reset configuration register (for hw bitswap)\n"));
			}
			SET_MEM_L(s->mem, AR5K_SLEEP_CTL, 0x0);
			break;

		case AR5K_TXCFG:
			/* ath5k_hw.c: 1122 */
			DEBUG_PRINT(("Setting transmit queue size to %u byte\n", (1 << (val+2)) ));

			s->transmit_queue_size = (1 << (val+2));
			break;

		case AR5K_CR:
			if (val == AR5K_CR_TXE0)	// TX Enable for queue 0 on 5210
			{
				DEBUG_PRINT(("TX enable for queue 0\n"));
			}
			else if (val == AR5K_CR_TXE1)	// TX Enable for queue 1 on 5210
			{
				DEBUG_PRINT(("TX enable for queue 1\n"));
			}
			else if (val == AR5K_CR_RXE)	// RX Enable
			{
				DEBUG_PRINT(("RX enable\n"));
				SET_MEM_L(s->mem, AR5K_DIAG_SW_5211, 0x0);
			}
			else if (val == AR5K_CR_TXD0)	// TX Disable for queue 0 on 5210
			{
				DEBUG_PRINT(("TX disable for queue 0\n"));
			}
			else if (val == AR5K_CR_TXD1)	// TX Disable for queue 1 on 5210
			{
				DEBUG_PRINT(("TX disable for queue 1\n"));
			}
			else if (val == AR5K_CR_RXD)	// RX Disable
			{
				DEBUG_PRINT(("RX disable\n"));
			}
			else if (val == AR5K_CR_SWI)	// unknown...
			{

				DEBUG_PRINT(("Undefined TX/RX en/disable CR_SWI\n"));
			}
			else
			{
				DEBUG_PRINT(("Undefined TX/RX en/disable\n"));
			}
			break;

		case AR5K_RXDP:
			/*
			 * unkown location, but this should
			 * set the location of the receive
			 * buffer's PHYSICAL address!!
			 */
			DEBUG_PRINT(("Setting receive queue to address 0x%x (%u)\n", val, val));

			/*
			 * This address will be queried again
			 * later... store it!!
			 */
			if (val == 0)
			{
				// hm... ar5424 resets its queue to 0 :-(
			}
			SET_MEM_L(s->mem, addr, val);
			s->receive_queue_address = (target_phys_addr_t)val;

			/*
			 * Madwifi hack: we allow only a certain
			 * number of packets in the receive queue!!
			 */
			s->receive_queue_count = 0;
			break;

		case AR5K_QUEUE_TXDP(0):
		case AR5K_QUEUE_TXDP(1):
		case AR5K_QUEUE_TXDP(2):
		case AR5K_QUEUE_TXDP(3):
		case AR5K_QUEUE_TXDP(4):
		case AR5K_QUEUE_TXDP(5):
		case AR5K_QUEUE_TXDP(6):
		case AR5K_QUEUE_TXDP(7):
		case AR5K_QUEUE_TXDP(8):
		case AR5K_QUEUE_TXDP(9):
		case AR5K_QUEUE_TXDP(10):
		case AR5K_QUEUE_TXDP(11):
		case AR5K_QUEUE_TXDP(12):
		case AR5K_QUEUE_TXDP(13):
		case AR5K_QUEUE_TXDP(14):
		case AR5K_QUEUE_TXDP(15):
			/*
			 * unkown location, but this should
			 * set the location of queue-dependent
			 * transmit buffer's PHYSICAL address!!
			 */
			DEBUG_PRINT(("Setting a transmit queue to address 0x%x (%u)\n", val, val));

			/*
			 * This address will be queried again
			 * later... store it!!
			 */
			SET_MEM_L(s->mem, addr, val);

			addr -= AR5K_QCU_TXDP_BASE;
			addr /= 4;
			if (addr >= 0 && addr < 16)
			{
				/*
				 * In case the given address specifies a
				 * valid DMA address, let's use it and copy
				 * the data into our device and process it
				 * once the queue is enabled
				 */
				s->transmit_queue_processed[addr] = 0;
				s->transmit_queue_address[addr] = (target_phys_addr_t)val;
			}
			else
			{
				DEBUG_PRINT(("unknown queue 0x%x (%u)\n", addr, addr));
			}
			break;

		case AR5K_RXCFG:
			/* ath5k_hw.c: 1124 */
			DEBUG_PRINT(("Setting receive queue size to %u byte\n", (1 << (val+2)) ));
			SET_MEM_L(s->mem, addr, val);
			break;

		case AR5K_QUEUE_QCUMASK(0):
		case AR5K_QUEUE_QCUMASK(1):
		case AR5K_QUEUE_QCUMASK(2):
		case AR5K_QUEUE_QCUMASK(3):
		case AR5K_QUEUE_QCUMASK(4):
		case AR5K_QUEUE_QCUMASK(5):
		case AR5K_QUEUE_QCUMASK(6):
		case AR5K_QUEUE_QCUMASK(7):
		case AR5K_QUEUE_QCUMASK(8):
		case AR5K_QUEUE_QCUMASK(9):
		case AR5K_QUEUE_QCUMASK(10):
		case AR5K_QUEUE_QCUMASK(11):
		case AR5K_QUEUE_QCUMASK(12):
		case AR5K_QUEUE_QCUMASK(13):
		case AR5K_QUEUE_QCUMASK(14):
		case AR5K_QUEUE_QCUMASK(15):
			DEBUG_PRINT(("ath5k_hw_reset_tx_queue for queue x (%u)\n", val));
			break;

		case AR5K_QUEUE_DFS_RETRY_LIMIT(0):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(1):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(2):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(3):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(4):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(5):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(6):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(7):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(8):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(9):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(10):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(11):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(12):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(13):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(14):
		case AR5K_QUEUE_DFS_RETRY_LIMIT(15):
			DEBUG_PRINT(("setting retry-limit for queue x to 0x%x (%u)\n", val, val));
			break;

		case AR5K_QUEUE_DFS_LOCAL_IFS(0):
		case AR5K_QUEUE_DFS_LOCAL_IFS(1):
		case AR5K_QUEUE_DFS_LOCAL_IFS(2):
		case AR5K_QUEUE_DFS_LOCAL_IFS(3):
		case AR5K_QUEUE_DFS_LOCAL_IFS(4):
		case AR5K_QUEUE_DFS_LOCAL_IFS(5):
		case AR5K_QUEUE_DFS_LOCAL_IFS(6):
		case AR5K_QUEUE_DFS_LOCAL_IFS(7):
		case AR5K_QUEUE_DFS_LOCAL_IFS(8):
		case AR5K_QUEUE_DFS_LOCAL_IFS(9):
		case AR5K_QUEUE_DFS_LOCAL_IFS(10):
		case AR5K_QUEUE_DFS_LOCAL_IFS(11):
		case AR5K_QUEUE_DFS_LOCAL_IFS(12):
		case AR5K_QUEUE_DFS_LOCAL_IFS(13):
		case AR5K_QUEUE_DFS_LOCAL_IFS(14):
		case AR5K_QUEUE_DFS_LOCAL_IFS(15):
			DEBUG_PRINT(("setting interframe space for queue x to 0x%x (%u)\n", val, val));
			break;

		case AR5K_QUEUE_MISC(0):
		case AR5K_QUEUE_MISC(1):
		case AR5K_QUEUE_MISC(2):
		case AR5K_QUEUE_MISC(3):
		case AR5K_QUEUE_MISC(4):
		case AR5K_QUEUE_MISC(5):
		case AR5K_QUEUE_MISC(6):
		case AR5K_QUEUE_MISC(7):
		case AR5K_QUEUE_MISC(8):
		case AR5K_QUEUE_MISC(9):
		case AR5K_QUEUE_MISC(10):
		case AR5K_QUEUE_MISC(11):
		case AR5K_QUEUE_MISC(12):
		case AR5K_QUEUE_MISC(13):
		case AR5K_QUEUE_MISC(14):
		case AR5K_QUEUE_MISC(15):
			DEBUG_PRINT(("setting options for queue x to 0x%x (%u)\n", val, val));
			break;

		case AR5K_SLEEP_CTL:
			SET_MEM_L(s->mem, AR5K_SLEEP_CTL, val);
			if (val == AR5K_SLEEP_CTL_SLE_WAKE)
			{
				DEBUG_PRINT(("waking up device\n"));

				/*
				 * yes, we are awake
				 *
				 * basically it just checks if power-down
				 * is false (val & AR5K_PCICFG_SPWR_DN == 0)
				 * but my AR5212 says 20 what has the same
				 * result but might be better ;-)
				 */
				SET_MEM_L(s->mem, AR5K_PCICFG, 0x14);
				SET_MEM_L(s->mem, AR5K_STA_ID1, 0x00049e2e);
// 				SET_MEM_L(s->mem, AR5K_STA_ID1, 0x0);

// 				SET_MEM_L(s->mem, AR5K_PCICFG, 0x34);
// 				SET_MEM_L(s->mem, AR5K_STA_ID1, AR5K_STA_ID1_PWR_SV);
			}
			else if (val == AR5K_SLEEP_CTL_SLE_SLP)
			{
				DEBUG_PRINT(("putting device to sleep\n"));
			}
			else
			{
				DEBUG_PRINT(("unknown SLEEP command %u\n", val));
			}
			break;

		case AR5K_PHY_PLL:
			/*
			 * ...set the PHY operating mode after reset
			 */

			/* ath5k_hw.c: 632 */
			DEBUG_PRINT(("setting PHY operating mode (PLL)\n"));
			break;

		case AR5K_PHY_MODE:
			/*
			 * ...set the PHY operating mode after reset
			 */

			/* ath5k_hw.c: 635 */
			DEBUG_PRINT(("setting PHY operating mode (mode)\n"));
			break;

		case AR5K_PHY_TURBO:
			/*
			 * ...set the PHY operating mode after reset
			 */

			/* ath5k_hw.c: 636 */
			DEBUG_PRINT(("setting PHY operating mode (turbo)\n"));
			break;


/******************************************************************
 *
 * ath5k_hw_init ---> ath5k_hw_radio_revision
 *
 ******************************************************************/


		case AR5K_PHY(0):
			/*
			 * Unknown
			 */
			if (val == AR5K_PHY_SHIFT_2GHZ)
			{
				DEBUG_PRINT(("requesting 2GHZ radio\n"));
				SET_MEM_L(s->mem, AR5K_PHY(0x100), 0x4c047000);
			}
			/* ath5k_hw.c: 662 */
			else if (val == AR5K_PHY_SHIFT_5GHZ)
			{
				DEBUG_PRINT(("requesting 5GHZ radio\n"));
				SET_MEM_L(s->mem, AR5K_PHY(0x100), 0x8e000000);
			}

			SET_MEM_L(s->mem, AR5K_SLEEP_CTL, 0x0);
			break;

		case AR5K_PHY(0x20):
			/*
			 * request radio revision
			 */

			/* ath5k_hw.c: 659 */
			if (val == AR5K_PHY_SHIFT_2GHZ)
			{
				DEBUG_PRINT(("requesting 2GHZ radio\n"));
				SET_MEM_L(s->mem, AR5K_PHY(0x100), 0x4c047000); // 1275359232);
			}
			/* ath5k_hw.c: 662 */
			else if (val == AR5K_PHY_SHIFT_5GHZ)
			{
				DEBUG_PRINT(("requesting 5GHZ radio\n"));
				SET_MEM_L(s->mem, AR5K_PHY(0x100), 0x7fffffff); // 2382364672);
			}
			/* ath5k_hw.c: 671 */
			else if (val == 0x00001c16)
			{
				DEBUG_PRINT(("requesting radio\n"));
			}
			/* ath5k_hw.c: 674 */
			else if (val == 0x00010000)
			{
				DEBUG_PRINT(("requesting radio 8 times...\n"));
				// NOW we request the radio revision (it was set before...)

				// SET_MEM_L(s->mem, 0x9c00, 0x8e026023);
				SET_MEM_L(s->mem, 0x9c00, 0x8e000000);

				SET_MEM_L(s->mem, 0x9c00, 0x4c047000);
			}

			break;

		/*
		 * Setting frequency is different for AR5210/AR5211/AR5212
		 *
		 * see ath5k_hw.c: 4590 ff
		 *
		 * they all set AR5K_PHY(0x27),
		 * AR5210 sets AR5K_PHY(0x30),
		 * AR5211 sets AR5K_PHY(0x34) and
		 * AR5212 sets AR5K_PHY(0x36)
		 *
		 *
		 * The virtual device seems to read out 0x34 for
		 * the current channel (e.g. after a packet has
		 * been received)!!
		 */
		case AR5K_PHY(0x27):
//  			fprintf(stderr, "0x%04x => 0x%08x (27)\n", addr, val);
			SET_MEM_L(s->mem, addr, val);
			s->current_frequency_partial_data[0] = val;
			updateFrequency(s);
			break;

		case AR5K_PHY(0x34):
//  			fprintf(stderr, "0x%04x => 0x%08x (34)\n", addr, val);
			SET_MEM_L(s->mem, addr, val);
			s->current_frequency_partial_data[1] = val;
			updateFrequency(s);
			break;

		/*
		 * these are used by AR521 and AR5212 respectively,
		 * but we seem to simulate an AR5211 and the calls
		 * destroy our channel frequency mapping :-(
		 *
		case AR5K_PHY(0x30):
			fprintf(stderr, "0x%04x => 0x%08x (30)\n", addr, val);
			SET_MEM_L(s->mem, addr, val);
			s->current_frequency_partial_data[1] = val;
			updateFrequency(s);
			break;
		case AR5K_PHY(0x36):
 			fprintf(stderr, "0x%04x => 0x%08x (36)\n", addr, val);
			SET_MEM_L(s->mem, addr, val);
			s->current_frequency_partial_data[1] = val;
			updateFrequency(s);
			break;
		 */



/*
		case AR5K_PHY(0x21):
		case AR5K_PHY(0x22):
		case AR5K_PHY(0x23):
		case AR5K_PHY(0x24):
		case AR5K_PHY(0x25):
		case AR5K_PHY(0x26):
		case AR5K_PHY(0x28):
		case AR5K_PHY(0x29):
		case AR5K_PHY(0x31):
		case AR5K_PHY(0x32):
		case AR5K_PHY(0x33):
		case AR5K_PHY(0x35):
		case AR5K_PHY(0x37):
		case AR5K_PHY(0x38):
		case AR5K_PHY(0x39):
		case AR5K_PHY(0x40):
		case AR5K_PHY(0x41):
		case AR5K_PHY(0x42):
		case AR5K_PHY(0x43):
		case AR5K_PHY(0x44):
		case AR5K_PHY(0x45):
		case AR5K_PHY(0x46):
		case AR5K_PHY(0x47):
		case AR5K_PHY(0x48):
		case AR5K_PHY(0x49):
		case AR5K_PHY(0x50):
			fprintf(stderr, "0x%04x => 0x%08x\n", addr, val);
			break;*/

/******************************************************************
 *
 * ath5k_hw_init ---> ath5k_hw_set_associd  (aka. set BSSID)
 *
 ******************************************************************/

		case AR5K_BSS_IDM0:
		case AR5K_BSS_IDM1:
			/*
			 * Set simple BSSID mask on 5212
			 */

			/* ath5k_hw.c: 2420 */
			DEBUG_PRINT(("setting bssid mask\n"));
			break;

		case AR5K_BSS_ID0:
		case AR5K_BSS_ID1:
			/*
			 * Set BSSID which triggers the "SME Join" operation
			 */

			/* ath5k_hw.c: 2432 & 2433 */
			DEBUG_PRINT(("setting bssid : %c%c%c%c \n", (val << 24) >> 24, (val << 16) >> 24, (val << 8) >> 24, val >> 24));
			break;

		case AR5K_STA_ID0:
			/*
			 * a set to client(adhoc|managed) | ap | monitor mode is coming
			 *
			 * if there are more than one chip present, this
			 * call defines which chip is to be used!
			 */

			/* ath5k_hw.c: 2358 */
			DEBUG_PRINT(("a set to client | ap | monitor mode is coming for station %u\n", val));

			// ext
			SET_MEM_L(s->mem, addr, val);

			break;

		case AR5K_STA_ID1:
			/*
			 * seems to have a double-meaning:
			 *
			 * setting client mode AND power mode
			 */

			/* ath5k_hw.c: 619 */
			DEBUG_PRINT(("setting power mode\n"));
			SET_MEM_L(s->mem, AR5K_STA_ID1, val);
			SET_MEM_L(s->mem, AR5K_STA_ID0, 0x800a1100);
			//SET_MEM_L(s->mem, 0xc, 0x1a7d823c);
			SET_MEM_L(s->mem, 0xc, 0x0);
			SET_MEM_L(s->mem, 0x00c0, 0x01040000);


			/* ath5k_hw.c: 2361 */
			if (val & AR5K_STA_ID1_ADHOC & AR5K_STA_ID1_DESC_ANTENNA)
			{
				DEBUG_PRINT(("setting device into ADHOC mode\n"));
			}
			else if (val & AR5K_STA_ID1_AP & AR5K_STA_ID1_RTS_DEF_ANTENNA)
			{
				DEBUG_PRINT(("setting device into managed mode\n"));
			}
			else if (val & AR5K_STA_ID1_DEFAULT_ANTENNA)
			{
				DEBUG_PRINT(("setting device into some other mode (probably monitor)\n"));
			}
			else
			{
				DEBUG_PRINT(("setting device into unknown mode\n"));
			}
			break;



/******************************************************************
 *
 * ath5k_hw_init ---> ath5k_eeprom_init
 *
 ******************************************************************/

		case AR5K_EEPROM_BASE:
			/*
			 * an access to an offset inside the
			 * EEPROM uses an initialization of
			 * the address at this location
			 */

			/* ath5k_hw.c: 1738 */
			DEBUG_PRINT(("there will be an access to the EEPROM at %p\n", (unsigned long*)val));

			/*
			 * set the data that will be returned
			 * after calling AR5K_EEPROM_CMD=READ
			 */
			switch (val)
			{
#if 0
				case AR5K_EEPROM_MAGIC:
					WRITE_EEPROM(s->mem, AR5K_EEPROM_MAGIC_VALUE);
					break;

				case AR5K_EEPROM_PROTECT:
					WRITE_EEPROM(s->mem, 0);
					break;

				case AR5K_EEPROM_REG_DOMAIN:
					/*
					 * reg-domain central europe ???
					 */
					WRITE_EEPROM(s->mem, 96);
					break;

				case AR5K_EEPROM_VERSION:
					WRITE_EEPROM(s->mem, AR5K_EEPROM_VERSION_3_4);
					break;

				case AR5K_EEPROM_HDR:
					WRITE_EEPROM(s->mem, 23046);
					break;

				case 195:
					/*
					 * an radio-GHZ specific eeprom data (AR5K_EEPROM_ANT_GAIN)
					 *
					 * on my AR5212 it is 0
					 */

					/* ath5k_hw.c: 2023 */
					WRITE_EEPROM(s->mem, 0);
					break;

				case 0x20:
					/*
					 * before we read the MAC addr, we read this (???)
					 *
					 * ATTENTION: this value is present in the EEPROM!!
					 */

					/* ath5k_hw.c : 2185 */
					break;

				case 0x1f:
					/*
					 * 1st part of MAC-addr
					 */
					DEBUG_PRINT(("EEPROM request first part of MAC\n"));
					WRITE_EEPROM(s->mem, (s->phys[0] << 8) | s->phys[1]);
					break;

				case 0x1e:
					/*
					 * 2nd part of MAC-addr
					 */
					DEBUG_PRINT(("EEPROM request second part of MAC\n"));
					WRITE_EEPROM(s->mem, (s->phys[2] << 8) | s->phys[3]);
					break;

				case 0x1d:
					/*
					 * 3rd part of MAC-addr
					 */
					DEBUG_PRINT(("EEPROM request third part of MAC\n"));
					WRITE_EEPROM(s->mem, (s->phys[4] << 8) | s->phys[5]);
					break;
#endif
				/*
				 * ATTENTION: if we modify anything in the
				 * eeprom, we might get (at least in linux we
				 * do) an EEPROM-checksum error!!
				 */

				case 0x0:
					/*
					 * this is not part of the EEPROM dumps for some reason!!
					 */
					DEBUG_PRINT(("EEPROM request 0x0\n"));
					WRITE_EEPROM(s->mem, 0x13);
					break;

				default:
					if (!get_eeprom_data(s, val, &h))
					{
						/*
						 * we have a hit in the internal eeprom-buffer
						 */
						DEBUG_PRINT(("EEPROM hit %u at %u\n", h, val));
						WRITE_EEPROM(s->mem, h);
					}
					else
					{
						DEBUG_PRINT(("EEPROM request at %p is unknown\n", (unsigned long*)val));
						WRITE_EEPROM(s->mem, 0);
					}
					break;
			}
			break;

		case AR5K_EEPROM_CMD:
			/*
			 * what type of access is specified as well
			 */

			/* ath5k_hw.c: 1739 */
			if (val & AR5K_EEPROM_CMD_READ)
			{
				DEBUG_PRINT(("the EEPROM access will be READ\n"));

				/*
				 * tell the device the read was successful
				 */
				SET_MEM_L(s->mem, AR5K_EEPROM_STAT_5210, AR5K_EEPROM_STAT_RDDONE);
				SET_MEM_L(s->mem, AR5K_EEPROM_STAT_5211, AR5K_EEPROM_STAT_RDDONE);
				/*
				 * and return the data that was set
				 * during the write to AR5K_EEPROM_BASE
				 */
			}
			else
			{
				DEBUG_PRINT(("the EEPROM access will be UNKNOWN\n"));
				fprintf(stderr, "Is this a write to the eeprom??\n");
			}
			break;


/******************************************************************
 *
 * additional reverse engineering:
 *
 ******************************************************************/

		case AR5K_USEC_5210:	// new
			SET_MEM_L(s->mem, AR5K_XRMODE, 0x2a80001a);
			SET_MEM_L(s->mem, AR5K_XRTIMEOUT, 0x13881c20);
			break;

		case AR5K_PHY_AGCCTL:	// new
			if (val & AR5K_PHY_AGCCTL_CAL)
			{
				SET_MEM_L(s->mem, AR5K_PHY_AGCCTL, val & (~AR5K_PHY_AGCCTL_CAL));
			}
			else if (val & AR5K_PHY_AGCCTL_NF)
			{
				SET_MEM_L(s->mem, AR5K_PHY_AGCCTL, val & (~AR5K_PHY_AGCCTL_NF));
			}
			break;

		default:
			if (addr / 4 < Atheros_WLAN_MEM_SIZE)
			{
				SET_MEM_L(s->mem, addr, val);
			}

			if ((addr >= AR5K_PCU_MIN) &&
				(addr <= AR5K_PCU_MAX))
			{
				DEBUG_PRINT(("Setting up ini-registers...!!\n"));
			}
			else
			{
				DEBUG_PRINT(("Unknown call to memory!!\n"));
			}
			break;
	}
}

#endif /* CONFIG_WIN32 */
