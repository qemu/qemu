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

#ifndef atheros_wlan_h
#define atheros_wlan_h 1


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


/*
 * debug Atheros_WLAN card
 *
 * i.e. show all access traces
 */
// #define DEBUG_Atheros_WLAN 1
// #define DEBUG_Atheros_AP_WLAN 1

#define PCI_FREQUENCY 33000000L

#if defined (DEBUG_Atheros_WLAN)
#  define DEBUG_PRINT(x) do { struct timeval __tt; gettimeofday(&__tt, NULL); printf("%u:%u  ", __tt.tv_sec, __tt.tv_usec); printf x ; } while (0)
#else
#  define DEBUG_PRINT(x)
#endif

#if defined (DEBUG_Atheros_AP_WLAN)
#  define DEBUG_PRINT_AP(x) printf x ;
#else
#  define DEBUG_PRINT_AP(x)
#endif



/*
 * The madwifi driver crashes if too
 * many frames are in the receive
 * queue linked list
 *
 * This can happen when interrupts are
 * not picked up right away (what can
 * happen due to qemu's lazy interrupt
 * checking/handling)!!
 *
 * UPDATE: BinaryHAL suddenly seems to
 * work with the WINDOWS_RX_FRAME as well
 * which is even better (because more frames
 * may be received concurrently...)
 */
#define MAX_CONCURRENT_RX_FRAMES_WINDOWS_OR_OPEN_HAL	999
#define MAX_CONCURRENT_RX_FRAMES_BINARY_HAL		10
#define MAX_CONCURRENT_RX_FRAMES			MAX_CONCURRENT_RX_FRAMES_WINDOWS_OR_OPEN_HAL

/*
 * In case we are connecting with a windows guest OS
 * (or the ndiswrapper of the windows driver) we must
 * define this macro... otherwise no packets will be
 * received.
 *
 * If connecting with a linux guest/madwifi with the
 * macro defined it won't work on the other hand!!!
 */
#define WINXP_DRIVER	1
#define LINUX_DRIVER	2

#define PCI_CONFIG_AR5212	1
#define PCI_CONFIG_AR5424	2





#define	IEEE80211_IDLE					0xff

#define	IEEE80211_TYPE_MGT				0x00
#define	IEEE80211_TYPE_CTL				0x01
#define	IEEE80211_TYPE_DATA				0x02

#define	IEEE80211_TYPE_MGT_SUBTYPE_BEACON		0x08
#define	IEEE80211_TYPE_MGT_SUBTYPE_ACTION		0x0d
#define	IEEE80211_TYPE_MGT_SUBTYPE_PROBE_REQ		0x04
#define	IEEE80211_TYPE_MGT_SUBTYPE_PROBE_RESP		0x05
#define	IEEE80211_TYPE_MGT_SUBTYPE_AUTHENTICATION	0x0b
#define	IEEE80211_TYPE_MGT_SUBTYPE_DEAUTHENTICATION	0x0c
#define	IEEE80211_TYPE_MGT_SUBTYPE_ASSOCIATION_REQ	0x00
#define	IEEE80211_TYPE_MGT_SUBTYPE_ASSOCIATION_RESP	0x01
#define	IEEE80211_TYPE_MGT_SUBTYPE_DISASSOCIATION	0x09

#define	IEEE80211_TYPE_CTL_SUBTYPE_ACK			0x0d

#define	IEEE80211_TYPE_DATA_SUBTYPE_DATA		0x00


#define	IEEE80211_BEACON_PARAM_SSID			0x00
#define	IEEE80211_BEACON_PARAM_SSID_STRING		"\x00"
#define	IEEE80211_BEACON_PARAM_RATES			0x01
#define	IEEE80211_BEACON_PARAM_RATES_STRING		"\x01"
#define	IEEE80211_BEACON_PARAM_CHANNEL			0x03
#define	IEEE80211_BEACON_PARAM_CHANNEL_STRING		"\x03"
#define	IEEE80211_BEACON_PARAM_EXTENDED_RATES		0x32
#define	IEEE80211_BEACON_PARAM_EXTENDED_RATES_STRING	"\x32"






#define	IEEE80211_CHANNEL1_FREQUENCY			2412
#define	IEEE80211_CHANNEL2_FREQUENCY			2417
#define	IEEE80211_CHANNEL3_FREQUENCY			2422
#define	IEEE80211_CHANNEL4_FREQUENCY			2427
#define	IEEE80211_CHANNEL5_FREQUENCY			2432
#define	IEEE80211_CHANNEL6_FREQUENCY			2437
#define	IEEE80211_CHANNEL7_FREQUENCY			2442
#define	IEEE80211_CHANNEL8_FREQUENCY			2447
#define	IEEE80211_CHANNEL9_FREQUENCY			2452
#define	IEEE80211_CHANNEL10_FREQUENCY			2457
#define	IEEE80211_CHANNEL11_FREQUENCY			2462


#define IEEE80211_HEADER_SIZE				24

struct mac80211_frame
{
	struct mac80211_frame_control
	{
		unsigned	protocol_version	:2;
		unsigned	type			:2;
		unsigned	sub_type		:4;

