/**
 * QEMU WLAN access point emulation
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
 * TODO: register_savevm is missing.
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
#include "qemu-timer.h"

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

/*
 * PCI and EEPROM definitions
 */
#include "hw/atheros_wlan.h"
#include "hw/atheros_wlan_ap.h"
#include "hw/atheros_wlan_io.h"
#include "hw/atheros_wlan_packet.h"

/*
 * MadWifi OPENHAL atheros constants
 */
#include "hw/ath5k_hw.h"
#include "hw/ath5kreg.h"
#include "hw/ath5k.h"

static int semaphore_helper(int semaphore, int sem_op, int semaphore_nr, short flags)
{
	struct sembuf semp;
	semp.sem_num = semaphore_nr;
	semp.sem_op = sem_op;
	semp.sem_flg = flags;

	if (semaphore == -1)
	{
		/*
		 * We don't have a semaphore... probably not
		 * that bad, but having one is better :-)
		 */
		return -1;
	}

	int ret;
	while ((ret = semop(semaphore, &semp, 1)) < 0)
	{
		if (errno == EAGAIN && flags == IPC_NOWAIT)
		{
			return errno;
		}
		else if (errno != EINTR)
		{
			fprintf(stderr, "Semaphore error: 0x%x / %u\n", errno, errno);
			return errno;
		}
	}

	return ret;
}


static int signal_semaphore(int semaphore, int semaphore_nr)
{
	return semaphore_helper(semaphore, 1, semaphore_nr, 0);
}
static int wait_semaphore(int semaphore, int semaphore_nr)
{
	return semaphore_helper(semaphore, -1, semaphore_nr, 0);
}

void Atheros_WLAN_insert_frame(Atheros_WLANState *s, struct mac80211_frame *frame)
{
	struct mac80211_frame *i_frame;

	wait_semaphore(s->access_semaphore, 0);

	s->inject_queue_size++;
	i_frame = s->inject_queue;
	if (!i_frame)
	{
		s->inject_queue = frame;
	}
	else
	{
		while (i_frame->next_frame)
		{
			i_frame = i_frame->next_frame;
		}

		i_frame->next_frame = frame;
	}

	if (!s->inject_timer_running)
	{
		// if the injection timer is not
		// running currently, let's schedule
		// one run...
		s->inject_timer_running = 1;
		qemu_mod_timer(s->inject_timer, qemu_get_clock(rt_clock) + 5);
	}

	signal_semaphore(s->access_semaphore, 0);
}

static void Atheros_WLAN_beacon_timer(void *opaque)
{
	struct mac80211_frame *frame;
	Atheros_WLANState *s = (Atheros_WLANState *)opaque;

	frame = Atheros_WLAN_create_beacon_frame();
	if (frame)
	{
		Atheros_WLAN_init_frame(s, frame);
		Atheros_WLAN_insert_frame(s, frame);
	}

	qemu_mod_timer(s->beacon_timer, qemu_get_clock(rt_clock) + 500);
}

static void Atheros_WLAN_inject_timer(void *opaque)
{
	Atheros_WLANState *s = (Atheros_WLANState *)opaque;
	struct mac80211_frame *frame;

	wait_semaphore(s->access_semaphore, 0);

	frame = s->inject_queue;
	if (frame)
	{
		// remove from queue
		s->inject_queue_size--;
		s->inject_queue = frame->next_frame;
	}
	signal_semaphore(s->access_semaphore, 0);

	if (!frame)
	{
		goto timer_done;
	}

	if (s->receive_queue_address == 0)
	{
		// we drop the packet
	}
	else
	{
		Atheros_WLAN_handleRxBuffer(s, frame, frame->frame_length);
	}

	free(frame);

timer_done:
	wait_semaphore(s->access_semaphore, 0);

	if (s->inject_queue_size > 0)
	{
		// there are more packets... schedule
		// the timer for sending them as well
		qemu_mod_timer(s->inject_timer, qemu_get_clock(rt_clock) + 25);
	}
	else
	{
		// we wait until a new packet schedules
		// us again
		s->inject_timer_running = 0;
	}

	signal_semaphore(s->access_semaphore, 0);
}


static int Atheros_WLAN_can_receive(void *opaque)
{
	Atheros_WLANState *s = (Atheros_WLANState *)opaque;

	if (s->ap_state != Atheros_WLAN__STATE_ASSOCIATED)
	{
		// we are currently not connected
		// to the access point
		return 0;
	}

	if (s->inject_queue_size > Atheros_WLAN__MAX_INJECT_QUEUE_SIZE)
	{
		// overload, please give me some time...
		return 0;
	}

	return 1;
}

