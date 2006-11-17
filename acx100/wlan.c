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

/***********************************************************************
** This code is based on elements which are
** Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
** info@linux-wlan.com
** http://www.linux-wlan.com
*/

#include <linux/config.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/if_arp.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>

#include "acx.h"


/***********************************************************************
*/
#define LOG_BAD_EID(hdr,len,ie_ptr) acx_log_bad_eid(hdr, len, ((wlan_ie_t*)ie_ptr))

#define IE_EID(ie_ptr) (((wlan_ie_t*)(ie_ptr))->eid)
#define IE_LEN(ie_ptr) (((wlan_ie_t*)(ie_ptr))->len)
#define OFFSET(hdr,off) (WLAN_HDR_A3_DATAP(hdr) + (off))


/***********************************************************************
** wlan_mgmt_decode_XXX
**
** Given a complete frame in f->hdr, sets the pointers in f to
** the areas that correspond to the parts of the frame.
**
** Assumptions:
**	1) f->len and f->hdr are already set
**	2) f->len is the length of the MAC header + data, the FCS
**	   is NOT included
**	3) all members except len and hdr are zero
** Arguments:
**	f	frame structure
**
** Returns:
**	nothing
**
** Side effects:
**	frame structure members are pointing at their
**	respective portions of the frame buffer.
*/
void
wlan_mgmt_decode_beacon(wlan_fr_beacon_t * f)
{
	u8 *ie_ptr;
	u8 *end = (u8*)f->hdr + f->len;

	f->type = WLAN_FSTYPE_BEACON;

	/*-- Fixed Fields ----*/
	f->ts = (u64 *) OFFSET(f->hdr, WLAN_BEACON_OFF_TS);
	f->bcn_int = (u16 *) OFFSET(f->hdr, WLAN_BEACON_OFF_BCN_INT);
	f->cap_info = (u16 *) OFFSET(f->hdr, WLAN_BEACON_OFF_CAPINFO);

	/*-- Information elements */
	ie_ptr = OFFSET(f->hdr, WLAN_BEACON_OFF_SSID);
	while (ie_ptr < end) {
		switch (IE_EID(ie_ptr)) {
		case WLAN_EID_SSID:
			f->ssid = (wlan_ie_ssid_t *) ie_ptr;
			break;
		case WLAN_EID_SUPP_RATES:
			f->supp_rates = (wlan_ie_supp_rates_t *) ie_ptr;
			break;
		case WLAN_EID_EXT_RATES:
			f->ext_rates = (wlan_ie_supp_rates_t *) ie_ptr;
			break;
		case WLAN_EID_FH_PARMS:
			f->fh_parms = (wlan_ie_fh_parms_t *) ie_ptr;
			break;
		case WLAN_EID_DS_PARMS:
			f->ds_parms = (wlan_ie_ds_parms_t *) ie_ptr;
			break;
		case WLAN_EID_CF_PARMS:
			f->cf_parms = (wlan_ie_cf_parms_t *) ie_ptr;
			break;
		case WLAN_EID_IBSS_PARMS:
			f->ibss_parms = (wlan_ie_ibss_parms_t *) ie_ptr;
			break;
		case WLAN_EID_TIM:
			f->tim = (wlan_ie_tim_t *) ie_ptr;
			break;
		case WLAN_EID_ERP_INFO:
			f->erp = (wlan_ie_erp_t *) ie_ptr;
			break;

		case WLAN_EID_COUNTRY:
		/* was seen: 07 06 47 42 20 01 0D 14 */
		case WLAN_EID_PWR_CONSTRAINT:
		/* was seen by Ashwin Mansinghka <ashwin_man@yahoo.com> from
		Atheros-based PCI card in AP mode using madwifi drivers: */
		/* 20 01 00 */
		case WLAN_EID_NONERP:
		/* was seen from WRT54GS with OpenWrt: 2F 01 07 */
		case WLAN_EID_UNKNOWN128:
		/* was seen by Jacek Jablonski <conexion2000@gmail.com> from Orinoco AP */
		/* 80 06 00 60 1D 2C 3B 00 */
		case WLAN_EID_UNKNOWN133:
		/* was seen by David Bronaugh <dbronaugh@linuxboxen.org> from ???? */
		/* 85 1E 00 00 84 12 07 00 FF 00 11 00 61 70 63 31 */
		/* 63 73 72 30 34 32 00 00 00 00 00 00 00 00 00 25 */
		case WLAN_EID_UNKNOWN223:
		/* was seen by Carlos Martin <carlosmn@gmail.com> from ???? */
		/* DF 20 01 1E 04 00 00 00 06 63 09 02 FF 0F 30 30 */
		/* 30 42 36 42 33 34 30 39 46 31 00 00 00 00 00 00 00 00 */
		case WLAN_EID_GENERIC:
		/* WPA: hostap code:
			if (pos[1] >= 4 &&
				pos[2] == 0x00 && pos[3] == 0x50 &&
				pos[4] == 0xf2 && pos[5] == 1) {
				wpa = pos;
				wpa_len = pos[1] + 2;
			}
		TI x4 mode: seen DD 04 08 00 28 00
		(08 00 28 is TI's OUI)
		last byte is probably 0/1 - disabled/enabled
		*/
		case WLAN_EID_RSN:
		/* hostap does something with it:
			rsn = pos;
			rsn_len = pos[1] + 2;
		*/
			break;

		default:
			LOG_BAD_EID(f->hdr, f->len, ie_ptr);
			break;
		}
		ie_ptr = ie_ptr + 2 + IE_LEN(ie_ptr);
	}
}