		union
		{
			struct mac80211_frame_control_flags
			{
				unsigned	to_ds		:1;
				unsigned	from_ds		:1;
				unsigned	more_frag	:1;
				unsigned	retry		:1;
				unsigned	power_mng	:1;
				unsigned	more_data	:1;
				unsigned	wep		:1;
				unsigned	order		:1;
			} __attribute__((packed)) frame_control_flags;
			uint8_t	flags;
		};

	} __attribute__((packed)) frame_control;
	uint16_t	duration_id;

	union
	{
		uint8_t		address_1[6];
		uint8_t		destination_address[6];
	};

	union
	{
		uint8_t		address_2[6];
		uint8_t		source_address[6];
	};

	union
	{
		uint8_t		address_3[6];
		uint8_t		bssid_address[6];
	};

	struct mac80211_sequence_control
	{
		unsigned	fragment_number		:4;
		unsigned	sequence_number		:12;
	} __attribute__((packed)) sequence_control;

	// WHEN IS THIS USED??
	// uint8_t		address_4[6];

	// variable length, 2312 byte plus 4 byte frame-checksum
	uint8_t		data_and_fcs[2316];

	unsigned int frame_length;
	struct mac80211_frame *next_frame;

} __attribute__((packed));


#define GET_MEM_L(_mem, _addr)			_mem[_addr >> 2]
#define SET_MEM_L(_mem, _addr, _val)		_mem[_addr >> 2] = _val

#define WRITE_EEPROM(_mem, _val)					\
		SET_MEM_L(_mem, AR5K_EEPROM_DATA_5210, _val);		\
		SET_MEM_L(_mem, AR5K_EEPROM_DATA_5211, _val);




#define Atheros_WLAN_PCI_REVID_ATHEROS		0x01
#define Atheros_WLAN_PCI_REVID			Atheros_WLAN_PCI_REVID_ATHEROS


#define KiB 					1024
#define Atheros_WLAN_MEM_SIZE			(64 * KiB)
#define Atheros_WLAN_MEM_SANITIZE(x)		(x & (Atheros_WLAN_MEM_SIZE - 1))

#define Atheros_WLAN__STATE_NOT_AUTHENTICATED	0
#define Atheros_WLAN__STATE_AUTHENTICATED	1
#define Atheros_WLAN__STATE_ASSOCIATED		2


#define Atheros_WLAN__MAX_INJECT_QUEUE_SIZE	20


/*
 * We use a semaphore to make sure
 * that accessing the linked lists
 * inside the state is done atomically
 */
#define ATHEROS_WLAN_ACCESS_SEM_KEY		20071


/*
 * AR521X uses a very complicated algorithm to
 * express current channel... too lazy to understand
 * it... just use a matrix :-)
 *
 * ATTENTION: This matrix is valid only for little-endian
 * as the algorithm uses bitswapping
 *
 * NOTE: Maybe, bitswapping also takes care of this and
 * big-endian values thus correspond with this matrix, but
 * I just don't care ;-)
 */
struct Atheros_WLAN_frequency {
	u_int32_t	value1;
	u_int32_t	value2;
	u_int32_t	frequency;
};

struct pending_interrupt
{
	uint32_t status;
	struct pending_interrupt *next;
};

typedef struct Atheros_WLANState
{
	PCIDevice *pci_dev;
	VLANClientState *vc;
	int Atheros_WLAN_mmio_io_addr;

	uint32_t device_driver_type;

	uint8_t ipaddr[4];				// currently unused
	uint8_t macaddr[6];				// mac address

	uint8_t ap_ipaddr[4];				// currently unused
	uint8_t ap_macaddr[6];				// mac address

	// int irq;
	qemu_irq irq;
	uint32_t interrupt_p_mask;			// primary interrupt mask
	uint32_t interrupt_s_mask[5];			// secondary interrupt masks
	uint8_t interrupt_enabled;
	struct pending_interrupt *pending_interrupts;
	int access_semaphore;

	uint32_t current_frequency_partial_data[2];
	uint32_t current_frequency;


	target_phys_addr_t receive_queue_address;
	uint32_t receive_queue_count;

	uint32_t transmit_queue_size;
	uint8_t transmit_queue_enabled[16];
	target_phys_addr_t transmit_queue_address[16];
	uint32_t transmit_queue_processed[16];

	uint32_t mem[Atheros_WLAN_MEM_SIZE / 4];

	int eeprom_size;
	uint32_t *eeprom_data;

	uint32_t ap_state;
	int inject_timer_running;
	unsigned int inject_sequence_number;

	// various timers
	QEMUTimer *beacon_timer;
	QEMUTimer *inject_timer;

	int inject_queue_size;
	struct mac80211_frame *inject_queue;

} Atheros_WLANState;


/***********************************************************/
/* PCI Atheros_WLAN definitions */

typedef struct PCIAtheros_WLANState {
    PCIDevice dev;
    Atheros_WLANState Atheros_WLAN;
} PCIAtheros_WLANState;


#endif // atheros_wlan_h
