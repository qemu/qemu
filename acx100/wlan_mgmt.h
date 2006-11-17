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

/***********************************************************************
** Constants
*/

/*-- Information Element IDs --------------------*/
#define WLAN_EID_SSID		0
#define WLAN_EID_SUPP_RATES	1
#define WLAN_EID_FH_PARMS	2
#define WLAN_EID_DS_PARMS	3
#define WLAN_EID_CF_PARMS	4
#define WLAN_EID_TIM		5
#define WLAN_EID_IBSS_PARMS	6
#define WLAN_EID_COUNTRY	7 /* 802.11d */
#define WLAN_EID_FH_HOP_PARMS	8 /* 802.11d */
#define WLAN_EID_FH_TABLE	9 /* 802.11d */
#define WLAN_EID_REQUEST	10 /* 802.11d */
/*-- values 11-15 reserved --*/
#define WLAN_EID_CHALLENGE	16
/*-- values 17-31 reserved for challenge text extension --*/
#define WLAN_EID_PWR_CONSTRAINT	32	/* 11h PowerConstraint */
#define WLAN_EID_ERP_INFO	42	/* was seen from WRT54GS with OpenWrt */
#define WLAN_EID_NONERP		47	/* was seen from WRT54GS with OpenWrt */
#define WLAN_EID_RSN		48
#define WLAN_EID_EXT_RATES	50
#define WLAN_EID_UNKNOWN128	128
#define WLAN_EID_UNKNOWN133	133
#define WLAN_EID_GENERIC	221	/* was seen from WRT54GS with OpenWrt */
#define WLAN_EID_UNKNOWN223	223

#if 0
#define WLAN_EID_PWR_CAP		33	/* 11h PowerCapability */
#define WLAN_EID_TPC_REQUEST		34	/* 11h TPC Request */
#define WLAN_EID_TPC_REPORT		35	/* 11h TPC Report */
#define WLAN_EID_SUPP_CHANNELS		36	/* 11h Supported Channels */
#define WLAN_EID_CHANNEL_SWITCH		37	/* 11h ChannelSwitch */
#define WLAN_EID_MEASURE_REQUEST	38	/* 11h MeasurementRequest */
#define WLAN_EID_MEASURE_REPORT		39	/* 11h MeasurementReport */
#define WLAN_EID_QUIET_ID		40	/* 11h Quiet */
#define WLAN_EID_IBSS_DFS_ID		41	/* 11h IBSS_DFS */
#endif

/*-- Reason Codes -------------------------------*/
#define WLAN_MGMT_REASON_RSVD			0
#define WLAN_MGMT_REASON_UNSPEC			1
#define WLAN_MGMT_REASON_PRIOR_AUTH_INVALID	2
#define WLAN_MGMT_REASON_DEAUTH_LEAVING		3
#define WLAN_MGMT_REASON_DISASSOC_INACTIVE	4
#define WLAN_MGMT_REASON_DISASSOC_AP_BUSY	5
#define WLAN_MGMT_REASON_CLASS2_NONAUTH		6
#define WLAN_MGMT_REASON_CLASS3_NONASSOC	7
#define WLAN_MGMT_REASON_DISASSOC_STA_HASLEFT	8
#define WLAN_MGMT_REASON_CANT_ASSOC_NONAUTH	9

/*-- Status Codes -------------------------------*/
#define WLAN_MGMT_STATUS_SUCCESS		0
#define WLAN_MGMT_STATUS_UNSPEC_FAILURE		1
#define WLAN_MGMT_STATUS_CAPS_UNSUPPORTED	10
#define WLAN_MGMT_STATUS_REASSOC_NO_ASSOC	11
#define WLAN_MGMT_STATUS_ASSOC_DENIED_UNSPEC	12
#define WLAN_MGMT_STATUS_UNSUPPORTED_AUTHALG	13
#define WLAN_MGMT_STATUS_RX_AUTH_NOSEQ		14
#define WLAN_MGMT_STATUS_CHALLENGE_FAIL		15
#define WLAN_MGMT_STATUS_AUTH_TIMEOUT		16
#define WLAN_MGMT_STATUS_ASSOC_DENIED_BUSY	17
#define WLAN_MGMT_STATUS_ASSOC_DENIED_RATES	18
/* p80211b additions */
#define WLAN_MGMT_STATUS_ASSOC_DENIED_NOSHORT	19
#define WLAN_MGMT_STATUS_ASSOC_DENIED_NOPBCC	20
#define WLAN_MGMT_STATUS_ASSOC_DENIED_NOAGILITY	21