static void Atheros_WLAN_receive(void *opaque, const uint8_t *buf, int size)
{
	struct mac80211_frame *frame;
	Atheros_WLANState *s = (Atheros_WLANState *)opaque;

	if (!Atheros_WLAN_can_receive(opaque))
	{
		// this should not happen, but in
		// case it does, let's simply drop
		// the packet
		return;
	}

	if (!s)
	{
		return;
	}

	/*
	 * A 802.3 packet comes from the qemu network. The
	 * access points turns it into a 802.11 frame and
	 * forwards it to the wireless device
	 */
	frame = Atheros_WLAN_create_data_packet(s, buf, size);
	if (frame)
	{
		Atheros_WLAN_init_frame(s, frame);
		Atheros_WLAN_insert_frame(s, frame);
	}
}

static void Atheros_WLAN_cleanup(VLANClientState *vc)
{
#if 0
    Atheros_WLANState *d = vc->opaque;
    unregister_savevm("e100", d);

    qemu_del_timer(d->poll_timer);
    qemu_free_timer(d->poll_timer);
#endif
}

void Atheros_WLAN_setup_ap(NICInfo *nd, PCIAtheros_WLANState *d)
{
	Atheros_WLANState *s;
	s = &d->Atheros_WLAN;

	s->ap_state = Atheros_WLAN__STATE_NOT_AUTHENTICATED;
	s->ap_macaddr[0] = 0x00;
	s->ap_macaddr[1] = 0x13;
	s->ap_macaddr[2] = 0x46;
	s->ap_macaddr[3] = 0xbf;
	s->ap_macaddr[4] = 0x31;
	s->ap_macaddr[5] = 0x59;

	s->inject_timer_running = 0;
	s->inject_sequence_number = 0;

	s->inject_queue = NULL;
	s->inject_queue_size = 0;

	s->access_semaphore = semget(ATHEROS_WLAN_ACCESS_SEM_KEY, 1, 0666 | IPC_CREAT);
	semctl(s->access_semaphore, 0, SETVAL, 1);

	s->beacon_timer = qemu_new_timer(rt_clock, Atheros_WLAN_beacon_timer, s);
	qemu_mod_timer(s->beacon_timer, qemu_get_clock(rt_clock));

	// setup the timer but only schedule
	// it when necessary...
	s->inject_timer = qemu_new_timer(rt_clock, Atheros_WLAN_inject_timer, s);

    s->vc = qdev_get_vlan_client(&d->dev.qdev,
                                 Atheros_WLAN_receive,
                                 Atheros_WLAN_can_receive,
                                 Atheros_WLAN_cleanup, s);

    qemu_format_nic_info_str(s->vc, s->macaddr);
}



void Atheros_WLAN_disable_irq(void *arg)
{
	Atheros_WLANState *s = (Atheros_WLANState *)arg;
	SET_MEM_L(s->mem, ATH_HW_IRQ_PENDING, ATH_HW_IRQ_PENDING_FALSE);
	qemu_set_irq(s->irq, 0);
	DEBUG_PRINT((">> Disabling irq\n"));
}

void Atheros_WLAN_enable_irq(void *arg)
{
	Atheros_WLANState *s = (Atheros_WLANState *)arg;

	if (!s->interrupt_enabled)
	{
		DEBUG_PRINT((">> Wanted to enable irq, but they are disabled\n"));
		Atheros_WLAN_disable_irq(s);
		return;
	}

	DEBUG_PRINT((">> Enabling irq\n"));
	SET_MEM_L(s->mem, ATH_HW_IRQ_PENDING, ATH_HW_IRQ_PENDING_TRUE);
	qemu_set_irq(s->irq, 1);
}


void Atheros_WLAN_update_irq(void *arg)
{
	Atheros_WLANState *s = (Atheros_WLANState *)arg;
	DEBUG_PRINT((">> Updating... irq-enabled is %u\n", s->interrupt_enabled));
	/*
	 * NOTE: Since we use shared interrupts
	 * the device driver will check if the
	 * interrupt really comes from this hardware
	 *
	 * This is done by checking the
	 * ATH_HW_IRQ_PENDING memory...
	 */
	if (/*(!s->interrupt_enabled) ||*/
	    (s->pending_interrupts == NULL))
	{
		SET_MEM_L(s->mem, AR5K_RAC_PISR, 0);
		goto disable_further_interrupts;
	}

	/*
	 * Make sure this is done atomically!!
	 */
	wait_semaphore(s->access_semaphore, 0);
	uint32_t status = 0x0;
	struct pending_interrupt *i = s->pending_interrupts;
	struct pending_interrupt *next;

	s->pending_interrupts = NULL;
	while (i != NULL)
	{
		next = i->next;
		if (1) //(s->interrupt_p_mask & i->status)
		{
			status |= i->status;
		}
		free(i);

		i = next;
	}

	SET_MEM_L(s->mem, AR5K_RAC_PISR, status);
	DEBUG_PRINT((">> Status set to %u\n", status));
	/*
	 * Atomic part done...
	 */
	signal_semaphore(s->access_semaphore, 0);


disable_further_interrupts:
	/*
	 * NOTE: At last, it will check if any
	 * more interrupts are pending. The call
	 * to check what type of interrupt was
	 * pending already put down the interrupt_pending
	 * bit for us (check the readl function for RAC)
	 *
	 * if_ath.c: 921
	 */
	Atheros_WLAN_disable_irq(s);
}