#ifdef UNUSED
void wlan_mgmt_decode_ibssatim(wlan_fr_ibssatim_t * f)
{
	f->type = WLAN_FSTYPE_ATIM;
	/*-- Fixed Fields ----*/
	/*-- Information elements */
}
#endif /* UNUSED */

void
wlan_mgmt_decode_disassoc(wlan_fr_disassoc_t * f)
{
	f->type = WLAN_FSTYPE_DISASSOC;

	/*-- Fixed Fields ----*/
	f->reason = (u16 *) OFFSET(f->hdr, WLAN_DISASSOC_OFF_REASON);

	/*-- Information elements */
}


void
wlan_mgmt_decode_assocreq(wlan_fr_assocreq_t * f)
{
	u8 *ie_ptr;
	u8 *end = (u8*)f->hdr + f->len;


	f->type = WLAN_FSTYPE_ASSOCREQ;

	/*-- Fixed Fields ----*/
	f->cap_info = (u16 *) OFFSET(f->hdr, WLAN_ASSOCREQ_OFF_CAP_INFO);
	f->listen_int = (u16 *) OFFSET(f->hdr, WLAN_ASSOCREQ_OFF_LISTEN_INT);

	/*-- Information elements */
	ie_ptr = OFFSET(f->hdr, WLAN_ASSOCREQ_OFF_SSID);
	while (ie_ptr < end) {
		switch (IE_EID(ie_ptr)) {
		case WLAN_EID_SSID:
			f->ssid = (wlan_ie_ssid_t *) ie_ptr;
			break;
		case WLAN_EID_SUPP_RATES:
			f->supp_rates = (wlan_ie_supp_rates_t *) ie_ptr;
			break;
		case WLAN_EID_EXT_RATES:
			f->ext_rates = (wlan_ie_supp_rates_t *) ie_ptr;
			break;
		default:
			LOG_BAD_EID(f->hdr, f->len, ie_ptr);
			break;
		}
		ie_ptr = ie_ptr + 2 + IE_LEN(ie_ptr);
	}
}


