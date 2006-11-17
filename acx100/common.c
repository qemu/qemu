/***********************************************************************
** Copyright (C) 2003  ACX100 Open Source Project
**
** The contents of this file are subject to the Mozilla Public
** License Version 1.1 (the "License"); you may not use this file
** except in compliance with the License. You may obtain a copy of
** the License at http://www.mozilla.org/MPL/
**
** Software distributed under the License is distributed on an "AS
** IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
** implied. See the License for the specific language governing
** rights and limitations under the License.
**
** Alternatively, the contents of this file may be used under the
** terms of the GNU Public License version 2 (the "GPL"), in which
** case the provisions of the GPL are applicable instead of the
** above.  If you wish to allow the use of your version of this file
** only under the terms of the GPL and not to allow others to use
** your version of this file under the MPL, indicate your decision
** by deleting the provisions above and replace them with the notice
** and other provisions required by the GPL.  If you do not delete
** the provisions above, a recipient may use your version of this
** file under either the MPL or the GPL.
** ---------------------------------------------------------------------
** Inquiries regarding the ACX100 Open Source Project can be
** made directly to:
**
** acx100-users@lists.sf.net
** http://acx100.sf.net
** ---------------------------------------------------------------------
*/

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/pm.h>
#include <linux/vmalloc.h>
#include <net/iw_handler.h>

#include "acx.h"


/***********************************************************************
*/
static client_t *acx_l_sta_list_alloc(acx_device_t *adev);
static client_t *acx_l_sta_list_get_from_hash(acx_device_t *adev, const u8 *address);

static int acx_l_process_data_frame_master(acx_device_t *adev, rxbuffer_t *rxbuf);
static int acx_l_process_data_frame_client(acx_device_t *adev, rxbuffer_t *rxbuf);
/* static int acx_l_process_NULL_frame(acx_device_t *adev, rxbuffer_t *rxbuf, int vala); */
static int acx_l_process_mgmt_frame(acx_device_t *adev, rxbuffer_t *rxbuf);
static void acx_l_process_disassoc_from_sta(acx_device_t *adev, const wlan_fr_disassoc_t *req);
static void acx_l_process_disassoc_from_ap(acx_device_t *adev, const wlan_fr_disassoc_t *req);
static void acx_l_process_deauth_from_sta(acx_device_t *adev, const wlan_fr_deauthen_t *req);
static void acx_l_process_deauth_from_ap(acx_device_t *adev, const wlan_fr_deauthen_t *req);
static int acx_l_process_probe_response(acx_device_t *adev, wlan_fr_proberesp_t *req, const rxbuffer_t *rxbuf);
static int acx_l_process_assocresp(acx_device_t *adev, const wlan_fr_assocresp_t *req);
static int acx_l_process_reassocresp(acx_device_t *adev, const wlan_fr_reassocresp_t *req);
static int acx_l_process_authen(acx_device_t *adev, const wlan_fr_authen_t *req);
static int acx_l_transmit_assocresp(acx_device_t *adev, const wlan_fr_assocreq_t *req);
static int acx_l_transmit_reassocresp(acx_device_t *adev, const wlan_fr_reassocreq_t *req);
static int acx_l_transmit_deauthen(acx_device_t *adev, const u8 *addr, u16 reason);
static int acx_l_transmit_authen1(acx_device_t *adev);
static int acx_l_transmit_authen2(acx_device_t *adev, const wlan_fr_authen_t *req, client_t *clt);
static int acx_l_transmit_authen3(acx_device_t *adev, const wlan_fr_authen_t *req);
static int acx_l_transmit_authen4(acx_device_t *adev, const wlan_fr_authen_t *req);
static int acx_l_transmit_assoc_req(acx_device_t *adev);


/***********************************************************************
*/
#if ACX_DEBUG
unsigned int acx_debug /* will add __read_mostly later */ = ACX_DEFAULT_MSG;
/* parameter is 'debug', corresponding var is acx_debug */
module_param_named(debug, acx_debug, uint, 0);
MODULE_PARM_DESC(debug, "Debug level mask (see L_xxx constants)");
#endif

#ifdef MODULE_LICENSE
MODULE_LICENSE("Dual MPL/GPL");
#endif
/* USB had this: MODULE_AUTHOR("Martin Wawro <martin.wawro AT uni-dortmund.de>"); */
MODULE_AUTHOR("ACX100 Open Source Driver development team");
MODULE_DESCRIPTION("Driver for TI ACX1xx based wireless cards (CardBus/PCI/USB)");


/***********************************************************************
*/
/* Probably a number of acx's intermediate buffers for USB transfers,
** not to be confused with number of descriptors in tx/rx rings
** (which are not directly accessible to host in USB devices) */
#define USB_RX_CNT 10
#define USB_TX_CNT 10


/***********************************************************************
*/

/* minutes to wait until next radio recalibration: */
#define RECALIB_PAUSE	5

/* Please keep acx_reg_domain_ids_len in sync... */
const u8 acx_reg_domain_ids[acx_reg_domain_ids_len] =
	{ 0x10, 0x20, 0x30, 0x31, 0x32, 0x40, 0x41, 0x51 };
static const u16 reg_domain_channel_masks[acx_reg_domain_ids_len] =
	{ 0x07ff, 0x07ff, 0x1fff, 0x0600, 0x1e00, 0x2000, 0x3fff, 0x01fc };
const char * const
acx_reg_domain_strings[] = {
	/* 0 */	" 1-11 FCC (USA)",
	/* 1 */	" 1-11 DOC/IC (Canada)",
/* BTW: WLAN use in ETSI is regulated by ETSI standard EN 300 328-2 V1.1.2 */
	/* 2 */	" 1-13 ETSI (Europe)",
	/* 3 */	"10-11 Spain",
	/* 4 */	"10-13 France",
	/* 5 */	"   14 MKK (Japan)",
	/* 6 */	" 1-14 MKK1",
	/* 7 */	"  3-9 Israel (not all firmware versions)",
	NULL /* needs to remain as last entry */
};



/***********************************************************************
** Debugging support
*/
#ifdef PARANOID_LOCKING
static unsigned max_lock_time;
static unsigned max_sem_time;

void
acx_lock_unhold() { max_lock_time = 0; }
void
acx_sem_unhold() { max_sem_time = 0; }

static inline const char*
sanitize_str(const char *s)
{
	const char* t = strrchr(s, '/');
	if (t) return t + 1;
	return s;
}

void
acx_lock_debug(acx_device_t *adev, const char* where)
{
	unsigned int count = 100*1000*1000;
	where = sanitize_str(where);
	while (--count) {
		if (!spin_is_locked(&adev->lock)) break;
		cpu_relax();
	}
	if (!count) {
		printk(KERN_EMERG "LOCKUP: already taken at %s!\n", adev->last_lock);
		BUG();
	}
	adev->last_lock = where;
	rdtscl(adev->lock_time);
}
void
acx_unlock_debug(acx_device_t *adev, const char* where)
{
#ifdef SMP
	if (!spin_is_locked(&adev->lock)) {
		where = sanitize_str(where);
		printk(KERN_EMERG "STRAY UNLOCK at %s!\n", where);
		BUG();
	}
#endif
	if (acx_debug & L_LOCK) {
		unsigned long diff;
		rdtscl(diff);
		diff -= adev->lock_time;
		if (diff > max_lock_time) {
			where = sanitize_str(where);
			printk("max lock hold time %ld CPU ticks from %s "
				"to %s\n", diff, adev->last_lock, where);
			max_lock_time = diff;
		}
	}
}
void
acx_down_debug(acx_device_t *adev, const char* where)
{
	int sem_count;
	unsigned long timeout = jiffies + 5*HZ;

	where = sanitize_str(where);

	for (;;) {
		sem_count = atomic_read(&adev->sem.count);
		if (sem_count) break;
		if (time_after(jiffies, timeout))
			break;
		msleep(5);
	}
	if (!sem_count) {
		printk(KERN_EMERG "D STATE at %s! last sem at %s\n",
			where, adev->last_sem);
		dump_stack();
	}
	adev->last_sem = where;
	adev->sem_time = jiffies;
	down(&adev->sem);
	if (acx_debug & L_LOCK) {
		printk("%s: sem_down %d -> %d\n",
			where, sem_count, atomic_read(&adev->sem.count));
	}
}
void
acx_up_debug(acx_device_t *adev, const char* where)
{
	int sem_count = atomic_read(&adev->sem.count);
	if (sem_count) {
		where = sanitize_str(where);
		printk(KERN_EMERG "STRAY UP at %s! sem.count=%d\n", where, sem_count);
		dump_stack();
	}
	if (acx_debug & L_LOCK) {
		unsigned long diff = jiffies - adev->sem_time;
		if (diff > max_sem_time) {
			where = sanitize_str(where);
			printk("max sem hold time %ld jiffies from %s "
				"to %s\n", diff, adev->last_sem, where);
			max_sem_time = diff;
		}
	}
	up(&adev->sem);
	if (acx_debug & L_LOCK) {
		where = sanitize_str(where);
		printk("%s: sem_up %d -> %d\n",
			where, sem_count, atomic_read(&adev->sem.count));
	}
}
#endif /* PARANOID_LOCKING */


/***********************************************************************
*/
#if ACX_DEBUG > 1

static int acx_debug_func_indent;
#define DEBUG_TSC 0
#define FUNC_INDENT_INCREMENT 2

#if DEBUG_TSC
#define TIMESTAMP(d) unsigned long d; rdtscl(d)
#else
#define TIMESTAMP(d) unsigned long d = jiffies
#endif

static const char
spaces[] = "          " "          "; /* Nx10 spaces */

void
log_fn_enter(const char *funcname)
{
	int indent;
	TIMESTAMP(d);

	indent = acx_debug_func_indent;
	if (indent >= sizeof(spaces))
		indent = sizeof(spaces)-1;

	printk("%08ld %s==> %s\n",
		d % 100000000,
		spaces + (sizeof(spaces)-1) - indent,
		funcname
	);

	acx_debug_func_indent += FUNC_INDENT_INCREMENT;
}
void
log_fn_exit(const char *funcname)
{
	int indent;
	TIMESTAMP(d);

	acx_debug_func_indent -= FUNC_INDENT_INCREMENT;

	indent = acx_debug_func_indent;
	if (indent >= sizeof(spaces))
		indent = sizeof(spaces)-1;

	printk("%08ld %s<== %s\n",
		d % 100000000,
		spaces + (sizeof(spaces)-1) - indent,
		funcname
	);
}
void
log_fn_exit_v(const char *funcname, int v)
{
	int indent;
	TIMESTAMP(d);

	acx_debug_func_indent -= FUNC_INDENT_INCREMENT;

	indent = acx_debug_func_indent;
	if (indent >= sizeof(spaces))
		indent = sizeof(spaces)-1;

	printk("%08ld %s<== %s: %08X\n",
		d % 100000000,
		spaces + (sizeof(spaces)-1) - indent,
		funcname,
		v
	);
}
#endif /* ACX_DEBUG > 1 */


/***********************************************************************
** Basically a msleep with logging
*/
void
acx_s_msleep(int ms)
{
	FN_ENTER;
	msleep(ms);
	FN_EXIT0;
}


/***********************************************************************
** Not inlined: it's larger than it seems
*/
void
acx_print_mac(const char *head, const u8 *mac, const char *tail)
{
	printk("%s"MACSTR"%s", head, MAC(mac), tail);
}


/***********************************************************************
** acx_get_status_name
*/
static const char*
acx_get_status_name(u16 status)
{
	static const char * const str[] = {
		"STOPPED", "SCANNING", "WAIT_AUTH",
		"AUTHENTICATED", "ASSOCIATED", "INVALID??"
	};
	if (status > VEC_SIZE(str)-1)
		status = VEC_SIZE(str)-1;

	return str[status];
}


/***********************************************************************
** acx_get_packet_type_string
*/
#if ACX_DEBUG
const char*
acx_get_packet_type_string(u16 fc)
{
	static const char * const mgmt_arr[] = {
		"MGMT/AssocReq", "MGMT/AssocResp", "MGMT/ReassocReq",
		"MGMT/ReassocResp", "MGMT/ProbeReq", "MGMT/ProbeResp",
		"MGMT/UNKNOWN", "MGMT/UNKNOWN", "MGMT/Beacon", "MGMT/ATIM",
		"MGMT/Disassoc", "MGMT/Authen", "MGMT/Deauthen"
	};
	static const char * const ctl_arr[] = {
		"CTL/PSPoll", "CTL/RTS", "CTL/CTS", "CTL/Ack", "CTL/CFEnd",
		"CTL/CFEndCFAck"
	};
	static const char * const data_arr[] = {
		"DATA/DataOnly", "DATA/Data CFAck", "DATA/Data CFPoll",
		"DATA/Data CFAck/CFPoll", "DATA/Null", "DATA/CFAck",
		"DATA/CFPoll", "DATA/CFAck/CFPoll"
	};
	const char *str;
	u8 fstype = (WF_FC_FSTYPE & fc) >> 4;
	u8 ctl;

	switch (WF_FC_FTYPE & fc) {
	case WF_FTYPE_MGMT:
		if (fstype < VEC_SIZE(mgmt_arr))
			str = mgmt_arr[fstype];
		else
			str = "MGMT/UNKNOWN";
		break;
	case WF_FTYPE_CTL:
		ctl = fstype - 0x0a;
		if (ctl < VEC_SIZE(ctl_arr))
			str = ctl_arr[ctl];
		else
			str = "CTL/UNKNOWN";
		break;
	case WF_FTYPE_DATA:
		if (fstype < VEC_SIZE(data_arr))
			str = data_arr[fstype];
		else
			str = "DATA/UNKNOWN";
		break;
	default:
		str = "UNKNOWN";
		break;
	}
	return str;
}
#endif


/***********************************************************************
** acx_wlan_reason_str
*/
static inline const char*
acx_wlan_reason_str(u16 reason)
{
	static const char* const reason_str[] = {
	/*  0 */	"?",
	/*  1 */	"unspecified",
	/*  2 */	"prev auth is not valid",
	/*  3 */	"leaving BBS",
	/*  4 */	"due to inactivity",
	/*  5 */	"AP is busy",
	/*  6 */	"got class 2 frame from non-auth'ed STA",
	/*  7 */	"got class 3 frame from non-assoc'ed STA",
	/*  8 */	"STA has left BSS",
	/*  9 */	"assoc without auth is not allowed",
	/* 10 */	"bad power setting (802.11h)",
	/* 11 */	"bad channel (802.11i)",
	/* 12 */	"?",
	/* 13 */	"invalid IE",
	/* 14 */	"MIC failure",
	/* 15 */	"four-way handshake timeout",
	/* 16 */	"group key handshake timeout",
	/* 17 */	"IE is different",
	/* 18 */	"invalid group cipher",
	/* 19 */	"invalid pairwise cipher",
	/* 20 */	"invalid AKMP",
	/* 21 */	"unsupported RSN version",
	/* 22 */	"invalid RSN IE cap",
	/* 23 */	"802.1x failed",
	/* 24 */	"cipher suite rejected"
	};
	return reason < VEC_SIZE(reason_str) ? reason_str[reason] : "?";
}


/***********************************************************************
** acx_cmd_status_str
*/
const char*
acx_cmd_status_str(unsigned int state)
{
	static const char * const cmd_error_strings[] = {
		"Idle",
		"Success",
		"Unknown Command",
		"Invalid Information Element",
		"Channel rejected",
		"Channel invalid in current regulatory domain",
		"MAC invalid",
		"Command rejected (read-only information element)",
		"Command rejected",
		"Already asleep",
		"TX in progress",
		"Already awake",
		"Write only",
		"RX in progress",
		"Invalid parameter",
		"Scan in progress",
		"Failed"
	};
	return state < VEC_SIZE(cmd_error_strings) ?
			cmd_error_strings[state] : "?";
}


/***********************************************************************
** get_status_string
*/
static inline const char*
get_status_string(unsigned int status)
{
	/* A bit shortened, but hopefully still understandable */
	static const char * const status_str[] = {
	/* 0 */	"Successful",
	/* 1 */	"Unspecified failure",
	/* 2 */	"reserved",
	/* 3 */	"reserved",
	/* 4 */	"reserved",
	/* 5 */	"reserved",
	/* 6 */	"reserved",
	/* 7 */	"reserved",
	/* 8 */	"reserved",
	/* 9 */	"reserved",
	/*10 */	"Cannot support all requested capabilities in Capability Information field",
	/*11 */	"Reassoc denied (reason outside of 802.11b scope)",
	/*12 */	"Assoc denied (reason outside of 802.11b scope), maybe MAC filtering by peer?",
	/*13 */	"Responding station doesnt support specified auth algorithm",
	/*14 */	"Auth rejected: wrong transaction sequence number",
	/*15 */	"Auth rejected: challenge failure",
	/*16 */	"Auth rejected: timeout for next frame in sequence",
	/*17 */	"Assoc denied: too many STAs on this AP",
	/*18 */	"Assoc denied: requesting STA doesnt support all data rates in basic set",
	/*19 */	"Assoc denied: requesting STA doesnt support Short Preamble",
	/*20 */	"Assoc denied: requesting STA doesnt support PBCC Modulation",
	/*21 */	"Assoc denied: requesting STA doesnt support Channel Agility"
	/*22 */	"reserved",
	/*23 */	"reserved",
	/*24 */	"reserved",
	/*25 */	"Assoc denied: requesting STA doesnt support Short Slot Time",
	/*26 */	"Assoc denied: requesting STA doesnt support DSSS-OFDM"
	};

	return status_str[status < VEC_SIZE(status_str) ? status : 2];
}


/***********************************************************************
*/
void
acx_log_bad_eid(wlan_hdr_t* hdr, int len, wlan_ie_t* ie_ptr)
{
	if (acx_debug & L_ASSOC) {
		int offset = (u8*)ie_ptr - (u8*)hdr;
		printk("acx: unknown EID %d in mgmt frame at offset %d. IE: ",
				ie_ptr->eid, offset);
	/* IE len can be bogus, IE can extend past packet end. Oh well... */
		acx_dump_bytes(ie_ptr, ie_ptr->len + 2);
		if (acx_debug & L_DATA) {
			printk("frame (%s): ",
			acx_get_packet_type_string(le16_to_cpu(hdr->fc)));
			acx_dump_bytes(hdr, len);
		}
	}
}


/***********************************************************************
*/
#if ACX_DEBUG
void
acx_dump_bytes(const void *data, int num)
{
	const u8* ptr = (const u8*)data;

	if (num <= 0) {
		printk("\n");
		return;
	}

	while (num >= 16) {
		printk( "%02X %02X %02X %02X %02X %02X %02X %02X "
			"%02X %02X %02X %02X %02X %02X %02X %02X\n",
			ptr[0], ptr[1], ptr[2], ptr[3],
			ptr[4], ptr[5], ptr[6], ptr[7],
			ptr[8], ptr[9], ptr[10], ptr[11],
			ptr[12], ptr[13], ptr[14], ptr[15]);
		num -= 16;
		ptr += 16;
	}
	if (num > 0) {
		while (--num > 0)
			printk("%02X ", *ptr++);
		printk("%02X\n", *ptr);
	}
}
#endif


/***********************************************************************
** acx_s_get_firmware_version
*/
void
acx_s_get_firmware_version(acx_device_t *adev)
{
	fw_ver_t fw;
	u8 hexarr[4] = { 0, 0, 0, 0 };
	int hexidx = 0, val = 0;
	const char *num;
	char c;

	FN_ENTER;

	memset(fw.fw_id, 'E', FW_ID_SIZE);
	acx_s_interrogate(adev, &fw, ACX1xx_IE_FWREV);
	memcpy(adev->firmware_version, fw.fw_id, FW_ID_SIZE);
	adev->firmware_version[FW_ID_SIZE] = '\0';

	log(L_DEBUG, "fw_ver: fw_id='%s' hw_id=%08X\n",
				adev->firmware_version, fw.hw_id);

	if (strncmp(fw.fw_id, "Rev ", 4) != 0) {
		printk("acx: strange firmware version string "
			"'%s', please report\n", adev->firmware_version);
		adev->firmware_numver = 0x01090407; /* assume 1.9.4.7 */
	} else {
		num = &fw.fw_id[4];
		while (1) {
			c = *num++;
			if ((c == '.') || (c == '\0')) {
				hexarr[hexidx++] = val;
				if ((hexidx > 3) || (c == '\0')) /* end? */
					break;
				val = 0;
				continue;
			}
			if ((c >= '0') && (c <= '9'))
				c -= '0';
			else
				c = c - 'a' + (char)10;
			val = val*16 + c;
		}

		adev->firmware_numver = (u32)(
				(hexarr[0] << 24) + (hexarr[1] << 16)
				+ (hexarr[2] << 8) + hexarr[3]);
		log(L_DEBUG, "firmware_numver 0x%08X\n", adev->firmware_numver);
	}
	if (IS_ACX111(adev)) {
		if (adev->firmware_numver == 0x00010011) {
			/* This one does not survive floodpinging */
			printk("acx: firmware '%s' is known to be buggy, "
				"please upgrade\n", adev->firmware_version);
		}
	}

	adev->firmware_id = le32_to_cpu(fw.hw_id);

	/* we're able to find out more detailed chip names now */
	switch (adev->firmware_id & 0xffff0000) {
		case 0x01010000:
		case 0x01020000:
			adev->chip_name = "TNETW1100A";
			break;
		case 0x01030000:
			adev->chip_name = "TNETW1100B";
			break;
		case 0x03000000:
		case 0x03010000:
			adev->chip_name = "TNETW1130";
			break;
		case 0x04030000: /* 0x04030101 is TNETW1450 */
			adev->chip_name = "TNETW1450";
			break;
		default:
			printk("acx: unknown chip ID 0x%08X, "
				"please report\n", adev->firmware_id);
			break;
	}

	FN_EXIT0;
}


/***********************************************************************
** acx_display_hardware_details
**
** Displays hw/fw version, radio type etc...
*/
void
acx_display_hardware_details(acx_device_t *adev)
{
	const char *radio_str, *form_str;

	FN_ENTER;

	switch (adev->radio_type) {
	case RADIO_MAXIM_0D:
		radio_str = "Maxim";
		break;
	case RADIO_RFMD_11:
		radio_str = "RFMD";
		break;
	case RADIO_RALINK_15:
		radio_str = "Ralink";
		break;
	case RADIO_RADIA_16:
		radio_str = "Radia";
		break;
	case RADIO_UNKNOWN_17:
		/* TI seems to have a radio which is
		 * additionally 802.11a capable, too */
		radio_str = "802.11a/b/g radio?! Please report";
		break;
	case RADIO_UNKNOWN_19:
		radio_str = "A radio used by Safecom cards?! Please report";
		break;
	case RADIO_UNKNOWN_1B:
		radio_str = "An unknown radio used by TNETW1450 USB adapters";
		break;
	default:
		radio_str = "UNKNOWN, please report radio type name!";
		break;
	}

	switch (adev->form_factor) {
	case 0x00:
		form_str = "unspecified";
		break;
	case 0x01:
		form_str = "(mini-)PCI / CardBus";
		break;
	case 0x02:
		form_str = "USB";
		break;
	case 0x03:
		form_str = "Compact Flash";
		break;
	default:
		form_str = "UNKNOWN, please report";
		break;
	}

	printk("acx: form factor 0x%02X (%s), "
		"radio type 0x%02X (%s), EEPROM version 0x%02X, "
		"uploaded firmware '%s' (0x%08X)\n",
		adev->form_factor, form_str, adev->radio_type, radio_str,
		adev->eeprom_version, adev->firmware_version,
		adev->firmware_id);

	FN_EXIT0;
}


/***********************************************************************
*/
int
acx_e_change_mtu(struct net_device *ndev, int mtu)
{
	enum {
		MIN_MTU = 256,
		MAX_MTU = WLAN_DATA_MAXLEN - (ETH_HLEN)
	};

	if (mtu < MIN_MTU || mtu > MAX_MTU)
		return -EINVAL;

	ndev->mtu = mtu;
	return 0;
}


/***********************************************************************
** acx_e_get_stats, acx_e_get_wireless_stats
*/
struct net_device_stats*
acx_e_get_stats(struct net_device *ndev)
{
	acx_device_t *adev = ndev2adev(ndev);
	return &adev->stats;
}

struct iw_statistics*
acx_e_get_wireless_stats(struct net_device *ndev)
{
	acx_device_t *adev = ndev2adev(ndev);
	return &adev->wstats;
}


/***********************************************************************
** maps acx111 tx descr rate field to acx100 one
*/
const u8
acx_bitpos2rate100[] = {
	RATE100_1	,/* 0 */
	RATE100_2	,/* 1 */
	RATE100_5	,/* 2 */
	RATE100_2	,/* 3, should not happen */
	RATE100_2	,/* 4, should not happen */
	RATE100_11	,/* 5 */
	RATE100_2	,/* 6, should not happen */
	RATE100_2	,/* 7, should not happen */
	RATE100_22	,/* 8 */
	RATE100_2	,/* 9, should not happen */
	RATE100_2	,/* 10, should not happen */
	RATE100_2	,/* 11, should not happen */
	RATE100_2	,/* 12, should not happen */
	RATE100_2	,/* 13, should not happen */
	RATE100_2	,/* 14, should not happen */
	RATE100_2	,/* 15, should not happen */
};

u8
acx_rate111to100(u16 r) {
	return acx_bitpos2rate100[highest_bit(r)];
}


/***********************************************************************
** Calculate level like the feb 2003 windows driver seems to do
*/
static u8
acx_signal_to_winlevel(u8 rawlevel)
{
	/* u8 winlevel = (u8) (0.5 + 0.625 * rawlevel); */
	u8 winlevel = ((4 + (rawlevel * 5)) / 8);

	if (winlevel > 100)
		winlevel = 100;
	return winlevel;
}

u8
acx_signal_determine_quality(u8 signal, u8 noise)
{
	int qual;

	qual = (((signal - 30) * 100 / 70) + (100 - noise * 4)) / 2;

	if (qual > 100)
		return 100;
	if (qual < 0)
		return 0;
	return qual;
}


/***********************************************************************
** Interrogate/configure commands
*/

/* FIXME: the lengths given here probably aren't always correct.
 * They should be gradually replaced by proper "sizeof(acx1XX_ie_XXXX)-4",
 * unless the firmware actually expects a different length than the struct length */
static const u16
acx100_ie_len[] = {
	0,
	ACX100_IE_ACX_TIMER_LEN,
	sizeof(acx100_ie_powersave_t)-4, /* is that 6 or 8??? */
	ACX1xx_IE_QUEUE_CONFIG_LEN,
	ACX100_IE_BLOCK_SIZE_LEN,
	ACX1xx_IE_MEMORY_CONFIG_OPTIONS_LEN,
	ACX1xx_IE_RATE_FALLBACK_LEN,
	ACX100_IE_WEP_OPTIONS_LEN,
	ACX1xx_IE_MEMORY_MAP_LEN, /*	ACX1xx_IE_SSID_LEN, */
	0,
	ACX1xx_IE_ASSOC_ID_LEN,
	0,
	ACX111_IE_CONFIG_OPTIONS_LEN,
	ACX1xx_IE_FWREV_LEN,
	ACX1xx_IE_FCS_ERROR_COUNT_LEN,
	ACX1xx_IE_MEDIUM_USAGE_LEN,
	ACX1xx_IE_RXCONFIG_LEN,
	0,
	0,
	sizeof(fw_stats_t)-4,
	0,
	ACX1xx_IE_FEATURE_CONFIG_LEN,
	ACX111_IE_KEY_CHOOSE_LEN,
	ACX1FF_IE_MISC_CONFIG_TABLE_LEN,
	ACX1FF_IE_WONE_CONFIG_LEN,
	0,
	ACX1FF_IE_TID_CONFIG_LEN,
	0,
	0,
	0,
	ACX1FF_IE_CALIB_ASSESSMENT_LEN,
	ACX1FF_IE_BEACON_FILTER_OPTIONS_LEN,
	ACX1FF_IE_LOW_RSSI_THRESH_OPT_LEN,
	ACX1FF_IE_NOISE_HISTOGRAM_RESULTS_LEN,
	0,
	ACX1FF_IE_PACKET_DETECT_THRESH_LEN,
	ACX1FF_IE_TX_CONFIG_OPTIONS_LEN,
	ACX1FF_IE_CCA_THRESHOLD_LEN,
	ACX1FF_IE_EVENT_MASK_LEN,
	ACX1FF_IE_DTIM_PERIOD_LEN,
	0,
	ACX1FF_IE_ACI_CONFIG_SET_LEN,
	0,
	0,
	0,
	0,
	0,
	0,
	ACX1FF_IE_EEPROM_VER_LEN,
};

static const u16
acx100_ie_len_dot11[] = {
	0,
	ACX1xx_IE_DOT11_STATION_ID_LEN,
	0,
	ACX100_IE_DOT11_BEACON_PERIOD_LEN,
	ACX1xx_IE_DOT11_DTIM_PERIOD_LEN,
	ACX1xx_IE_DOT11_SHORT_RETRY_LIMIT_LEN,
	ACX1xx_IE_DOT11_LONG_RETRY_LIMIT_LEN,
	ACX100_IE_DOT11_WEP_DEFAULT_KEY_WRITE_LEN,
	ACX1xx_IE_DOT11_MAX_XMIT_MSDU_LIFETIME_LEN,
	0,
	ACX1xx_IE_DOT11_CURRENT_REG_DOMAIN_LEN,
	ACX1xx_IE_DOT11_CURRENT_ANTENNA_LEN,
	0,
	ACX1xx_IE_DOT11_TX_POWER_LEVEL_LEN,
	ACX1xx_IE_DOT11_CURRENT_CCA_MODE_LEN,
	ACX100_IE_DOT11_ED_THRESHOLD_LEN,
	ACX1xx_IE_DOT11_WEP_DEFAULT_KEY_SET_LEN,
	0,
	0,
	0,
};

static const u16
acx111_ie_len[] = {
	0,
	ACX100_IE_ACX_TIMER_LEN,
	sizeof(acx111_ie_powersave_t)-4,
	ACX1xx_IE_QUEUE_CONFIG_LEN,
	ACX100_IE_BLOCK_SIZE_LEN,
	ACX1xx_IE_MEMORY_CONFIG_OPTIONS_LEN,
	ACX1xx_IE_RATE_FALLBACK_LEN,
	ACX100_IE_WEP_OPTIONS_LEN,
	ACX1xx_IE_MEMORY_MAP_LEN, /*	ACX1xx_IE_SSID_LEN, */
	0,
	ACX1xx_IE_ASSOC_ID_LEN,
	0,
	ACX111_IE_CONFIG_OPTIONS_LEN,
	ACX1xx_IE_FWREV_LEN,
	ACX1xx_IE_FCS_ERROR_COUNT_LEN,
	ACX1xx_IE_MEDIUM_USAGE_LEN,
	ACX1xx_IE_RXCONFIG_LEN,
	0,
	0,
	sizeof(fw_stats_t)-4,
	0,
	ACX1xx_IE_FEATURE_CONFIG_LEN,
	ACX111_IE_KEY_CHOOSE_LEN,
	ACX1FF_IE_MISC_CONFIG_TABLE_LEN,
	ACX1FF_IE_WONE_CONFIG_LEN,
	0,
	ACX1FF_IE_TID_CONFIG_LEN,
	0,
	0,
	0,
	ACX1FF_IE_CALIB_ASSESSMENT_LEN,
	ACX1FF_IE_BEACON_FILTER_OPTIONS_LEN,
	ACX1FF_IE_LOW_RSSI_THRESH_OPT_LEN,
	ACX1FF_IE_NOISE_HISTOGRAM_RESULTS_LEN,
	0,
	ACX1FF_IE_PACKET_DETECT_THRESH_LEN,
	ACX1FF_IE_TX_CONFIG_OPTIONS_LEN,
	ACX1FF_IE_CCA_THRESHOLD_LEN,
	ACX1FF_IE_EVENT_MASK_LEN,
	ACX1FF_IE_DTIM_PERIOD_LEN,
	0,
	ACX1FF_IE_ACI_CONFIG_SET_LEN,
	0,
	0,
	0,
	0,
	0,
	0,
	ACX1FF_IE_EEPROM_VER_LEN,
};