void Atheros_WLAN_append_irq(Atheros_WLANState *s, struct pending_interrupt intr)
{
	struct pending_interrupt *new_intr;
	new_intr = (struct pending_interrupt *)malloc(sizeof(struct pending_interrupt));
	memcpy(new_intr, &intr, sizeof(intr));

	/*
	 * Make sure this is done atomically!!
	 */
	wait_semaphore(s->access_semaphore, 0);

	if (s->pending_interrupts == NULL)
	{
		s->pending_interrupts = new_intr;
	}
	else
	{
		/*
		 * Insert at the end of the
		 * list to assure correct order
		 * of interrupts!
		 */
		struct pending_interrupt *i = s->pending_interrupts;
		while (i->next != NULL)
		{
			i = i->next;
		}

		new_intr->next = NULL;
		i->next = new_intr;
	}

	/*
	 * Atomic part done...
	 */
	signal_semaphore(s->access_semaphore, 0);
}








void Atheros_WLAN_handleRxBuffer(Atheros_WLANState *s, struct mac80211_frame *frame, uint32_t frame_length)
{
	struct ath_desc desc;
	struct ath5k_ar5212_rx_status *rx_status;
	rx_status = (struct ath5k_ar5212_rx_status*)&desc.ds_hw[0];

	if (s->receive_queue_address == 0)
	{
		return;
	}

	cpu_physical_memory_read(s->receive_queue_address, (uint8_t*)&desc, sizeof(desc));

	/*
	 * Put some good base-data into
	 * the descriptor. Length & co
	 * will be modified below...
	 *
	 * NOTE: Better set everything correctly
	 *
	 * Look at ath5k_hw.c: proc_tx_desc
	 */
	desc.ds_ctl0 = 0x0;
	desc.ds_ctl1 = 0x9c0;
	desc.ds_hw[0] = 0x126d806a;
	desc.ds_hw[1] = 0x49860003;
	desc.ds_hw[2] = 0x0;
	desc.ds_hw[3] = 0x0;


	/*
	 * Filter out old length and put in correct value...
	 */
	rx_status->rx_status_0 &= ~AR5K_AR5212_DESC_RX_STATUS0_DATA_LEN;
	rx_status->rx_status_0 |= frame_length;
	rx_status->rx_status_0 &= ~AR5K_AR5211_DESC_RX_STATUS0_MORE;

	/*
	 * Write descriptor and packet back to DMA memory...
	 */
	cpu_physical_memory_write(s->receive_queue_address, (uint8_t*)&desc, sizeof(desc));
	cpu_physical_memory_write((target_phys_addr_t)desc.ds_data, (uint8_t*)frame, sizeof(struct mac80211_frame));

	/*
	 * Set address to next position
	 * in single-linked list
	 *
	 * The receive list's last element
	 * points to itself to avoid overruns.
	 * This way, at some point no more
	 * packets will be received, but (I
	 * ASSUME) that it is the drivers
	 * responsibility to reset the address
	 * list!
	 *
	 *
	 * NOTE: It seems the real madwifi cannot
	 * handle multiple packets at once. so we
	 * set the buffer to NULL to make the injection
	 * fail next time until an interrupt was
	 * received by the driver and a new buffer
	 * is registered!!
	 */
	s->receive_queue_address =
		((++s->receive_queue_count) > MAX_CONCURRENT_RX_FRAMES)
			? 0
			: (target_phys_addr_t)desc.ds_link;


	DEBUG_PRINT((">> Enabling rx\n"));
	/*
	 * Notify the driver about the new packet
	 */
	struct pending_interrupt intr;
	intr.status = AR5K_INT_RX;
	Atheros_WLAN_append_irq(s, intr);
	Atheros_WLAN_enable_irq(s);
}