/*-- Auth Algorithm Field ---------------------------*/
#define WLAN_AUTH_ALG_OPENSYSTEM		0
#define WLAN_AUTH_ALG_SHAREDKEY			1

/*-- Management Frame Field Offsets -------------*/
/* Note: Not all fields are listed because of variable lengths */
/* Note: These offsets are from the start of the frame data */

#define WLAN_BEACON_OFF_TS			0
#define WLAN_BEACON_OFF_BCN_INT			8
#define WLAN_BEACON_OFF_CAPINFO			10
#define WLAN_BEACON_OFF_SSID			12

#define WLAN_DISASSOC_OFF_REASON		0

#define WLAN_ASSOCREQ_OFF_CAP_INFO		0
#define WLAN_ASSOCREQ_OFF_LISTEN_INT		2
#define WLAN_ASSOCREQ_OFF_SSID			4

#define WLAN_ASSOCRESP_OFF_CAP_INFO		0
#define WLAN_ASSOCRESP_OFF_STATUS		2
#define WLAN_ASSOCRESP_OFF_AID			4
#define WLAN_ASSOCRESP_OFF_SUPP_RATES		6

#define WLAN_REASSOCREQ_OFF_CAP_INFO		0
#define WLAN_REASSOCREQ_OFF_LISTEN_INT		2
#define WLAN_REASSOCREQ_OFF_CURR_AP		4
#define WLAN_REASSOCREQ_OFF_SSID		10

#define WLAN_REASSOCRESP_OFF_CAP_INFO		0
#define WLAN_REASSOCRESP_OFF_STATUS		2
#define WLAN_REASSOCRESP_OFF_AID		4
#define WLAN_REASSOCRESP_OFF_SUPP_RATES		6

#define WLAN_PROBEREQ_OFF_SSID			0

#define WLAN_PROBERESP_OFF_TS			0
#define WLAN_PROBERESP_OFF_BCN_INT		8
#define WLAN_PROBERESP_OFF_CAP_INFO		10
#define WLAN_PROBERESP_OFF_SSID			12

#define WLAN_AUTHEN_OFF_AUTH_ALG		0
#define WLAN_AUTHEN_OFF_AUTH_SEQ		2
#define WLAN_AUTHEN_OFF_STATUS			4
#define WLAN_AUTHEN_OFF_CHALLENGE		6

#define WLAN_DEAUTHEN_OFF_REASON		0

enum {
IEEE16(WF_MGMT_CAP_ESS,		0x0001)
IEEE16(WF_MGMT_CAP_IBSS,	0x0002)
/* In (re)assoc request frames by STA:
** Pollable=0, PollReq=0: STA is not CF-Pollable
** 0 1: STA is CF-Pollable, not requesting to be placed on the CF-Polling list
** 1 0: STA is CF-Pollable, requesting to be placed on the CF-Polling list
** 1 1: STA is CF-Pollable, requesting never to be polled
** In beacon, proberesp, (re)assoc resp frames by AP:
** 0 0: No point coordinator at AP
** 0 1: Point coordinator at AP for delivery only (no polling)
** 1 0: Point coordinator at AP for delivery and polling
** 1 1: Reserved  */
IEEE16(WF_MGMT_CAP_CFPOLLABLE,	0x0004)
IEEE16(WF_MGMT_CAP_CFPOLLREQ,	0x0008)
/* 1=non-WEP data frames are disallowed */
IEEE16(WF_MGMT_CAP_PRIVACY,	0x0010)
/* In beacon,  proberesp, (re)assocresp by AP/AdHoc:
** 1=use of shortpre is allowed ("I can receive shortpre") */
IEEE16(WF_MGMT_CAP_SHORT,	0x0020)
IEEE16(WF_MGMT_CAP_PBCC,	0x0040)
IEEE16(WF_MGMT_CAP_AGILITY,	0x0080)
/* In (re)assoc request frames by STA:
** 1=short slot time implemented and enabled
**   NB: AP shall use long slot time beginning at the next Beacon after assoc
**   of STA with this bit set to 0
** In beacon, proberesp, (re)assoc resp frames by AP:
** currently used slot time value: 0/1 - long/short */
IEEE16(WF_MGMT_CAP_SHORTSLOT,	0x0400)
/* In (re)assoc request frames by STA: 1=CCK-OFDM is implemented and enabled
** In beacon, proberesp, (re)assoc resp frames by AP/AdHoc:
** 1=CCK-OFDM is allowed */
IEEE16(WF_MGMT_CAP_CCKOFDM,	0x2000)
};


/***********************************************************************
** Types
*/

/* Information Element types */