void
wlan_mgmt_decode_assocresp(wlan_fr_assocresp_t * f)
{
	f->type = WLAN_FSTYPE_ASSOCRESP;

	/*-- Fixed Fields ----*/
	f->cap_info = (u16 *) OFFSET(f->hdr, WLAN_ASSOCRESP_OFF_CAP_INFO);
	f->status = (u16 *) OFFSET(f->hdr, WLAN_ASSOCRESP_OFF_STATUS);
	f->aid = (u16 *) OFFSET(f->hdr, WLAN_ASSOCRESP_OFF_AID);

	/*-- Information elements */
	f->supp_rates = (wlan_ie_supp_rates_t *)
			OFFSET(f->hdr, WLAN_ASSOCRESP_OFF_SUPP_RATES);
}


#ifdef UNUSED
void
wlan_mgmt_decode_reassocreq(wlan_fr_reassocreq_t * f)
{
	u8 *ie_ptr;
	u8 *end = (u8*)f->hdr + f->len;

	f->type = WLAN_FSTYPE_REASSOCREQ;

	/*-- Fixed Fields ----*/
	f->cap_info = (u16 *) OFFSET(f->hdr, WLAN_REASSOCREQ_OFF_CAP_INFO);
	f->listen_int = (u16 *) OFFSET(f->hdr, WLAN_REASSOCREQ_OFF_LISTEN_INT);
	f->curr_ap = (u8 *) OFFSET(f->hdr, WLAN_REASSOCREQ_OFF_CURR_AP);

	/*-- Information elements */
	ie_ptr = OFFSET(f->hdr, WLAN_REASSOCREQ_OFF_SSID);
	while (ie_ptr < end) {
		switch (IE_EID(ie_ptr)) {
		case WLAN_EID_SSID:
			f->ssid = (wlan_ie_ssid_t *) ie_ptr;
			break;
		case WLAN_EID_SUPP_RATES:
			f->supp_rates = (wlan_ie_supp_rates_t *) ie_ptr;
			break;
		case WLAN_EID_EXT_RATES:
			f->ext_rates = (wlan_ie_supp_rates_t *) ie_ptr;
			break;
		default:
			LOG_BAD_EID(f->hdr, f->len, ie_ptr);
			break;
		}
		ie_ptr = ie_ptr + 2 + IE_LEN(ie_ptr);
	}
}


void
wlan_mgmt_decode_reassocresp(wlan_fr_reassocresp_t * f)
{
	f->type = WLAN_FSTYPE_REASSOCRESP;

	/*-- Fixed Fields ----*/
	f->cap_info = (u16 *) OFFSET(f->hdr, WLAN_REASSOCRESP_OFF_CAP_INFO);
	f->status = (u16 *) OFFSET(f->hdr, WLAN_REASSOCRESP_OFF_STATUS);
	f->aid = (u16 *) OFFSET(f->hdr, WLAN_REASSOCRESP_OFF_AID);

	/*-- Information elements */
	f->supp_rates = (wlan_ie_supp_rates_t *)
			OFFSET(f->hdr, WLAN_REASSOCRESP_OFF_SUPP_RATES);
}


void
wlan_mgmt_decode_probereq(wlan_fr_probereq_t * f)
{
	u8 *ie_ptr;
	u8 *end = (u8*)f->hdr + f->len;

	f->type = WLAN_FSTYPE_PROBEREQ;

	/*-- Fixed Fields ----*/

	/*-- Information elements */
	ie_ptr = OFFSET(f->hdr, WLAN_PROBEREQ_OFF_SSID);
	while (ie_ptr < end) {
		switch (IE_EID(ie_ptr)) {
		case WLAN_EID_SSID:
			f->ssid = (wlan_ie_ssid_t *) ie_ptr;
			break;
		case WLAN_EID_SUPP_RATES:
			f->supp_rates = (wlan_ie_supp_rates_t *) ie_ptr;
			break;
		case WLAN_EID_EXT_RATES:
			f->ext_rates = (wlan_ie_supp_rates_t *) ie_ptr;
			break;
		default:
			LOG_BAD_EID(f->hdr, f->len, ie_ptr);
			break;
		}
		ie_ptr = ie_ptr + 2 + IE_LEN(ie_ptr);
	}
}
#endif /* UNUSED */