void Atheros_WLAN_handleTxBuffer(Atheros_WLANState *s, uint32_t queue)
{
	struct ath_desc desc;
	struct mac80211_frame frame;

	if (s->transmit_queue_address[queue] == 0)
	{
		return;
	}

	cpu_physical_memory_read(s->transmit_queue_address[queue], (uint8_t*)&desc, sizeof(desc));

	if (s->transmit_queue_processed[queue])
	{
		/*
		 * Maybe we already processed the frame
		 * and have not gotten the address of the
		 * next frame buffer but still got a call
		 * to send the next frame
		 *
		 * this way we have to process the next
		 * frame in the single linked list!!
		 */
		s->transmit_queue_address[queue] = (target_phys_addr_t)desc.ds_link;

		/*
		 * And now get the frame we really have to process...
		 */
		cpu_physical_memory_read(s->transmit_queue_address[queue], (uint8_t*)&desc, sizeof(desc));
	}

	uint32_t segment_len, frame_length = 0, more;
	uint8_t *frame_pos = (uint8_t*)&frame;
	struct ath5k_ar5212_tx_desc *tx_desc;
	tx_desc = (struct ath5k_ar5212_tx_desc*)&desc.ds_ctl0;
	do
	{
		more = tx_desc->tx_control_1 & AR5K_AR5211_DESC_TX_CTL1_MORE;
		segment_len = tx_desc->tx_control_1 & AR5K_AR5212_DESC_TX_CTL1_BUF_LEN;

		cpu_physical_memory_read((target_phys_addr_t)desc.ds_data, frame_pos, segment_len);
		frame_pos += segment_len;
		frame_length += segment_len;


		/*
		 * Notify successful transmission
		 *
		 * NOTE: It'd be better to leave the
		 * descriptor as it is and only modify
		 * the transmit-ok-bits --> this way
		 * the timestamp and co. would stay
		 * valid...
		 *
		 * Look at ath5k_hw.c: proc_tx_desc
		 *
		 * NOTE: Not sure if this acknowledgement
		 * must be copied back for every single
		 * descriptor in a multi-segment frame,
		 * but better safe than sorry!!
		 */
		desc.ds_ctl0 = 0x213f002f;
		desc.ds_ctl1 = 0x2b;
		desc.ds_hw[0] = 0xf0000;
		desc.ds_hw[1] = 0x1b;
		desc.ds_hw[2] = 0xab640001;
		desc.ds_hw[3] = 0x4a019;

		/*
		 *
		 * struct ath5k_tx_status *tx_status = (struct ath5k_tx_status*)&desc.ds_hw[2];
		 * tx_status->tx_status_1 |= AR5K_DESC_TX_STATUS1_DONE;
		 * tx_status->tx_status_0 |= AR5K_DESC_TX_STATUS0_FRAME_XMIT_OK;
		 *
		 *
		 * Write descriptor back to DMA memory...
		 */
		cpu_physical_memory_write(s->transmit_queue_address[queue], (uint8_t*)&desc, sizeof(desc));

		if (more && frame_length < sizeof(frame))
		{
			/*
			 * This is done at the end of the loop
			 * since sometimes the next-link is not
			 * yet set (assuming frame is a 1-segment
			 * frame)!!
			 *
			 * This is very strange (and maybe obsolete
			 * by this version) but let's do it the safe
			 * way and not mess it up :-)
			 */
			s->transmit_queue_address[queue] = (target_phys_addr_t)desc.ds_link;
			cpu_physical_memory_read(s->transmit_queue_address[queue], (uint8_t*)&desc, sizeof(desc));
		}
	}
	while (more && frame_length < sizeof(frame));


	struct pending_interrupt intr;
	intr.status = AR5K_INT_TX;
	Atheros_WLAN_append_irq(s, intr);
	Atheros_WLAN_enable_irq(s);

	/*
	 * Set address to next position
	 * in single-linked list
	 *
	 * The transmit list's last element
	 * points to itself to avoid overruns.
	 * This way, at some point no more
	 * packets will be received, but (I
	 * ASSUME) that it is the drivers
	 * responsibility to reset the address
	 * list!
	 */
	s->transmit_queue_processed[queue] = 1;

	frame.frame_length = frame_length + 4;
	Atheros_WLAN_handle_frame(s, &frame);
}