/* prototype structure, all IEs start with these members */
typedef struct wlan_ie {
	u8 eid;
	u8 len;
} WLAN_PACKED wlan_ie_t;

/*-- Service Set Identity (SSID)  -----------------*/
typedef struct wlan_ie_ssid {
	u8 eid;
	u8 len;
	u8 ssid[1];	/* may be zero */
} WLAN_PACKED wlan_ie_ssid_t;

/*-- Supported Rates  -----------------------------*/
typedef struct wlan_ie_supp_rates {
	u8 eid;
	u8 len;
	u8 rates[1];	/* had better be at LEAST one! */
} WLAN_PACKED wlan_ie_supp_rates_t;

/*-- FH Parameter Set  ----------------------------*/
typedef struct wlan_ie_fh_parms {
	u8 eid;
	u8 len;
	u16 dwell;
	u8 hopset;
	u8 hoppattern;
	u8 hopindex;
} WLAN_PACKED wlan_ie_fh_parms_t;

/*-- DS Parameter Set  ----------------------------*/
typedef struct wlan_ie_ds_parms {
	u8 eid;
	u8 len;
	u8 curr_ch;
} WLAN_PACKED wlan_ie_ds_parms_t;

/*-- CF Parameter Set  ----------------------------*/
typedef struct wlan_ie_cf_parms {
	u8 eid;
	u8 len;
	u8 cfp_cnt;
	u8 cfp_period;
	u16 cfp_maxdur;
	u16 cfp_durremaining;
} WLAN_PACKED wlan_ie_cf_parms_t;

/*-- TIM ------------------------------------------*/
typedef struct wlan_ie_tim {
	u8 eid;
	u8 len;
	u8 dtim_cnt;
	u8 dtim_period;
	u8 bitmap_ctl;
	u8 virt_bm[1];
} WLAN_PACKED wlan_ie_tim_t;

/*-- IBSS Parameter Set ---------------------------*/
typedef struct wlan_ie_ibss_parms {
	u8 eid;
	u8 len;
	u16 atim_win;
} WLAN_PACKED wlan_ie_ibss_parms_t;

/*-- Challenge Text  ------------------------------*/
typedef struct wlan_ie_challenge {
	u8 eid;
	u8 len;
	u8 challenge[1];
} WLAN_PACKED wlan_ie_challenge_t;

/*-- ERP (42) -------------------------------------*/
typedef struct wlan_ie_erp {
	u8 eid;
	u8 len;
	/* bit 0:Non ERP present
	**     1:Use Protection
	**     2:Barker Preamble mode
	**     3-7:reserved */
	u8 erp;
} WLAN_PACKED wlan_ie_erp_t;

/* Types for parsing mgmt frames */

/* prototype structure, all mgmt frame types will start with these members */
typedef struct wlan_fr_mgmt {
	u16 type;
	u16 len;		/* DOES NOT include FCS */
	wlan_hdr_t *hdr;
	/* used for target specific data, skb in Linux */
	/*-- fixed fields -----------*/
	/*-- info elements ----------*/
} WLAN_PACKED wlan_fr_mgmt_t;

/*-- Beacon ---------------------------------------*/
typedef struct wlan_fr_beacon {
	u16 type;
	u16 len;
	wlan_hdr_t *hdr;
	/*-- fixed fields -----------*/
	u64 *ts;
	u16 *bcn_int;
	u16 *cap_info;
	/*-- info elements ----------*/
	wlan_ie_ssid_t *ssid;
	wlan_ie_supp_rates_t *supp_rates;
	wlan_ie_supp_rates_t *ext_rates;
	wlan_ie_fh_parms_t *fh_parms;
	wlan_ie_ds_parms_t *ds_parms;
	wlan_ie_cf_parms_t *cf_parms;
	wlan_ie_ibss_parms_t *ibss_parms;
	wlan_ie_tim_t *tim;	/* in beacon only, not proberesp */
	wlan_ie_erp_t *erp;	/* in beacon only, not proberesp */
} wlan_fr_beacon_t;
#define wlan_fr_proberesp wlan_fr_beacon
#define wlan_fr_proberesp_t wlan_fr_beacon_t

/*-- IBSS ATIM ------------------------------------*/
typedef struct wlan_fr_ibssatim {
	u16 type;
	u16 len;
	wlan_hdr_t *hdr;
	/*-- fixed fields -----------*/
	/*-- info elements ----------*/
	/* this frame type has a null body */
} wlan_fr_ibssatim_t;