static const u16
acx111_ie_len_dot11[] = {
	0,
	ACX1xx_IE_DOT11_STATION_ID_LEN,
	0,
	ACX100_IE_DOT11_BEACON_PERIOD_LEN,
	ACX1xx_IE_DOT11_DTIM_PERIOD_LEN,
	ACX1xx_IE_DOT11_SHORT_RETRY_LIMIT_LEN,
	ACX1xx_IE_DOT11_LONG_RETRY_LIMIT_LEN,
	ACX100_IE_DOT11_WEP_DEFAULT_KEY_WRITE_LEN,
	ACX1xx_IE_DOT11_MAX_XMIT_MSDU_LIFETIME_LEN,
	0,
	ACX1xx_IE_DOT11_CURRENT_REG_DOMAIN_LEN,
	ACX1xx_IE_DOT11_CURRENT_ANTENNA_LEN,
	0,
	ACX1xx_IE_DOT11_TX_POWER_LEVEL_LEN,
	ACX1xx_IE_DOT11_CURRENT_CCA_MODE_LEN,
	ACX100_IE_DOT11_ED_THRESHOLD_LEN,
	ACX1xx_IE_DOT11_WEP_DEFAULT_KEY_SET_LEN,
	0,
	0,
	0,
};


#undef FUNC
#define FUNC "configure"
#if !ACX_DEBUG
int
acx_s_configure(acx_device_t *adev, void *pdr, int type)
{
#else
int
acx_s_configure_debug(acx_device_t *adev, void *pdr, int type, const char* typestr)
{
#endif
	u16 len;
	int res;

	if (type < 0x1000)
		len = adev->ie_len[type];
	else
		len = adev->ie_len_dot11[type - 0x1000];

	log(L_CTL, FUNC"(type:%s,len:%u)\n", typestr, len);
	if (unlikely(!len)) {
		log(L_DEBUG, "zero-length type %s?!\n", typestr);
	}

	((acx_ie_generic_t *)pdr)->type = cpu_to_le16(type);
	((acx_ie_generic_t *)pdr)->len = cpu_to_le16(len);
	res = acx_s_issue_cmd(adev, ACX1xx_CMD_CONFIGURE, pdr, len + 4);
	if (unlikely(OK != res)) {
#if ACX_DEBUG
		printk("%s: "FUNC"(type:%s) FAILED\n", adev->ndev->name, typestr);
#else
		printk("%s: "FUNC"(type:0x%X) FAILED\n", adev->ndev->name, type);
#endif
		/* dump_stack() is already done in issue_cmd() */
	}
	return res;
}

#undef FUNC
#define FUNC "interrogate"
#if !ACX_DEBUG
int
acx_s_interrogate(acx_device_t *adev, void *pdr, int type)
{
#else
int
acx_s_interrogate_debug(acx_device_t *adev, void *pdr, int type,
		const char* typestr)
{
#endif
	u16 len;
	int res;

	/* FIXME: no check whether this exceeds the array yet.
	 * We should probably remember the number of entries... */
	if (type < 0x1000)
		len = adev->ie_len[type];
	else
		len = adev->ie_len_dot11[type-0x1000];

	log(L_CTL, FUNC"(type:%s,len:%u)\n", typestr, len);

	((acx_ie_generic_t *)pdr)->type = cpu_to_le16(type);
	((acx_ie_generic_t *)pdr)->len = cpu_to_le16(len);
	res = acx_s_issue_cmd(adev, ACX1xx_CMD_INTERROGATE, pdr, len + 4);
	if (unlikely(OK != res)) {
#if ACX_DEBUG
		printk("%s: "FUNC"(type:%s) FAILED\n", adev->ndev->name, typestr);
#else
		printk("%s: "FUNC"(type:0x%X) FAILED\n", adev->ndev->name, type);
#endif
		/* dump_stack() is already done in issue_cmd() */
	}
	return res;
}

#if CMD_DISCOVERY
void
great_inquisitor(acx_device_t *adev)
{
	static struct {
		u16	type;
		u16	len;
		/* 0x200 was too large here: */
		u8	data[0x100 - 4];
	} ACX_PACKED ie;
	u16 type;

	FN_ENTER;

	/* 0..0x20, 0x1000..0x1020 */
	for (type = 0; type <= 0x1020; type++) {
		if (type == 0x21)
			type = 0x1000;
		ie.type = cpu_to_le16(type);
		ie.len = cpu_to_le16(sizeof(ie) - 4);
		acx_s_issue_cmd(adev, ACX1xx_CMD_INTERROGATE, &ie, sizeof(ie));
	}
	FN_EXIT0;
}
#endif


#ifdef CONFIG_PROC_FS
/***********************************************************************
** /proc files
*/
/***********************************************************************
** acx_l_proc_output
** Generate content for our /proc entry
**
** Arguments:
**	buf is a pointer to write output to
**	adev is the usual pointer to our private struct acx_device
** Returns:
**	number of bytes actually written to buf
** Side effects:
**	none
*/
static int
acx_l_proc_output(char *buf, acx_device_t *adev)
{
	char *p = buf;
	int i;

	FN_ENTER;

	p += sprintf(p,
		"acx driver version:\t\t" ACX_RELEASE "\n"
		"Wireless extension version:\t" STRING(WIRELESS_EXT) "\n"
		"chip name:\t\t\t%s (0x%08X)\n"
		"radio type:\t\t\t0x%02X\n"
		"form factor:\t\t\t0x%02X\n"
		"EEPROM version:\t\t\t0x%02X\n"
		"firmware version:\t\t%s (0x%08X)\n",
		adev->chip_name, adev->firmware_id,
		adev->radio_type,
		adev->form_factor,
		adev->eeprom_version,
		adev->firmware_version, adev->firmware_numver);

	for (i = 0; i < VEC_SIZE(adev->sta_list); i++) {
		struct client *bss = &adev->sta_list[i];
		if (!bss->used) continue;
		p += sprintf(p, "BSS %u BSSID "MACSTR" ESSID %s channel %u "
			"Cap 0x%X SIR %u SNR %u\n",
			i, MAC(bss->bssid), (char*)bss->essid, bss->channel,
			bss->cap_info, bss->sir, bss->snr);
	}
	p += sprintf(p, "status:\t\t\t%u (%s)\n",
			adev->status, acx_get_status_name(adev->status));

	FN_EXIT1(p - buf);
	return p - buf;
}


/***********************************************************************
*/
static int
acx_s_proc_diag_output(char *buf, acx_device_t *adev)
{
	char *p = buf;
	unsigned long flags;
	unsigned int len = 0, partlen;
	u32 temp1, temp2;
	u8 *st, *st_end;
#ifdef __BIG_ENDIAN
	u8 *st2;
#endif
	fw_stats_t *fw_stats;
	char *part_str = NULL;
	fw_stats_tx_t *tx = NULL;
	fw_stats_rx_t *rx = NULL;
	fw_stats_dma_t *dma = NULL;
	fw_stats_irq_t *irq = NULL;
	fw_stats_wep_t *wep = NULL;
	fw_stats_pwr_t *pwr = NULL;
	fw_stats_mic_t *mic = NULL;
	fw_stats_aes_t *aes = NULL;
	fw_stats_event_t *evt = NULL;

	FN_ENTER;

	acx_lock(adev, flags);

	if (IS_PCI(adev))
		p = acxpci_s_proc_diag_output(p, adev);

	p += sprintf(p,
		"\n"
		"** network status **\n"
		"dev_state_mask 0x%04X\n"
		"status %u (%s), "
		"mode %u, channel %u, "
		"reg_dom_id 0x%02X, reg_dom_chanmask 0x%04X, ",
		adev->dev_state_mask,
		adev->status, acx_get_status_name(adev->status),
		adev->mode, adev->channel,
		adev->reg_dom_id, adev->reg_dom_chanmask
		);
	p += sprintf(p,
		"ESSID \"%s\", essid_active %d, essid_len %d, "
		"essid_for_assoc \"%s\", nick \"%s\"\n"
		"WEP ena %d, restricted %d, idx %d\n",
		adev->essid, adev->essid_active, (int)adev->essid_len,
		adev->essid_for_assoc, adev->nick,
		adev->wep_enabled, adev->wep_restricted,
		adev->wep_current_index);
	p += sprintf(p, "dev_addr  "MACSTR"\n", MAC(adev->dev_addr));
	p += sprintf(p, "bssid     "MACSTR"\n", MAC(adev->bssid));
	p += sprintf(p, "ap_filter "MACSTR"\n", MAC(adev->ap));

	p += sprintf(p,
		"\n"
		"** PHY status **\n"
		"tx_disabled %d, tx_level_dbm %d\n" /* "tx_level_val %d, tx_level_auto %d\n" */
		"sensitivity %d, antenna 0x%02X, ed_threshold %d, cca %d, preamble_mode %d\n"
		"rts_threshold %d, frag_threshold %d, short_retry %d, long_retry %d\n"
		"msdu_lifetime %d, listen_interval %d, beacon_interval %d\n",
		adev->tx_disabled, adev->tx_level_dbm, /* adev->tx_level_val, adev->tx_level_auto, */
		adev->sensitivity, adev->antenna, adev->ed_threshold, adev->cca, adev->preamble_mode,
		adev->rts_threshold, adev->frag_threshold, adev->short_retry, adev->long_retry,
		adev->msdu_lifetime, adev->listen_interval, adev->beacon_interval);

	acx_unlock(adev, flags);

	p += sprintf(p,
		"\n"
		"** Firmware **\n"
		"NOTE: version dependent statistics layout, "
		"please report if you suspect wrong parsing!\n"
		"\n"
		"version \"%s\"\n", adev->firmware_version);

	/* TODO: may replace kmalloc/memset with kzalloc once
	 * Linux 2.6.14 is widespread */
	fw_stats = kmalloc(sizeof(*fw_stats), GFP_KERNEL);
	if (!fw_stats) {
		FN_EXIT1(0);
		return 0;
	}
	memset(fw_stats, 0, sizeof(*fw_stats));

	st = (u8 *)fw_stats;

	part_str = "statistics query command";

	if (OK != acx_s_interrogate(adev, st, ACX1xx_IE_FIRMWARE_STATISTICS))
		goto fw_stats_end;

	st += sizeof(u16);
	len = *(u16 *)st;

	if (len > sizeof(*fw_stats)) {
		p += sprintf(p,
			"firmware version with bigger fw_stats struct detected\n"
			"(%u vs. %u), please report\n", len, sizeof(fw_stats_t));
		if (len > sizeof(*fw_stats)) {
			p += sprintf(p, "struct size exceeded allocation!\n");
			len = sizeof(*fw_stats);
		}
	}
	st += sizeof(u16);
	st_end = st - 2*sizeof(u16) + len;

#ifdef __BIG_ENDIAN
	/* let's make one bold assumption here:
	 * (hopefully!) *all* statistics fields are u32 only,
	 * thus if we need to make endianness corrections
	 * we can simply do them in one go, in advance */
	st2 = (u8 *)fw_stats;
	for (temp1 = 0; temp1 < len; temp1 += 4, st2 += 4)
		*(u32 *)st2 = le32_to_cpu(*(u32 *)st2);
#endif

	part_str = "Rx/Tx";

	/* directly at end of a struct part? --> no error! */
	if (st == st_end)
		goto fw_stats_end;

	tx = (fw_stats_tx_t *)st;
	st += sizeof(fw_stats_tx_t);
	rx = (fw_stats_rx_t *)st;
	st += sizeof(fw_stats_rx_t);
	partlen = sizeof(fw_stats_tx_t) + sizeof(fw_stats_rx_t);

	if (IS_ACX100(adev)) {
		/* at least ACX100 PCI F/W 1.9.8.b
		 * and ACX100 USB F/W 1.0.7-USB
		 * don't have those two fields... */
		st -= 2*sizeof(u32);

		/* our parsing doesn't quite match this firmware yet,
		 * log failure */
		if (st > st_end)
			goto fw_stats_fail;
		temp1 = temp2 = 999999999;
	} else {
		if (st > st_end)
			goto fw_stats_fail;
		temp1 = rx->rx_aci_events;
		temp2 = rx->rx_aci_resets;
	}

	p += sprintf(p,
		"%s:\n"
		"  tx_desc_overfl %u\n"
		"  rx_OutOfMem %u, rx_hdr_overfl %u, rx_hw_stuck %u\n"
		"  rx_dropped_frame %u, rx_frame_ptr_err %u, rx_xfr_hint_trig %u\n"
		"  rx_aci_events %u, rx_aci_resets %u\n",
		part_str,
		tx->tx_desc_of,
		rx->rx_oom,
		rx->rx_hdr_of,
		rx->rx_hw_stuck,
		rx->rx_dropped_frame,
		rx->rx_frame_ptr_err,
		rx->rx_xfr_hint_trig,
		temp1,
		temp2);

	part_str = "DMA";

	if (st == st_end)
		goto fw_stats_end;

	dma = (fw_stats_dma_t *)st;
	partlen = sizeof(fw_stats_dma_t);
	st += partlen;

	if (st > st_end)
		goto fw_stats_fail;

	p += sprintf(p,
		"%s:\n"
		"  rx_dma_req %u, rx_dma_err %u, tx_dma_req %u, tx_dma_err %u\n",
		part_str,
		dma->rx_dma_req,
		dma->rx_dma_err,
		dma->tx_dma_req,
		dma->tx_dma_err);

	part_str = "IRQ";

	if (st == st_end)
		goto fw_stats_end;

	irq = (fw_stats_irq_t *)st;
	partlen = sizeof(fw_stats_irq_t);
	st += partlen;

	if (st > st_end)
		goto fw_stats_fail;

	p += sprintf(p,
		"%s:\n"
		"  cmd_cplt %u, fiq %u\n"
		"  rx_hdrs %u, rx_cmplt %u, rx_mem_overfl %u, rx_rdys %u\n"
		"  irqs %u, tx_procs %u, decrypt_done %u\n"
		"  dma_0_done %u, dma_1_done %u, tx_exch_complet %u\n"
		"  commands %u, rx_procs %u, hw_pm_mode_changes %u\n"
		"  host_acks %u, pci_pm %u, acm_wakeups %u\n",
		part_str,
		irq->cmd_cplt,
		irq->fiq,
		irq->rx_hdrs,
		irq->rx_cmplt,
		irq->rx_mem_of,
		irq->rx_rdys,
		irq->irqs,
		irq->tx_procs,
		irq->decrypt_done,
		irq->dma_0_done,
		irq->dma_1_done,
		irq->tx_exch_complet,
		irq->commands,
		irq->rx_procs,
		irq->hw_pm_mode_changes,
		irq->host_acks,
		irq->pci_pm,
		irq->acm_wakeups);

	part_str = "WEP";

	if (st == st_end)
		goto fw_stats_end;

	wep = (fw_stats_wep_t *)st;
	partlen = sizeof(fw_stats_wep_t);
	st += partlen;

	if (
	    (IS_PCI(adev) && IS_ACX100(adev))
	||  (IS_USB(adev) && IS_ACX100(adev))
	) {
		/* at least ACX100 PCI F/W 1.9.8.b
		 * and ACX100 USB F/W 1.0.7-USB
		 * don't have those two fields... */
		st -= 2*sizeof(u32);
		if (st > st_end)
			goto fw_stats_fail;
		temp1 = temp2 = 999999999;
	} else {
		if (st > st_end)
			goto fw_stats_fail;
		temp1 = wep->wep_pkt_decrypt;
		temp2 = wep->wep_decrypt_irqs;
	}

	p += sprintf(p,
		"%s:\n"
		"  wep_key_count %u, wep_default_key_count %u, dot11_def_key_mib %u\n"
		"  wep_key_not_found %u, wep_decrypt_fail %u\n"
		"  wep_pkt_decrypt %u, wep_decrypt_irqs %u\n",
		part_str,
		wep->wep_key_count,
		wep->wep_default_key_count,
		wep->dot11_def_key_mib,
		wep->wep_key_not_found,
		wep->wep_decrypt_fail,
		temp1,
		temp2);

	part_str = "power";

	if (st == st_end)
		goto fw_stats_end;

	pwr = (fw_stats_pwr_t *)st;
	partlen = sizeof(fw_stats_pwr_t);
	st += partlen;

	if (st > st_end)
		goto fw_stats_fail;

	p += sprintf(p,
		"%s:\n"
		"  tx_start_ctr %u, no_ps_tx_too_short %u\n"
		"  rx_start_ctr %u, no_ps_rx_too_short %u\n"
		"  lppd_started %u\n"
		"  no_lppd_too_noisy %u, no_lppd_too_short %u, no_lppd_matching_frame %u\n",
		part_str,
		pwr->tx_start_ctr,
		pwr->no_ps_tx_too_short,
		pwr->rx_start_ctr,
		pwr->no_ps_rx_too_short,
		pwr->lppd_started,
		pwr->no_lppd_too_noisy,
		pwr->no_lppd_too_short,
		pwr->no_lppd_matching_frame);

	part_str = "MIC";

	if (st == st_end)
		goto fw_stats_end;

	mic = (fw_stats_mic_t *)st;
	partlen = sizeof(fw_stats_mic_t);
	st += partlen;

	if (st > st_end)
		goto fw_stats_fail;

	p += sprintf(p,
		"%s:\n"
		"  mic_rx_pkts %u, mic_calc_fail %u\n",
		part_str,
		mic->mic_rx_pkts,
		mic->mic_calc_fail);

	part_str = "AES";

	if (st == st_end)
		goto fw_stats_end;

	aes = (fw_stats_aes_t *)st;
	partlen = sizeof(fw_stats_aes_t);
	st += partlen;

	if (st > st_end)
		goto fw_stats_fail;

	p += sprintf(p,
		"%s:\n"
		"  aes_enc_fail %u, aes_dec_fail %u\n"
		"  aes_enc_pkts %u, aes_dec_pkts %u\n"
		"  aes_enc_irq %u, aes_dec_irq %u\n",
		part_str,
		aes->aes_enc_fail,
		aes->aes_dec_fail,
		aes->aes_enc_pkts,
		aes->aes_dec_pkts,
		aes->aes_enc_irq,
		aes->aes_dec_irq);

	part_str = "event";

	if (st == st_end)
		goto fw_stats_end;

	evt = (fw_stats_event_t *)st;
	partlen = sizeof(fw_stats_event_t);
	st += partlen;

	if (st > st_end)
		goto fw_stats_fail;

	p += sprintf(p,
		"%s:\n"
		"  heartbeat %u, calibration %u\n"
		"  rx_mismatch %u, rx_mem_empty %u, rx_pool %u\n"
		"  oom_late %u\n"
		"  phy_tx_err %u, tx_stuck %u\n",
		part_str,
		evt->heartbeat,
		evt->calibration,
		evt->rx_mismatch,
		evt->rx_mem_empty,
		evt->rx_pool,
		evt->oom_late,
		evt->phy_tx_err,
		evt->tx_stuck);

	if (st < st_end)
		goto fw_stats_bigger;

	goto fw_stats_end;

fw_stats_fail:
	st -= partlen;
	p += sprintf(p,
		"failed at %s part (size %u), offset %u (struct size %u), "
		"please report\n", part_str, partlen,
		(int)st - (int)fw_stats, len);

fw_stats_bigger:
	for (; st < st_end; st += 4)
		p += sprintf(p,
			"UNKN%3d: %u\n", (int)st - (int)fw_stats, *(u32 *)st);

fw_stats_end:
	kfree(fw_stats);

	FN_EXIT1(p - buf);
	return p - buf;
}


/***********************************************************************
*/
static int
acx_s_proc_phy_output(char *buf, acx_device_t *adev)
{
	char *p = buf;
	int i;

	FN_ENTER;

	/*
	if (RADIO_RFMD_11 != adev->radio_type) {
		printk("sorry, not yet adapted for radio types "
			"other than RFMD, please verify "
			"PHY size etc. first!\n");
		goto end;
	}
	*/

	/* The PHY area is only 0x80 bytes long; further pages after that
	 * only have some page number registers with altered value,
	 * all other registers remain the same. */
	for (i = 0; i < 0x80; i++) {
		acx_s_read_phy_reg(adev, i, p++);
	}

	FN_EXIT1(p - buf);
	return p - buf;
}


/***********************************************************************
** acx_e_read_proc_XXXX
** Handle our /proc entry
**
** Arguments:
**	standard kernel read_proc interface
** Returns:
**	number of bytes written to buf
** Side effects:
**	none
*/
static int
acx_e_read_proc(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data)
{
	acx_device_t *adev = (acx_device_t*)data;
	unsigned long flags;
	int length;

	FN_ENTER;

	acx_sem_lock(adev);
	acx_lock(adev, flags);
	/* fill buf */
	length = acx_l_proc_output(buf, adev);
	acx_unlock(adev, flags);
	acx_sem_unlock(adev);

	/* housekeeping */
	if (length <= offset + count)
		*eof = 1;
	*start = buf + offset;
	length -= offset;
	if (length > count)
		length = count;
	if (length < 0)
		length = 0;
	FN_EXIT1(length);
	return length;
}

static int
acx_e_read_proc_diag(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data)
{
	acx_device_t *adev = (acx_device_t*)data;
	int length;

	FN_ENTER;

	acx_sem_lock(adev);
	/* fill buf */
	length = acx_s_proc_diag_output(buf, adev);
	acx_sem_unlock(adev);

	/* housekeeping */
	if (length <= offset + count)
		*eof = 1;
	*start = buf + offset;
	length -= offset;
	if (length > count)
		length = count;
	if (length < 0)
		length = 0;
	FN_EXIT1(length);
	return length;
}

static int
acx_e_read_proc_eeprom(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data)
{
	acx_device_t *adev = (acx_device_t*)data;
	int length;

	FN_ENTER;

	/* fill buf */
	length = 0;
	if (IS_PCI(adev)) {
		acx_sem_lock(adev);
		length = acxpci_proc_eeprom_output(buf, adev);
		acx_sem_unlock(adev);
	}

	/* housekeeping */
	if (length <= offset + count)
		*eof = 1;
	*start = buf + offset;
	length -= offset;
	if (length > count)
		length = count;
	if (length < 0)
		length = 0;
	FN_EXIT1(length);
	return length;
}

static int
acx_e_read_proc_phy(char *buf, char **start, off_t offset, int count,
		     int *eof, void *data)
{
	acx_device_t *adev = (acx_device_t*)data;
	int length;

	FN_ENTER;

	acx_sem_lock(adev);
	/* fill buf */
	length = acx_s_proc_phy_output(buf, adev);
	acx_sem_unlock(adev);

	/* housekeeping */
	if (length <= offset + count)
		*eof = 1;
	*start = buf + offset;
	length -= offset;
	if (length > count)
		length = count;
	if (length < 0)
		length = 0;
	FN_EXIT1(length);
	return length;
}


/***********************************************************************
** /proc files registration
*/
static const char * const
proc_files[] = { "", "_diag", "_eeprom", "_phy" };

static read_proc_t * const
proc_funcs[] = {
	acx_e_read_proc,
	acx_e_read_proc_diag,
	acx_e_read_proc_eeprom,
	acx_e_read_proc_phy
};

static int
manage_proc_entries(const struct net_device *ndev, int remove)
{
	acx_device_t *adev = ndev2adev((struct net_device *)ndev);
	char procbuf[80];
	int i;

	for (i = 0; i < VEC_SIZE(proc_files); i++)	{
		snprintf(procbuf, sizeof(procbuf),
			"driver/acx_%s%s", ndev->name, proc_files[i]);
		log(L_INIT, "%sing /proc entry %s\n",
			remove ? "remov" : "creat", procbuf);
		if (!remove) {
			if (!create_proc_read_entry(procbuf, 0, 0, proc_funcs[i], adev)) {
				printk("acx: cannot register /proc entry %s\n", procbuf);
				return NOT_OK;
			}
		} else {
			remove_proc_entry(procbuf, NULL);
		}
	}
	return OK;
}

int
acx_proc_register_entries(const struct net_device *ndev)
{
	return manage_proc_entries(ndev, 0);
}

int
acx_proc_unregister_entries(const struct net_device *ndev)
{
	return manage_proc_entries(ndev, 1);
}
#endif /* CONFIG_PROC_FS */


/***********************************************************************
** acx_cmd_join_bssid
**
** Common code for both acx100 and acx111.
*/
/* NB: does NOT match RATE100_nn but matches ACX[111]_SCAN_RATE_n */
static const u8
bitpos2genframe_txrate[] = {
	10,	/*  0.  1 Mbit/s */
	20,	/*  1.  2 Mbit/s */
	55,	/*  2.  5.5 Mbit/s */
	0x0B,	/*  3.  6 Mbit/s */
	0x0F,	/*  4.  9 Mbit/s */
	110,	/*  5. 11 Mbit/s */
	0x0A,	/*  6. 12 Mbit/s */
	0x0E,	/*  7. 18 Mbit/s */
	220,	/*  8. 22 Mbit/s */
	0x09,	/*  9. 24 Mbit/s */
	0x0D,	/* 10. 36 Mbit/s */
	0x08,	/* 11. 48 Mbit/s */
	0x0C,	/* 12. 54 Mbit/s */
	10,	/* 13.  1 Mbit/s, should never happen */
	10,	/* 14.  1 Mbit/s, should never happen */
	10,	/* 15.  1 Mbit/s, should never happen */
};

/* Looks scary, eh?
** Actually, each one compiled into one AND and one SHIFT,
** 31 bytes in x86 asm (more if uints are replaced by u16/u8) */
static inline unsigned int
rate111to5bits(unsigned int rate)
{
	return (rate & 0x7)
	| ( (rate & RATE111_11) / (RATE111_11/JOINBSS_RATES_11) )
	| ( (rate & RATE111_22) / (RATE111_22/JOINBSS_RATES_22) )
	;
}

static void
acx_s_cmd_join_bssid(acx_device_t *adev, const u8 *bssid)
{
	acx_joinbss_t tmp;
	int dtim_interval;
	int i;

	if (mac_is_zero(bssid))
		return;

	FN_ENTER;

	dtim_interval =	(ACX_MODE_0_ADHOC == adev->mode) ?
			1 : adev->dtim_interval;

	memset(&tmp, 0, sizeof(tmp));

	for (i = 0; i < ETH_ALEN; i++) {
		tmp.bssid[i] = bssid[ETH_ALEN-1 - i];
	}

	tmp.beacon_interval = cpu_to_le16(adev->beacon_interval);

	/* Basic rate set. Control frame responses (such as ACK or CTS frames)
	** are sent with one of these rates */
	if (IS_ACX111(adev)) {
		/* It was experimentally determined that rates_basic
		** can take 11g rates as well, not only rates
		** defined with JOINBSS_RATES_BASIC111_nnn.
		** Just use RATE111_nnn constants... */
		tmp.u.acx111.dtim_interval = dtim_interval;
		tmp.u.acx111.rates_basic = cpu_to_le16(adev->rate_basic);
		log(L_ASSOC, "rates_basic:%04X, rates_supported:%04X\n",
			adev->rate_basic, adev->rate_oper);
	} else {
		tmp.u.acx100.dtim_interval = dtim_interval;
		tmp.u.acx100.rates_basic = rate111to5bits(adev->rate_basic);
		tmp.u.acx100.rates_supported = rate111to5bits(adev->rate_oper);
		log(L_ASSOC, "rates_basic:%04X->%02X, "
			"rates_supported:%04X->%02X\n",
			adev->rate_basic, tmp.u.acx100.rates_basic,
			adev->rate_oper, tmp.u.acx100.rates_supported);
	}

	/* Setting up how Beacon, Probe Response, RTS, and PS-Poll frames
	** will be sent (rate/modulation/preamble) */
	tmp.genfrm_txrate = bitpos2genframe_txrate[lowest_bit(adev->rate_basic)];
	tmp.genfrm_mod_pre = 0; /* FIXME: was = adev->capab_short (which was always 0); */
	/* we can use short pre *if* all peers can understand it */
	/* FIXME #2: we need to correctly set PBCC/OFDM bits here too */

	/* we switch fw to STA mode in MONITOR mode, it seems to be
	** the only mode where fw does not emit beacons by itself
	** but allows us to send anything (we really want to retain
	** ability to tx arbitrary frames in MONITOR mode)
	*/
	tmp.macmode = (adev->mode != ACX_MODE_MONITOR ? adev->mode : ACX_MODE_2_STA);
	tmp.channel = adev->channel;
	tmp.essid_len = adev->essid_len;
	/* NOTE: the code memcpy'd essid_len + 1 before, which is WRONG! */
	memcpy(tmp.essid, adev->essid, tmp.essid_len);
	acx_s_issue_cmd(adev, ACX1xx_CMD_JOIN, &tmp, tmp.essid_len + 0x11);

	log(L_ASSOC|L_DEBUG, "BSS_Type = %u\n", tmp.macmode);
	acxlog_mac(L_ASSOC|L_DEBUG, "JoinBSSID MAC:", adev->bssid, "\n");

	acx_update_capabilities(adev);
	FN_EXIT0;
}


/***********************************************************************
** acx_s_cmd_start_scan
**
** Issue scan command to the hardware
**
** unified function for both ACX111 and ACX100
*/
static void
acx_s_scan_chan(acx_device_t *adev)
{
	union {
		acx111_scan_t acx111;
		acx100_scan_t acx100;
	} s;

	FN_ENTER;

	memset(&s, 0, sizeof(s));

	/* first common positions... */

	s.acx111.count = cpu_to_le16(adev->scan_count);
	s.acx111.rate = adev->scan_rate;
	s.acx111.options = adev->scan_mode;
	s.acx111.chan_duration = cpu_to_le16(adev->scan_duration);
	s.acx111.max_probe_delay = cpu_to_le16(adev->scan_probe_delay);

	/* ...then differences */

	if (IS_ACX111(adev)) {
		s.acx111.channel_list_select = 0; /* scan every allowed channel */
		/*s.acx111.channel_list_select = 1;*/ /* scan given channels */
		/*s.acx111.modulation = 0x40;*/ /* long preamble? OFDM? -> only for active scan */
		s.acx111.modulation = 0;
		/*s.acx111.channel_list[0] = 6;
		s.acx111.channel_list[1] = 4;*/
	} else {
		s.acx100.start_chan = cpu_to_le16(1);
		s.acx100.flags = cpu_to_le16(0x8000);
	}

	acx_s_issue_cmd(adev, ACX1xx_CMD_SCAN, &s, sizeof(s));
	FN_EXIT0;
}


void
acx_s_cmd_start_scan(acx_device_t *adev)
{
	/* time_before check is 'just in case' thing */
	if (!(adev->irq_status & HOST_INT_SCAN_COMPLETE)
	 && time_before(jiffies, adev->scan_start + 10*HZ)
	) {
		log(L_INIT, "start_scan: seems like previous scan "
		"is still running. Not starting anew. Please report\n");
		return;
	}

	log(L_INIT, "starting radio scan\n");
	/* remember that fw is commanded to do scan */
	adev->scan_start = jiffies;
	CLEAR_BIT(adev->irq_status, HOST_INT_SCAN_COMPLETE);
	/* issue it */
	acx_s_scan_chan(adev);
}


/***********************************************************************
** acx111 feature config
*/
static int
acx111_s_get_feature_config(acx_device_t *adev,
		u32 *feature_options, u32 *data_flow_options)
{
	struct acx111_ie_feature_config feat;

	if (!IS_ACX111(adev)) {
		return NOT_OK;
	}

	memset(&feat, 0, sizeof(feat));

	if (OK != acx_s_interrogate(adev, &feat, ACX1xx_IE_FEATURE_CONFIG)) {
		return NOT_OK;
	}
	log(L_DEBUG,
		"got Feature option:0x%X, DataFlow option: 0x%X\n",
		feat.feature_options,
		feat.data_flow_options);

	if (feature_options)
		*feature_options = le32_to_cpu(feat.feature_options);
	if (data_flow_options)
		*data_flow_options = le32_to_cpu(feat.data_flow_options);

	return OK;
}

static int
acx111_s_set_feature_config(acx_device_t *adev,
	u32 feature_options, u32 data_flow_options,
	unsigned int mode /* 0 == remove, 1 == add, 2 == set */)
{
	struct acx111_ie_feature_config feat;

	if (!IS_ACX111(adev)) {
		return NOT_OK;
	}

	if ((mode < 0) || (mode > 2))
		return NOT_OK;

	if (mode != 2)
		/* need to modify old data */
		acx111_s_get_feature_config(adev, &feat.feature_options, &feat.data_flow_options);
	else {
		/* need to set a completely new value */
		feat.feature_options = 0;
		feat.data_flow_options = 0;
	}

	if (mode == 0) { /* remove */
		CLEAR_BIT(feat.feature_options, cpu_to_le32(feature_options));
		CLEAR_BIT(feat.data_flow_options, cpu_to_le32(data_flow_options));
	} else { /* add or set */
		SET_BIT(feat.feature_options, cpu_to_le32(feature_options));
		SET_BIT(feat.data_flow_options, cpu_to_le32(data_flow_options));
	}

	log(L_DEBUG,
		"old: feature 0x%08X dataflow 0x%08X. mode: %u\n"
		"new: feature 0x%08X dataflow 0x%08X\n",
		feature_options, data_flow_options, mode,
		le32_to_cpu(feat.feature_options),
		le32_to_cpu(feat.data_flow_options));

	if (OK != acx_s_configure(adev, &feat, ACX1xx_IE_FEATURE_CONFIG)) {
		return NOT_OK;
	}

	return OK;
}

static inline int
acx111_s_feature_off(acx_device_t *adev, u32 f, u32 d)
{
	return acx111_s_set_feature_config(adev, f, d, 0);
}
static inline int
acx111_s_feature_on(acx_device_t *adev, u32 f, u32 d)
{
	return acx111_s_set_feature_config(adev, f, d, 1);
}
static inline int
acx111_s_feature_set(acx_device_t *adev, u32 f, u32 d)
{
	return acx111_s_set_feature_config(adev, f, d, 2);
}


/***********************************************************************
** acx100_s_init_memory_pools
*/
static int
acx100_s_init_memory_pools(acx_device_t *adev, const acx_ie_memmap_t *mmt)
{
	acx100_ie_memblocksize_t MemoryBlockSize;
	acx100_ie_memconfigoption_t MemoryConfigOption;
	int TotalMemoryBlocks;
	int RxBlockNum;
	int TotalRxBlockSize;
	int TxBlockNum;
	int TotalTxBlockSize;

	FN_ENTER;

	/* Let's see if we can follow this:
	   first we select our memory block size (which I think is
	   completely arbitrary) */
	MemoryBlockSize.size = cpu_to_le16(adev->memblocksize);

	/* Then we alert the card to our decision of block size */
	if (OK != acx_s_configure(adev, &MemoryBlockSize, ACX100_IE_BLOCK_SIZE)) {
		goto bad;
	}

	/* We figure out how many total blocks we can create, using
	   the block size we chose, and the beginning and ending
	   memory pointers, i.e.: end-start/size */
	TotalMemoryBlocks = (le32_to_cpu(mmt->PoolEnd) - le32_to_cpu(mmt->PoolStart)) / adev->memblocksize;

	log(L_DEBUG, "TotalMemoryBlocks=%u (%u bytes)\n",
		TotalMemoryBlocks, TotalMemoryBlocks*adev->memblocksize);

	/* MemoryConfigOption.DMA_config bitmask:
			access to ACX memory is to be done:
	0x00080000	using PCI conf space?!
	0x00040000	using IO instructions?
	0x00000000	using memory access instructions
	0x00020000	using local memory block linked list (else what?)
	0x00010000	using host indirect descriptors (else host must access ACX memory?)
	*/
	if (IS_PCI(adev)) {
		MemoryConfigOption.DMA_config = cpu_to_le32(0x30000);
		/* Declare start of the Rx host pool */
		MemoryConfigOption.pRxHostDesc = cpu2acx(adev->rxhostdesc_startphy);
		log(L_DEBUG, "pRxHostDesc 0x%08X, rxhostdesc_startphy 0x%lX\n",
				acx2cpu(MemoryConfigOption.pRxHostDesc),
				(long)adev->rxhostdesc_startphy);
	} else {
		MemoryConfigOption.DMA_config = cpu_to_le32(0x20000);
	}

	/* 50% of the allotment of memory blocks go to tx descriptors */
	TxBlockNum = TotalMemoryBlocks / 2;
	MemoryConfigOption.TxBlockNum = cpu_to_le16(TxBlockNum);

	/* and 50% go to the rx descriptors */
	RxBlockNum = TotalMemoryBlocks - TxBlockNum;
	MemoryConfigOption.RxBlockNum = cpu_to_le16(RxBlockNum);

	/* size of the tx and rx descriptor queues */
	TotalTxBlockSize = TxBlockNum * adev->memblocksize;
	TotalRxBlockSize = RxBlockNum * adev->memblocksize;
	log(L_DEBUG, "TxBlockNum %u RxBlockNum %u TotalTxBlockSize %u "
		"TotalTxBlockSize %u\n", TxBlockNum, RxBlockNum,
		TotalTxBlockSize, TotalRxBlockSize);


	/* align the tx descriptor queue to an alignment of 0x20 (32 bytes) */
	MemoryConfigOption.rx_mem =
		cpu_to_le32((le32_to_cpu(mmt->PoolStart) + 0x1f) & ~0x1f);

	/* align the rx descriptor queue to units of 0x20
	 * and offset it by the tx descriptor queue */
	MemoryConfigOption.tx_mem =
		cpu_to_le32((le32_to_cpu(mmt->PoolStart) + TotalRxBlockSize + 0x1f) & ~0x1f);
	log(L_DEBUG, "rx_mem %08X rx_mem %08X\n",
		MemoryConfigOption.tx_mem, MemoryConfigOption.rx_mem);

	/* alert the device to our decision */
	if (OK != acx_s_configure(adev, &MemoryConfigOption, ACX1xx_IE_MEMORY_CONFIG_OPTIONS)) {
		goto bad;
	}

	/* and tell the device to kick it into gear */
	if (OK != acx_s_issue_cmd(adev, ACX100_CMD_INIT_MEMORY, NULL, 0)) {
		goto bad;
	}
	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/***********************************************************************
** acx100_s_create_dma_regions
**
** Note that this fn messes up heavily with hardware, but we cannot
** lock it (we need to sleep). Not a problem since IRQs can't happen
*/
static int
acx100_s_create_dma_regions(acx_device_t *adev)
{
	acx100_ie_queueconfig_t queueconf;
	acx_ie_memmap_t memmap;
	int res = NOT_OK;
	u32 tx_queue_start, rx_queue_start;

	FN_ENTER;

	/* read out the acx100 physical start address for the queues */
	if (OK != acx_s_interrogate(adev, &memmap, ACX1xx_IE_MEMORY_MAP)) {
		goto fail;
	}

	tx_queue_start = le32_to_cpu(memmap.QueueStart);
	rx_queue_start = tx_queue_start + TX_CNT * sizeof(txdesc_t);

	log(L_DEBUG, "initializing Queue Indicator\n");

	memset(&queueconf, 0, sizeof(queueconf));

	/* Not needed for PCI, so we can avoid setting them altogether */
	if (IS_USB(adev)) {
		queueconf.NumTxDesc = USB_TX_CNT;
		queueconf.NumRxDesc = USB_RX_CNT;
	}

	/* calculate size of queues */
	queueconf.AreaSize = cpu_to_le32(
			TX_CNT * sizeof(txdesc_t) +
			RX_CNT * sizeof(rxdesc_t) + 8
			);
	queueconf.NumTxQueues = 1;  /* number of tx queues */
	/* sets the beginning of the tx descriptor queue */
	queueconf.TxQueueStart = memmap.QueueStart;
	/* done by memset: queueconf.TxQueuePri = 0; */
	queueconf.RxQueueStart = cpu_to_le32(rx_queue_start);
	queueconf.QueueOptions = 1;		/* auto reset descriptor */
	/* sets the end of the rx descriptor queue */
	queueconf.QueueEnd = cpu_to_le32(
			rx_queue_start + RX_CNT * sizeof(rxdesc_t)
			);
	/* sets the beginning of the next queue */
	queueconf.HostQueueEnd = cpu_to_le32(le32_to_cpu(queueconf.QueueEnd) + 8);
	if (OK != acx_s_configure(adev, &queueconf, ACX1xx_IE_QUEUE_CONFIG)) {
		goto fail;
	}

	if (IS_PCI(adev)) {
	/* sets the beginning of the rx descriptor queue, after the tx descrs */
		if (OK != acxpci_s_create_hostdesc_queues(adev))
			goto fail;
		acxpci_create_desc_queues(adev, tx_queue_start, rx_queue_start);
	}

	if (OK != acx_s_interrogate(adev, &memmap, ACX1xx_IE_MEMORY_MAP)) {
		goto fail;
	}

	memmap.PoolStart = cpu_to_le32(
			(le32_to_cpu(memmap.QueueEnd) + 4 + 0x1f) & ~0x1f
			);

	if (OK != acx_s_configure(adev, &memmap, ACX1xx_IE_MEMORY_MAP)) {
		goto fail;
	}

	if (OK != acx100_s_init_memory_pools(adev, &memmap)) {
		goto fail;
	}

	res = OK;
	goto end;

fail:
	acx_s_msleep(1000); /* ? */
	if (IS_PCI(adev))
		acxpci_free_desc_queues(adev);
end:
	FN_EXIT1(res);
	return res;
}


/***********************************************************************
** acx111_s_create_dma_regions
**
** Note that this fn messes heavily with hardware, but we cannot
** lock it (we need to sleep). Not a problem since IRQs can't happen
*/
#define ACX111_PERCENT(percent) ((percent)/5)

static int
acx111_s_create_dma_regions(acx_device_t *adev)
{
	struct acx111_ie_memoryconfig memconf;
	struct acx111_ie_queueconfig queueconf;
	u32 tx_queue_start, rx_queue_start;

	FN_ENTER;

	/* Calculate memory positions and queue sizes */

	/* Set up our host descriptor pool + data pool */
	if (IS_PCI(adev)) {
		if (OK != acxpci_s_create_hostdesc_queues(adev))
			goto fail;
	}

	memset(&memconf, 0, sizeof(memconf));
	/* the number of STAs (STA contexts) to support
	** NB: was set to 1 and everything seemed to work nevertheless... */
	memconf.no_of_stations = cpu_to_le16(VEC_SIZE(adev->sta_list));
	/* specify the memory block size. Default is 256 */
	memconf.memory_block_size = cpu_to_le16(adev->memblocksize);
	/* let's use 50%/50% for tx/rx (specify percentage, units of 5%) */
	memconf.tx_rx_memory_block_allocation = ACX111_PERCENT(50);
	/* set the count of our queues
	** NB: struct acx111_ie_memoryconfig shall be modified
	** if we ever will switch to more than one rx and/or tx queue */
	memconf.count_rx_queues = 1;
	memconf.count_tx_queues = 1;
	/* 0 == Busmaster Indirect Memory Organization, which is what we want
	 * (using linked host descs with their allocated mem).
	 * 2 == Generic Bus Slave */
	/* done by memset: memconf.options = 0; */
	/* let's use 25% for fragmentations and 75% for frame transfers
	 * (specified in units of 5%) */
	memconf.fragmentation = ACX111_PERCENT(75);
	/* Rx descriptor queue config */
	memconf.rx_queue1_count_descs = RX_CNT;
	memconf.rx_queue1_type = 7; /* must be set to 7 */
	/* done by memset: memconf.rx_queue1_prio = 0; low prio */
	if (IS_PCI(adev)) {
		memconf.rx_queue1_host_rx_start = cpu2acx(adev->rxhostdesc_startphy);
	}
	/* Tx descriptor queue config */
	memconf.tx_queue1_count_descs = TX_CNT;
	/* done by memset: memconf.tx_queue1_attributes = 0; lowest priority */

	/* NB1: this looks wrong: (memconf,ACX1xx_IE_QUEUE_CONFIG),
	** (queueconf,ACX1xx_IE_MEMORY_CONFIG_OPTIONS) look swapped, eh?
	** But it is actually correct wrt IE numbers.
	** NB2: sizeof(memconf) == 28 == 0x1c but configure(ACX1xx_IE_QUEUE_CONFIG)
	** writes 0x20 bytes (because same IE for acx100 uses struct acx100_ie_queueconfig
	** which is 4 bytes larger. what a mess. TODO: clean it up) */
	if (OK != acx_s_configure(adev, &memconf, ACX1xx_IE_QUEUE_CONFIG)) {
		goto fail;
	}

	acx_s_interrogate(adev, &queueconf, ACX1xx_IE_MEMORY_CONFIG_OPTIONS);

	tx_queue_start = le32_to_cpu(queueconf.tx1_queue_address);
	rx_queue_start = le32_to_cpu(queueconf.rx1_queue_address);

	log(L_INIT, "dump queue head (from card):\n"
		       "len: %u\n"
		       "tx_memory_block_address: %X\n"
		       "rx_memory_block_address: %X\n"
		       "tx1_queue address: %X\n"
		       "rx1_queue address: %X\n",
				le16_to_cpu(queueconf.len),
				le32_to_cpu(queueconf.tx_memory_block_address),
				le32_to_cpu(queueconf.rx_memory_block_address),
				tx_queue_start,
				rx_queue_start);

	if (IS_PCI(adev))
		acxpci_create_desc_queues(adev, tx_queue_start, rx_queue_start);

	FN_EXIT1(OK);
	return OK;
fail:
	if (IS_PCI(adev))
		acxpci_free_desc_queues(adev);

	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/***********************************************************************
*/
static void
acx_s_initialize_rx_config(acx_device_t *adev)
{
	struct {
		u16	id;
		u16	len;
		u16	rx_cfg1;
		u16	rx_cfg2;
	} ACX_PACKED cfg;

	switch (adev->mode) {
	case ACX_MODE_OFF:
		adev->rx_config_1 = (u16) (0
			/* | RX_CFG1_INCLUDE_RXBUF_HDR	*/
			/* | RX_CFG1_FILTER_SSID	*/
			/* | RX_CFG1_FILTER_BCAST	*/
			/* | RX_CFG1_RCV_MC_ADDR1	*/
			/* | RX_CFG1_RCV_MC_ADDR0	*/
			/* | RX_CFG1_FILTER_ALL_MULTI	*/
			/* | RX_CFG1_FILTER_BSSID	*/
			/* | RX_CFG1_FILTER_MAC		*/
			/* | RX_CFG1_RCV_PROMISCUOUS	*/
			/* | RX_CFG1_INCLUDE_FCS	*/
			/* | RX_CFG1_INCLUDE_PHY_HDR	*/
			);
		adev->rx_config_2 = (u16) (0
			/*| RX_CFG2_RCV_ASSOC_REQ	*/
			/*| RX_CFG2_RCV_AUTH_FRAMES	*/
			/*| RX_CFG2_RCV_BEACON_FRAMES	*/
			/*| RX_CFG2_RCV_CONTENTION_FREE	*/
			/*| RX_CFG2_RCV_CTRL_FRAMES	*/
			/*| RX_CFG2_RCV_DATA_FRAMES	*/
			/*| RX_CFG2_RCV_BROKEN_FRAMES	*/
			/*| RX_CFG2_RCV_MGMT_FRAMES	*/
			/*| RX_CFG2_RCV_PROBE_REQ	*/
			/*| RX_CFG2_RCV_PROBE_RESP	*/
			/*| RX_CFG2_RCV_ACK_FRAMES	*/
			/*| RX_CFG2_RCV_OTHER		*/
			);
		break;
	case ACX_MODE_MONITOR:
		adev->rx_config_1 = (u16) (0
			/* | RX_CFG1_INCLUDE_RXBUF_HDR	*/
			/* | RX_CFG1_FILTER_SSID	*/
			/* | RX_CFG1_FILTER_BCAST	*/
			/* | RX_CFG1_RCV_MC_ADDR1	*/
			/* | RX_CFG1_RCV_MC_ADDR0	*/
			/* | RX_CFG1_FILTER_ALL_MULTI	*/
			/* | RX_CFG1_FILTER_BSSID	*/
			/* | RX_CFG1_FILTER_MAC		*/
			| RX_CFG1_RCV_PROMISCUOUS
			/* | RX_CFG1_INCLUDE_FCS	*/
			/* | RX_CFG1_INCLUDE_PHY_HDR	*/
			);
		adev->rx_config_2 = (u16) (0
			| RX_CFG2_RCV_ASSOC_REQ
			| RX_CFG2_RCV_AUTH_FRAMES
			| RX_CFG2_RCV_BEACON_FRAMES
			| RX_CFG2_RCV_CONTENTION_FREE
			| RX_CFG2_RCV_CTRL_FRAMES
			| RX_CFG2_RCV_DATA_FRAMES
			| RX_CFG2_RCV_BROKEN_FRAMES
			| RX_CFG2_RCV_MGMT_FRAMES
			| RX_CFG2_RCV_PROBE_REQ
			| RX_CFG2_RCV_PROBE_RESP
			| RX_CFG2_RCV_ACK_FRAMES
			| RX_CFG2_RCV_OTHER
			);
		break;
	default:
		adev->rx_config_1 = (u16) (0
			/* | RX_CFG1_INCLUDE_RXBUF_HDR	*/
			/* | RX_CFG1_FILTER_SSID	*/
			/* | RX_CFG1_FILTER_BCAST	*/
			/* | RX_CFG1_RCV_MC_ADDR1	*/
			/* | RX_CFG1_RCV_MC_ADDR0	*/
			/* | RX_CFG1_FILTER_ALL_MULTI	*/
			/* | RX_CFG1_FILTER_BSSID	*/
			| RX_CFG1_FILTER_MAC
			/* | RX_CFG1_RCV_PROMISCUOUS	*/
			/* | RX_CFG1_INCLUDE_FCS	*/
			/* | RX_CFG1_INCLUDE_PHY_HDR	*/
			);
		adev->rx_config_2 = (u16) (0
			| RX_CFG2_RCV_ASSOC_REQ
			| RX_CFG2_RCV_AUTH_FRAMES
			| RX_CFG2_RCV_BEACON_FRAMES
			| RX_CFG2_RCV_CONTENTION_FREE
			| RX_CFG2_RCV_CTRL_FRAMES
			| RX_CFG2_RCV_DATA_FRAMES
			/*| RX_CFG2_RCV_BROKEN_FRAMES	*/
			| RX_CFG2_RCV_MGMT_FRAMES
			| RX_CFG2_RCV_PROBE_REQ
			| RX_CFG2_RCV_PROBE_RESP
			/*| RX_CFG2_RCV_ACK_FRAMES	*/
			| RX_CFG2_RCV_OTHER
			);
		break;
	}
	adev->rx_config_1 |= RX_CFG1_INCLUDE_RXBUF_HDR;

	if ((adev->rx_config_1 & RX_CFG1_INCLUDE_PHY_HDR)
	 || (adev->firmware_numver >= 0x02000000))
		adev->phy_header_len = IS_ACX111(adev) ? 8 : 4;
	else
		adev->phy_header_len = 0;

	log(L_INIT, "setting RXconfig to %04X:%04X\n",
			adev->rx_config_1, adev->rx_config_2);
	cfg.rx_cfg1 = cpu_to_le16(adev->rx_config_1);
	cfg.rx_cfg2 = cpu_to_le16(adev->rx_config_2);
	acx_s_configure(adev, &cfg, ACX1xx_IE_RXCONFIG);
}


/***********************************************************************
** acx_s_set_defaults
*/
void
acx_s_set_defaults(acx_device_t *adev)
{
	unsigned long flags;

	FN_ENTER;

	/* do it before getting settings, prevent bogus channel 0 warning */
	adev->channel = 1;

	/* query some settings from the card.
	 * NOTE: for some settings, e.g. CCA and ED (ACX100!), an initial
	 * query is REQUIRED, otherwise the card won't work correctly! */
	adev->get_mask = GETSET_ANTENNA|GETSET_SENSITIVITY|GETSET_STATION_ID|GETSET_REG_DOMAIN;
	/* Only ACX100 supports ED and CCA */
	if (IS_ACX100(adev))
		adev->get_mask |= GETSET_CCA|GETSET_ED_THRESH;

	acx_s_update_card_settings(adev);

	acx_lock(adev, flags);

	/* set our global interrupt mask */
	if (IS_PCI(adev))
		acxpci_set_interrupt_mask(adev);

	adev->led_power = 1; /* LED is active on startup */
	adev->brange_max_quality = 60; /* LED blink max quality is 60 */
	adev->brange_time_last_state_change = jiffies;

	/* copy the MAC address we just got from the card
	 * into our MAC address used during current 802.11 session */
	MAC_COPY(adev->dev_addr, adev->ndev->dev_addr);
	MAC_BCAST(adev->ap);

	adev->essid_len =
		snprintf(adev->essid, sizeof(adev->essid), "STA%02X%02X%02X",
			adev->dev_addr[3], adev->dev_addr[4], adev->dev_addr[5]);
	adev->essid_active = 1;

	/* we have a nick field to waste, so why not abuse it
	 * to announce the driver version? ;-) */
	strncpy(adev->nick, "acx " ACX_RELEASE, IW_ESSID_MAX_SIZE);

	if (IS_PCI(adev)) { /* FIXME: this should be made to apply to USB, too! */
		/* first regulatory domain entry in EEPROM == default reg. domain */
		adev->reg_dom_id = adev->cfgopt_domains.list[0];
	}

	/* 0xffff would be better, but then we won't get a "scan complete"
	 * interrupt, so our current infrastructure will fail: */
	adev->scan_count = 1;
	adev->scan_mode = ACX_SCAN_OPT_ACTIVE;
	adev->scan_duration = 100;
	adev->scan_probe_delay = 200;
	/* reported to break scanning: adev->scan_probe_delay = adev->cfgopt_probe_delay; */
	adev->scan_rate = ACX_SCAN_RATE_1;

	adev->mode = ACX_MODE_2_STA;
	adev->auth_alg = WLAN_AUTH_ALG_OPENSYSTEM;
	adev->listen_interval = 100;
	adev->beacon_interval = DEFAULT_BEACON_INTERVAL;
	adev->dtim_interval = DEFAULT_DTIM_INTERVAL;

	adev->msdu_lifetime = DEFAULT_MSDU_LIFETIME;

	adev->rts_threshold = DEFAULT_RTS_THRESHOLD;
	adev->frag_threshold = 2346;

	/* use standard default values for retry limits */
	adev->short_retry = 7; /* max. retries for (short) non-RTS packets */
	adev->long_retry = 4; /* max. retries for long (RTS) packets */

	adev->preamble_mode = 2; /* auto */
	adev->fallback_threshold = 3;
	adev->stepup_threshold = 10;
	adev->rate_bcast = RATE111_1;
	adev->rate_bcast100 = RATE100_1;
	adev->rate_basic = RATE111_1 | RATE111_2;
	adev->rate_auto = 1;
	if (IS_ACX111(adev)) {
		adev->rate_oper = RATE111_ALL;
	} else {
		adev->rate_oper = RATE111_ACX100_COMPAT;
	}

	/* Supported Rates element - the rates here are given in units of
	 * 500 kbit/s, plus 0x80 added. See 802.11-1999.pdf item 7.3.2.2 */
	acx_l_update_ratevector(adev);

	/* set some more defaults */
	if (IS_ACX111(adev)) {
		/* 30mW (15dBm) is default, at least in my acx111 card: */
		adev->tx_level_dbm = 15;
	} else {
		/* don't use max. level, since it might be dangerous
		 * (e.g. WRT54G people experience
		 * excessive Tx power damage!) */
		adev->tx_level_dbm = 18;
	}
	/* adev->tx_level_auto = 1; */
	if (IS_ACX111(adev)) {
		/* start with sensitivity level 1 out of 3: */
		adev->sensitivity = 1;
	}

/* #define ENABLE_POWER_SAVE */
#ifdef ENABLE_POWER_SAVE
	adev->ps_wakeup_cfg = PS_CFG_ENABLE | PS_CFG_WAKEUP_ALL_BEAC;
	adev->ps_listen_interval = 1;
	adev->ps_options = PS_OPT_ENA_ENHANCED_PS | PS_OPT_TX_PSPOLL | PS_OPT_STILL_RCV_BCASTS;
	adev->ps_hangover_period = 30;
	adev->ps_enhanced_transition_time = 0;
#else
	adev->ps_wakeup_cfg = 0;
	adev->ps_listen_interval = 0;
	adev->ps_options = 0;
	adev->ps_hangover_period = 0;
	adev->ps_enhanced_transition_time = 0;
#endif

	/* These settings will be set in fw on ifup */
	adev->set_mask = 0
		| GETSET_RETRY
		| SET_MSDU_LIFETIME
	/* configure card to do rate fallback when in auto rate mode */
		| SET_RATE_FALLBACK
		| SET_RXCONFIG
		| GETSET_TXPOWER
	/* better re-init the antenna value we got above */
		| GETSET_ANTENNA
#if POWER_SAVE_80211
		| GETSET_POWER_80211
#endif
		;

	acx_unlock(adev, flags);
	acx_lock_unhold(); /* hold time 844814 CPU ticks @2GHz */

	acx_s_initialize_rx_config(adev);

	FN_EXIT0;
}


/***********************************************************************
** FIXME: this should be solved in a general way for all radio types
** by decoding the radio firmware module,
** since it probably has some standard structure describing how to
** set the power level of the radio module which it controls.
** Or maybe not, since the radio module probably has a function interface
** instead which then manages Tx level programming :-\
*/
static int
acx111_s_set_tx_level(acx_device_t *adev, u8 level_dbm)
{
	struct acx111_ie_tx_level tx_level;

	/* my acx111 card has two power levels in its configoptions (== EEPROM):
	 * 1 (30mW) [15dBm]
	 * 2 (10mW) [10dBm]
	 * For now, just assume all other acx111 cards have the same.
	 * FIXME: Ideally we would query it here, but we first need a
	 * standard way to query individual configoptions easily.
	 * Well, now we have proper cfgopt txpower variables, but this still
	 * hasn't been done yet, since it also requires dBm <-> mW conversion here... */
	if (level_dbm <= 12) {
		tx_level.level = 2; /* 10 dBm */
		adev->tx_level_dbm = 10;
	} else {
		tx_level.level = 1; /* 15 dBm */
		adev->tx_level_dbm = 15;
	}
	if (level_dbm != adev->tx_level_dbm)
		log(L_INIT, "acx111 firmware has specific "
			"power levels only: adjusted %d dBm to %d dBm!\n",
			level_dbm, adev->tx_level_dbm);

	return acx_s_configure(adev, &tx_level, ACX1xx_IE_DOT11_TX_POWER_LEVEL);
}

static int
acx_s_set_tx_level(acx_device_t *adev, u8 level_dbm)
{
	if (IS_ACX111(adev)) {
		return acx111_s_set_tx_level(adev, level_dbm);
	}
	if (IS_PCI(adev)) {
		return acx100pci_s_set_tx_level(adev, level_dbm);
	}
	return OK;
}


/***********************************************************************
*/
#ifdef UNUSED
/* Returns the current tx level (ACX111) */
static u8
acx111_s_get_tx_level(acx_device_t *adev)
{
	struct acx111_ie_tx_level tx_level;

	tx_level.level = 0;
	acx_s_interrogate(adev, &tx_level, ACX1xx_IE_DOT11_TX_POWER_LEVEL);
	return tx_level.level;
}
#endif


/***********************************************************************
** acx_l_rxmonitor
** Called from IRQ context only
*/
static void
acx_l_rxmonitor(acx_device_t *adev, const rxbuffer_t *rxbuf)
{
	wlansniffrm_t *msg;
	struct sk_buff *skb;
	void *datap;
	unsigned int skb_len;
	int payload_offset;

	FN_ENTER;

	/* we are in big luck: the acx100 doesn't modify any of the fields */
	/* in the 802.11 frame. just pass this packet into the PF_PACKET */
	/* subsystem. yeah. */
	payload_offset = ((u8*)acx_get_wlan_hdr(adev, rxbuf) - (u8*)rxbuf);
	skb_len = RXBUF_BYTES_USED(rxbuf) - payload_offset;

	/* sanity check */
	if (unlikely(skb_len > WLAN_A4FR_MAXLEN_WEP)) {
		printk("%s: monitor mode panic: oversized frame!\n",
				adev->ndev->name);
		goto end;
	}

	if (adev->ndev->type == ARPHRD_IEEE80211_PRISM)
		skb_len += sizeof(*msg);

	/* allocate skb */
	skb = dev_alloc_skb(skb_len);
	if (unlikely(!skb)) {
		printk("%s: no memory for skb (%u bytes)\n",
				adev->ndev->name, skb_len);
		goto end;
	}

	skb_put(skb, skb_len);

	if (adev->ndev->type == ARPHRD_IEEE80211) {
		/* when in raw 802.11 mode, just copy frame as-is */
		datap = skb->data;
	} else if (adev->ndev->type == ARPHRD_IEEE80211_PRISM) {
		/* emulate prism header */
		msg = (wlansniffrm_t*)skb->data;
		datap = msg + 1;

		msg->msgcode = WLANSNIFFFRM;
		msg->msglen = sizeof(*msg);
		strncpy(msg->devname, adev->ndev->name, sizeof(msg->devname)-1);
		msg->devname[sizeof(msg->devname)-1] = '\0';

		msg->hosttime.did = WLANSNIFFFRM_hosttime;
		msg->hosttime.status = WLANITEM_STATUS_data_ok;
		msg->hosttime.len = 4;
		msg->hosttime.data = jiffies;

		msg->mactime.did = WLANSNIFFFRM_mactime;
		msg->mactime.status = WLANITEM_STATUS_data_ok;
		msg->mactime.len = 4;
		msg->mactime.data = rxbuf->time;

		msg->channel.did = WLANSNIFFFRM_channel;
		msg->channel.status = WLANITEM_STATUS_data_ok;
		msg->channel.len = 4;
		msg->channel.data = adev->channel;

		msg->rssi.did = WLANSNIFFFRM_rssi;
		msg->rssi.status = WLANITEM_STATUS_no_value;
		msg->rssi.len = 4;
		msg->rssi.data = 0;

		msg->sq.did = WLANSNIFFFRM_sq;
		msg->sq.status = WLANITEM_STATUS_no_value;
		msg->sq.len = 4;
		msg->sq.data = 0;

		msg->signal.did = WLANSNIFFFRM_signal;
		msg->signal.status = WLANITEM_STATUS_data_ok;
		msg->signal.len = 4;
		msg->signal.data = rxbuf->phy_snr;

		msg->noise.did = WLANSNIFFFRM_noise;
		msg->noise.status = WLANITEM_STATUS_data_ok;
		msg->noise.len = 4;
		msg->noise.data = rxbuf->phy_level;

		msg->rate.did = WLANSNIFFFRM_rate;
		msg->rate.status = WLANITEM_STATUS_data_ok;
		msg->rate.len = 4;
		msg->rate.data = rxbuf->phy_plcp_signal / 5;

		msg->istx.did = WLANSNIFFFRM_istx;
		msg->istx.status = WLANITEM_STATUS_data_ok;
		msg->istx.len = 4;
		msg->istx.data = 0;	/* tx=0: it's not a tx packet */

		skb_len -= sizeof(*msg);

		msg->frmlen.did = WLANSNIFFFRM_signal;
		msg->frmlen.status = WLANITEM_STATUS_data_ok;
		msg->frmlen.len = 4;
		msg->frmlen.data = skb_len;
	} else {
		printk("acx: unsupported netdev type %d!\n", adev->ndev->type);
		dev_kfree_skb(skb);
		return;
	}

	/* sanity check (keep it here) */
	if (unlikely((int)skb_len < 0)) {
		printk("acx: skb_len=%d. Driver bug, please report\n", (int)skb_len);
		dev_kfree_skb(skb);
		return;
	}
	memcpy(datap, ((unsigned char*)rxbuf)+payload_offset, skb_len);

	skb->dev = adev->ndev;
	skb->dev->last_rx = jiffies;

	skb->mac.raw = skb->data;
	skb->ip_summed = CHECKSUM_NONE;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_80211_RAW);
	netif_rx(skb);

	adev->stats.rx_packets++;
	adev->stats.rx_bytes += skb->len;

end:
	FN_EXIT0;
}


/***********************************************************************
** acx_l_rx_ieee802_11_frame
**
** Called from IRQ context only
*/

/* All these contortions are for saner dup logging
**
** We want: (a) to know about excessive dups
** (b) to not spam kernel log about occasional dups
**
** 1/64 threshold was chosen by running "ping -A"
** It gave "rx: 59 DUPs in 2878 packets" only with 4 parallel
** "ping -A" streams running. */
/* 2005-10-11: bumped up to 1/8
** subtract a $smallint from dup_count in order to
** avoid "2 DUPs in 19 packets" messages */
static inline int
acx_l_handle_dup(acx_device_t *adev, u16 seq)
{
	if (adev->dup_count) {
		adev->nondup_count++;
		if (time_after(jiffies, adev->dup_msg_expiry)) {
			/* Log only if more than 1 dup in 64 packets */
			if (adev->nondup_count/8 < adev->dup_count-5) {
				printk(KERN_INFO "%s: rx: %d DUPs in "
					"%d packets received in 10 secs\n",
					adev->ndev->name,
					adev->dup_count,
					adev->nondup_count);
			}
			adev->dup_count = 0;
			adev->nondup_count = 0;
		}
	}
	if (unlikely(seq == adev->last_seq_ctrl)) {
		if (!adev->dup_count++)
			adev->dup_msg_expiry = jiffies + 10*HZ;
		adev->stats.rx_errors++;
		return 1; /* a dup */
	}
	adev->last_seq_ctrl = seq;
	return 0;
}

static int
acx_l_rx_ieee802_11_frame(acx_device_t *adev, rxbuffer_t *rxbuf)
{
	unsigned int ftype, fstype;
	const wlan_hdr_t *hdr;
	int result = NOT_OK;

	FN_ENTER;

	hdr = acx_get_wlan_hdr(adev, rxbuf);

	/* see IEEE 802.11-1999.pdf chapter 7 "MAC frame formats" */
	if (unlikely((hdr->fc & WF_FC_PVERi) != 0)) {
		printk_ratelimited(KERN_INFO "rx: unsupported 802.11 protocol\n");
		goto end;
	}

	ftype = hdr->fc & WF_FC_FTYPEi;
	fstype = hdr->fc & WF_FC_FSTYPEi;

	switch (ftype) {
	/* check data frames first, for speed */
	case WF_FTYPE_DATAi:
		switch (fstype) {
		case WF_FSTYPE_DATAONLYi:
			if (acx_l_handle_dup(adev, hdr->seq))
				break; /* a dup, simply discard it */

			/* TODO:
			if (WF_FC_FROMTODSi == (hdr->fc & WF_FC_FROMTODSi)) {
				result = acx_l_process_data_frame_wds(adev, rxbuf);
				break;
			}
			*/

			switch (adev->mode) {
			case ACX_MODE_3_AP:
				result = acx_l_process_data_frame_master(adev, rxbuf);
				break;
			case ACX_MODE_0_ADHOC:
			case ACX_MODE_2_STA:
				result = acx_l_process_data_frame_client(adev, rxbuf);
				break;
			}
		case WF_FSTYPE_DATA_CFACKi:
		case WF_FSTYPE_DATA_CFPOLLi:
		case WF_FSTYPE_DATA_CFACK_CFPOLLi:
		case WF_FSTYPE_CFPOLLi:
		case WF_FSTYPE_CFACK_CFPOLLi:
		/*   see above.
			acx_process_class_frame(adev, rxbuf, 3); */
			break;
		case WF_FSTYPE_NULLi:
			/* acx_l_process_NULL_frame(adev, rxbuf, 3); */
			break;
		/* FIXME: same here, see above */
		case WF_FSTYPE_CFACKi:
		default:
			break;
		}
		break;
	case WF_FTYPE_MGMTi:
		result = acx_l_process_mgmt_frame(adev, rxbuf);
		break;
	case WF_FTYPE_CTLi:
		if (fstype == WF_FSTYPE_PSPOLLi)
			result = OK;
		/*   this call is irrelevant, since
		 *   acx_process_class_frame is a stub, so return
		 *   immediately instead.
		 * return acx_process_class_frame(adev, rxbuf, 3); */
		break;
	default:
		break;
	}
end:
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_l_process_rxbuf
**
** NB: used by USB code also
*/
void
acx_l_process_rxbuf(acx_device_t *adev, rxbuffer_t *rxbuf)
{
	struct wlan_hdr *hdr;
	unsigned int qual;
	int buf_len;
	u16 fc;

	hdr = acx_get_wlan_hdr(adev, rxbuf);
	fc = le16_to_cpu(hdr->fc);
	/* length of frame from control field to first byte of FCS */
	buf_len = RXBUF_BYTES_RCVD(adev, rxbuf);

	if ( ((WF_FC_FSTYPE & fc) != WF_FSTYPE_BEACON)
	  || (acx_debug & L_XFER_BEACON)
	) {
		log(L_XFER|L_DATA, "rx: %s "
			"time:%u len:%u signal:%u SNR:%u macstat:%02X "
			"phystat:%02X phyrate:%u status:%u\n",
			acx_get_packet_type_string(fc),
			le32_to_cpu(rxbuf->time),
			buf_len,
			acx_signal_to_winlevel(rxbuf->phy_level),
			acx_signal_to_winlevel(rxbuf->phy_snr),
			rxbuf->mac_status,
			rxbuf->phy_stat_baseband,
			rxbuf->phy_plcp_signal,
			adev->status);
	}

	if (unlikely(acx_debug & L_DATA)) {
		printk("rx: 802.11 buf[%u]: ", buf_len);
		acx_dump_bytes(hdr, buf_len);
	}

	/* FIXME: should check for Rx errors (rxbuf->mac_status?
	 * discard broken packets - but NOT for monitor!)
	 * and update Rx packet statistics here */

	if (unlikely(adev->mode == ACX_MODE_MONITOR)) {
		acx_l_rxmonitor(adev, rxbuf);
	} else if (likely(buf_len >= WLAN_HDR_A3_LEN)) {
		acx_l_rx_ieee802_11_frame(adev, rxbuf);
	} else {
		log(L_DEBUG|L_XFER|L_DATA,
		       "rx: NOT receiving packet (%s): "
		       "size too small (%u)\n",
		       acx_get_packet_type_string(fc),
		       buf_len);
	}

	/* Now check Rx quality level, AFTER processing packet.
	 * I tried to figure out how to map these levels to dBm
	 * values, but for the life of me I really didn't
	 * manage to get it. Either these values are not meant to
	 * be expressed in dBm, or it's some pretty complicated
	 * calculation. */

#ifdef FROM_SCAN_SOURCE_ONLY
	/* only consider packets originating from the MAC
	 * address of the device that's managing our BSSID.
	 * Disable it for now, since it removes information (levels
	 * from different peers) and slows the Rx path. */
	if (adev->ap_client
	 && mac_is_equal(hdr->a2, adev->ap_client->address)) {
#endif
		adev->wstats.qual.level = acx_signal_to_winlevel(rxbuf->phy_level);
		adev->wstats.qual.noise = acx_signal_to_winlevel(rxbuf->phy_snr);
#ifndef OLD_QUALITY
		qual = acx_signal_determine_quality(adev->wstats.qual.level,
				adev->wstats.qual.noise);
#else
		qual = (adev->wstats.qual.noise <= 100) ?
				100 - adev->wstats.qual.noise : 0;
#endif
		adev->wstats.qual.qual = qual;
		adev->wstats.qual.updated = 7; /* all 3 indicators updated */
#ifdef FROM_SCAN_SOURCE_ONLY
	}
#endif
}


/***********************************************************************
** acx_l_handle_txrate_auto
**
** Theory of operation:
** client->rate_cap is a bitmask of rates client is capable of.
** client->rate_cfg is a bitmask of allowed (configured) rates.
** It is set as a result of iwconfig rate N [auto]
** or iwpriv set_rates "N,N,N N,N,N" commands.
** It can be fixed (e.g. 0x0080 == 18Mbit only),
** auto (0x00ff == 18Mbit or any lower value),
** and code handles any bitmask (0x1081 == try 54Mbit,18Mbit,1Mbit _only_).
**
** client->rate_cur is a value for rate111 field in tx descriptor.
** It is always set to txrate_cfg sans zero or more most significant
** bits. This routine handles selection of new rate_cur value depending on
** outcome of last tx event.
**
** client->rate_100 is a precalculated rate value for acx100
** (we can do without it, but will need to calculate it on each tx).
**
** You cannot configure mixed usage of 5.5 and/or 11Mbit rate
** with PBCC and CCK modulation. Either both at CCK or both at PBCC.
** In theory you can implement it, but so far it is considered not worth doing.
**
** 22Mbit, of course, is PBCC always. */

/* maps acx100 tx descr rate field to acx111 one */
static u16
rate100to111(u8 r)
{
	switch (r) {
	case RATE100_1:	return RATE111_1;
	case RATE100_2:	return RATE111_2;
	case RATE100_5:
	case (RATE100_5 | RATE100_PBCC511):	return RATE111_5;
	case RATE100_11:
	case (RATE100_11 | RATE100_PBCC511):	return RATE111_11;
	case RATE100_22:	return RATE111_22;
	default:
		printk("acx: unexpected acx100 txrate: %u! "
			"Please report\n", r);
		return RATE111_1;
	}
}


void
acx_l_handle_txrate_auto(acx_device_t *adev, struct client *txc,
			u16 cur, u8 rate100, u16 rate111,
			u8 error, int pkts_to_ignore)
{
	u16 sent_rate;
	int slower_rate_was_used;

	/* vda: hmm. current code will do this:
	** 1. send packets at 11 Mbit, stepup++
	** 2. will try to send at 22Mbit. hardware will see no ACK,
	**    retries at 11Mbit, success. code notes that used rate
	**    is lower. stepup = 0, fallback++
	** 3. repeat step 2 fallback_count times. Fall back to
	**    11Mbit. go to step 1.
	** If stepup_count is large (say, 16) and fallback_count
	** is small (3), this wouldn't be too bad wrt throughput */

	if (unlikely(!cur)) {
		printk("acx: BUG! ratemask is empty\n");
		return; /* or else we may lock up the box */
	}

	/* do some preparations, i.e. calculate the one rate that was
	 * used to send this packet */
	if (IS_ACX111(adev)) {
		sent_rate = 1 << highest_bit(rate111 & RATE111_ALL);
	} else {
		sent_rate = rate100to111(rate100);
	}
	/* sent_rate has only one bit set now, corresponding to tx rate
	 * which was used by hardware to tx this particular packet */

	/* now do the actual auto rate management */
	log(L_XFER, "tx: %sclient=%p/"MACSTR" used=%04X cur=%04X cfg=%04X "
		"__=%u/%u ^^=%u/%u\n",
		(txc->ignore_count > 0) ? "[IGN] " : "",
		txc, MAC(txc->address), sent_rate, cur, txc->rate_cfg,
		txc->fallback_count, adev->fallback_threshold,
		txc->stepup_count, adev->stepup_threshold
	);

	/* we need to ignore old packets already in the tx queue since
	 * they use older rate bytes configured before our last rate change,
	 * otherwise our mechanism will get confused by interpreting old data.
	 * Do it after logging above */
	if (txc->ignore_count) {
		txc->ignore_count--;
		return;
	}

	/* true only if the only nonzero bit in sent_rate is
	** less significant than highest nonzero bit in cur */
	slower_rate_was_used = ( cur > ((sent_rate<<1)-1) );

	if (slower_rate_was_used || error) {
		txc->stepup_count = 0;
		if (++txc->fallback_count <= adev->fallback_threshold)
			return;
		txc->fallback_count = 0;

		/* clear highest 1 bit in cur */
		sent_rate = RATE111_54;
		while (!(cur & sent_rate)) sent_rate >>= 1;
		CLEAR_BIT(cur, sent_rate);
		if (!cur) /* we can't disable all rates! */
			cur = sent_rate;
		log(L_XFER, "tx: falling back to ratemask %04X\n", cur);

	} else { /* there was neither lower rate nor error */
		txc->fallback_count = 0;
		if (++txc->stepup_count <= adev->stepup_threshold)
			return;
		txc->stepup_count = 0;

		/* Sanitize. Sort of not needed, but I dont trust hw that much...
		** what if it can report bogus tx rates sometimes? */
		while (!(cur & sent_rate)) sent_rate >>= 1;

		/* try to find a higher sent_rate that isn't yet in our
		 * current set, but is an allowed cfg */
		while (1) {
			sent_rate <<= 1;
			if (sent_rate > txc->rate_cfg)
				/* no higher rates allowed by config */
				return;
			if (!(cur & sent_rate) && (txc->rate_cfg & sent_rate))
				/* found */
				break;
			/* not found, try higher one */
		}
		SET_BIT(cur, sent_rate);
		log(L_XFER, "tx: stepping up to ratemask %04X\n", cur);
	}

	txc->rate_cur = cur;
	txc->ignore_count = pkts_to_ignore;
	/* calculate acx100 style rate byte if needed */
	if (IS_ACX100(adev)) {
		txc->rate_100 = acx_bitpos2rate100[highest_bit(cur)];
	}
}


/***********************************************************************
** acx_i_start_xmit
**
** Called by network core. Can be called outside of process context.
*/
int
acx_i_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	acx_device_t *adev = ndev2adev(ndev);
	tx_t *tx;
	void *txbuf;
	unsigned long flags;
	int txresult = NOT_OK;
	int len;

	FN_ENTER;

	if (unlikely(!skb)) {
		/* indicate success */
		txresult = OK;
		goto end_no_unlock;
	}
	if (unlikely(!adev)) {
		goto end_no_unlock;
	}

	acx_lock(adev, flags);

	if (unlikely(!(adev->dev_state_mask & ACX_STATE_IFACE_UP))) {
		goto end;
	}
	if (unlikely(adev->mode == ACX_MODE_OFF)) {
		goto end;
	}
	if (unlikely(acx_queue_stopped(ndev))) {
		log(L_DEBUG, "%s: called when queue stopped\n", __func__);
		goto end;
	}
	if (unlikely(ACX_STATUS_4_ASSOCIATED != adev->status)) {
		log(L_XFER, "trying to xmit, but not associated yet: "
			"aborting...\n");
		/* silently drop the packet, since we're not connected yet */
		txresult = OK;
		/* ...but indicate an error nevertheless */
		adev->stats.tx_errors++;
		goto end;
	}

	tx = acx_l_alloc_tx(adev);
	if (unlikely(!tx)) {
		printk_ratelimited("%s: start_xmit: txdesc ring is full, "
			"dropping tx\n", ndev->name);
		txresult = NOT_OK;
		goto end;
	}

	txbuf = acx_l_get_txbuf(adev, tx);
	if (unlikely(!txbuf)) {
		/* Card was removed */
		txresult = NOT_OK;
		acx_l_dealloc_tx(adev, tx);
		goto end;
	}
	len = acx_ether_to_txbuf(adev, txbuf, skb);
	if (unlikely(len < 0)) {
		/* Error in packet conversion */
		txresult = NOT_OK;
		acx_l_dealloc_tx(adev, tx);
		goto end;
	}
	acx_l_tx_data(adev, tx, len);
	ndev->trans_start = jiffies;

	txresult = OK;
	adev->stats.tx_packets++;
	adev->stats.tx_bytes += skb->len;

end:
	acx_unlock(adev, flags);

end_no_unlock:
	if ((txresult == OK) && skb)
		dev_kfree_skb_any(skb);

	FN_EXIT1(txresult);
	return txresult;
}


/***********************************************************************
** acx_l_update_ratevector
**
** Updates adev->rate_supported[_len] according to rate_{basic,oper}
*/
const u8
acx_bitpos2ratebyte[] = {
	DOT11RATEBYTE_1,
	DOT11RATEBYTE_2,
	DOT11RATEBYTE_5_5,
	DOT11RATEBYTE_6_G,
	DOT11RATEBYTE_9_G,
	DOT11RATEBYTE_11,
	DOT11RATEBYTE_12_G,
	DOT11RATEBYTE_18_G,
	DOT11RATEBYTE_22,
	DOT11RATEBYTE_24_G,
	DOT11RATEBYTE_36_G,
	DOT11RATEBYTE_48_G,
	DOT11RATEBYTE_54_G,
};

void
acx_l_update_ratevector(acx_device_t *adev)
{
	u16 bcfg = adev->rate_basic;
	u16 ocfg = adev->rate_oper;
	u8 *supp = adev->rate_supported;
	const u8 *dot11 = acx_bitpos2ratebyte;

	FN_ENTER;

	while (ocfg) {
		if (ocfg & 1) {
			*supp = *dot11;
			if (bcfg & 1) {
				*supp |= 0x80;
			}
			supp++;
		}
		dot11++;
		ocfg >>= 1;
		bcfg >>= 1;
	}
	adev->rate_supported_len = supp - adev->rate_supported;
	if (acx_debug & L_ASSOC) {
		printk("new ratevector: ");
		acx_dump_bytes(adev->rate_supported, adev->rate_supported_len);
	}
	FN_EXIT0;
}


/***********************************************************************
** acx_l_sta_list_init
*/
static void
acx_l_sta_list_init(acx_device_t *adev)
{
	FN_ENTER;
	memset(adev->sta_hash_tab, 0, sizeof(adev->sta_hash_tab));
	memset(adev->sta_list, 0, sizeof(adev->sta_list));
	FN_EXIT0;
}


/***********************************************************************
** acx_l_sta_list_get_from_hash
*/
static inline client_t*
acx_l_sta_list_get_from_hash(acx_device_t *adev, const u8 *address)
{
	return adev->sta_hash_tab[address[5] % VEC_SIZE(adev->sta_hash_tab)];
}


/***********************************************************************
** acx_l_sta_list_get
*/
client_t*
acx_l_sta_list_get(acx_device_t *adev, const u8 *address)
{
	client_t *client;
	FN_ENTER;
	client = acx_l_sta_list_get_from_hash(adev, address);
	while (client) {
		if (mac_is_equal(address, client->address)) {
			client->mtime = jiffies;
			break;
		}
		client = client->next;
	}
	FN_EXIT0;
	return client;
}


/***********************************************************************
** acx_l_sta_list_del
*/
void
acx_l_sta_list_del(acx_device_t *adev, client_t *victim)
{
	client_t *client, *next;

	client = acx_l_sta_list_get_from_hash(adev, victim->address);
	next = client;
	/* tricky. next = client on first iteration only,
	** on all other iters next = client->next */
	while (next) {
		if (next == victim) {
			client->next = victim->next;
			/* Overkill */
			memset(victim, 0, sizeof(*victim));
			break;
		}
		client = next;
		next = client->next;
	}
}


/***********************************************************************
** acx_l_sta_list_alloc
**
** Never fails - will evict oldest client if needed
*/
static client_t*
acx_l_sta_list_alloc(acx_device_t *adev)
{
	int i;
	unsigned long age, oldest_age;
	client_t *client, *oldest;

	FN_ENTER;

	oldest = &adev->sta_list[0];
	oldest_age = 0;
	for (i = 0; i < VEC_SIZE(adev->sta_list); i++) {
		client = &adev->sta_list[i];

		if (!client->used) {
			goto found;
		} else {
			age = jiffies - client->mtime;
			if (oldest_age < age) {
				oldest_age = age;
				oldest = client;
			}
		}
	}
	acx_l_sta_list_del(adev, oldest);
	client = oldest;
found:
	memset(client, 0, sizeof(*client));
	FN_EXIT0;
	return client;
}


/***********************************************************************
** acx_l_sta_list_add
**
** Never fails - will evict oldest client if needed
*/
/* In case we will reimplement it differently... */
#define STA_LIST_ADD_CAN_FAIL 0

static client_t*
acx_l_sta_list_add(acx_device_t *adev, const u8 *address)
{
	client_t *client;
	int index;

	FN_ENTER;

	client = acx_l_sta_list_alloc(adev);

	client->mtime = jiffies;
	MAC_COPY(client->address, address);
	client->used = CLIENT_EXIST_1;
	client->auth_alg = WLAN_AUTH_ALG_SHAREDKEY;
	client->auth_step = 1;
	/* give some tentative peer rate values
	** (needed because peer may do auth without probing us first,
	** thus we'll have no idea of peer's ratevector yet).
	** Will be overwritten by scanning or assoc code */
	client->rate_cap = adev->rate_basic;
	client->rate_cfg = adev->rate_basic;
	client->rate_cur = 1 << lowest_bit(adev->rate_basic);

	index = address[5] % VEC_SIZE(adev->sta_hash_tab);
	client->next = adev->sta_hash_tab[index];
	adev->sta_hash_tab[index] = client;

	acxlog_mac(L_ASSOC, "sta_list_add: sta=", address, "\n");

	FN_EXIT0;
	return client;
}


/***********************************************************************
** acx_l_sta_list_get_or_add
**
** Never fails - will evict oldest client if needed
*/
static client_t*
acx_l_sta_list_get_or_add(acx_device_t *adev, const u8 *address)
{
	client_t *client = acx_l_sta_list_get(adev, address);
	if (!client)
		client = acx_l_sta_list_add(adev, address);
	return client;
}


/***********************************************************************
** acx_set_status
**
** This function is called in many atomic regions, must not sleep
**
** This function does not need locking UNLESS you call it
** as acx_set_status(ACX_STATUS_4_ASSOCIATED), bacause this can
** wake queue. This can race with stop_queue elsewhere.
** See acx_stop_queue comment. */
void
acx_set_status(acx_device_t *adev, u16 new_status)
{
#define QUEUE_OPEN_AFTER_ASSOC 1 /* this really seems to be needed now */
	u16 old_status = adev->status;

	FN_ENTER;

	log(L_ASSOC, "%s(%d):%s\n",
	       __func__, new_status, acx_get_status_name(new_status));

	/* wireless_send_event never sleeps */
	if (ACX_STATUS_4_ASSOCIATED == new_status) {
		union iwreq_data wrqu;

		wrqu.data.length = 0;
		wrqu.data.flags = 0;
		wireless_send_event(adev->ndev, SIOCGIWSCAN, &wrqu, NULL);

		wrqu.data.length = 0;
		wrqu.data.flags = 0;
		MAC_COPY(wrqu.ap_addr.sa_data, adev->bssid);
		wrqu.ap_addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(adev->ndev, SIOCGIWAP, &wrqu, NULL);
	} else {
		union iwreq_data wrqu;

		/* send event with empty BSSID to indicate we're not associated */
		MAC_ZERO(wrqu.ap_addr.sa_data);
		wrqu.ap_addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(adev->ndev, SIOCGIWAP, &wrqu, NULL);
	}

	adev->status = new_status;

	switch (new_status) {
	case ACX_STATUS_1_SCANNING:
		adev->scan_retries = 0;
		/* 1.0 s initial scan time */
		acx_set_timer(adev, 1000000);
		break;
	case ACX_STATUS_2_WAIT_AUTH:
	case ACX_STATUS_3_AUTHENTICATED:
		adev->auth_or_assoc_retries = 0;
		acx_set_timer(adev, 1500000); /* 1.5 s */
		break;
	}

#if QUEUE_OPEN_AFTER_ASSOC
	if (new_status == ACX_STATUS_4_ASSOCIATED)	{
		if (old_status < ACX_STATUS_4_ASSOCIATED) {
			/* ah, we're newly associated now,
			 * so let's indicate carrier */
			acx_carrier_on(adev->ndev, "after association");
			acx_wake_queue(adev->ndev, "after association");
		}
	} else {
		/* not associated any more, so let's kill carrier */
		if (old_status >= ACX_STATUS_4_ASSOCIATED) {
			acx_carrier_off(adev->ndev, "after losing association");
			acx_stop_queue(adev->ndev, "after losing association");
		}
	}
#endif
	FN_EXIT0;
}


/***********************************************************************
** acx_i_timer
**
** Fires up periodically. Used to kick scan/auth/assoc if something goes wrong
*/
void
acx_i_timer(unsigned long address)
{
	unsigned long flags;
	acx_device_t *adev = (acx_device_t*)address;

	FN_ENTER;

	acx_lock(adev, flags);

	log(L_DEBUG|L_ASSOC, "%s: adev->status=%d (%s)\n",
		__func__, adev->status, acx_get_status_name(adev->status));

	switch (adev->status) {
	case ACX_STATUS_1_SCANNING:
		/* was set to 0 by set_status() */
		if (++adev->scan_retries < 7) {
			acx_set_timer(adev, 1000000);
			/* used to interrogate for scan status.
			** We rely on SCAN_COMPLETE IRQ instead */
			log(L_ASSOC, "continuing scan (%d sec)\n",
					adev->scan_retries);
		} else {
			log(L_ASSOC, "stopping scan\n");
			/* send stop_scan cmd when we leave the interrupt context,
			 * and make a decision what to do next (COMPLETE_SCAN) */
			acx_schedule_task(adev,
				ACX_AFTER_IRQ_CMD_STOP_SCAN + ACX_AFTER_IRQ_COMPLETE_SCAN);
		}
		break;
	case ACX_STATUS_2_WAIT_AUTH:
		/* was set to 0 by set_status() */
		if (++adev->auth_or_assoc_retries < 10) {
			log(L_ASSOC, "resend authen1 request (attempt %d)\n",
					adev->auth_or_assoc_retries + 1);
			acx_l_transmit_authen1(adev);
		} else {
			/* time exceeded: fall back to scanning mode */
			log(L_ASSOC,
			       "authen1 request reply timeout, giving up\n");
			/* we are a STA, need to find AP anyhow */
			acx_set_status(adev, ACX_STATUS_1_SCANNING);
			acx_schedule_task(adev, ACX_AFTER_IRQ_RESTART_SCAN);
		}
		/* used to be 1500000, but some other driver uses 2.5s */
		acx_set_timer(adev, 2500000);
		break;
	case ACX_STATUS_3_AUTHENTICATED:
		/* was set to 0 by set_status() */
		if (++adev->auth_or_assoc_retries < 10) {
			log(L_ASSOC, "resend assoc request (attempt %d)\n",
					adev->auth_or_assoc_retries + 1);
			acx_l_transmit_assoc_req(adev);
		} else {
			/* time exceeded: give up */
			log(L_ASSOC,
				"association request reply timeout, giving up\n");
			/* we are a STA, need to find AP anyhow */
			acx_set_status(adev, ACX_STATUS_1_SCANNING);
			acx_schedule_task(adev, ACX_AFTER_IRQ_RESTART_SCAN);
		}
		acx_set_timer(adev, 2500000); /* see above */
		break;
	case ACX_STATUS_4_ASSOCIATED:
	default:
		break;
	}

	acx_unlock(adev, flags);

	FN_EXIT0;
}


/***********************************************************************
** acx_set_timer
**
** Sets the 802.11 state management timer's timeout.
*/
void
acx_set_timer(acx_device_t *adev, int timeout_us)
{
	FN_ENTER;

	log(L_DEBUG|L_IRQ, "%s(%u ms)\n", __func__, timeout_us/1000);
	if (!(adev->dev_state_mask & ACX_STATE_IFACE_UP)) {
		printk("attempt to set the timer "
			"when the card interface is not up!\n");
		goto end;
	}

	/* first check if the timer was already initialized, THEN modify it */
	if (adev->mgmt_timer.function) {
		mod_timer(&adev->mgmt_timer,
				jiffies + (timeout_us * HZ / 1000000));
	}
end:
	FN_EXIT0;
}


/***********************************************************************
** acx_l_transmit_assocresp
**
** We are an AP here
*/
static const u8
dot11ratebyte[] = {
	DOT11RATEBYTE_1,
	DOT11RATEBYTE_2,
	DOT11RATEBYTE_5_5,
	DOT11RATEBYTE_6_G,
	DOT11RATEBYTE_9_G,
	DOT11RATEBYTE_11,
	DOT11RATEBYTE_12_G,
	DOT11RATEBYTE_18_G,
	DOT11RATEBYTE_22,
	DOT11RATEBYTE_24_G,
	DOT11RATEBYTE_36_G,
	DOT11RATEBYTE_48_G,
	DOT11RATEBYTE_54_G,
};

static inline int
find_pos(const u8 *p, int size, u8 v)
{
	int i;
	for (i = 0; i < size; i++)
		if (p[i] == v)
			return i;
	/* printk a message about strange byte? */
	return 0;
}

static void
add_bits_to_ratemasks(u8* ratevec, int len, u16* brate, u16* orate)
{
	while (len--) {
		int n = 1 << find_pos(dot11ratebyte,
				sizeof(dot11ratebyte), *ratevec & 0x7f);
		if (*ratevec & 0x80)
			*brate |= n;
		*orate |= n;
		ratevec++;
	}
}

static int
acx_l_transmit_assocresp(acx_device_t *adev, const wlan_fr_assocreq_t *req)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct assocresp_frame_body *body;
	u8 *p;
	const u8 *da;
	/* const u8 *sa; */
	const u8 *bssid;
	client_t *clt;

	FN_ENTER;

	/* sa = req->hdr->a1; */
	da = req->hdr->a2;
	bssid = req->hdr->a3;

	clt = acx_l_sta_list_get(adev, da);
	if (!clt)
		goto ok;

	/* Assoc without auth is a big no-no */
	/* Let's be liberal: if already assoc'ed STA sends assoc req again,
	** we won't be rude */
	if (clt->used != CLIENT_AUTHENTICATED_2
	 && clt->used != CLIENT_ASSOCIATED_3) {
		acx_l_transmit_deauthen(adev, da, WLAN_MGMT_REASON_CLASS2_NONAUTH);
		goto bad;
	}

	clt->used = CLIENT_ASSOCIATED_3;

	if (clt->aid == 0)
		clt->aid = ++adev->aid;
	clt->cap_info = ieee2host16(*(req->cap_info));

	/* We cheat here a bit. We don't really care which rates are flagged
	** as basic by the client, so we stuff them in single ratemask */
	clt->rate_cap = 0;
	if (req->supp_rates)
		add_bits_to_ratemasks(req->supp_rates->rates,
			req->supp_rates->len, &clt->rate_cap, &clt->rate_cap);
	if (req->ext_rates)
		add_bits_to_ratemasks(req->ext_rates->rates,
			req->ext_rates->len, &clt->rate_cap, &clt->rate_cap);
	/* We can check that client supports all basic rates,
	** and deny assoc if not. But let's be liberal, right? ;) */
	clt->rate_cfg = clt->rate_cap & adev->rate_oper;
	if (!clt->rate_cfg) clt->rate_cfg = 1 << lowest_bit(adev->rate_oper);
	clt->rate_cur = 1 << lowest_bit(clt->rate_cfg);
	if (IS_ACX100(adev))
		clt->rate_100 = acx_bitpos2rate100[lowest_bit(clt->rate_cfg)];
	clt->fallback_count = clt->stepup_count = 0;
	clt->ignore_count = 16;

	tx = acx_l_alloc_tx(adev);
	if (!tx)
		goto bad;
	head = acx_l_get_txbuf(adev, tx);
	if (!head) {
		acx_l_dealloc_tx(adev, tx);
		goto bad;
	}
	body = (void*)(head + 1);

	head->fc = WF_FSTYPE_ASSOCRESPi;
	head->dur = req->hdr->dur;
	MAC_COPY(head->da, da);
	MAC_COPY(head->sa, adev->dev_addr);
	MAC_COPY(head->bssid, bssid);
	head->seq = req->hdr->seq;

	body->cap_info = host2ieee16(adev->capabilities);
	body->status = host2ieee16(0);
	body->aid = host2ieee16(clt->aid);
	p = wlan_fill_ie_rates((u8*)&body->rates, adev->rate_supported_len,
							adev->rate_supported);
	p = wlan_fill_ie_rates_ext(p, adev->rate_supported_len,
							adev->rate_supported);

	acx_l_tx_data(adev, tx, p - (u8*)head);
ok:
	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/***********************************************************************
* acx_l_transmit_reassocresp

You may be wondering, just like me, what the hell ReAuth is.
In practice it was seen sent by STA when STA feels like losing connection.

[802.11]

5.4.2.3 Reassociation

Association is sufficient for no-transition message delivery between
IEEE 802.11 stations. Additional functionality is needed to support
BSS-transition mobility. The additional required functionality
is provided by the reassociation service. Reassociation is a DSS.
The reassociation service is invoked to 'move' a current association
from one AP to another. This keeps the DS informed of the current
mapping between AP and STA as the station moves from BSS to BSS within
an ESS. Reassociation also enables changing association attributes
of an established association while the STA remains associated with
the same AP. Reassociation is always initiated by the mobile STA.

5.4.3.1 Authentication
...
A STA may be authenticated with many other STAs at any given instant.

5.4.3.1.1 Preauthentication

Because the authentication process could be time-consuming (depending
on the authentication protocol in use), the authentication service can
be invoked independently of the association service. Preauthentication
is typically done by a STA while it is already associated with an AP
(with which it previously authenticated). IEEE 802.11 does not require
that STAs preauthenticate with APs. However, authentication is required
before an association can be established. If the authentication is left
until reassociation time, this may impact the speed with which a STA can
reassociate between APs, limiting BSS-transition mobility performance.
The use of preauthentication takes the authentication service overhead
out of the time-critical reassociation process.

5.7.3 Reassociation

For a STA to reassociate, the reassociation service causes the following
message to occur:

  Reassociation request

* Message type: Management
* Message subtype: Reassociation request
* Information items:
  - IEEE address of the STA
  - IEEE address of the AP with which the STA will reassociate
  - IEEE address of the AP with which the STA is currently associated
  - ESSID
* Direction of message: From STA to 'new' AP

The address of the current AP is included for efficiency. The inclusion
of the current AP address facilitates MAC reassociation to be independent
of the DS implementation.

  Reassociation response
* Message type: Management
* Message subtype: Reassociation response
* Information items:
  - Result of the requested reassociation. (success/failure)
  - If the reassociation is successful, the response shall include the AID.
* Direction of message: From AP to STA

7.2.3.6 Reassociation Request frame format

The frame body of a management frame of subtype Reassociation Request
contains the information shown in Table 9.

Table 9 Reassociation Request frame body
Order Information
1 Capability information
2 Listen interval
3 Current AP address
4 SSID
5 Supported rates

7.2.3.7 Reassociation Response frame format

The frame body of a management frame of subtype Reassociation Response
contains the information shown in Table 10.

Table 10 Reassociation Response frame body
Order Information
1 Capability information
2 Status code
3 Association ID (AID)
4 Supported rates

*/
static int
acx_l_transmit_reassocresp(acx_device_t *adev, const wlan_fr_reassocreq_t *req)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct reassocresp_frame_body *body;
	u8 *p;
	const u8 *da;
	/* const u8 *sa; */
	const u8 *bssid;
	client_t *clt;

	FN_ENTER;

	/* sa = req->hdr->a1; */
	da = req->hdr->a2;
	bssid = req->hdr->a3;

	/* Must be already authenticated, so it must be in the list */
	clt = acx_l_sta_list_get(adev, da);
	if (!clt)
		goto ok;

	/* Assoc without auth is a big no-no */
	/* Already assoc'ed STAs sending ReAssoc req are ok per 802.11 */
	if (clt->used != CLIENT_AUTHENTICATED_2
	 && clt->used != CLIENT_ASSOCIATED_3) {
		acx_l_transmit_deauthen(adev, da, WLAN_MGMT_REASON_CLASS2_NONAUTH);
		goto bad;
	}

	clt->used = CLIENT_ASSOCIATED_3;
	if (clt->aid == 0) {
		clt->aid = ++adev->aid;
	}
	if (req->cap_info)
		clt->cap_info = ieee2host16(*(req->cap_info));

	/* We cheat here a bit. We don't really care which rates are flagged
	** as basic by the client, so we stuff them in single ratemask */
	clt->rate_cap = 0;
	if (req->supp_rates)
		add_bits_to_ratemasks(req->supp_rates->rates,
			req->supp_rates->len, &clt->rate_cap, &clt->rate_cap);
	if (req->ext_rates)
		add_bits_to_ratemasks(req->ext_rates->rates,
			req->ext_rates->len, &clt->rate_cap, &clt->rate_cap);
	/* We can check that client supports all basic rates,
	** and deny assoc if not. But let's be liberal, right? ;) */
	clt->rate_cfg = clt->rate_cap & adev->rate_oper;
	if (!clt->rate_cfg) clt->rate_cfg = 1 << lowest_bit(adev->rate_oper);
	clt->rate_cur = 1 << lowest_bit(clt->rate_cfg);
	if (IS_ACX100(adev))
		clt->rate_100 = acx_bitpos2rate100[lowest_bit(clt->rate_cfg)];

	clt->fallback_count = clt->stepup_count = 0;
	clt->ignore_count = 16;

	tx = acx_l_alloc_tx(adev);
	if (!tx)
		goto ok;
	head = acx_l_get_txbuf(adev, tx);
	if (!head) {
		acx_l_dealloc_tx(adev, tx);
		goto ok;
	}
	body = (void*)(head + 1);

	head->fc = WF_FSTYPE_REASSOCRESPi;
	head->dur = req->hdr->dur;
	MAC_COPY(head->da, da);
	MAC_COPY(head->sa, adev->dev_addr);
	MAC_COPY(head->bssid, bssid);
	head->seq = req->hdr->seq;

	/* IEs: 1. caps */
	body->cap_info = host2ieee16(adev->capabilities);
	/* 2. status code */
	body->status = host2ieee16(0);
	/* 3. AID */
	body->aid = host2ieee16(clt->aid);
	/* 4. supp rates */
	p = wlan_fill_ie_rates((u8*)&body->rates, adev->rate_supported_len,
							adev->rate_supported);
	/* 5. ext supp rates */
	p = wlan_fill_ie_rates_ext(p, adev->rate_supported_len,
							adev->rate_supported);

	acx_l_tx_data(adev, tx, p - (u8*)head);
ok:
	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/***********************************************************************
** acx_l_process_disassoc_from_sta
*/
static void
acx_l_process_disassoc_from_sta(acx_device_t *adev, const wlan_fr_disassoc_t *req)
{
	const u8 *ta;
	client_t *clt;

	FN_ENTER;

	ta = req->hdr->a2;
	clt = acx_l_sta_list_get(adev, ta);
	if (!clt)
		goto end;

	if (clt->used != CLIENT_ASSOCIATED_3
	 && clt->used != CLIENT_AUTHENTICATED_2) {
		/* it's disassociating, but it's
		** not even authenticated! Let it know that */
		acxlog_mac(L_ASSOC|L_XFER, "peer ", ta, "has sent disassoc "
			"req but it is not even auth'ed! sending deauth\n");
		acx_l_transmit_deauthen(adev, ta,
			WLAN_MGMT_REASON_CLASS2_NONAUTH);
		clt->used = CLIENT_EXIST_1;
	} else {
		/* mark it as auth'ed only */
		clt->used = CLIENT_AUTHENTICATED_2;
	}
end:
	FN_EXIT0;
}


/***********************************************************************
** acx_l_process_deauthen_from_sta
*/
static void
acx_l_process_deauth_from_sta(acx_device_t *adev, const wlan_fr_deauthen_t *req)
{
	const wlan_hdr_t *hdr;
	client_t *client;

	FN_ENTER;

	hdr = req->hdr;

	if (acx_debug & L_ASSOC) {
		acx_print_mac("got deauth from sta:", hdr->a2, " ");
		acx_print_mac("a1:", hdr->a1, " ");
		acx_print_mac("a3:", hdr->a3, " ");
		acx_print_mac("adev->addr:", adev->dev_addr, " ");
		acx_print_mac("adev->bssid:", adev->bssid, "\n");
	}

	if (!mac_is_equal(adev->dev_addr, hdr->a1)) {
		goto end;
	}

	client = acx_l_sta_list_get(adev, hdr->a2);
	if (!client) {
		goto end;
	}
	client->used = CLIENT_EXIST_1;
end:
	FN_EXIT0;
}


/***********************************************************************
** acx_l_process_disassoc_from_ap
*/
static void
acx_l_process_disassoc_from_ap(acx_device_t *adev, const wlan_fr_disassoc_t *req)
{
	FN_ENTER;

	if (!adev->ap_client) {
		/* Hrm, we aren't assoc'ed yet anyhow... */
		goto end;
	}

	printk("%s: got disassoc frame with reason %d (%s)\n",
		adev->ndev->name, *req->reason,
		acx_wlan_reason_str(*req->reason));

	if (mac_is_equal(adev->dev_addr, req->hdr->a1)) {
		acx_l_transmit_deauthen(adev, adev->bssid,
				WLAN_MGMT_REASON_DEAUTH_LEAVING);
		SET_BIT(adev->set_mask, GETSET_RESCAN);
		acx_schedule_task(adev, ACX_AFTER_IRQ_UPDATE_CARD_CFG);
	}
end:
	FN_EXIT0;
}


/***********************************************************************
** acx_l_process_deauth_from_ap
*/
static void
acx_l_process_deauth_from_ap(acx_device_t *adev, const wlan_fr_deauthen_t *req)
{
	FN_ENTER;

	if (!adev->ap_client) {
		/* Hrm, we aren't assoc'ed yet anyhow... */
		goto end;
	}

	printk("%s: got deauth frame with reason %d (%s)\n",
		adev->ndev->name, *req->reason,
		acx_wlan_reason_str(*req->reason));

	/* Chk: is ta verified to be from our AP? */
	if (mac_is_equal(adev->dev_addr, req->hdr->a1)) {
		log(L_DEBUG, "AP sent us deauth packet\n");
		SET_BIT(adev->set_mask, GETSET_RESCAN);
		acx_schedule_task(adev, ACX_AFTER_IRQ_UPDATE_CARD_CFG);
	}
end:
	FN_EXIT0;
}


/***********************************************************************
** acx_l_rx
**
** The end of the Rx path. Pulls data from a rxhostdesc into a socket
** buffer and feeds it to the network stack via netif_rx().
*/
static void
acx_l_rx(acx_device_t *adev, rxbuffer_t *rxbuf)
{
	FN_ENTER;
	if (likely(adev->dev_state_mask & ACX_STATE_IFACE_UP)) {
		struct sk_buff *skb;
		skb = acx_rxbuf_to_ether(adev, rxbuf);
		if (likely(skb)) {
			netif_rx(skb);
			adev->ndev->last_rx = jiffies;
			adev->stats.rx_packets++;
			adev->stats.rx_bytes += skb->len;
		}
	}
	FN_EXIT0;
}


/***********************************************************************
** acx_l_process_data_frame_master
*/
static int
acx_l_process_data_frame_master(acx_device_t *adev, rxbuffer_t *rxbuf)
{
	struct wlan_hdr *hdr;
	struct tx *tx;
	void *txbuf;
	int len;
	int result = NOT_OK;

	FN_ENTER;

	hdr = acx_get_wlan_hdr(adev, rxbuf);

	switch (WF_FC_FROMTODSi & hdr->fc) {
	case 0:
	case WF_FC_FROMDSi:
		log(L_DEBUG, "ap->sta or adhoc->adhoc data frame ignored\n");
		goto done;
	case WF_FC_TODSi:
		break;
	default: /* WF_FC_FROMTODSi */
		log(L_DEBUG, "wds data frame ignored (TODO)\n");
		goto done;
	}

	/* check if it is our BSSID, if not, leave */
	if (!mac_is_equal(adev->bssid, hdr->a1)) {
		goto done;
	}

	if (mac_is_equal(adev->dev_addr, hdr->a3)) {
		/* this one is for us */
		acx_l_rx(adev, rxbuf);
	} else {
		if (mac_is_bcast(hdr->a3)) {
			/* this one is bcast, rx it too */
			acx_l_rx(adev, rxbuf);
		}
		tx = acx_l_alloc_tx(adev);
		if (!tx) {
			goto fail;
		}
		/* repackage, tx, and hope it someday reaches its destination */
		/* order is important, we do it in-place */
		MAC_COPY(hdr->a1, hdr->a3);
		MAC_COPY(hdr->a3, hdr->a2);
		MAC_COPY(hdr->a2, adev->bssid);
		/* To_DS = 0, From_DS = 1 */
		hdr->fc = WF_FC_FROMDSi + WF_FTYPE_DATAi;

		txbuf = acx_l_get_txbuf(adev, tx);
		if (txbuf) {
			len = RXBUF_BYTES_RCVD(adev, rxbuf);
			memcpy(txbuf, hdr, len);
			acx_l_tx_data(adev, tx, len);
		} else {
			acx_l_dealloc_tx(adev, tx);
		}
	}
done:
	result = OK;
fail:
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_l_process_data_frame_client
*/
static int
acx_l_process_data_frame_client(acx_device_t *adev, rxbuffer_t *rxbuf)
{
	const u8 *da, *bssid;
	const wlan_hdr_t *hdr;
	struct net_device *ndev = adev->ndev;
	int result = NOT_OK;

	FN_ENTER;

	if (ACX_STATUS_4_ASSOCIATED != adev->status)
		goto drop;

	hdr = acx_get_wlan_hdr(adev, rxbuf);

	switch (WF_FC_FROMTODSi & hdr->fc) {
	case 0:
		if (adev->mode != ACX_MODE_0_ADHOC) {
			log(L_DEBUG, "adhoc->adhoc data frame ignored\n");
			goto drop;
		}
		bssid = hdr->a3;
		break;
	case WF_FC_FROMDSi:
		if (adev->mode != ACX_MODE_2_STA) {
			log(L_DEBUG, "ap->sta data frame ignored\n");
			goto drop;
		}
		bssid = hdr->a2;
		break;
	case WF_FC_TODSi:
		log(L_DEBUG, "sta->ap data frame ignored\n");
		goto drop;
	default: /* WF_FC_FROMTODSi: wds->wds */
		log(L_DEBUG, "wds data frame ignored (todo)\n");
		goto drop;
	}

	da = hdr->a1;

	if (unlikely(acx_debug & L_DEBUG)) {
		acx_print_mac("rx: da=", da, "");
		acx_print_mac(" bssid=", bssid, "");
		acx_print_mac(" adev->bssid=", adev->bssid, "");
		acx_print_mac(" adev->addr=", adev->dev_addr, "\n");
	}

	/* promiscuous mode --> receive all packets */
	if (unlikely(ndev->flags & IFF_PROMISC))
		goto process;

	/* FIRST, check if it is our BSSID */
	if (!mac_is_equal(adev->bssid, bssid)) {
		/* is not our BSSID, so bail out */
		goto drop;
	}

	/* then, check if it is our address */
	if (mac_is_equal(adev->dev_addr, da)) {
		goto process;
	}

	/* then, check if it is broadcast */
	if (mac_is_bcast(da)) {
		goto process;
	}

	if (mac_is_mcast(da)) {
		/* unconditionally receive all multicasts */
		if (ndev->flags & IFF_ALLMULTI)
			goto process;

		/* FIXME: need to check against the list of
		 * multicast addresses that are configured
		 * for the interface (ifconfig) */
		log(L_XFER, "FIXME: multicast packet, need to check "
			"against a list of multicast addresses "
			"(to be created!); accepting packet for now\n");
		/* for now, just accept it here */
		goto process;
	}

	log(L_DEBUG, "rx: foreign packet, dropping\n");
	goto drop;
process:
	/* receive packet */
	acx_l_rx(adev, rxbuf);

	result = OK;
drop:
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_l_process_mgmt_frame
**
** Theory of operation: mgmt packet gets parsed (to make it easy
** to access variable-sized IEs), results stored in 'parsed'.
** Then we react to the packet.
*/
typedef union parsed_mgmt_req {
	wlan_fr_mgmt_t mgmt;
	wlan_fr_assocreq_t assocreq;
	wlan_fr_reassocreq_t reassocreq;
	wlan_fr_assocresp_t assocresp;
	wlan_fr_reassocresp_t reassocresp;
	wlan_fr_beacon_t beacon;
	wlan_fr_disassoc_t disassoc;
	wlan_fr_authen_t authen;
	wlan_fr_deauthen_t deauthen;
	wlan_fr_proberesp_t proberesp;
} parsed_mgmt_req_t;

void BUG_excessive_stack_usage(void);

static int
acx_l_process_mgmt_frame(acx_device_t *adev, rxbuffer_t *rxbuf)
{
	parsed_mgmt_req_t parsed;	/* takes ~100 bytes of stack */
	wlan_hdr_t *hdr;
	int adhoc, sta_scan, sta, ap;
	int len;

	if (sizeof(parsed) > 256)
		BUG_excessive_stack_usage();

	FN_ENTER;

	hdr = acx_get_wlan_hdr(adev, rxbuf);

	/* Management frames never have these set */
	if (WF_FC_FROMTODSi & hdr->fc) {
		FN_EXIT1(NOT_OK);
		return NOT_OK;
	}

	len = RXBUF_BYTES_RCVD(adev, rxbuf);
	if (WF_FC_ISWEPi & hdr->fc)
		len -= 0x10;

	adhoc = (adev->mode == ACX_MODE_0_ADHOC);
	sta_scan = ((adev->mode == ACX_MODE_2_STA)
		 && (adev->status != ACX_STATUS_4_ASSOCIATED));
	sta = ((adev->mode == ACX_MODE_2_STA)
	    && (adev->status == ACX_STATUS_4_ASSOCIATED));
	ap = (adev->mode == ACX_MODE_3_AP);

	switch (WF_FC_FSTYPEi & hdr->fc) {
	/* beacons first, for speed */
	case WF_FSTYPE_BEACONi:
		memset(&parsed.beacon, 0, sizeof(parsed.beacon));
		parsed.beacon.hdr = hdr;
		parsed.beacon.len = len;
		if (acx_debug & L_DATA) {
			printk("beacon len:%d fc:%04X dur:%04X seq:%04X",
			       len, hdr->fc, hdr->dur, hdr->seq);
			acx_print_mac(" a1:", hdr->a1, "");
			acx_print_mac(" a2:", hdr->a2, "");
			acx_print_mac(" a3:", hdr->a3, "\n");
		}
		wlan_mgmt_decode_beacon(&parsed.beacon);
		/* beacon and probe response are very similar, so... */
		acx_l_process_probe_response(adev, &parsed.beacon, rxbuf);
		break;
	case WF_FSTYPE_ASSOCREQi:
		if (!ap)
			break;
		memset(&parsed.assocreq, 0, sizeof(parsed.assocreq));
		parsed.assocreq.hdr = hdr;
		parsed.assocreq.len = len;
		wlan_mgmt_decode_assocreq(&parsed.assocreq);
		if (mac_is_equal(hdr->a1, adev->bssid)
		 && mac_is_equal(hdr->a3, adev->bssid)) {
			acx_l_transmit_assocresp(adev, &parsed.assocreq);
		}
		break;
	case WF_FSTYPE_REASSOCREQi:
		if (!ap)
			break;
		memset(&parsed.assocreq, 0, sizeof(parsed.assocreq));
		parsed.assocreq.hdr = hdr;
		parsed.assocreq.len = len;
		wlan_mgmt_decode_assocreq(&parsed.assocreq);
		/* reassocreq and assocreq are equivalent */
		acx_l_transmit_reassocresp(adev, &parsed.reassocreq);
		break;
	case WF_FSTYPE_ASSOCRESPi:
		if (!sta_scan)
			break;
		memset(&parsed.assocresp, 0, sizeof(parsed.assocresp));
		parsed.assocresp.hdr = hdr;
		parsed.assocresp.len = len;
		wlan_mgmt_decode_assocresp(&parsed.assocresp);
		acx_l_process_assocresp(adev, &parsed.assocresp);
		break;
	case WF_FSTYPE_REASSOCRESPi:
		if (!sta_scan)
			break;
		memset(&parsed.assocresp, 0, sizeof(parsed.assocresp));
		parsed.assocresp.hdr = hdr;
		parsed.assocresp.len = len;
		wlan_mgmt_decode_assocresp(&parsed.assocresp);
		acx_l_process_reassocresp(adev, &parsed.reassocresp);
		break;
	case WF_FSTYPE_PROBEREQi:
		if (ap || adhoc) {
			/* FIXME: since we're supposed to be an AP,
			** we need to return a Probe Response packet.
			** Currently firmware is doing it for us,
			** but firmware is buggy! See comment elsewhere --vda */
		}
		break;
	case WF_FSTYPE_PROBERESPi:
		memset(&parsed.proberesp, 0, sizeof(parsed.proberesp));
		parsed.proberesp.hdr = hdr;
		parsed.proberesp.len = len;
		wlan_mgmt_decode_proberesp(&parsed.proberesp);
		acx_l_process_probe_response(adev, &parsed.proberesp, rxbuf);
		break;
	case 6:
	case 7:
		/* exit */
		break;
	case WF_FSTYPE_ATIMi:
		/* exit */
		break;
	case WF_FSTYPE_DISASSOCi:
		if (!sta && !ap)
			break;
		memset(&parsed.disassoc, 0, sizeof(parsed.disassoc));
		parsed.disassoc.hdr = hdr;
		parsed.disassoc.len = len;
		wlan_mgmt_decode_disassoc(&parsed.disassoc);
		if (sta)
			acx_l_process_disassoc_from_ap(adev, &parsed.disassoc);
		else
			acx_l_process_disassoc_from_sta(adev, &parsed.disassoc);
		break;
	case WF_FSTYPE_AUTHENi:
		if (!sta_scan && !ap)
			break;
		memset(&parsed.authen, 0, sizeof(parsed.authen));
		parsed.authen.hdr = hdr;
		parsed.authen.len = len;
		wlan_mgmt_decode_authen(&parsed.authen);
		acx_l_process_authen(adev, &parsed.authen);
		break;
	case WF_FSTYPE_DEAUTHENi:
		if (!sta && !ap)
			break;
		memset(&parsed.deauthen, 0, sizeof(parsed.deauthen));
		parsed.deauthen.hdr = hdr;
		parsed.deauthen.len = len;
		wlan_mgmt_decode_deauthen(&parsed.deauthen);
		if (sta)
			acx_l_process_deauth_from_ap(adev, &parsed.deauthen);
		else
			acx_l_process_deauth_from_sta(adev, &parsed.deauthen);
		break;
	}

	FN_EXIT1(OK);
	return OK;
}


#ifdef UNUSED
/***********************************************************************
** acx_process_class_frame
**
** Called from IRQ context only
*/
static int
acx_process_class_frame(acx_device_t *adev, rxbuffer_t *rxbuf, int vala)
{
	return OK;
}
#endif


/***********************************************************************
** acx_l_process_NULL_frame
*/
#ifdef BOGUS_ITS_NOT_A_NULL_FRAME_HANDLER_AT_ALL
static int
acx_l_process_NULL_frame(acx_device_t *adev, rxbuffer_t *rxbuf, int vala)
{
	const signed char *esi;
	const u8 *ebx;
	const wlan_hdr_t *hdr;
	const client_t *client;
	int result = NOT_OK;

	hdr = acx_get_wlan_hdr(adev, rxbuf);

	switch (WF_FC_FROMTODSi & hdr->fc) {
	case 0:
		esi = hdr->a1;
		ebx = hdr->a2;
		break;
	case WF_FC_FROMDSi:
		esi = hdr->a1;
		ebx = hdr->a3;
		break;
	case WF_FC_TODSi:
		esi = hdr->a1;
		ebx = hdr->a2;
		break;
	default: /* WF_FC_FROMTODSi */
		esi = hdr->a1; /* added by me! --vda */
		ebx = hdr->a2;
	}

	if (esi[0x0] < 0) {
		result = OK;
		goto done;
	}

	client = acx_l_sta_list_get(adev, ebx);
	if (client)
		result = NOT_OK;
	else {
#ifdef IS_IT_BROKEN
		log(L_DEBUG|L_XFER, "<transmit_deauth 7>\n");
		acx_l_transmit_deauthen(adev, ebx,
			WLAN_MGMT_REASON_CLASS2_NONAUTH);
#else
		log(L_DEBUG, "received NULL frame from unknown client! "
			"We really shouldn't send deauthen here, right?\n");
#endif
		result = OK;
	}
done:
	return result;
}
#endif


/***********************************************************************
** acx_l_process_probe_response
*/
static int
acx_l_process_probe_response(acx_device_t *adev, wlan_fr_proberesp_t *req,
			const rxbuffer_t *rxbuf)
{
	struct client *bss;
	wlan_hdr_t *hdr;

	FN_ENTER;

	hdr = req->hdr;

	if (mac_is_equal(hdr->a3, adev->dev_addr)) {
		log(L_ASSOC, "huh, scan found our own MAC!?\n");
		goto ok; /* just skip this one silently */
	}

	bss = acx_l_sta_list_get_or_add(adev, hdr->a2);

	/* NB: be careful modifying bss data! It may be one
	** of the already known clients (like our AP if we are a STA)
	** Thus do not blindly modify e.g. current ratemask! */

	if (STA_LIST_ADD_CAN_FAIL && !bss) {
		/* uh oh, we found more sites/stations than we can handle with
		 * our current setup: pull the emergency brake and stop scanning! */
		acx_schedule_task(adev, ACX_AFTER_IRQ_CMD_STOP_SCAN);
		/* TODO: a nice comment what below call achieves --vda */
		acx_set_status(adev, ACX_STATUS_2_WAIT_AUTH);
		goto ok;
	}
	/* NB: get_or_add already filled bss->address = hdr->a2 */
	MAC_COPY(bss->bssid, hdr->a3);

	/* copy the ESSID element */
	if (req->ssid && req->ssid->len <= IW_ESSID_MAX_SIZE) {
		bss->essid_len = req->ssid->len;
		memcpy(bss->essid, req->ssid->ssid, req->ssid->len);
		bss->essid[req->ssid->len] = '\0';
	} else {
		/* Either no ESSID IE or oversized one */
		printk("%s: received packet has bogus ESSID\n",
						    adev->ndev->name);
	}

	if (req->ds_parms)
		bss->channel = req->ds_parms->curr_ch;
	if (req->cap_info)
		bss->cap_info = ieee2host16(*req->cap_info);

	bss->sir = acx_signal_to_winlevel(rxbuf->phy_level);
	bss->snr = acx_signal_to_winlevel(rxbuf->phy_snr);

	bss->rate_cap = 0;	/* operational mask */
	bss->rate_bas = 0;	/* basic mask */
	if (req->supp_rates)
		add_bits_to_ratemasks(req->supp_rates->rates,
			req->supp_rates->len, &bss->rate_bas, &bss->rate_cap);
	if (req->ext_rates)
		add_bits_to_ratemasks(req->ext_rates->rates,
			req->ext_rates->len, &bss->rate_bas, &bss->rate_cap);
	/* Fix up any possible bogosity - code elsewhere
	 * is not expecting empty masks */
	if (!bss->rate_cap)
		bss->rate_cap = adev->rate_basic;
	if (!bss->rate_bas)
		bss->rate_bas = 1 << lowest_bit(bss->rate_cap);
	if (!bss->rate_cur)
		bss->rate_cur = 1 << lowest_bit(bss->rate_bas);

	/* People moan about this being too noisy at L_ASSOC */
	log(L_DEBUG,
		"found %s: ESSID='%s' ch=%d "
		"BSSID="MACSTR" caps=0x%04X SIR=%d SNR=%d\n",
		(bss->cap_info & WF_MGMT_CAP_IBSS) ? "Ad-Hoc peer" : "AP",
		bss->essid, bss->channel, MAC(bss->bssid), bss->cap_info,
		bss->sir, bss->snr);
ok:
	FN_EXIT0;
	return OK;
}


/***********************************************************************
** acx_l_process_assocresp
*/
static int
acx_l_process_assocresp(acx_device_t *adev, const wlan_fr_assocresp_t *req)
{
	const wlan_hdr_t *hdr;
	int res = OK;

	FN_ENTER;

	hdr = req->hdr;

	if ((ACX_MODE_2_STA == adev->mode)
	 && mac_is_equal(adev->dev_addr, hdr->a1)) {
		u16 st = ieee2host16(*(req->status));
		if (WLAN_MGMT_STATUS_SUCCESS == st) {
			adev->aid = ieee2host16(*(req->aid));
			/* tell the card we are associated when
			** we are out of interrupt context */
			acx_schedule_task(adev, ACX_AFTER_IRQ_CMD_ASSOCIATE);
		} else {

			/* TODO: we shall delete peer from sta_list, and try
			** other candidates... */

			printk("%s: association FAILED: peer sent "
				"response code %d (%s)\n",
				adev->ndev->name, st, get_status_string(st));
			res = NOT_OK;
		}
	}

	FN_EXIT1(res);
	return res;
}


/***********************************************************************
** acx_l_process_reassocresp
*/
static int
acx_l_process_reassocresp(acx_device_t *adev, const wlan_fr_reassocresp_t *req)
{
	const wlan_hdr_t *hdr;
	int result = NOT_OK;
	u16 st;

	FN_ENTER;

	hdr = req->hdr;

	if (!mac_is_equal(adev->dev_addr, hdr->a1)) {
		goto end;
	}
	st = ieee2host16(*(req->status));
	if (st == WLAN_MGMT_STATUS_SUCCESS) {
		acx_set_status(adev, ACX_STATUS_4_ASSOCIATED);
		result = OK;
	} else {
		printk("%s: reassociation FAILED: peer sent "
			"response code %d (%s)\n",
			adev->ndev->name, st, get_status_string(st));
	}
end:
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_l_process_authen
**
** Called only in STA_SCAN or AP mode
*/
static int
acx_l_process_authen(acx_device_t *adev, const wlan_fr_authen_t *req)
{
	const wlan_hdr_t *hdr;
	client_t *clt;
	wlan_ie_challenge_t *chal;
	u16 alg, seq, status;
	int ap, result;

	FN_ENTER;

	hdr = req->hdr;

	if (acx_debug & L_ASSOC) {
		acx_print_mac("AUTHEN adev->addr=", adev->dev_addr, " ");
		acx_print_mac("a1=", hdr->a1, " ");
		acx_print_mac("a2=", hdr->a2, " ");
		acx_print_mac("a3=", hdr->a3, " ");
		acx_print_mac("adev->bssid=", adev->bssid, "\n");
	}

	if (!mac_is_equal(adev->dev_addr, hdr->a1)
	 || !mac_is_equal(adev->bssid, hdr->a3)) {
		result = OK;
		goto end;
	}

	alg = ieee2host16(*(req->auth_alg));
	seq = ieee2host16(*(req->auth_seq));
	status = ieee2host16(*(req->status));

	ap = (adev->mode == ACX_MODE_3_AP);

	if (adev->auth_alg <= 1) {
		if (adev->auth_alg != alg) {
			log(L_ASSOC, "auth algorithm mismatch: "
				"our:%d peer:%d\n", adev->auth_alg, alg);
			result = NOT_OK;
			goto end;
		}
	}
	log(L_ASSOC, "algorithm is ok\n");

	if (ap) {
		clt = acx_l_sta_list_get_or_add(adev, hdr->a2);
		if (STA_LIST_ADD_CAN_FAIL && !clt) {
			log(L_ASSOC, "could not allocate room for client\n");
			result = NOT_OK;
			goto end;
		}
	} else {
		clt = adev->ap_client;
		if (!mac_is_equal(clt->address, hdr->a2)) {
			printk("%s: malformed auth frame from AP?!\n",
					adev->ndev->name);
			result = NOT_OK;
			goto end;
		}
	}

	/* now check which step in the authentication sequence we are
	 * currently in, and act accordingly */
	log(L_ASSOC, "acx_process_authen auth seq step %d\n", seq);
	switch (seq) {
	case 1:
		if (!ap)
			break;
		acx_l_transmit_authen2(adev, req, clt);
		break;
	case 2:
		if (ap)
			break;
		if (status == WLAN_MGMT_STATUS_SUCCESS) {
			if (alg == WLAN_AUTH_ALG_OPENSYSTEM) {
				acx_set_status(adev, ACX_STATUS_3_AUTHENTICATED);
				acx_l_transmit_assoc_req(adev);
			} else
			if (alg == WLAN_AUTH_ALG_SHAREDKEY) {
				acx_l_transmit_authen3(adev, req);
			}
		} else {
			printk("%s: auth FAILED: peer sent "
				"response code %d (%s), "
				"still waiting for authentication\n",
				adev->ndev->name,
				status,	get_status_string(status));
			acx_set_status(adev, ACX_STATUS_2_WAIT_AUTH);
		}
		break;
	case 3:
		if (!ap)
			break;
		if ((clt->auth_alg != WLAN_AUTH_ALG_SHAREDKEY)
		 || (alg != WLAN_AUTH_ALG_SHAREDKEY)
		 || (clt->auth_step != 2))
			break;
		chal = req->challenge;
		if (!chal
		 || memcmp(chal->challenge, clt->challenge_text, WLAN_CHALLENGE_LEN)
		 || (chal->eid != WLAN_EID_CHALLENGE)
		 || (chal->len != WLAN_CHALLENGE_LEN)
		)
			break;
		acx_l_transmit_authen4(adev, req);
		MAC_COPY(clt->address, hdr->a2);
		clt->used = CLIENT_AUTHENTICATED_2;
		clt->auth_step = 4;
		clt->seq = ieee2host16(hdr->seq);
		break;
	case 4:
		if (ap)
			break;
		/* ok, we're through: we're authenticated. Woohoo!! */
		acx_set_status(adev, ACX_STATUS_3_AUTHENTICATED);
		log(L_ASSOC, "Authenticated!\n");
		/* now that we're authenticated, request association */
		acx_l_transmit_assoc_req(adev);
		break;
	}
	result = NOT_OK;
end:
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_gen_challenge
*/
static inline void
acx_gen_challenge(wlan_ie_challenge_t* d)
{
	FN_ENTER;
	d->eid = WLAN_EID_CHALLENGE;
	d->len = WLAN_CHALLENGE_LEN;
	get_random_bytes(d->challenge, WLAN_CHALLENGE_LEN);
	FN_EXIT0;
}


/***********************************************************************
** acx_l_transmit_deauthen
*/
static int
acx_l_transmit_deauthen(acx_device_t *adev, const u8 *addr, u16 reason)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct deauthen_frame_body *body;

	FN_ENTER;

	tx = acx_l_alloc_tx(adev);
	if (!tx)
		goto bad;
	head = acx_l_get_txbuf(adev, tx);
	if (!head) {
		acx_l_dealloc_tx(adev, tx);
		goto bad;
	}
	body = (void*)(head + 1);

	head->fc = (WF_FTYPE_MGMTi | WF_FSTYPE_DEAUTHENi);
	head->dur = 0;
	MAC_COPY(head->da, addr);
	MAC_COPY(head->sa, adev->dev_addr);
	MAC_COPY(head->bssid, adev->bssid);
	head->seq = 0;

	log(L_DEBUG|L_ASSOC|L_XFER,
		"sending deauthen to "MACSTR" for %d\n",
		MAC(addr), reason);

	body->reason = host2ieee16(reason);

	/* body is fixed size here, but beware of cutting-and-pasting this -
	** do not use sizeof(*body) for variable sized mgmt packets! */
	acx_l_tx_data(adev, tx, WLAN_HDR_A3_LEN + sizeof(*body));

	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/***********************************************************************
** acx_l_transmit_authen1
*/
static int
acx_l_transmit_authen1(acx_device_t *adev)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct auth_frame_body *body;

	FN_ENTER;

	log(L_ASSOC, "sending authentication1 request, "
		"awaiting response\n");

	tx = acx_l_alloc_tx(adev);
	if (!tx)
		goto bad;
	head = acx_l_get_txbuf(adev, tx);
	if (!head) {
		acx_l_dealloc_tx(adev, tx);
		goto bad;
	}
	body = (void*)(head + 1);

	head->fc = WF_FSTYPE_AUTHENi;
	head->dur = host2ieee16(0x8000);
	MAC_COPY(head->da, adev->bssid);
	MAC_COPY(head->sa, adev->dev_addr);
	MAC_COPY(head->bssid, adev->bssid);
	head->seq = 0;

	body->auth_alg = host2ieee16(adev->auth_alg);
	body->auth_seq = host2ieee16(1);
	body->status = host2ieee16(0);

	acx_l_tx_data(adev, tx, WLAN_HDR_A3_LEN + 2 + 2 + 2);

	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/***********************************************************************
** acx_l_transmit_authen2
*/
static int
acx_l_transmit_authen2(acx_device_t *adev, const wlan_fr_authen_t *req,
		      client_t *clt)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct auth_frame_body *body;
	unsigned int packet_len;

	FN_ENTER;

	if (!clt)
		goto ok;

	MAC_COPY(clt->address, req->hdr->a2);
#ifdef UNUSED
	clt->ps = ((WF_FC_PWRMGTi & req->hdr->fc) != 0);
#endif
	clt->auth_alg = ieee2host16(*(req->auth_alg));
	clt->auth_step = 2;
	clt->seq = ieee2host16(req->hdr->seq);

	tx = acx_l_alloc_tx(adev);
	if (!tx)
		goto bad;
	head = acx_l_get_txbuf(adev, tx);
	if (!head) {
		acx_l_dealloc_tx(adev, tx);
		goto bad;
	}
	body = (void*)(head + 1);

	head->fc = WF_FSTYPE_AUTHENi;
	head->dur = req->hdr->dur;
	MAC_COPY(head->da, req->hdr->a2);
	MAC_COPY(head->sa, adev->dev_addr);
	MAC_COPY(head->bssid, req->hdr->a3);
	head->seq = req->hdr->seq;

	/* already in IEEE format, no endianness conversion */
	body->auth_alg = *(req->auth_alg);
	body->auth_seq = host2ieee16(2);
	body->status = host2ieee16(0);

	packet_len = WLAN_HDR_A3_LEN + 2 + 2 + 2;
	if (ieee2host16(*(req->auth_alg)) == WLAN_AUTH_ALG_OPENSYSTEM) {
		clt->used = CLIENT_AUTHENTICATED_2;
	} else {	/* shared key */
		acx_gen_challenge(&body->challenge);
		memcpy(&clt->challenge_text, body->challenge.challenge, WLAN_CHALLENGE_LEN);
		packet_len += 2 + 2 + 2 + 1+1+WLAN_CHALLENGE_LEN;
	}

	acxlog_mac(L_ASSOC|L_XFER,
		"transmit_auth2: BSSID=", head->bssid, "\n");

	acx_l_tx_data(adev, tx, packet_len);
ok:
	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/***********************************************************************
** acx_l_transmit_authen3
*/
static int
acx_l_transmit_authen3(acx_device_t *adev, const wlan_fr_authen_t *req)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct auth_frame_body *body;
	unsigned int packet_len;

	FN_ENTER;

	tx = acx_l_alloc_tx(adev);
	if (!tx)
		goto ok;
	head = acx_l_get_txbuf(adev, tx);
	if (!head) {
		acx_l_dealloc_tx(adev, tx);
		goto ok;
	}
	body = (void*)(head + 1);

	head->fc = WF_FC_ISWEPi + WF_FSTYPE_AUTHENi;
	/* FIXME: is this needed?? authen4 does it...
	head->dur = req->hdr->dur;
	head->seq = req->hdr->seq;
	*/
	MAC_COPY(head->da, adev->bssid);
	MAC_COPY(head->sa, adev->dev_addr);
	MAC_COPY(head->bssid, adev->bssid);

	/* already in IEEE format, no endianness conversion */
	body->auth_alg = *(req->auth_alg);
	body->auth_seq = host2ieee16(3);
	body->status = host2ieee16(0);
	memcpy(&body->challenge, req->challenge, req->challenge->len + 2);
	packet_len = WLAN_HDR_A3_LEN + 8 + req->challenge->len;

	log(L_ASSOC|L_XFER, "transmit_authen3!\n");

	acx_l_tx_data(adev, tx, packet_len);
ok:
	FN_EXIT1(OK);
	return OK;
}


/***********************************************************************
** acx_l_transmit_authen4
*/
static int
acx_l_transmit_authen4(acx_device_t *adev, const wlan_fr_authen_t *req)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct auth_frame_body *body;

	FN_ENTER;

	tx = acx_l_alloc_tx(adev);
	if (!tx)
		goto ok;
	head = acx_l_get_txbuf(adev, tx);
	if (!head) {
		acx_l_dealloc_tx(adev, tx);
		goto ok;
	}
	body = (void*)(head + 1);

	head->fc = WF_FSTYPE_AUTHENi; /* 0xb0 */
	head->dur = req->hdr->dur;
	MAC_COPY(head->da, req->hdr->a2);
	MAC_COPY(head->sa, adev->dev_addr);
	MAC_COPY(head->bssid, req->hdr->a3);
	head->seq = req->hdr->seq;

	/* already in IEEE format, no endianness conversion */
	body->auth_alg = *(req->auth_alg);
	body->auth_seq = host2ieee16(4);
	body->status = host2ieee16(0);

	acx_l_tx_data(adev, tx, WLAN_HDR_A3_LEN + 2 + 2 + 2);
ok:
	FN_EXIT1(OK);
	return OK;
}


/***********************************************************************
** acx_l_transmit_assoc_req
**
** adev->ap_client is a current candidate AP here
*/
static int
acx_l_transmit_assoc_req(acx_device_t *adev)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	u8 *body, *p, *prate;
	unsigned int packet_len;
	u16 cap;

	FN_ENTER;

	log(L_ASSOC, "sending association request, "
			"awaiting response. NOT ASSOCIATED YET\n");
	tx = acx_l_alloc_tx(adev);
	if (!tx)
		goto bad;
	head = acx_l_get_txbuf(adev, tx);
	if (!head) {
		acx_l_dealloc_tx(adev, tx);
		goto bad;
	}
	body = (void*)(head + 1);

	head->fc = WF_FSTYPE_ASSOCREQi;
	head->dur = host2ieee16(0x8000);
	MAC_COPY(head->da, adev->bssid);
	MAC_COPY(head->sa, adev->dev_addr);
	MAC_COPY(head->bssid, adev->bssid);
	head->seq = 0;

	p = body;
	/* now start filling the AssocReq frame body */

	/* since this assoc request will most likely only get
	 * sent in the STA to AP case (and not when Ad-Hoc IBSS),
	 * the cap combination indicated here will thus be
	 * WF_MGMT_CAP_ESSi *always* (no IBSS ever)
	 * The specs are more than non-obvious on all that:
	 *
	 * 802.11 7.3.1.4 Capability Information field
	** APs set the ESS subfield to 1 and the IBSS subfield to 0 within
	** Beacon or Probe Response management frames. STAs within an IBSS
	** set the ESS subfield to 0 and the IBSS subfield to 1 in transmitted
	** Beacon or Probe Response management frames
	**
	** APs set the Privacy subfield to 1 within transmitted Beacon,
	** Probe Response, Association Response, and Reassociation Response
	** if WEP is required for all data type frames within the BSS.
	** STAs within an IBSS set the Privacy subfield to 1 in Beacon
	** or Probe Response management frames if WEP is required
	** for all data type frames within the IBSS */

	/* note that returning 0 will be refused by several APs...
	 * (so this indicates that you're probably supposed to
	 * "confirm" the ESS mode) */
	cap = WF_MGMT_CAP_ESSi;

	/* this one used to be a check on wep_restricted,
	 * but more likely it's wep_enabled instead */
	if (adev->wep_enabled)
		SET_BIT(cap, WF_MGMT_CAP_PRIVACYi);

	/* Probably we can just set these always, because our hw is
	** capable of shortpre and PBCC --vda */
	/* only ask for short preamble if the peer station supports it */
	if (adev->ap_client->cap_info & WF_MGMT_CAP_SHORT)
		SET_BIT(cap, WF_MGMT_CAP_SHORTi);
	/* only ask for PBCC support if the peer station supports it */
	if (adev->ap_client->cap_info & WF_MGMT_CAP_PBCC)
		SET_BIT(cap, WF_MGMT_CAP_PBCCi);

	/* IEs: 1. caps */
	*(u16*)p = cap;	p += 2;
	/* 2. listen interval */
	*(u16*)p = host2ieee16(adev->listen_interval); p += 2;
	/* 3. ESSID */
	p = wlan_fill_ie_ssid(p,
			strlen(adev->essid_for_assoc), adev->essid_for_assoc);
	/* 4. supp rates */
	prate = p;
	p = wlan_fill_ie_rates(p,
			adev->rate_supported_len, adev->rate_supported);
	/* 5. ext supp rates */
	p = wlan_fill_ie_rates_ext(p,
			adev->rate_supported_len, adev->rate_supported);

	if (acx_debug & L_DEBUG) {
		printk("association: rates element\n");
		acx_dump_bytes(prate, p - prate);
	}

	/* calculate lengths */
	packet_len = WLAN_HDR_A3_LEN + (p - body);

	log(L_ASSOC, "association: requesting caps 0x%04X, ESSID '%s'\n",
		cap, adev->essid_for_assoc);

	acx_l_tx_data(adev, tx, packet_len);
	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}


/***********************************************************************
** acx_l_transmit_disassoc
**
** FIXME: looks like incomplete implementation of a helper:
** acx_l_transmit_disassoc(adev, clt) - kick this client (we're an AP)
** acx_l_transmit_disassoc(adev, NULL) - leave BSSID (we're a STA)
*/
#ifdef BROKEN
int
acx_l_transmit_disassoc(acx_device_t *adev, client_t *clt)
{
	struct tx *tx;
	struct wlan_hdr_mgmt *head;
	struct disassoc_frame_body *body;

	FN_ENTER;
/*	if (clt != NULL) { */
		tx = acx_l_alloc_tx(adev);
		if (!tx)
			goto bad;
		head = acx_l_get_txbuf(adev, tx);
		if (!head) {
			acx_l_dealloc_tx(adev, tx);
			goto bad;
		}
		body = (void*)(head + 1);

/*		clt->used = CLIENT_AUTHENTICATED_2; - not (yet?) associated */

		head->fc = WF_FSTYPE_DISASSOCi;
		head->dur = 0;
		/* huh? It muchly depends on whether we're STA or AP...
		** sta->ap: da=bssid, sa=own, bssid=bssid
		** ap->sta: da=sta, sa=bssid, bssid=bssid. FIXME! */
		MAC_COPY(head->da, adev->bssid);
		MAC_COPY(head->sa, adev->dev_addr);
		MAC_COPY(head->bssid, adev->dev_addr);
		head->seq = 0;

		/* "Class 3 frame received from nonassociated station." */
		body->reason = host2ieee16(7);

		/* fixed size struct, ok to sizeof */
		acx_l_tx_data(adev, tx, WLAN_HDR_A3_LEN + sizeof(*body));
/*	} */
	FN_EXIT1(OK);
	return OK;
bad:
	FN_EXIT1(NOT_OK);
	return NOT_OK;
}
#endif


/***********************************************************************
** acx_s_complete_scan
**
** Called either from after_interrupt_task() if:
** 1) there was Scan_Complete IRQ, or
** 2) scanning expired in timer()
** We need to decide which ESS or IBSS to join.
** Iterates thru adev->sta_list:
**	if adev->ap is not bcast, will join only specified
**	ESS or IBSS with this bssid
**	checks peers' caps for ESS/IBSS bit
**	checks peers' SSID, allows exact match or hidden SSID
** If station to join is chosen:
**	points adev->ap_client to the chosen struct client
**	sets adev->essid_for_assoc for future assoc attempt
** Auth/assoc is not yet performed
** Returns OK if there is no need to restart scan
*/
int
acx_s_complete_scan(acx_device_t *adev)
{
	struct client *bss;
	unsigned long flags;
	u16 needed_cap;
	int i;
	int idx_found = -1;
	int result = OK;

	FN_ENTER;

	switch (adev->mode) {
	case ACX_MODE_0_ADHOC:
		needed_cap = WF_MGMT_CAP_IBSS; /* 2, we require Ad-Hoc */
		break;
	case ACX_MODE_2_STA:
		needed_cap = WF_MGMT_CAP_ESS; /* 1, we require Managed */
		break;
	default:
		printk("acx: driver bug: mode=%d in complete_scan()\n", adev->mode);
		dump_stack();
		goto end;
	}

	acx_lock(adev, flags);

	/* TODO: sta_iterator hiding implementation would be nice here... */

	for (i = 0; i < VEC_SIZE(adev->sta_list); i++) {
		bss = &adev->sta_list[i];
		if (!bss->used) continue;

		log(L_ASSOC, "scan table: SSID='%s' CH=%d SIR=%d SNR=%d\n",
			bss->essid, bss->channel, bss->sir, bss->snr);

		if (!mac_is_bcast(adev->ap))
			if (!mac_is_equal(bss->bssid, adev->ap))
				continue; /* keep looking */

		/* broken peer with no mode flags set? */
		if (unlikely(!(bss->cap_info & (WF_MGMT_CAP_ESS | WF_MGMT_CAP_IBSS)))) {
			printk("%s: strange peer "MACSTR" found with "
				"neither ESS (AP) nor IBSS (Ad-Hoc) "
				"capability - skipped\n",
				adev->ndev->name, MAC(bss->address));
			continue;
		}
		log(L_ASSOC, "peer_cap 0x%04X, needed_cap 0x%04X\n",
		       bss->cap_info, needed_cap);

		/* does peer station support what we need? */
		if ((bss->cap_info & needed_cap) != needed_cap)
			continue; /* keep looking */

		/* strange peer with NO basic rates?! */
		if (unlikely(!bss->rate_bas)) {
			printk("%s: strange peer "MACSTR" with empty rate set "
				"- skipped\n",
				adev->ndev->name, MAC(bss->address));
			continue;
		}

		/* do we support all basic rates of this peer? */
		if ((bss->rate_bas & adev->rate_oper) != bss->rate_bas)	{
/* we probably need to have all rates as operational rates,
   even in case of an 11M-only configuration */
#ifdef THIS_IS_TROUBLESOME
			printk("%s: peer "MACSTR": incompatible basic rates "
				"(AP requests 0x%04X, we have 0x%04X) "
				"- skipped\n",
				adev->ndev->name, MAC(bss->address),
				bss->rate_bas, adev->rate_oper);
			continue;
#else
			printk("%s: peer "MACSTR": incompatible basic rates "
				"(AP requests 0x%04X, we have 0x%04X). "
				"Considering anyway...\n",
				adev->ndev->name, MAC(bss->address),
				bss->rate_bas, adev->rate_oper);
#endif
		}

		if ( !(adev->reg_dom_chanmask & (1<<(bss->channel-1))) ) {
			printk("%s: warning: peer "MACSTR" is on channel %d "
				"outside of channel range of current "
				"regulatory domain - couldn't join "
				"even if other settings match. "
				"You might want to adapt your config\n",
				adev->ndev->name, MAC(bss->address),
				bss->channel);
			continue; /* keep looking */
		}

		if (!adev->essid_active || !strcmp(bss->essid, adev->essid)) {
			log(L_ASSOC,
			       "found station with matching ESSID! ('%s' "
			       "station, '%s' config)\n",
			       bss->essid,
			       (adev->essid_active) ? adev->essid : "[any]");
			/* TODO: continue looking for peer with better SNR */
			bss->used = CLIENT_JOIN_CANDIDATE;
			idx_found = i;

			/* stop searching if this station is
			 * on the current channel, otherwise
			 * keep looking for an even better match */
			if (bss->channel == adev->channel)
				break;
		} else
		if (!bss->essid[0]
		 || ((' ' == bss->essid[0]) && !bss->essid[1])
		) {
			/* hmm, station with empty or single-space SSID:
			 * using hidden SSID broadcast?
			 */
			/* This behaviour is broken: which AP from zillion
			** of APs with hidden SSID you'd try?
			** We should use Probe requests to get Probe responses
			** and check for real SSID (are those never hidden?) */
			bss->used = CLIENT_JOIN_CANDIDATE;
			if (idx_found == -1)
				idx_found = i;
			log(L_ASSOC, "found station with empty or "
				"single-space (hidden) SSID, considering "
				"for assoc attempt\n");
			/* ...and keep looking for better matches */
		} else {
			log(L_ASSOC, "ESSID doesn't match! ('%s' "
				"station, '%s' config)\n",
				bss->essid,
				(adev->essid_active) ? adev->essid : "[any]");
		}
	}

	/* TODO: iterate thru join candidates instead */
	/* TODO: rescan if not associated within some timeout */
	if (idx_found != -1) {
		char *essid_src;
		size_t essid_len;

		bss = &adev->sta_list[idx_found];
		adev->ap_client = bss;

		if (bss->essid[0] == '\0') {
			/* if the ESSID of the station we found is empty
			 * (no broadcast), then use user-configured ESSID
			 * instead */
			essid_src = adev->essid;
			essid_len = adev->essid_len;
		} else {
			essid_src = bss->essid;
			essid_len = strlen(bss->essid);
		}

		acx_update_capabilities(adev);

		memcpy(adev->essid_for_assoc, essid_src, essid_len);
		adev->essid_for_assoc[essid_len] = '\0';
		adev->channel = bss->channel;
		MAC_COPY(adev->bssid, bss->bssid);

		bss->rate_cfg = (bss->rate_cap & adev->rate_oper);
		bss->rate_cur = 1 << lowest_bit(bss->rate_cfg);
		bss->rate_100 = acx_rate111to100(bss->rate_cur);

		acxlog_mac(L_ASSOC,
			"matching station found: ", adev->bssid, ", joining\n");

		/* TODO: do we need to switch to the peer's channel first? */

		if (ACX_MODE_0_ADHOC == adev->mode) {
			acx_set_status(adev, ACX_STATUS_4_ASSOCIATED);
		} else {
			acx_l_transmit_authen1(adev);
			acx_set_status(adev, ACX_STATUS_2_WAIT_AUTH);
		}
	} else { /* idx_found == -1 */
		/* uh oh, no station found in range */
		if (ACX_MODE_0_ADHOC == adev->mode) {
			printk("%s: no matching station found in range, "
				"generating our own IBSS instead\n",
				adev->ndev->name);
			/* we do it the HostAP way: */
			MAC_COPY(adev->bssid, adev->dev_addr);
			adev->bssid[0] |= 0x02; /* 'local assigned addr' bit */
			/* add IBSS bit to our caps... */
			acx_update_capabilities(adev);
			acx_set_status(adev, ACX_STATUS_4_ASSOCIATED);
			/* In order to cmd_join be called below */
			idx_found = 0;
		} else {
			/* we shall scan again, AP can be
			** just temporarily powered off */
			log(L_ASSOC,
				"no matching station found in range yet\n");
			acx_set_status(adev, ACX_STATUS_1_SCANNING);
			result = NOT_OK;
		}
	}

	acx_unlock(adev, flags);

	if (idx_found != -1) {
		if (ACX_MODE_0_ADHOC == adev->mode) {
			/* need to update channel in beacon template */
			SET_BIT(adev->set_mask, SET_TEMPLATES);
			if (ACX_STATE_IFACE_UP & adev->dev_state_mask)
				acx_s_update_card_settings(adev);
		}
		/* Inform firmware on our decision to start or join BSS */
		acx_s_cmd_join_bssid(adev, adev->bssid);
	}

end:
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_s_read_fw
**
** Loads a firmware image
**
** Returns:
**  0				unable to load file
**  pointer to firmware		success
*/
firmware_image_t*
acx_s_read_fw(struct device *dev, const char *file, u32 *size)
{
	firmware_image_t *res;
	const struct firmware *fw_entry;

	res = NULL;
	log(L_INIT, "requesting firmware image '%s'\n", file);
	if (!request_firmware(&fw_entry, file, dev)) {
		*size = 8;
		if (fw_entry->size >= 8)
			*size = 8 + le32_to_cpu(*(u32 *)(fw_entry->data + 4));
		if (fw_entry->size != *size) {
			printk("acx: firmware size does not match "
				"firmware header: %d != %d, "
				"aborting fw upload\n",
				(int) fw_entry->size, (int) *size);
			goto release_ret;
		}
		res = vmalloc(*size);
		if (!res) {
			printk("acx: no memory for firmware "
				"(%u bytes)\n", *size);
			goto release_ret;
		}
		memcpy(res, fw_entry->data, fw_entry->size);
release_ret:
		release_firmware(fw_entry);
		return res;
	}
	printk("acx: firmware image '%s' was not provided. "
		"Check your hotplug scripts\n", file);

	/* checksum will be verified in write_fw, so don't bother here */
	return res;
}


/***********************************************************************
** acx_s_set_wepkey
*/
static void
acx100_s_set_wepkey(acx_device_t *adev)
{
	ie_dot11WEPDefaultKey_t dk;
	int i;

	for (i = 0; i < DOT11_MAX_DEFAULT_WEP_KEYS; i++) {
		if (adev->wep_keys[i].size != 0) {
			log(L_INIT, "setting WEP key: %d with "
				"total size: %d\n", i, (int) adev->wep_keys[i].size);
			dk.action = 1;
			dk.keySize = adev->wep_keys[i].size;
			dk.defaultKeyNum = i;
			memcpy(dk.key, adev->wep_keys[i].key, dk.keySize);
			acx_s_configure(adev, &dk, ACX100_IE_DOT11_WEP_DEFAULT_KEY_WRITE);
		}
	}
}

static void
acx111_s_set_wepkey(acx_device_t *adev)
{
	acx111WEPDefaultKey_t dk;
	int i;

	for (i = 0; i < DOT11_MAX_DEFAULT_WEP_KEYS; i++) {
		if (adev->wep_keys[i].size != 0) {
			log(L_INIT, "setting WEP key: %d with "
				"total size: %d\n", i, (int) adev->wep_keys[i].size);
			memset(&dk, 0, sizeof(dk));
			dk.action = cpu_to_le16(1); /* "add key"; yes, that's a 16bit value */
			dk.keySize = adev->wep_keys[i].size;

			/* are these two lines necessary? */
			dk.type = 0;              /* default WEP key */
			dk.index = 0;             /* ignored when setting default key */

			dk.defaultKeyNum = i;
			memcpy(dk.key, adev->wep_keys[i].key, dk.keySize);
			acx_s_issue_cmd(adev, ACX1xx_CMD_WEP_MGMT, &dk, sizeof(dk));
		}
	}
}

static void
acx_s_set_wepkey(acx_device_t *adev)
{
	if (IS_ACX111(adev))
		acx111_s_set_wepkey(adev);
	else
		acx100_s_set_wepkey(adev);
}


/***********************************************************************
** acx100_s_init_wep
**
** FIXME: this should probably be moved into the new card settings
** management, but since we're also modifying the memory map layout here
** due to the WEP key space we want, we should take care...
*/
static int
acx100_s_init_wep(acx_device_t *adev)
{
	acx100_ie_wep_options_t options;
	ie_dot11WEPDefaultKeyID_t dk;
	acx_ie_memmap_t pt;
	int res = NOT_OK;

	FN_ENTER;

	if (OK != acx_s_interrogate(adev, &pt, ACX1xx_IE_MEMORY_MAP)) {
		goto fail;
	}

	log(L_DEBUG, "CodeEnd:%X\n", pt.CodeEnd);

	pt.WEPCacheStart = cpu_to_le32(le32_to_cpu(pt.CodeEnd) + 0x4);
	pt.WEPCacheEnd   = cpu_to_le32(le32_to_cpu(pt.CodeEnd) + 0x4);

	if (OK != acx_s_configure(adev, &pt, ACX1xx_IE_MEMORY_MAP)) {
		goto fail;
	}

	/* let's choose maximum setting: 4 default keys, plus 10 other keys: */
	options.NumKeys = cpu_to_le16(DOT11_MAX_DEFAULT_WEP_KEYS + 10);
	options.WEPOption = 0x00;

	log(L_ASSOC, "%s: writing WEP options\n", __func__);
	acx_s_configure(adev, &options, ACX100_IE_WEP_OPTIONS);

	acx100_s_set_wepkey(adev);

	if (adev->wep_keys[adev->wep_current_index].size != 0) {
		log(L_ASSOC, "setting active default WEP key number: %d\n",
				adev->wep_current_index);
		dk.KeyID = adev->wep_current_index;
		acx_s_configure(adev, &dk, ACX1xx_IE_DOT11_WEP_DEFAULT_KEY_SET); /* 0x1010 */
	}
	/* FIXME!!! wep_key_struct is filled nowhere! But adev
	 * is initialized to 0, and we don't REALLY need those keys either */
/*		for (i = 0; i < 10; i++) {
		if (adev->wep_key_struct[i].len != 0) {
			MAC_COPY(wep_mgmt.MacAddr, adev->wep_key_struct[i].addr);
			wep_mgmt.KeySize = cpu_to_le16(adev->wep_key_struct[i].len);
			memcpy(&wep_mgmt.Key, adev->wep_key_struct[i].key, le16_to_cpu(wep_mgmt.KeySize));
			wep_mgmt.Action = cpu_to_le16(1);
			log(L_ASSOC, "writing WEP key %d (len %d)\n", i, le16_to_cpu(wep_mgmt.KeySize));
			if (OK == acx_s_issue_cmd(adev, ACX1xx_CMD_WEP_MGMT, &wep_mgmt, sizeof(wep_mgmt))) {
				adev->wep_key_struct[i].index = i;
			}
		}
	}
*/

	/* now retrieve the updated WEPCacheEnd pointer... */
	if (OK != acx_s_interrogate(adev, &pt, ACX1xx_IE_MEMORY_MAP)) {
		printk("%s: ACX1xx_IE_MEMORY_MAP read #2 FAILED\n",
				adev->ndev->name);
		goto fail;
	}
	/* ...and tell it to start allocating templates at that location */
	/* (no endianness conversion needed) */
	pt.PacketTemplateStart = pt.WEPCacheEnd;

	if (OK != acx_s_configure(adev, &pt, ACX1xx_IE_MEMORY_MAP)) {
		printk("%s: ACX1xx_IE_MEMORY_MAP write #2 FAILED\n",
				adev->ndev->name);
		goto fail;
	}
	res = OK;

fail:
	FN_EXIT1(res);
	return res;
}


static int
acx_s_init_max_template_generic(acx_device_t *adev, unsigned int len, unsigned int cmd)
{
	int res;
	union {
		acx_template_nullframe_t null;
		acx_template_beacon_t b;
		acx_template_tim_t tim;
		acx_template_probereq_t preq;
		acx_template_proberesp_t presp;
	} templ;

	memset(&templ, 0, len);
	templ.null.size = cpu_to_le16(len - 2);
	res = acx_s_issue_cmd(adev, cmd, &templ, len);
	return res;
}

static inline int
acx_s_init_max_null_data_template(acx_device_t *adev)
{
	return acx_s_init_max_template_generic(
		adev, sizeof(acx_template_nullframe_t), ACX1xx_CMD_CONFIG_NULL_DATA
	);
}

static inline int
acx_s_init_max_beacon_template(acx_device_t *adev)
{
	return acx_s_init_max_template_generic(
		adev, sizeof(acx_template_beacon_t), ACX1xx_CMD_CONFIG_BEACON
	);
}

static inline int
acx_s_init_max_tim_template(acx_device_t *adev)
{
	return acx_s_init_max_template_generic(
		adev, sizeof(acx_template_tim_t), ACX1xx_CMD_CONFIG_TIM
	);
}

static inline int
acx_s_init_max_probe_response_template(acx_device_t *adev)
{
	return acx_s_init_max_template_generic(
		adev, sizeof(acx_template_proberesp_t), ACX1xx_CMD_CONFIG_PROBE_RESPONSE
	);
}

static inline int
acx_s_init_max_probe_request_template(acx_device_t *adev)
{
	return acx_s_init_max_template_generic(
		adev, sizeof(acx_template_probereq_t), ACX1xx_CMD_CONFIG_PROBE_REQUEST
	);
}

/***********************************************************************
** acx_s_set_tim_template
**
** FIXME: In full blown driver we will regularly update partial virtual bitmap
** by calling this function
** (it can be done by irq handler on each DTIM irq or by timer...)

[802.11 7.3.2.6] TIM information element:
- 1 EID
- 1 Length
1 1 DTIM Count
    indicates how many beacons (including this) appear before next DTIM
    (0=this one is a DTIM)
2 1 DTIM Period
    number of beacons between successive DTIMs
    (0=reserved, 1=all TIMs are DTIMs, 2=every other, etc)
3 1 Bitmap Control
    bit0: Traffic Indicator bit associated with Assoc ID 0 (Bcast AID?)
    set to 1 in TIM elements with a value of 0 in the DTIM Count field
    when one or more broadcast or multicast frames are buffered at the AP.
    bit1-7: Bitmap Offset (logically Bitmap_Offset = Bitmap_Control & 0xFE).
4 n Partial Virtual Bitmap
    Visible part of traffic-indication bitmap.
    Full bitmap consists of 2008 bits (251 octets) such that bit number N
    (0<=N<=2007) in the bitmap corresponds to bit number (N mod 8)
    in octet number N/8 where the low-order bit of each octet is bit0,
    and the high order bit is bit7.
    Each set bit in virtual bitmap corresponds to traffic buffered by AP
    for a specific station (with corresponding AID?).
    Partial Virtual Bitmap shows a part of bitmap which has non-zero.
    Bitmap Offset is a number of skipped zero octets (see above).
    'Missing' octets at the tail are also assumed to be zero.
    Example: Length=6, Bitmap_Offset=2, Partial_Virtual_Bitmap=55 55 55
    This means that traffic-indication bitmap is:
    00000000 00000000 01010101 01010101 01010101 00000000 00000000...
    (is bit0 in the map is always 0 and real value is in Bitmap Control bit0?)
*/
static int
acx_s_set_tim_template(acx_device_t *adev)
{
/* For now, configure smallish test bitmap, all zero ("no pending data") */
	enum { bitmap_size = 5 };

	acx_template_tim_t t;
	int result;

	FN_ENTER;

	memset(&t, 0, sizeof(t));
	t.size = 5 + bitmap_size; /* eid+len+count+period+bmap_ctrl + bmap */
	t.tim_eid = WLAN_EID_TIM;
	t.len = 3 + bitmap_size; /* count+period+bmap_ctrl + bmap */
	result = acx_s_issue_cmd(adev, ACX1xx_CMD_CONFIG_TIM, &t, sizeof(t));
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_fill_beacon_or_proberesp_template
**
** For frame format info, please see 802.11-1999.pdf item 7.2.3.9 and below!!
**
** NB: we use the fact that
** struct acx_template_proberesp and struct acx_template_beacon are the same
** (well, almost...)
**
** [802.11] Beacon's body consist of these IEs:
** 1 Timestamp
** 2 Beacon interval
** 3 Capability information
** 4 SSID
** 5 Supported rates (up to 8 rates)
** 6 FH Parameter Set (frequency-hopping PHYs only)
** 7 DS Parameter Set (direct sequence PHYs only)
** 8 CF Parameter Set (only if PCF is supported)
** 9 IBSS Parameter Set (ad-hoc only)
**
** Beacon only:
** 10 TIM (AP only) (see 802.11 7.3.2.6)
** 11 Country Information (802.11d)
** 12 FH Parameters (802.11d)
** 13 FH Pattern Table (802.11d)
** ... (?!! did not yet find relevant PDF file... --vda)
** 19 ERP Information (extended rate PHYs)
** 20 Extended Supported Rates (if more than 8 rates)
**
** Proberesp only:
** 10 Country information (802.11d)
** 11 FH Parameters (802.11d)
** 12 FH Pattern Table (802.11d)
** 13-n Requested information elements (802.11d)
** ????
** 18 ERP Information (extended rate PHYs)
** 19 Extended Supported Rates (if more than 8 rates)
*/
static int
acx_fill_beacon_or_proberesp_template(acx_device_t *adev,
					struct acx_template_beacon *templ,
					u16 fc /* in host order! */)
{
	int len;
	u8 *p;

	FN_ENTER;

	memset(templ, 0, sizeof(*templ));
	MAC_BCAST(templ->da);
	MAC_COPY(templ->sa, adev->dev_addr);
	MAC_COPY(templ->bssid, adev->bssid);

	templ->beacon_interval = cpu_to_le16(adev->beacon_interval);
	acx_update_capabilities(adev);
	templ->cap = cpu_to_le16(adev->capabilities);

	p = templ->variable;
	p = wlan_fill_ie_ssid(p, adev->essid_len, adev->essid);
	p = wlan_fill_ie_rates(p, adev->rate_supported_len, adev->rate_supported);
	p = wlan_fill_ie_ds_parms(p, adev->channel);
	/* NB: should go AFTER tim, but acx seem to keep tim last always */
	p = wlan_fill_ie_rates_ext(p, adev->rate_supported_len, adev->rate_supported);

	switch (adev->mode) {
	case ACX_MODE_0_ADHOC:
		/* ATIM window */
		p = wlan_fill_ie_ibss_parms(p, 0); break;
	case ACX_MODE_3_AP:
		/* TIM IE is set up as separate template */
		break;
	}

	len = p - (u8*)templ;
	templ->fc = cpu_to_le16(WF_FTYPE_MGMT | fc);
	/* - 2: do not count 'u16 size' field */
	templ->size = cpu_to_le16(len - 2);

	FN_EXIT1(len);
	return len;
}


#if POWER_SAVE_80211
/***********************************************************************
** acx_s_set_null_data_template
*/
static int
acx_s_set_null_data_template(acx_device_t *adev)
{
	struct acx_template_nullframe b;
	int result;

	FN_ENTER;

	/* memset(&b, 0, sizeof(b)); not needed, setting all members */

	b.size = cpu_to_le16(sizeof(b) - 2);
	b.hdr.fc = WF_FTYPE_MGMTi | WF_FSTYPE_NULLi;
	b.hdr.dur = 0;
	MAC_BCAST(b.hdr.a1);
	MAC_COPY(b.hdr.a2, adev->dev_addr);
	MAC_COPY(b.hdr.a3, adev->bssid);
	b.hdr.seq = 0;

	result = acx_s_issue_cmd(adev, ACX1xx_CMD_CONFIG_NULL_DATA, &b, sizeof(b));

	FN_EXIT1(result);
	return result;
}
#endif


/***********************************************************************
** acx_s_set_beacon_template
*/
static int
acx_s_set_beacon_template(acx_device_t *adev)
{
	struct acx_template_beacon bcn;
	int len, result;

	FN_ENTER;

	len = acx_fill_beacon_or_proberesp_template(adev, &bcn, WF_FSTYPE_BEACON);
	result = acx_s_issue_cmd(adev, ACX1xx_CMD_CONFIG_BEACON, &bcn, len);

	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_s_set_probe_response_template
*/
static int
acx_s_set_probe_response_template(acx_device_t *adev)
{
	struct acx_template_proberesp pr;
	int len, result;

	FN_ENTER;

	len = acx_fill_beacon_or_proberesp_template(adev, &pr, WF_FSTYPE_PROBERESP);
	result = acx_s_issue_cmd(adev, ACX1xx_CMD_CONFIG_PROBE_RESPONSE, &pr, len);

	FN_EXIT1(result);
	return result;
}


/***********************************************************************
** acx_s_init_packet_templates()
**
** NOTE: order is very important here, to have a correct memory layout!
** init templates: max Probe Request (station mode), max NULL data,
** max Beacon, max TIM, max Probe Response.
*/
static int
acx_s_init_packet_templates(acx_device_t *adev)
{
	acx_ie_memmap_t mm; /* ACX100 only */
	int result = NOT_OK;

	FN_ENTER;

	log(L_DEBUG|L_INIT, "initializing max packet templates\n");

	if (OK != acx_s_init_max_probe_request_template(adev))
		goto failed;

	if (OK != acx_s_init_max_null_data_template(adev))
		goto failed;

	if (OK != acx_s_init_max_beacon_template(adev))
		goto failed;

	if (OK != acx_s_init_max_tim_template(adev))
		goto failed;

	if (OK != acx_s_init_max_probe_response_template(adev))
		goto failed;

	if (IS_ACX111(adev)) {
		/* ACX111 doesn't need the memory map magic below,
		 * and the other templates will be set later (acx_start) */
		result = OK;
		goto success;
	}

	/* ACX100 will have its TIM template set,
	 * and we also need to update the memory map */

	if (OK != acx_s_set_tim_template(adev))
		goto failed_acx100;

	log(L_DEBUG, "sizeof(memmap)=%d bytes\n", (int)sizeof(mm));

	if (OK != acx_s_interrogate(adev, &mm, ACX1xx_IE_MEMORY_MAP))
		goto failed_acx100;

	mm.QueueStart = cpu_to_le32(le32_to_cpu(mm.PacketTemplateEnd) + 4);
	if (OK != acx_s_configure(adev, &mm, ACX1xx_IE_MEMORY_MAP))
		goto failed_acx100;

	result = OK;
	goto success;

failed_acx100:
	log(L_DEBUG|L_INIT,
		/* "cb=0x%X\n" */
		"ACXMemoryMap:\n"
		".CodeStart=0x%X\n"
		".CodeEnd=0x%X\n"
		".WEPCacheStart=0x%X\n"
		".WEPCacheEnd=0x%X\n"
		".PacketTemplateStart=0x%X\n"
		".PacketTemplateEnd=0x%X\n",
		/* len, */
		le32_to_cpu(mm.CodeStart),
		le32_to_cpu(mm.CodeEnd),
		le32_to_cpu(mm.WEPCacheStart),
		le32_to_cpu(mm.WEPCacheEnd),
		le32_to_cpu(mm.PacketTemplateStart),
		le32_to_cpu(mm.PacketTemplateEnd));

failed:
	printk("%s: %s() FAILED\n", adev->ndev->name, __func__);

success:
	FN_EXIT1(result);
	return result;
}


/***********************************************************************
*/
static int
acx_s_set_probe_request_template(acx_device_t *adev)
{
	struct acx_template_probereq probereq;
	char *p;
	int res;
	int frame_len;

	FN_ENTER;

	memset(&probereq, 0, sizeof(probereq));

	probereq.fc = WF_FTYPE_MGMTi | WF_FSTYPE_PROBEREQi;
	MAC_BCAST(probereq.da);
	MAC_COPY(probereq.sa, adev->dev_addr);
	MAC_BCAST(probereq.bssid);

	p = probereq.variable;
	p = wlan_fill_ie_ssid(p, adev->essid_len, adev->essid);
	p = wlan_fill_ie_rates(p, adev->rate_supported_len, adev->rate_supported);
	p = wlan_fill_ie_rates_ext(p, adev->rate_supported_len, adev->rate_supported);
	frame_len = p - (char*)&probereq;
	probereq.size = cpu_to_le16(frame_len - 2);

	res = acx_s_issue_cmd(adev, ACX1xx_CMD_CONFIG_PROBE_REQUEST, &probereq, frame_len);
	FN_EXIT0;
	return res;
}


/***********************************************************************
** acx_s_init_mac
*/
int
acx_s_init_mac(acx_device_t *adev)
{
	int result = NOT_OK;

	FN_ENTER;

	if (IS_ACX111(adev)) {
		adev->ie_len = acx111_ie_len;
		adev->ie_len_dot11 = acx111_ie_len_dot11;
	} else {
		adev->ie_len = acx100_ie_len;
		adev->ie_len_dot11 = acx100_ie_len_dot11;
	}

	if (IS_PCI(adev)) {
		adev->memblocksize = 256; /* 256 is default */
		/* try to load radio for both ACX100 and ACX111, since both
		 * chips have at least some firmware versions making use of an
		 * external radio module */
		acxpci_s_upload_radio(adev);
	} else {
		adev->memblocksize = 128;
	}

	if (IS_ACX111(adev)) {
		/* for ACX111, the order is different from ACX100
		   1. init packet templates
		   2. create station context and create dma regions
		   3. init wep default keys
		*/
		if (OK != acx_s_init_packet_templates(adev))
			goto fail;
		if (OK != acx111_s_create_dma_regions(adev)) {
			printk("%s: acx111_create_dma_regions FAILED\n",
						adev->ndev->name);
			goto fail;
		}
	} else {
		if (OK != acx100_s_init_wep(adev))
			goto fail;
		if (OK != acx_s_init_packet_templates(adev))
			goto fail;
		if (OK != acx100_s_create_dma_regions(adev)) {
			printk("%s: acx100_create_dma_regions FAILED\n",
						adev->ndev->name);
			goto fail;
		}
	}

	MAC_COPY(adev->ndev->dev_addr, adev->dev_addr);
	result = OK;

fail:
	if (result)
		printk("acx: init_mac() FAILED\n");
	FN_EXIT1(result);
	return result;
}


void
acx_s_set_sane_reg_domain(acx_device_t *adev, int do_set)
{
	unsigned mask;

	unsigned int i;

	for (i = 0; i < sizeof(acx_reg_domain_ids); i++)
		if (acx_reg_domain_ids[i] == adev->reg_dom_id)
			break;

	if (sizeof(acx_reg_domain_ids) == i) {
		log(L_INIT, "Invalid or unsupported regulatory domain"
			       " 0x%02X specified, falling back to FCC (USA)!"
			       " Please report if this sounds fishy!\n",
				adev->reg_dom_id);
		i = 0;
		adev->reg_dom_id = acx_reg_domain_ids[i];

		/* since there was a mismatch, we need to force updating */
		do_set = 1;
	}

	if (do_set) {
		acx_ie_generic_t dom;
		dom.m.bytes[0] = adev->reg_dom_id;
		acx_s_configure(adev, &dom, ACX1xx_IE_DOT11_CURRENT_REG_DOMAIN);
	}

	adev->reg_dom_chanmask = reg_domain_channel_masks[i];

	mask = (1 << (adev->channel - 1));
	if (!(adev->reg_dom_chanmask & mask)) {
	/* hmm, need to adjust our channel to reside within domain */
		mask = 1;
		for (i = 1; i <= 14; i++) {
			if (adev->reg_dom_chanmask & mask) {
				printk("%s: adjusting selected channel from %d "
					"to %d due to new regulatory domain\n",
					adev->ndev->name, adev->channel, i);
				adev->channel = i;
				break;
			}
			mask <<= 1;
		}
	}
}


#if POWER_SAVE_80211
static void
acx_s_update_80211_powersave_mode(acx_device_t *adev)
{
	/* merge both structs in a union to be able to have common code */
	union {
		acx111_ie_powersave_t acx111;
		acx100_ie_powersave_t acx100;
	} pm;

	/* change 802.11 power save mode settings */
	log(L_INIT, "updating 802.11 power save mode settings: "
		"wakeup_cfg 0x%02X, listen interval %u, "
		"options 0x%02X, hangover period %u, "
		"enhanced_ps_transition_time %u\n",
		adev->ps_wakeup_cfg, adev->ps_listen_interval,
		adev->ps_options, adev->ps_hangover_period,
		adev->ps_enhanced_transition_time);
	acx_s_interrogate(adev, &pm, ACX1xx_IE_POWER_MGMT);
	log(L_INIT, "Previous PS mode settings: wakeup_cfg 0x%02X, "
		"listen interval %u, options 0x%02X, "
		"hangover period %u, "
		"enhanced_ps_transition_time %u, beacon_rx_time %u\n",
		pm.acx111.wakeup_cfg,
		pm.acx111.listen_interval,
		pm.acx111.options,
		pm.acx111.hangover_period,
		IS_ACX111(adev) ?
			pm.acx111.enhanced_ps_transition_time
		      : pm.acx100.enhanced_ps_transition_time,
		IS_ACX111(adev) ?
			pm.acx111.beacon_rx_time
		      : (u32)-1
		);
	pm.acx111.wakeup_cfg = adev->ps_wakeup_cfg;
	pm.acx111.listen_interval = adev->ps_listen_interval;
	pm.acx111.options = adev->ps_options;
	pm.acx111.hangover_period = adev->ps_hangover_period;
	if (IS_ACX111(adev)) {
		pm.acx111.beacon_rx_time = cpu_to_le32(adev->ps_beacon_rx_time);
		pm.acx111.enhanced_ps_transition_time = cpu_to_le32(adev->ps_enhanced_transition_time);
	} else {
		pm.acx100.enhanced_ps_transition_time = cpu_to_le16(adev->ps_enhanced_transition_time);
	}
	acx_s_configure(adev, &pm, ACX1xx_IE_POWER_MGMT);
	acx_s_interrogate(adev, &pm, ACX1xx_IE_POWER_MGMT);
	log(L_INIT, "wakeup_cfg: 0x%02X\n", pm.acx111.wakeup_cfg);
	acx_s_msleep(40);
	acx_s_interrogate(adev, &pm, ACX1xx_IE_POWER_MGMT);
	log(L_INIT, "wakeup_cfg: 0x%02X\n", pm.acx111.wakeup_cfg);
	log(L_INIT, "power save mode change %s\n",
		(pm.acx111.wakeup_cfg & PS_CFG_PENDING) ? "FAILED" : "was successful");
	/* FIXME: maybe verify via PS_CFG_PENDING bit here
	 * that power save mode change was successful. */
	/* FIXME: we shouldn't trigger a scan immediately after
	 * fiddling with power save mode (since the firmware is sending
	 * a NULL frame then). */
}
#endif


/***********************************************************************
** acx_s_update_card_settings
**
** Applies accumulated changes in various adev->xxxx members
** Called by ioctl commit handler, acx_start, acx_set_defaults,
** acx_s_after_interrupt_task (if IRQ_CMD_UPDATE_CARD_CFG),
*/
static void
acx111_s_sens_radio_16_17(acx_device_t *adev)
{
	u32 feature1, feature2;

	if ((adev->sensitivity < 1) || (adev->sensitivity > 3)) {
		printk("%s: invalid sensitivity setting (1..3), "
			"setting to 1\n", adev->ndev->name);
		adev->sensitivity = 1;
	}
	acx111_s_get_feature_config(adev, &feature1, &feature2);
	CLEAR_BIT(feature1, FEATURE1_LOW_RX|FEATURE1_EXTRA_LOW_RX);
	if (adev->sensitivity > 1)
		SET_BIT(feature1, FEATURE1_LOW_RX);
	if (adev->sensitivity > 2)
		SET_BIT(feature1, FEATURE1_EXTRA_LOW_RX);
	acx111_s_feature_set(adev, feature1, feature2);
}


void
acx_s_update_card_settings(acx_device_t *adev)
{
	unsigned long flags;
	unsigned int start_scan = 0;
	int i;

	FN_ENTER;

	log(L_INIT, "get_mask 0x%08X, set_mask 0x%08X\n",
			adev->get_mask, adev->set_mask);

	/* Track dependencies betweed various settings */

	if (adev->set_mask & (GETSET_MODE|GETSET_RESCAN|GETSET_WEP)) {
		log(L_INIT, "important setting has been changed. "
			"Need to update packet templates, too\n");
		SET_BIT(adev->set_mask, SET_TEMPLATES);
	}
	if (adev->set_mask & GETSET_CHANNEL) {
		/* This will actually tune RX/TX to the channel */
		SET_BIT(adev->set_mask, GETSET_RX|GETSET_TX);
		switch (adev->mode) {
		case ACX_MODE_0_ADHOC:
		case ACX_MODE_3_AP:
			/* Beacons contain channel# - update them */
			SET_BIT(adev->set_mask, SET_TEMPLATES);
		}
		switch (adev->mode) {
		case ACX_MODE_0_ADHOC:
		case ACX_MODE_2_STA:
			start_scan = 1;
		}
	}

	/* Apply settings */

#ifdef WHY_SHOULD_WE_BOTHER /* imagine we were just powered off */
	/* send a disassoc request in case it's required */
	if (adev->set_mask & (GETSET_MODE|GETSET_RESCAN|GETSET_CHANNEL|GETSET_WEP)) {
		if (ACX_MODE_2_STA == adev->mode) {
			if (ACX_STATUS_4_ASSOCIATED == adev->status) {
				log(L_ASSOC, "we were ASSOCIATED - "
					"sending disassoc request\n");
				acx_lock(adev, flags);
				acx_l_transmit_disassoc(adev, NULL);
				/* FIXME: deauth? */
				acx_unlock(adev, flags);
			}
			/* need to reset some other stuff as well */
			log(L_DEBUG, "resetting bssid\n");
			MAC_ZERO(adev->bssid);
			SET_BIT(adev->set_mask, SET_TEMPLATES|SET_STA_LIST);
			start_scan = 1;
		}
	}
#endif

	if (adev->get_mask & GETSET_STATION_ID) {
		u8 stationID[4 + ACX1xx_IE_DOT11_STATION_ID_LEN];
		const u8 *paddr;

		acx_s_interrogate(adev, &stationID, ACX1xx_IE_DOT11_STATION_ID);
		paddr = &stationID[4];
		for (i = 0; i < ETH_ALEN; i++) {
			/* we copy the MAC address (reversed in
			 * the card) to the netdevice's MAC
			 * address, and on ifup it will be
			 * copied into iwadev->dev_addr */
			adev->ndev->dev_addr[ETH_ALEN - 1 - i] = paddr[i];
		}
		CLEAR_BIT(adev->get_mask, GETSET_STATION_ID);
	}

	if (adev->get_mask & GETSET_SENSITIVITY) {
		if ((RADIO_RFMD_11 == adev->radio_type)
		|| (RADIO_MAXIM_0D == adev->radio_type)
		|| (RADIO_RALINK_15 == adev->radio_type)) {
			acx_s_read_phy_reg(adev, 0x30, &adev->sensitivity);
		} else {
			log(L_INIT, "don't know how to get sensitivity "
				"for radio type 0x%02X\n", adev->radio_type);
			adev->sensitivity = 0;
		}
		log(L_INIT, "got sensitivity value %u\n", adev->sensitivity);

		CLEAR_BIT(adev->get_mask, GETSET_SENSITIVITY);
	}

	if (adev->get_mask & GETSET_ANTENNA) {
		u8 antenna[4 + ACX1xx_IE_DOT11_CURRENT_ANTENNA_LEN];

		memset(antenna, 0, sizeof(antenna));
		acx_s_interrogate(adev, antenna, ACX1xx_IE_DOT11_CURRENT_ANTENNA);
		adev->antenna = antenna[4];
		log(L_INIT, "got antenna value 0x%02X\n", adev->antenna);
		CLEAR_BIT(adev->get_mask, GETSET_ANTENNA);
	}

	if (adev->get_mask & GETSET_ED_THRESH) {
		if (IS_ACX100(adev))	{
			u8 ed_threshold[4 + ACX100_IE_DOT11_ED_THRESHOLD_LEN];

			memset(ed_threshold, 0, sizeof(ed_threshold));
			acx_s_interrogate(adev, ed_threshold, ACX100_IE_DOT11_ED_THRESHOLD);
			adev->ed_threshold = ed_threshold[4];
		} else {
			log(L_INIT, "acx111 doesn't support ED\n");
			adev->ed_threshold = 0;
		}
		log(L_INIT, "got Energy Detect (ED) threshold %u\n", adev->ed_threshold);
		CLEAR_BIT(adev->get_mask, GETSET_ED_THRESH);
	}

	if (adev->get_mask & GETSET_CCA) {
		if (IS_ACX100(adev))	{
			u8 cca[4 + ACX1xx_IE_DOT11_CURRENT_CCA_MODE_LEN];

			memset(cca, 0, sizeof(adev->cca));
			acx_s_interrogate(adev, cca, ACX1xx_IE_DOT11_CURRENT_CCA_MODE);
			adev->cca = cca[4];
		} else {
			log(L_INIT, "acx111 doesn't support CCA\n");
			adev->cca = 0;
		}
		log(L_INIT, "got Channel Clear Assessment (CCA) value %u\n", adev->cca);
		CLEAR_BIT(adev->get_mask, GETSET_CCA);
	}

	if (adev->get_mask & GETSET_REG_DOMAIN) {
		acx_ie_generic_t dom;

		acx_s_interrogate(adev, &dom, ACX1xx_IE_DOT11_CURRENT_REG_DOMAIN);
		adev->reg_dom_id = dom.m.bytes[0];
		acx_s_set_sane_reg_domain(adev, 0);
		log(L_INIT, "got regulatory domain 0x%02X\n", adev->reg_dom_id);
		CLEAR_BIT(adev->get_mask, GETSET_REG_DOMAIN);
	}

	if (adev->set_mask & GETSET_STATION_ID) {
		u8 stationID[4 + ACX1xx_IE_DOT11_STATION_ID_LEN];
		u8 *paddr;

		paddr = &stationID[4];
		for (i = 0; i < ETH_ALEN; i++) {
			/* copy the MAC address we obtained when we noticed
			 * that the ethernet iface's MAC changed
			 * to the card (reversed in
			 * the card!) */
			paddr[i] = adev->dev_addr[ETH_ALEN - 1 - i];
		}
		acx_s_configure(adev, &stationID, ACX1xx_IE_DOT11_STATION_ID);
		CLEAR_BIT(adev->set_mask, GETSET_STATION_ID);
	}

	if (adev->set_mask & SET_TEMPLATES) {
		log(L_INIT, "updating packet templates\n");
		switch (adev->mode) {
		case ACX_MODE_2_STA:
			acx_s_set_probe_request_template(adev);
#if POWER_SAVE_80211
			acx_s_set_null_data_template(adev);
#endif
			break;
		case ACX_MODE_0_ADHOC:
			acx_s_set_probe_request_template(adev);
#if POWER_SAVE_80211
			/* maybe power save functionality is somehow possible
			 * for Ad-Hoc mode, too... FIXME: verify it somehow? firmware debug fields? */
			acx_s_set_null_data_template(adev);
#endif
			/* fall through */
		case ACX_MODE_3_AP:
			acx_s_set_beacon_template(adev);
			acx_s_set_tim_template(adev);
			/* BTW acx111 firmware would not send probe responses
			** if probe request does not have all basic rates flagged
			** by 0x80! Thus firmware does not conform to 802.11,
			** it should ignore 0x80 bit in ratevector from STA.
			** We can 'fix' it by not using this template and
			** sending probe responses by hand. TODO --vda */
			acx_s_set_probe_response_template(adev);
		}
		/* Needed if generated frames are to be emitted at different tx rate now */
		log(L_IRQ, "redoing cmd_join_bssid() after template cfg\n");
		acx_s_cmd_join_bssid(adev, adev->bssid);
		CLEAR_BIT(adev->set_mask, SET_TEMPLATES);
	}
	if (adev->set_mask & SET_STA_LIST) {
		acx_lock(adev, flags);
		acx_l_sta_list_init(adev);
		CLEAR_BIT(adev->set_mask, SET_STA_LIST);
		acx_unlock(adev, flags);
	}
	if (adev->set_mask & SET_RATE_FALLBACK) {
		u8 rate[4 + ACX1xx_IE_RATE_FALLBACK_LEN];

		/* configure to not do fallbacks when not in auto rate mode */
		rate[4] = (adev->rate_auto) ? /* adev->txrate_fallback_retries */ 1 : 0;
		log(L_INIT, "updating Tx fallback to %u retries\n", rate[4]);
		acx_s_configure(adev, &rate, ACX1xx_IE_RATE_FALLBACK);
		CLEAR_BIT(adev->set_mask, SET_RATE_FALLBACK);
	}
	if (adev->set_mask & GETSET_TXPOWER) {
		log(L_INIT, "updating transmit power: %u dBm\n",
					adev->tx_level_dbm);
		acx_s_set_tx_level(adev, adev->tx_level_dbm);
		CLEAR_BIT(adev->set_mask, GETSET_TXPOWER);
	}

	if (adev->set_mask & GETSET_SENSITIVITY) {
		log(L_INIT, "updating sensitivity value: %u\n",
					adev->sensitivity);
		switch (adev->radio_type) {
		case RADIO_RFMD_11:
		case RADIO_MAXIM_0D:
		case RADIO_RALINK_15:
			acx_s_write_phy_reg(adev, 0x30, adev->sensitivity);
			break;
		case RADIO_RADIA_16:
		case RADIO_UNKNOWN_17:
			acx111_s_sens_radio_16_17(adev);
			break;
		default:
			log(L_INIT, "don't know how to modify sensitivity "
				"for radio type 0x%02X\n", adev->radio_type);
		}
		CLEAR_BIT(adev->set_mask, GETSET_SENSITIVITY);
	}

	if (adev->set_mask & GETSET_ANTENNA) {
		/* antenna */
		u8 antenna[4 + ACX1xx_IE_DOT11_CURRENT_ANTENNA_LEN];

		memset(antenna, 0, sizeof(antenna));
		antenna[4] = adev->antenna;
		log(L_INIT, "updating antenna value: 0x%02X\n",
					adev->antenna);
		acx_s_configure(adev, &antenna, ACX1xx_IE_DOT11_CURRENT_ANTENNA);
		CLEAR_BIT(adev->set_mask, GETSET_ANTENNA);
	}

	if (adev->set_mask & GETSET_ED_THRESH) {
		/* ed_threshold */
		log(L_INIT, "updating Energy Detect (ED) threshold: %u\n",
					adev->ed_threshold);
		if (IS_ACX100(adev)) {
			u8 ed_threshold[4 + ACX100_IE_DOT11_ED_THRESHOLD_LEN];

			memset(ed_threshold, 0, sizeof(ed_threshold));
			ed_threshold[4] = adev->ed_threshold;
			acx_s_configure(adev, &ed_threshold, ACX100_IE_DOT11_ED_THRESHOLD);
		}
		else
			log(L_INIT, "acx111 doesn't support ED!\n");
		CLEAR_BIT(adev->set_mask, GETSET_ED_THRESH);
	}

	if (adev->set_mask & GETSET_CCA) {
		/* CCA value */
		log(L_INIT, "updating Channel Clear Assessment "
				"(CCA) value: 0x%02X\n", adev->cca);
		if (IS_ACX100(adev))	{
			u8 cca[4 + ACX1xx_IE_DOT11_CURRENT_CCA_MODE_LEN];

			memset(cca, 0, sizeof(cca));
			cca[4] = adev->cca;
			acx_s_configure(adev, &cca, ACX1xx_IE_DOT11_CURRENT_CCA_MODE);
		}
		else
			log(L_INIT, "acx111 doesn't support CCA!\n");
		CLEAR_BIT(adev->set_mask, GETSET_CCA);
	}

	if (adev->set_mask & GETSET_LED_POWER) {
		/* Enable Tx */
		log(L_INIT, "updating power LED status: %u\n", adev->led_power);

		acx_lock(adev, flags);
		if (IS_PCI(adev))
			acxpci_l_power_led(adev, adev->led_power);
		CLEAR_BIT(adev->set_mask, GETSET_LED_POWER);
		acx_unlock(adev, flags);
	}

	if (adev->set_mask & GETSET_POWER_80211) {
#if POWER_SAVE_80211
		acx_s_update_80211_powersave_mode(adev);
#endif
		CLEAR_BIT(adev->set_mask, GETSET_POWER_80211);
	}

	if (adev->set_mask & GETSET_CHANNEL) {
		/* channel */
		log(L_INIT, "updating channel to: %u\n", adev->channel);
		CLEAR_BIT(adev->set_mask, GETSET_CHANNEL);
	}

	if (adev->set_mask & GETSET_TX) {
		/* set Tx */
		log(L_INIT, "updating: %s Tx\n",
				adev->tx_disabled ? "disable" : "enable");
		if (adev->tx_disabled)
			acx_s_issue_cmd(adev, ACX1xx_CMD_DISABLE_TX, NULL, 0);
		else
			acx_s_issue_cmd(adev, ACX1xx_CMD_ENABLE_TX, &adev->channel, 1);
		CLEAR_BIT(adev->set_mask, GETSET_TX);
	}

	if (adev->set_mask & GETSET_RX) {
		/* Enable Rx */
		log(L_INIT, "updating: enable Rx on channel: %u\n",
				adev->channel);
		acx_s_issue_cmd(adev, ACX1xx_CMD_ENABLE_RX, &adev->channel, 1);
		CLEAR_BIT(adev->set_mask, GETSET_RX);
	}

	if (adev->set_mask & GETSET_RETRY) {
		u8 short_retry[4 + ACX1xx_IE_DOT11_SHORT_RETRY_LIMIT_LEN];
		u8 long_retry[4 + ACX1xx_IE_DOT11_LONG_RETRY_LIMIT_LEN];

		log(L_INIT, "updating short retry limit: %u, long retry limit: %u\n",
					adev->short_retry, adev->long_retry);
		short_retry[0x4] = adev->short_retry;
		long_retry[0x4] = adev->long_retry;
		acx_s_configure(adev, &short_retry, ACX1xx_IE_DOT11_SHORT_RETRY_LIMIT);
		acx_s_configure(adev, &long_retry, ACX1xx_IE_DOT11_LONG_RETRY_LIMIT);
		CLEAR_BIT(adev->set_mask, GETSET_RETRY);
	}

	if (adev->set_mask & SET_MSDU_LIFETIME) {
		u8 xmt_msdu_lifetime[4 + ACX1xx_IE_DOT11_MAX_XMIT_MSDU_LIFETIME_LEN];

		log(L_INIT, "updating tx MSDU lifetime: %u\n",
					adev->msdu_lifetime);
		*(u32 *)&xmt_msdu_lifetime[4] = cpu_to_le32((u32)adev->msdu_lifetime);
		acx_s_configure(adev, &xmt_msdu_lifetime, ACX1xx_IE_DOT11_MAX_XMIT_MSDU_LIFETIME);
		CLEAR_BIT(adev->set_mask, SET_MSDU_LIFETIME);
	}

	if (adev->set_mask & GETSET_REG_DOMAIN) {
		log(L_INIT, "updating regulatory domain: 0x%02X\n",
					adev->reg_dom_id);
		acx_s_set_sane_reg_domain(adev, 1);
		CLEAR_BIT(adev->set_mask, GETSET_REG_DOMAIN);
	}

	if (adev->set_mask & GETSET_MODE) {
		adev->ndev->type = (adev->mode == ACX_MODE_MONITOR) ?
			adev->monitor_type : ARPHRD_ETHER;

		switch (adev->mode) {
		case ACX_MODE_3_AP:

			acx_lock(adev, flags);
			acx_l_sta_list_init(adev);
			adev->aid = 0;
			adev->ap_client = NULL;
			MAC_COPY(adev->bssid, adev->dev_addr);
			/* this basically says "we're connected" */
			acx_set_status(adev, ACX_STATUS_4_ASSOCIATED);
			acx_unlock(adev, flags);

			acx111_s_feature_off(adev, 0, FEATURE2_NO_TXCRYPT|FEATURE2_SNIFFER);
			/* start sending beacons */
			acx_s_cmd_join_bssid(adev, adev->bssid);
			break;
		case ACX_MODE_MONITOR:
			acx111_s_feature_on(adev, 0, FEATURE2_NO_TXCRYPT|FEATURE2_SNIFFER);
			/* this stops beacons */
			acx_s_cmd_join_bssid(adev, adev->bssid);
			/* this basically says "we're connected" */
			acx_set_status(adev, ACX_STATUS_4_ASSOCIATED);
			SET_BIT(adev->set_mask, SET_RXCONFIG|SET_WEP_OPTIONS);
			break;
		case ACX_MODE_0_ADHOC:
		case ACX_MODE_2_STA:
			acx111_s_feature_off(adev, 0, FEATURE2_NO_TXCRYPT|FEATURE2_SNIFFER);

			acx_lock(adev, flags);
			adev->aid = 0;
			adev->ap_client = NULL;
			acx_unlock(adev, flags);

			/* we want to start looking for peer or AP */
			start_scan = 1;
			break;
		case ACX_MODE_OFF:
			/* TODO: disable RX/TX, stop any scanning activity etc: */
			/* adev->tx_disabled = 1; */
			/* SET_BIT(adev->set_mask, GETSET_RX|GETSET_TX); */

			/* This stops beacons (invalid macmode...) */
			acx_s_cmd_join_bssid(adev, adev->bssid);
			acx_set_status(adev, ACX_STATUS_0_STOPPED);
			break;
		}
		CLEAR_BIT(adev->set_mask, GETSET_MODE);
	}

	if (adev->set_mask & SET_RXCONFIG) {
		acx_s_initialize_rx_config(adev);
		CLEAR_BIT(adev->set_mask, SET_RXCONFIG);
	}

	if (adev->set_mask & GETSET_RESCAN) {
		switch (adev->mode) {
		case ACX_MODE_0_ADHOC:
		case ACX_MODE_2_STA:
			start_scan = 1;
			break;
		}
		CLEAR_BIT(adev->set_mask, GETSET_RESCAN);
	}

	if (adev->set_mask & GETSET_WEP) {
		/* encode */

		ie_dot11WEPDefaultKeyID_t dkey;
#ifdef DEBUG_WEP
		struct {
			u16 type;
			u16 len;
			u8  val;
		} ACX_PACKED keyindic;
#endif
		log(L_INIT, "updating WEP key settings\n");

		acx_s_set_wepkey(adev);

		dkey.KeyID = adev->wep_current_index;
		log(L_INIT, "setting WEP key %u as default\n", dkey.KeyID);
		acx_s_configure(adev, &dkey, ACX1xx_IE_DOT11_WEP_DEFAULT_KEY_SET);
#ifdef DEBUG_WEP
		keyindic.val = 3;
		acx_s_configure(adev, &keyindic, ACX111_IE_KEY_CHOOSE);
#endif
		start_scan = 1;
		CLEAR_BIT(adev->set_mask, GETSET_WEP);
	}

	if (adev->set_mask & SET_WEP_OPTIONS) {
		acx100_ie_wep_options_t options;

		if (IS_ACX111(adev)) {
			log(L_DEBUG, "setting WEP Options for acx111 is not supported\n");
		} else {
			log(L_INIT, "setting WEP Options\n");

			/* let's choose maximum setting: 4 default keys,
			 * plus 10 other keys: */
			options.NumKeys = cpu_to_le16(DOT11_MAX_DEFAULT_WEP_KEYS + 10);
			/* don't decrypt default key only,
			 * don't override decryption: */
			options.WEPOption = 0;
			if (adev->mode == ACX_MODE_MONITOR) {
				/* don't decrypt default key only,
				 * override decryption mechanism: */
				options.WEPOption = 2;
			}

			acx_s_configure(adev, &options, ACX100_IE_WEP_OPTIONS);
		}
		CLEAR_BIT(adev->set_mask, SET_WEP_OPTIONS);
	}

	/* Rescan was requested */
	if (start_scan) {
		switch (adev->mode) {
		case ACX_MODE_0_ADHOC:
		case ACX_MODE_2_STA:
			/* We can avoid clearing list if join code
			** will be a bit more clever about not picking
			** 'bad' AP over and over again */
			acx_lock(adev, flags);
			adev->ap_client = NULL;
			acx_l_sta_list_init(adev);
			acx_set_status(adev, ACX_STATUS_1_SCANNING);
			acx_unlock(adev, flags);

			acx_s_cmd_start_scan(adev);
		}
	}

	/* debug, rate, and nick don't need any handling */
	/* what about sniffing mode?? */

	log(L_INIT, "get_mask 0x%08X, set_mask 0x%08X - after update\n",
			adev->get_mask, adev->set_mask);

/* end: */
	FN_EXIT0;
}


/***********************************************************************
** acx_e_after_interrupt_task
*/
static int
acx_s_recalib_radio(acx_device_t *adev)
{
	if (IS_ACX111(adev)) {
		acx111_cmd_radiocalib_t cal;

		printk("%s: recalibrating radio\n", adev->ndev->name);
		/* automatic recalibration, choose all methods: */
		cal.methods = cpu_to_le32(0x8000000f);
		/* automatic recalibration every 60 seconds (value in TUs)
		 * I wonder what the firmware default here is? */
		cal.interval = cpu_to_le32(58594);
		return acx_s_issue_cmd_timeo(adev, ACX111_CMD_RADIOCALIB,
			&cal, sizeof(cal), CMD_TIMEOUT_MS(100));
	} else {
		/* On ACX100, we need to recalibrate the radio
		 * by issuing a GETSET_TX|GETSET_RX */
		if (/* (OK == acx_s_issue_cmd(adev, ACX1xx_CMD_DISABLE_TX, NULL, 0)) &&
		    (OK == acx_s_issue_cmd(adev, ACX1xx_CMD_DISABLE_RX, NULL, 0)) && */
		    (OK == acx_s_issue_cmd(adev, ACX1xx_CMD_ENABLE_TX, &adev->channel, 1)) &&
		    (OK == acx_s_issue_cmd(adev, ACX1xx_CMD_ENABLE_RX, &adev->channel, 1)) )
			return OK;
		return NOT_OK;
	}
}

static void
acx_s_after_interrupt_recalib(acx_device_t *adev)
{
	int res;

	/* this helps with ACX100 at least;
	 * hopefully ACX111 also does a
	 * recalibration here */

	/* clear flag beforehand, since we want to make sure
	 * it's cleared; then only set it again on specific circumstances */
	CLEAR_BIT(adev->after_interrupt_jobs, ACX_AFTER_IRQ_CMD_RADIO_RECALIB);

	/* better wait a bit between recalibrations to
	 * prevent overheating due to torturing the card
	 * into working too long despite high temperature
	 * (just a safety measure) */
	if (adev->recalib_time_last_success
	 && time_before(jiffies, adev->recalib_time_last_success
					+ RECALIB_PAUSE * 60 * HZ)) {
		if (adev->recalib_msg_ratelimit <= 4) {
			printk("%s: less than " STRING(RECALIB_PAUSE)
				" minutes since last radio recalibration, "
				"not recalibrating (maybe card is too hot?)\n",
				adev->ndev->name);
			adev->recalib_msg_ratelimit++;
			if (adev->recalib_msg_ratelimit == 5)
				printk("disabling above message\n");
		}
		return;
	}

	adev->recalib_msg_ratelimit = 0;

	/* note that commands sometimes fail (card busy),
	 * so only clear flag if we were fully successful */
	res = acx_s_recalib_radio(adev);
	if (res == OK) {
		printk("%s: successfully recalibrated radio\n",
						adev->ndev->name);
		adev->recalib_time_last_success = jiffies;
		adev->recalib_failure_count = 0;
	} else {
		/* failed: resubmit, but only limited
		 * amount of times within some time range
		 * to prevent endless loop */

		adev->recalib_time_last_success = 0; /* we failed */

		/* if some time passed between last
		 * attempts, then reset failure retry counter
		 * to be able to do next recalib attempt */
		if (time_after(jiffies, adev->recalib_time_last_attempt + 5*HZ))
			adev->recalib_failure_count = 0;

		if (adev->recalib_failure_count < 5) {
			/* increment inside only, for speedup of outside path */
			adev->recalib_failure_count++;
			adev->recalib_time_last_attempt = jiffies;
			acx_schedule_task(adev, ACX_AFTER_IRQ_CMD_RADIO_RECALIB);
		}
	}
}

static void
acx_e_after_interrupt_task(void *data)
{
	struct net_device *ndev = (struct net_device*)data;
	acx_device_t *adev = ndev2adev(ndev);

	FN_ENTER;

	acx_sem_lock(adev);

	if (!adev->after_interrupt_jobs)
		goto end; /* no jobs to do */

#if TX_CLEANUP_IN_SOFTIRQ
	/* can happen only on PCI */
	if (adev->after_interrupt_jobs & ACX_AFTER_IRQ_TX_CLEANUP) {
		acx_lock(adev, flags);
		acxpci_l_clean_txdesc(adev);
		CLEAR_BIT(adev->after_interrupt_jobs, ACX_AFTER_IRQ_TX_CLEANUP);
		acx_unlock(adev, flags);
	}
#endif
	/* we see lotsa tx errors */
	if (adev->after_interrupt_jobs & ACX_AFTER_IRQ_CMD_RADIO_RECALIB) {
		acx_s_after_interrupt_recalib(adev);
	}

	/* a poor interrupt code wanted to do update_card_settings() */
	if (adev->after_interrupt_jobs & ACX_AFTER_IRQ_UPDATE_CARD_CFG) {
		if (ACX_STATE_IFACE_UP & adev->dev_state_mask)
			acx_s_update_card_settings(adev);
		CLEAR_BIT(adev->after_interrupt_jobs, ACX_AFTER_IRQ_UPDATE_CARD_CFG);
	}

	/* 1) we detected that no Scan_Complete IRQ came from fw, or
	** 2) we found too many STAs */
	if (adev->after_interrupt_jobs & ACX_AFTER_IRQ_CMD_STOP_SCAN) {
		log(L_IRQ, "sending a stop scan cmd...\n");
		acx_s_issue_cmd(adev, ACX1xx_CMD_STOP_SCAN, NULL, 0);
		/* HACK: set the IRQ bit, since we won't get a
		 * scan complete IRQ any more on ACX111 (works on ACX100!),
		 * since _we_, not a fw, have stopped the scan */
		SET_BIT(adev->irq_status, HOST_INT_SCAN_COMPLETE);
		CLEAR_BIT(adev->after_interrupt_jobs, ACX_AFTER_IRQ_CMD_STOP_SCAN);
	}

	/* either fw sent Scan_Complete or we detected that
	** no Scan_Complete IRQ came from fw. Finish scanning,
	** pick join partner if any */
	if (adev->after_interrupt_jobs & ACX_AFTER_IRQ_COMPLETE_SCAN) {
		if (adev->status == ACX_STATUS_1_SCANNING) {
			if (OK != acx_s_complete_scan(adev)) {
				SET_BIT(adev->after_interrupt_jobs,
					ACX_AFTER_IRQ_RESTART_SCAN);
			}
		} else {
			/* + scan kills current join status - restore it
			**   (do we need it for STA?) */
			/* + does it happen only with active scans?
			**   active and passive scans? ALL scans including
			**   background one? */
			/* + was not verified that everything is restored
			**   (but at least we start to emit beacons again) */
			switch (adev->mode) {
			case ACX_MODE_0_ADHOC:
			case ACX_MODE_3_AP:
				log(L_IRQ, "redoing cmd_join_bssid() after scan\n");
				acx_s_cmd_join_bssid(adev, adev->bssid);
			}
		}
		CLEAR_BIT(adev->after_interrupt_jobs, ACX_AFTER_IRQ_COMPLETE_SCAN);
	}

	/* STA auth or assoc timed out, start over again */
	if (adev->after_interrupt_jobs & ACX_AFTER_IRQ_RESTART_SCAN) {
		log(L_IRQ, "sending a start_scan cmd...\n");
		acx_s_cmd_start_scan(adev);
		CLEAR_BIT(adev->after_interrupt_jobs, ACX_AFTER_IRQ_RESTART_SCAN);
	}

	/* whee, we got positive assoc response! 8) */
	if (adev->after_interrupt_jobs & ACX_AFTER_IRQ_CMD_ASSOCIATE) {
		acx_ie_generic_t pdr;
		/* tiny race window exists, checking that we still a STA */
		switch (adev->mode) {
		case ACX_MODE_2_STA:
			pdr.m.aid = cpu_to_le16(adev->aid);
			acx_s_configure(adev, &pdr, ACX1xx_IE_ASSOC_ID);
			acx_set_status(adev, ACX_STATUS_4_ASSOCIATED);
			log(L_ASSOC|L_DEBUG, "ASSOCIATED!\n");
			CLEAR_BIT(adev->after_interrupt_jobs, ACX_AFTER_IRQ_CMD_ASSOCIATE);
		}
	}
end:
	acx_sem_unlock(adev);
	FN_EXIT0;
}


/***********************************************************************
** acx_schedule_task
**
** Schedule the call of the after_interrupt method after leaving
** the interrupt context.
*/
void
acx_schedule_task(acx_device_t *adev, unsigned int set_flag)
{
	SET_BIT(adev->after_interrupt_jobs, set_flag);
	SCHEDULE_WORK(&adev->after_interrupt_task);
}


/***********************************************************************
*/
void
acx_init_task_scheduler(acx_device_t *adev)
{
	/* configure task scheduler */
	INIT_WORK(&adev->after_interrupt_task, acx_e_after_interrupt_task,
			adev->ndev);
}


/***********************************************************************
** acx_s_start
*/
void
acx_s_start(acx_device_t *adev)
{
	FN_ENTER;

	/*
	 * Ok, now we do everything that can possibly be done with ioctl
	 * calls to make sure that when it was called before the card
	 * was up we get the changes asked for
	 */

	SET_BIT(adev->set_mask, SET_TEMPLATES|SET_STA_LIST|GETSET_WEP
		|GETSET_TXPOWER|GETSET_ANTENNA|GETSET_ED_THRESH|GETSET_CCA
		|GETSET_REG_DOMAIN|GETSET_MODE|GETSET_CHANNEL
		|GETSET_TX|GETSET_RX);

	log(L_INIT, "updating initial settings on iface activation\n");
	acx_s_update_card_settings(adev);

	FN_EXIT0;
}


/***********************************************************************
** acx_update_capabilities
*/
void
acx_update_capabilities(acx_device_t *adev)
{
	u16 cap = 0;

	switch (adev->mode) {
	case ACX_MODE_3_AP:
		SET_BIT(cap, WF_MGMT_CAP_ESS); break;
	case ACX_MODE_0_ADHOC:
		SET_BIT(cap, WF_MGMT_CAP_IBSS); break;
	/* other types of stations do not emit beacons */
	}

	if (adev->wep_restricted) {
		SET_BIT(cap, WF_MGMT_CAP_PRIVACY);
	}
	if (adev->cfgopt_dot11ShortPreambleOption) {
		SET_BIT(cap, WF_MGMT_CAP_SHORT);
	}
	if (adev->cfgopt_dot11PBCCOption) {
		SET_BIT(cap, WF_MGMT_CAP_PBCC);
	}
	if (adev->cfgopt_dot11ChannelAgility) {
		SET_BIT(cap, WF_MGMT_CAP_AGILITY);
	}
	log(L_DEBUG, "caps updated from 0x%04X to 0x%04X\n",
				adev->capabilities, cap);
	adev->capabilities = cap;
}

/***********************************************************************
** Common function to parse ALL configoption struct formats
** (ACX100 and ACX111; FIXME: how to make it work with ACX100 USB!?!?).
** FIXME: logging should be removed here and added to a /proc file instead
*/
void
acx_s_parse_configoption(acx_device_t *adev, const acx111_ie_configoption_t *pcfg)
{
	const u8 *pEle;
	int i;
	int is_acx111 = IS_ACX111(adev);

	if (acx_debug & L_DEBUG) {
		printk("configoption struct content:\n");
		acx_dump_bytes(pcfg, sizeof(*pcfg));
	}

	if (( is_acx111 && (adev->eeprom_version == 5))
	||  (!is_acx111 && (adev->eeprom_version == 4))
	||  (!is_acx111 && (adev->eeprom_version == 5))) {
		/* these versions are known to be supported */
	} else {
		printk("unknown chip and EEPROM version combination (%s, v%d), "
			"don't know how to parse config options yet. "
			"Please report\n", is_acx111 ? "ACX111" : "ACX100",
			adev->eeprom_version);
		return;
	}

	/* first custom-parse the first part which has chip-specific layout */

	pEle = (const u8 *) pcfg;

	pEle += 4; /* skip (type,len) header */

	memcpy(adev->cfgopt_NVSv, pEle, sizeof(adev->cfgopt_NVSv));
	pEle += sizeof(adev->cfgopt_NVSv);

	if (is_acx111) {
		adev->cfgopt_NVS_vendor_offs = le16_to_cpu(*(u16 *)pEle);
		pEle += sizeof(adev->cfgopt_NVS_vendor_offs);

		adev->cfgopt_probe_delay = 200; /* good default value? */
		pEle += 2; /* FIXME: unknown, value 0x0001 */
	} else {
		memcpy(adev->cfgopt_MAC, pEle, sizeof(adev->cfgopt_MAC));
		pEle += sizeof(adev->cfgopt_MAC);

		adev->cfgopt_probe_delay = le16_to_cpu(*(u16 *)pEle);
		pEle += sizeof(adev->cfgopt_probe_delay);
		if ((adev->cfgopt_probe_delay < 100) || (adev->cfgopt_probe_delay > 500)) {
			printk("strange probe_delay value %d, "
				"tweaking to 200\n", adev->cfgopt_probe_delay);
			adev->cfgopt_probe_delay = 200;
		}
	}

	adev->cfgopt_eof_memory = le32_to_cpu(*(u32 *)pEle);
	pEle += sizeof(adev->cfgopt_eof_memory);

	printk("NVS_vendor_offs:%04X probe_delay:%d eof_memory:%d\n",
		adev->cfgopt_NVS_vendor_offs,
		adev->cfgopt_probe_delay,
		adev->cfgopt_eof_memory);

	adev->cfgopt_dot11CCAModes = *pEle++;
	adev->cfgopt_dot11Diversity = *pEle++;
	adev->cfgopt_dot11ShortPreambleOption = *pEle++;
	adev->cfgopt_dot11PBCCOption = *pEle++;
	adev->cfgopt_dot11ChannelAgility = *pEle++;
	adev->cfgopt_dot11PhyType = *pEle++;
	adev->cfgopt_dot11TempType = *pEle++;
	printk("CCAModes:%02X Diversity:%02X ShortPreOpt:%02X "
		"PBCC:%02X ChanAgil:%02X PHY:%02X Temp:%02X\n",
		adev->cfgopt_dot11CCAModes,
		adev->cfgopt_dot11Diversity,
		adev->cfgopt_dot11ShortPreambleOption,
		adev->cfgopt_dot11PBCCOption,
		adev->cfgopt_dot11ChannelAgility,
		adev->cfgopt_dot11PhyType,
		adev->cfgopt_dot11TempType);

	/* then use common parsing for next part which has common layout */

	pEle++; /* skip table_count (6) */

	adev->cfgopt_antennas.type = pEle[0];
	adev->cfgopt_antennas.len = pEle[1];
	printk("AntennaID:%02X Len:%02X Data:",
			adev->cfgopt_antennas.type, adev->cfgopt_antennas.len);
	for (i = 0; i < pEle[1]; i++) {
		adev->cfgopt_antennas.list[i] = pEle[i+2];
		printk("%02X ", pEle[i+2]);
	}
	printk("\n");

	pEle += pEle[1] + 2;
	adev->cfgopt_power_levels.type = pEle[0];
	adev->cfgopt_power_levels.len = pEle[1];
	printk("PowerLevelID:%02X Len:%02X Data:",
		adev->cfgopt_power_levels.type, adev->cfgopt_power_levels.len);
	for (i = 0; i < pEle[1]; i++) {
		adev->cfgopt_power_levels.list[i] = le16_to_cpu(*(u16 *)&pEle[i*2+2]);
		printk("%04X ", adev->cfgopt_power_levels.list[i]);
	}
	printk("\n");

	pEle += pEle[1]*2 + 2;
	adev->cfgopt_data_rates.type = pEle[0];
	adev->cfgopt_data_rates.len = pEle[1];
	printk("DataRatesID:%02X Len:%02X Data:",
		adev->cfgopt_data_rates.type, adev->cfgopt_data_rates.len);
	for (i = 0; i < pEle[1]; i++) {
		adev->cfgopt_data_rates.list[i] = pEle[i+2];
		printk("%02X ", pEle[i+2]);
	}
	printk("\n");

	pEle += pEle[1] + 2;
	adev->cfgopt_domains.type = pEle[0];
	adev->cfgopt_domains.len = pEle[1];
	printk("DomainID:%02X Len:%02X Data:",
			adev->cfgopt_domains.type, adev->cfgopt_domains.len);
	for (i = 0; i < pEle[1]; i++) {
		adev->cfgopt_domains.list[i] = pEle[i+2];
		printk("%02X ", pEle[i+2]);
	}
	printk("\n");

	pEle += pEle[1] + 2;
	adev->cfgopt_product_id.type = pEle[0];
	adev->cfgopt_product_id.len = pEle[1];
	for (i = 0; i < pEle[1]; i++) {
		adev->cfgopt_product_id.list[i] = pEle[i+2];
	}
	printk("ProductID:%02X Len:%02X Data:%.*s\n",
		adev->cfgopt_product_id.type, adev->cfgopt_product_id.len,
		adev->cfgopt_product_id.len, (char *)adev->cfgopt_product_id.list);

	pEle += pEle[1] + 2;
	adev->cfgopt_manufacturer.type = pEle[0];
	adev->cfgopt_manufacturer.len = pEle[1];
	for (i = 0; i < pEle[1]; i++) {
		adev->cfgopt_manufacturer.list[i] = pEle[i+2];
	}
	printk("ManufacturerID:%02X Len:%02X Data:%.*s\n",
		adev->cfgopt_manufacturer.type, adev->cfgopt_manufacturer.len,
		adev->cfgopt_manufacturer.len, (char *)adev->cfgopt_manufacturer.list);
/*
	printk("EEPROM part:\n");
	for (i=0; i<58; i++) {
		printk("%02X =======>  0x%02X\n",
			i, (u8 *)adev->cfgopt_NVSv[i-2]);
	}
*/
}


/***********************************************************************
*/
static int __init
acx_e_init_module(void)
{
	int r1,r2;

	acx_struct_size_check();

	printk("acx: this driver is still EXPERIMENTAL\n"
		"acx: reading README file and/or Craig's HOWTO is "
		"recommended, visit http://acx100.sf.net in case "
		"of further questions/discussion\n");

#if defined(CONFIG_ACX_PCI)
	r1 = acxpci_e_init_module();
#else
	r1 = -EINVAL;
#endif
#if defined(CONFIG_ACX_USB)
	r2 = acxusb_e_init_module();
#else
	r2 = -EINVAL;
#endif
	if (r2 && r1) /* both failed! */
		return r2 ? r2 : r1;
	/* return success if at least one succeeded */
	return 0;
}

static void __exit
acx_e_cleanup_module(void)
{
#if defined(CONFIG_ACX_PCI)
	acxpci_e_cleanup_module();
#endif
#if defined(CONFIG_ACX_USB)
	acxusb_e_cleanup_module();
#endif
}

module_init(acx_e_init_module)
module_exit(acx_e_cleanup_module)