/* TODO: decoding of beacon and proberesp can be merged (similar structure) */
void
wlan_mgmt_decode_proberesp(wlan_fr_proberesp_t * f)
{
	u8 *ie_ptr;
	u8 *end = (u8*)f->hdr + f->len;

	f->type = WLAN_FSTYPE_PROBERESP;

	/*-- Fixed Fields ----*/
	f->ts = (u64 *) OFFSET(f->hdr, WLAN_PROBERESP_OFF_TS);
	f->bcn_int = (u16 *) OFFSET(f->hdr, WLAN_PROBERESP_OFF_BCN_INT);
	f->cap_info = (u16 *) OFFSET(f->hdr, WLAN_PROBERESP_OFF_CAP_INFO);

	/*-- Information elements */
	ie_ptr = OFFSET(f->hdr, WLAN_PROBERESP_OFF_SSID);
	while (ie_ptr < end) {
		switch (IE_EID(ie_ptr)) {
		case WLAN_EID_SSID:
			f->ssid = (wlan_ie_ssid_t *) ie_ptr;
			break;
		case WLAN_EID_SUPP_RATES:
			f->supp_rates = (wlan_ie_supp_rates_t *) ie_ptr;
			break;
		case WLAN_EID_EXT_RATES:
			f->ext_rates = (wlan_ie_supp_rates_t *) ie_ptr;
			break;
		case WLAN_EID_FH_PARMS:
			f->fh_parms = (wlan_ie_fh_parms_t *) ie_ptr;
			break;
		case WLAN_EID_DS_PARMS:
			f->ds_parms = (wlan_ie_ds_parms_t *) ie_ptr;
			break;
		case WLAN_EID_CF_PARMS:
			f->cf_parms = (wlan_ie_cf_parms_t *) ie_ptr;
			break;
		case WLAN_EID_IBSS_PARMS:
			f->ibss_parms = (wlan_ie_ibss_parms_t *) ie_ptr;
			break;
#ifdef DONT_DO_IT_ADD_REAL_HANDLING_INSTEAD
		case WLAN_EID_COUNTRY:
			break;
		...
#endif
#ifdef SENT_HERE_BY_OPENWRT
		/* should those be trapped or handled?? */
		case WLAN_EID_ERP_INFO:
			break;
		case WLAN_EID_NONERP:
			break;
		case WLAN_EID_GENERIC:
			break;
#endif
		default:
			LOG_BAD_EID(f->hdr, f->len, ie_ptr);
			break;
		}

		ie_ptr = ie_ptr + 2 + IE_LEN(ie_ptr);
	}
}


void
wlan_mgmt_decode_authen(wlan_fr_authen_t * f)
{
	u8 *ie_ptr;
	u8 *end = (u8*)f->hdr + f->len;

	f->type = WLAN_FSTYPE_AUTHEN;

	/*-- Fixed Fields ----*/
	f->auth_alg = (u16 *) OFFSET(f->hdr, WLAN_AUTHEN_OFF_AUTH_ALG);
	f->auth_seq = (u16 *) OFFSET(f->hdr, WLAN_AUTHEN_OFF_AUTH_SEQ);
	f->status = (u16 *) OFFSET(f->hdr, WLAN_AUTHEN_OFF_STATUS);

	/*-- Information elements */
	ie_ptr = OFFSET(f->hdr, WLAN_AUTHEN_OFF_CHALLENGE);
	if ((ie_ptr < end) && (IE_EID(ie_ptr) == WLAN_EID_CHALLENGE)) {
		f->challenge = (wlan_ie_challenge_t *) ie_ptr;
	}
}


void
wlan_mgmt_decode_deauthen(wlan_fr_deauthen_t * f)
{
	f->type = WLAN_FSTYPE_DEAUTHEN;

	/*-- Fixed Fields ----*/
	f->reason = (u16 *) OFFSET(f->hdr, WLAN_DEAUTHEN_OFF_REASON);

	/*-- Information elements */
}