/*-- Disassociation -------------------------------*/
typedef struct wlan_fr_disassoc {
	u16 type;
	u16 len;
	wlan_hdr_t *hdr;
	/*-- fixed fields -----------*/
	u16 *reason;
	/*-- info elements ----------*/
} wlan_fr_disassoc_t;

/*-- Association Request --------------------------*/
typedef struct wlan_fr_assocreq {
	u16 type;
	u16 len;
	wlan_hdr_t *hdr;
	/*-- fixed fields -----------*/
	u16 *cap_info;
	u16 *listen_int;
	/*-- info elements ----------*/
	wlan_ie_ssid_t *ssid;
	wlan_ie_supp_rates_t *supp_rates;
	wlan_ie_supp_rates_t *ext_rates;
} wlan_fr_assocreq_t;

/*-- Association Response -------------------------*/
typedef struct wlan_fr_assocresp {
	u16 type;
	u16 len;
	wlan_hdr_t *hdr;
	/*-- fixed fields -----------*/
	u16 *cap_info;
	u16 *status;
	u16 *aid;
	/*-- info elements ----------*/
	wlan_ie_supp_rates_t *supp_rates;
	wlan_ie_supp_rates_t *ext_rates;
} wlan_fr_assocresp_t;

/*-- Reassociation Request ------------------------*/
typedef struct wlan_fr_reassocreq {
	u16 type;
	u16 len;
	wlan_hdr_t *hdr;
	/*-- fixed fields -----------*/
	u16 *cap_info;
	u16 *listen_int;
	u8 *curr_ap;
	/*-- info elements ----------*/
	wlan_ie_ssid_t *ssid;
	wlan_ie_supp_rates_t *supp_rates;
	wlan_ie_supp_rates_t *ext_rates;
} wlan_fr_reassocreq_t;

/*-- Reassociation Response -----------------------*/
typedef struct wlan_fr_reassocresp {
	u16 type;
	u16 len;
	wlan_hdr_t *hdr;
	/*-- fixed fields -----------*/
	u16 *cap_info;
	u16 *status;
	u16 *aid;
	/*-- info elements ----------*/
	wlan_ie_supp_rates_t *supp_rates;
	wlan_ie_supp_rates_t *ext_rates;
} wlan_fr_reassocresp_t;

/*-- Probe Request --------------------------------*/
typedef struct wlan_fr_probereq {
	u16 type;
	u16 len;
	wlan_hdr_t *hdr;
	/*-- fixed fields -----------*/
	/*-- info elements ----------*/
	wlan_ie_ssid_t *ssid;
	wlan_ie_supp_rates_t *supp_rates;
	wlan_ie_supp_rates_t *ext_rates;
} wlan_fr_probereq_t;

/*-- Authentication -------------------------------*/
typedef struct wlan_fr_authen {
	u16 type;
	u16 len;
	wlan_hdr_t *hdr;
	/*-- fixed fields -----------*/
	u16 *auth_alg;
	u16 *auth_seq;
	u16 *status;
	/*-- info elements ----------*/
	wlan_ie_challenge_t *challenge;
} wlan_fr_authen_t;

/*-- Deauthenication -----------------------------*/
typedef struct wlan_fr_deauthen {
	u16 type;
	u16 len;
	wlan_hdr_t *hdr;
	/*-- fixed fields -----------*/
	u16 *reason;
	/*-- info elements ----------*/
} wlan_fr_deauthen_t;

/* Types for building mgmt frames */

/* Warning. Several types used in below structs are
** in fact variable length. Use structs with such fields with caution */
typedef struct auth_frame_body {
	u16	auth_alg;
	u16	auth_seq;
	u16	status;
	wlan_ie_challenge_t challenge;
} WLAN_PACKED auth_frame_body_t;

typedef struct assocresp_frame_body {
	u16	cap_info;
	u16	status;
	u16	aid;
	wlan_ie_supp_rates_t rates;
} WLAN_PACKED assocresp_frame_body_t;

typedef struct reassocreq_frame_body {
	u16	cap_info;
	u16	listen_int;
	u8	current_ap[ETH_ALEN];
	wlan_ie_ssid_t ssid;
/* access to this one is disabled since ssid_t is variable length: */
     /* wlan_ie_supp_rates_t rates; */
} WLAN_PACKED reassocreq_frame_body_t;

typedef struct reassocresp_frame_body {
	u16	cap_info;
	u16	status;
	u16	aid;
	wlan_ie_supp_rates_t rates;
} WLAN_PACKED reassocresp_frame_body_t;

typedef struct deauthen_frame_body {
	u16	reason;
} WLAN_PACKED deauthen_frame_body_t;