void Atheros_WLAN_handle_frame(Atheros_WLANState *s, struct mac80211_frame *frame)
{
	struct mac80211_frame *reply = NULL;
	unsigned long ethernet_frame_size;
	unsigned char ethernet_frame[1518];

	if ((frame->frame_control.type == IEEE80211_TYPE_MGT) &&
	    (frame->frame_control.sub_type == IEEE80211_TYPE_MGT_SUBTYPE_PROBE_REQ))
	{
		reply = Atheros_WLAN_create_probe_response();
	}
	else if ((frame->frame_control.type == IEEE80211_TYPE_MGT) &&
		 (frame->frame_control.sub_type == IEEE80211_TYPE_MGT_SUBTYPE_AUTHENTICATION))
	{
		DEBUG_PRINT_AP(("Received authentication!\n"));
		reply = Atheros_WLAN_create_authentication();

		if (s->ap_state == Atheros_WLAN__STATE_NOT_AUTHENTICATED)
		{
			// if everything is going according to
			// the state machine, let's jump into the
			// next state
			s->ap_state = Atheros_WLAN__STATE_AUTHENTICATED;
		}
	}
	else if ((frame->frame_control.type == IEEE80211_TYPE_MGT) &&
		 (frame->frame_control.sub_type == IEEE80211_TYPE_MGT_SUBTYPE_DEAUTHENTICATION))
	{
		DEBUG_PRINT_AP(("Received deauthentication!\n"));
		reply = Atheros_WLAN_create_deauthentication();

		// some systems (e.g. WinXP) won't send a
		// disassociation. just believe that the
		// deauthentication is ok... nothing bad
		// can happen anyways ;-)
		s->ap_state = Atheros_WLAN__STATE_NOT_AUTHENTICATED;
	}
	else if ((frame->frame_control.type == IEEE80211_TYPE_MGT) &&
		 (frame->frame_control.sub_type == IEEE80211_TYPE_MGT_SUBTYPE_ASSOCIATION_REQ))
	{
		DEBUG_PRINT_AP(("Received association request!\n"));
		reply = Atheros_WLAN_create_association_response();

		if (s->ap_state == Atheros_WLAN__STATE_AUTHENTICATED)
		{
			// if everything is going according to
			// the state machine, let's jump into the
			// next state
			s->ap_state = Atheros_WLAN__STATE_ASSOCIATED;
		}
	}
	else if ((frame->frame_control.type == IEEE80211_TYPE_MGT) &&
		 (frame->frame_control.sub_type == IEEE80211_TYPE_MGT_SUBTYPE_DISASSOCIATION))
	{
		DEBUG_PRINT_AP(("Received disassociation!\n"));
		reply = Atheros_WLAN_create_disassociation();

		if (s->ap_state == Atheros_WLAN__STATE_ASSOCIATED)
		{
			// if everything is going according to
			// the state machine, let's jump into the
			// next state
			s->ap_state = Atheros_WLAN__STATE_AUTHENTICATED;
		}
	}
	else if ((frame->frame_control.type == IEEE80211_TYPE_DATA) &&
		 (s->ap_state == Atheros_WLAN__STATE_ASSOCIATED))
	{
		/*
		 * The access point uses the 802.11 frame
		 * and sends a 802.3 frame into the network...
		 * This packet is then understandable by
		 * qemu-slirp
		 *
		 * If we ever want the access point to offer
		 * some services, it can be added here!!
		 */
		// ethernet header type
		ethernet_frame[12] = frame->data_and_fcs[6];
		ethernet_frame[13] = frame->data_and_fcs[7];

		// the new originator of the packet is
		// the access point
		memcpy(&ethernet_frame[6], s->ap_macaddr, 6);

		if (ethernet_frame[12] == 0x08 && ethernet_frame[13] == 0x06)
		{
			// for arp request, we use a broadcast
			memset(&ethernet_frame[0], 0xff, 6);
		}
		else
		{
			// otherwise we forward the packet to
			// where it really belongs
			memcpy(&ethernet_frame[0], frame->destination_address, 6);
		}

		// add packet content
		ethernet_frame_size = frame->frame_length - 24 - 4 - 8;

		// for some reason, the packet is 22 bytes too small (??)
		ethernet_frame_size += 22;
		if (ethernet_frame_size > sizeof(ethernet_frame))
		{
			ethernet_frame_size = sizeof(ethernet_frame);
		}
		memcpy(&ethernet_frame[14], &frame->data_and_fcs[8], ethernet_frame_size);

		// add size of ethernet header
		ethernet_frame_size += 14;

		/*
		 * Send 802.3 frame
		 */
		qemu_send_packet(s->vc, ethernet_frame, ethernet_frame_size);
	}

	if (reply)
	{
		memcpy(reply->destination_address, frame->source_address, 6);
		Atheros_WLAN_init_frame(s, reply);
		Atheros_WLAN_insert_frame(s, reply);
	}
}

#endif /* CONFIG_WIN32 */