typedef struct disassoc_frame_body {
	u16	reason;
} WLAN_PACKED disassoc_frame_body_t;

typedef struct probereq_frame_body {
	wlan_ie_ssid_t ssid;
	wlan_ie_supp_rates_t rates;
} WLAN_PACKED probereq_frame_body_t;

typedef struct proberesp_frame_body {
	u8	timestamp[8];
	u16	beacon_int;
	u16	cap_info;
	wlan_ie_ssid_t ssid;
/* access to these is disabled since ssid_t is variable length: */
     /* wlan_ie_supp_rates_t rates; */
     /* fhps_t	fhps; */
     /* dsps_t	dsps; */
     /* cfps_t	cfps; */
} WLAN_PACKED proberesp_frame_body_t;


/***********************************************************************
** Functions
*/

/* Helpers for parsing mgmt frames */
void wlan_mgmt_decode_ibssatim(wlan_fr_ibssatim_t *f);
void wlan_mgmt_decode_assocreq(wlan_fr_assocreq_t *f);
void wlan_mgmt_decode_assocresp(wlan_fr_assocresp_t *f);
void wlan_mgmt_decode_authen(wlan_fr_authen_t *f);
void wlan_mgmt_decode_beacon(wlan_fr_beacon_t *f);
void wlan_mgmt_decode_deauthen(wlan_fr_deauthen_t *f);
void wlan_mgmt_decode_disassoc(wlan_fr_disassoc_t *f);
void wlan_mgmt_decode_probereq(wlan_fr_probereq_t *f);
void wlan_mgmt_decode_proberesp(wlan_fr_proberesp_t *f);
void wlan_mgmt_decode_reassocreq(wlan_fr_reassocreq_t *f);
void wlan_mgmt_decode_reassocresp(wlan_fr_reassocresp_t *f);

/* Helpers for building mgmt frames */
static inline u8*
wlan_fill_ie_ssid(u8 *p, int len, const char *ssid)
{
	struct wlan_ie_ssid *ie = (void*)p;
	ie->eid = WLAN_EID_SSID;
	ie->len = len;
	memcpy(ie->ssid, ssid, len);
	return p + len + 2;
}
/* This controls whether we create 802.11g 'ext supported rates' IEs
** or just create overlong 'supported rates' IEs instead
** (non-11g compliant) */
#define WE_OBEY_802_11G 1
static inline u8*
wlan_fill_ie_rates(u8 *p, int len, const u8 *rates)
{
	struct wlan_ie_supp_rates *ie = (void*)p;
#if WE_OBEY_802_11G
	if (len > 8 ) len = 8;
#endif
	/* supported rates (1 to 8 octets) */
	ie->eid = WLAN_EID_SUPP_RATES;
	ie->len = len;
	memcpy(ie->rates, rates, len);
	return p + len + 2;
}
/* This one wouldn't create an IE at all if not needed */
static inline u8*
wlan_fill_ie_rates_ext(u8 *p, int len, const u8 *rates)
{
	struct wlan_ie_supp_rates *ie = (void*)p;
#if !WE_OBEY_802_11G
	return p;
#endif
	len -= 8;
	if (len <= 0) return p;
	/* ext supported rates */
	ie->eid = WLAN_EID_EXT_RATES;
	ie->len = len;
	memcpy(ie->rates, rates+8, len);
	return p + len + 2;
}
static inline u8*
wlan_fill_ie_ds_parms(u8 *p, int channel)
{
	struct wlan_ie_ds_parms *ie = (void*)p;
	ie->eid = WLAN_EID_DS_PARMS;
	ie->len = 1;
	ie->curr_ch = channel;
	return p + sizeof(*ie);
}
static inline u8*
wlan_fill_ie_ibss_parms(u8 *p, int atim_win)
{
	struct wlan_ie_ibss_parms *ie = (void*)p;
	ie->eid = WLAN_EID_IBSS_PARMS;
	ie->len = 2;
	ie->atim_win = atim_win;
	return p + sizeof(*ie);
}
static inline u8*
wlan_fill_ie_tim(u8 *p,	int rem, int period, int bcast,
		int ofs, int len, const u8 *vbm)
{
	struct wlan_ie_tim *ie = (void*)p;
	ie->eid = WLAN_EID_TIM;
	ie->len = len + 3;
	ie->dtim_cnt = rem;
	ie->dtim_period = period;
	ie->bitmap_ctl = ofs | (bcast!=0);
	if (vbm)
		memcpy(ie->virt_bm, vbm, len); /* min 1 byte */
	else
		ie->virt_bm[0] = 0;
	return p + len + 3 + 2;
}
