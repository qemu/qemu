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

/* mini-doc

Here are all 11b/11g/11a rates and modulations:

     11b 11g 11a
     --- --- ---
 1  |B  |B  |
 2  |Q  |Q  |
 5.5|Cp |C p|
 6  |   |Od |O
 9  |   |od |o
11  |Cp |C p|
12  |   |Od |O
18  |   |od |o
22  |   |  p|
24  |   |Od |O
33  |   |  p|
36  |   |od |o
48  |   |od |o
54  |   |od |o

Mandatory:
 B - DBPSK (Differential Binary Phase Shift Keying)
 Q - DQPSK (Differential Quaternary Phase Shift Keying)
 C - CCK (Complementary Code Keying, a form of DSSS
		(Direct Sequence Spread Spectrum) modulation)
 O - OFDM (Orthogonal Frequency Division Multiplexing)
Optional:
 o - OFDM
 d - CCK-OFDM (also known as DSSS-OFDM)
 p - PBCC (Packet Binary Convolutional Coding)

The term CCK-OFDM may be used interchangeably with DSSS-OFDM
(the IEEE 802.11g-2003 standard uses the latter terminology).
In the CCK-OFDM, the PLCP header of the frame uses the CCK form of DSSS,
while the PLCP payload (the MAC frame) is modulated using OFDM.

Basically, you must use CCK-OFDM if you have mixed 11b/11g environment,
or else (pure OFDM) 11b equipment may not realize that AP
is sending a packet and start sending its own one.
Sadly, looks like acx111 does not support CCK-OFDM, only pure OFDM.

Re PBCC: avoid using it. It makes sense only if you have
TI "11b+" hardware. You _must_ use PBCC in order to reach 22Mbps on it.

Preambles:

Long preamble (at 1Mbit rate, takes 144 us):
    16 bytes	ones
     2 bytes	0xF3A0 (lsb sent first)
PLCP header follows (at 1Mbit also):
     1 byte	Signal: speed, in 0.1Mbit units, except for:
		33Mbit: 33 (instead of 330 - doesn't fit in octet)
		all CCK-OFDM rates: 30
     1 byte	Service
	0,1,4:	reserved
	2:	1=locked clock
	3:	1=PBCC
	5:	Length Extension (PBCC 22,33Mbit (11g only))  <-
	6:	Length Extension (PBCC 22,33Mbit (11g only))  <- BLACK MAGIC HERE
	7:	Length Extension                              <-
     2 bytes	Length (time needed to tx this frame)
		a) 5.5 Mbit/s CCK
		   Length = octets*8/5.5, rounded up to integer
		b) 11 Mbit/s CCK
		   Length = octets*8/11, rounded up to integer
		   Service bit 7:
			0 = rounding took less than 8/11
			1 = rounding took more than or equal to 8/11
		c) 5.5 Mbit/s PBCC
		   Length = (octets+1)*8/5.5, rounded up to integer
		d) 11 Mbit/s PBCC
		   Length = (octets+1)*8/11, rounded up to integer
		   Service bit 7:
			0 = rounding took less than 8/11
			1 = rounding took more than or equal to 8/11
		e) 22 Mbit/s PBCC
		   Length = (octets+1)*8/22, rounded up to integer
		   Service bits 6,7:
			00 = rounding took less than 8/22ths
			01 = rounding took 8/22...15/22ths
			10 = rounding took 16/22ths or more.
		f) 33 Mbit/s PBCC
		   Length = (octets+1)*8/33, rounded up to integer
		   Service bits 5,6,7:
			000 rounding took less than 8/33
			001 rounding took 8/33...15/33
			010 rounding took 16/33...23/33
			011 rounding took 24/33...31/33
			100 rounding took 32/33 or more
     2 bytes	CRC

PSDU follows (up to 2346 bytes at selected rate)

While Signal value alone is not enough to determine rate and modulation,
Signal+Service is always sufficient.

Short preamble (at 1Mbit rate, takes 72 us):
     7 bytes	zeroes
     2 bytes	0x05CF (lsb sent first)
PLCP header follows *at 2Mbit/s*. Format is the same as in long preamble.
PSDU follows (up to 2346 bytes at selected rate)

OFDM preamble is completely different, uses OFDM
modulation from the start and thus easily identifiable.
Not shown here.
*/


/***********************************************************************
** Constants
*/

#define WLAN_HDR_A3_LEN			24
#define WLAN_HDR_A4_LEN			30
/* IV structure:
** 3 bytes: Initialization Vector (24 bits)
** 1 byte: 0..5: padding, must be 0; 6..7: key selector (0-3)
*/
#define WLAN_WEP_IV_LEN			4
/* 802.11 says 2312 but looks like 2312 is a max size of _WEPed data_ */
#define WLAN_DATA_MAXLEN		2304
#define WLAN_WEP_ICV_LEN		4
#define WLAN_FCS_LEN			4
#define WLAN_A3FR_MAXLEN		(WLAN_HDR_A3_LEN + WLAN_DATA_MAXLEN)
#define WLAN_A4FR_MAXLEN		(WLAN_HDR_A4_LEN + WLAN_DATA_MAXLEN)
#define WLAN_A3FR_MAXLEN_FCS		(WLAN_HDR_A3_LEN + WLAN_DATA_MAXLEN + 4)
#define WLAN_A4FR_MAXLEN_FCS		(WLAN_HDR_A4_LEN + WLAN_DATA_MAXLEN + 4)
#define WLAN_A3FR_MAXLEN_WEP		(WLAN_A3FR_MAXLEN + 8)
#define WLAN_A4FR_MAXLEN_WEP		(WLAN_A4FR_MAXLEN + 8)
#define WLAN_A3FR_MAXLEN_WEP_FCS	(WLAN_A3FR_MAXLEN_FCS + 8)
#define WLAN_A4FR_MAXLEN_WEP_FCS	(WLAN_A4FR_MAXLEN_FCS + 8)

#define WLAN_BSS_TS_LEN			8
#define WLAN_SSID_MAXLEN		32
#define WLAN_BEACON_FR_MAXLEN		(WLAN_HDR_A3_LEN + 334)
#define WLAN_ATIM_FR_MAXLEN		(WLAN_HDR_A3_LEN + 0)
#define WLAN_DISASSOC_FR_MAXLEN		(WLAN_HDR_A3_LEN + 2)
#define WLAN_ASSOCREQ_FR_MAXLEN		(WLAN_HDR_A3_LEN + 48)
#define WLAN_ASSOCRESP_FR_MAXLEN	(WLAN_HDR_A3_LEN + 16)
#define WLAN_REASSOCREQ_FR_MAXLEN	(WLAN_HDR_A3_LEN + 54)
#define WLAN_REASSOCRESP_FR_MAXLEN	(WLAN_HDR_A3_LEN + 16)
#define WLAN_PROBEREQ_FR_MAXLEN		(WLAN_HDR_A3_LEN + 44)
#define WLAN_PROBERESP_FR_MAXLEN	(WLAN_HDR_A3_LEN + 78)
#define WLAN_AUTHEN_FR_MAXLEN		(WLAN_HDR_A3_LEN + 261)
#define WLAN_DEAUTHEN_FR_MAXLEN		(WLAN_HDR_A3_LEN + 2)
#define WLAN_CHALLENGE_IE_LEN		130
#define WLAN_CHALLENGE_LEN		128
#define WLAN_WEP_MAXKEYLEN		13
#define WLAN_WEP_NKEYS			4

/*--- Frame Control Field -------------------------------------*/
/* Frame Types */
#define WLAN_FTYPE_MGMT			0x00
#define WLAN_FTYPE_CTL			0x01
#define WLAN_FTYPE_DATA			0x02

/* Frame subtypes */
/* Management */
#define WLAN_FSTYPE_ASSOCREQ		0x00
#define WLAN_FSTYPE_ASSOCRESP		0x01
#define WLAN_FSTYPE_REASSOCREQ		0x02
#define WLAN_FSTYPE_REASSOCRESP		0x03
#define WLAN_FSTYPE_PROBEREQ		0x04
#define WLAN_FSTYPE_PROBERESP		0x05
#define WLAN_FSTYPE_BEACON		0x08
#define WLAN_FSTYPE_ATIM		0x09
#define WLAN_FSTYPE_DISASSOC		0x0a
#define WLAN_FSTYPE_AUTHEN		0x0b
#define WLAN_FSTYPE_DEAUTHEN		0x0c

/* Control */
#define WLAN_FSTYPE_PSPOLL		0x0a
#define WLAN_FSTYPE_RTS			0x0b
#define WLAN_FSTYPE_CTS			0x0c
#define WLAN_FSTYPE_ACK			0x0d
#define WLAN_FSTYPE_CFEND		0x0e
#define WLAN_FSTYPE_CFENDCFACK		0x0f

/* Data */
#define WLAN_FSTYPE_DATAONLY		0x00
#define WLAN_FSTYPE_DATA_CFACK		0x01
#define WLAN_FSTYPE_DATA_CFPOLL		0x02
#define WLAN_FSTYPE_DATA_CFACK_CFPOLL	0x03
#define WLAN_FSTYPE_NULL		0x04
#define WLAN_FSTYPE_CFACK		0x05
#define WLAN_FSTYPE_CFPOLL		0x06
#define WLAN_FSTYPE_CFACK_CFPOLL	0x07

/*--- FC Constants v. 2.0 ------------------------------------*/
/* Each constant is defined twice: WF_CONST is in host        */
/* byteorder, WF_CONSTi is in ieee byteorder.                 */
/* Usage:                                                     */
/* printf("the frame subtype is %X", WF_FC_FTYPEi & rx.fc);   */
/* tx.fc = WF_FTYPE_CTLi | WF_FSTYPE_RTSi;                    */
/*------------------------------------------------------------*/

enum {
/*--- Frame Control Field -------------------------------------*/
/* Protocol version: always 0 for current 802.11 standards */
IEEE16(WF_FC_PVER,			0x0003)
IEEE16(WF_FC_FTYPE,			0x000c)
IEEE16(WF_FC_FSTYPE,			0x00f0)
IEEE16(WF_FC_TODS,			0x0100)
IEEE16(WF_FC_FROMDS,			0x0200)
IEEE16(WF_FC_FROMTODS,			0x0300)
IEEE16(WF_FC_MOREFRAG,			0x0400)
IEEE16(WF_FC_RETRY,			0x0800)
/* Indicates PS mode in which STA will be after successful completion
** of current frame exchange sequence. Always 0 for AP frames */
IEEE16(WF_FC_PWRMGT,			0x1000)
/* What MoreData=1 means:
** From AP to STA in PS mode: don't sleep yet, I have more frames for you
** From Contention-Free (CF) Pollable STA in response to a CF-Poll:
**   STA has buffered frames for transmission in response to next CF-Poll
** Bcast/mcast frames transmitted from AP:
**   when additional bcast/mcast frames remain to be transmitted by AP
**   during this beacon interval
** In all other cases MoreData=0 */
IEEE16(WF_FC_MOREDATA,			0x2000)
IEEE16(WF_FC_ISWEP,			0x4000)
IEEE16(WF_FC_ORDER,			0x8000)

/* Frame Types */
IEEE16(WF_FTYPE_MGMT,			0x00)
IEEE16(WF_FTYPE_CTL,			0x04)
IEEE16(WF_FTYPE_DATA,			0x08)

/* Frame subtypes */
/* Management */
IEEE16(WF_FSTYPE_ASSOCREQ,		0x00)
IEEE16(WF_FSTYPE_ASSOCRESP,		0x10)
IEEE16(WF_FSTYPE_REASSOCREQ,		0x20)
IEEE16(WF_FSTYPE_REASSOCRESP,		0x30)
IEEE16(WF_FSTYPE_PROBEREQ,		0x40)
IEEE16(WF_FSTYPE_PROBERESP,		0x50)
IEEE16(WF_FSTYPE_BEACON,		0x80)
IEEE16(WF_FSTYPE_ATIM,			0x90)
IEEE16(WF_FSTYPE_DISASSOC,		0xa0)
IEEE16(WF_FSTYPE_AUTHEN,		0xb0)
IEEE16(WF_FSTYPE_DEAUTHEN,		0xc0)

/* Control */
IEEE16(WF_FSTYPE_PSPOLL,		0xa0)
IEEE16(WF_FSTYPE_RTS,			0xb0)
IEEE16(WF_FSTYPE_CTS,			0xc0)
IEEE16(WF_FSTYPE_ACK,			0xd0)
IEEE16(WF_FSTYPE_CFEND,			0xe0)
IEEE16(WF_FSTYPE_CFENDCFACK,		0xf0)

/* Data */
IEEE16(WF_FSTYPE_DATAONLY,		0x00)
IEEE16(WF_FSTYPE_DATA_CFACK,		0x10)
IEEE16(WF_FSTYPE_DATA_CFPOLL,		0x20)
IEEE16(WF_FSTYPE_DATA_CFACK_CFPOLL,	0x30)
IEEE16(WF_FSTYPE_NULL,			0x40)
IEEE16(WF_FSTYPE_CFACK,			0x50)
IEEE16(WF_FSTYPE_CFPOLL,		0x60)
IEEE16(WF_FSTYPE_CFACK_CFPOLL,		0x70)
};


/***********************************************************************
** Macros
*/

/*--- Duration Macros ----------------------------------------*/
/* Macros to get/set the bitfields of the Duration Field      */
/*  - the duration value is only valid when bit15 is zero     */
/*  - the firmware handles these values, so I'm not going     */
/*    these macros right now.                                 */
/*------------------------------------------------------------*/

/*--- Sequence Control  Macros -------------------------------*/
/* Macros to get/set the bitfields of the Sequence Control    */
/* Field.                                                     */
/*------------------------------------------------------------*/
#define WLAN_GET_SEQ_FRGNUM(n) ((u16)(n) & 0x000f)
#define WLAN_GET_SEQ_SEQNUM(n) (((u16)(n) & 0xfff0) >> 4)

/*--- Data ptr macro -----------------------------------------*/
/* Creates a u8* to the data portion of a frame               */
/* Assumes you're passing in a ptr to the beginning of the hdr*/
/*------------------------------------------------------------*/
#define WLAN_HDR_A3_DATAP(p) (((u8*)(p)) + WLAN_HDR_A3_LEN)
#define WLAN_HDR_A4_DATAP(p) (((u8*)(p)) + WLAN_HDR_A4_LEN)


/***********************************************************************
** Types
*/

/* 802.11 header type
**
** Note the following:
** a1 *always* is receiver's mac or bcast/mcast
** a2 *always* is transmitter's mac, if a2 exists
** seq: [0:3] frag#, [4:15] seq# - used for dup detection
** (dups from retries have same seq#) */
typedef struct wlan_hdr {
	u16	fc;
	u16	dur;
	u8	a1[ETH_ALEN];
	u8	a2[ETH_ALEN];
	u8	a3[ETH_ALEN];
	u16	seq;
	u8	a4[ETH_ALEN];
} WLAN_PACKED wlan_hdr_t;

/* Separate structs for use if frame type is known */
typedef struct wlan_hdr_a3 {
	u16	fc;
	u16	dur;
	u8	a1[ETH_ALEN];
	u8	a2[ETH_ALEN];
	u8	a3[ETH_ALEN];
	u16	seq;
} WLAN_PACKED wlan_hdr_a3_t;

typedef struct wlan_hdr_mgmt {
	u16	fc;
	u16	dur;
	u8	da[ETH_ALEN];
	u8	sa[ETH_ALEN];
	u8	bssid[ETH_ALEN];
	u16	seq;
} WLAN_PACKED wlan_hdr_mgmt_t;

#ifdef NOT_NEEDED_YET
typedef struct { /* ad-hoc peer->peer (to/from DS = 0/0) */
	u16	fc;
	u16	dur;
	u8	da[ETH_ALEN];
	u8	sa[ETH_ALEN];
	u8	bssid[ETH_ALEN];
	u16	seq;
} WLAN_PACKED ibss;
typedef struct { /* ap->sta (to/from DS = 0/1) */
	u16	fc;
	u16	dur;
	u8	da[ETH_ALEN];
	u8	bssid[ETH_ALEN];
	u8	sa[ETH_ALEN];
	u16	seq;
} WLAN_PACKED fromap;
typedef struct { /* sta->ap (to/from DS = 1/0) */
	u16	fc;
	u16	dur;
	u8	bssid[ETH_ALEN];
	u8	sa[ETH_ALEN];
	u8	da[ETH_ALEN];
	u16	seq;
} WLAN_PACKED toap;
typedef struct { /* wds->wds (to/from DS = 1/1), the only 4addr pkt */
	u16	fc;
	u16	dur;
	u8	ra[ETH_ALEN];
	u8	ta[ETH_ALEN];
	u8	da[ETH_ALEN];
	u16	seq;
	u8	sa[ETH_ALEN];
} WLAN_PACKED wds;
typedef struct { /* all management packets */
	u16	fc;
	u16	dur;
	u8	da[ETH_ALEN];
	u8	sa[ETH_ALEN];
	u8	bssid[ETH_ALEN];
	u16	seq;
} WLAN_PACKED mgmt;
typedef struct { /* has no body, just a FCS */
	u16	fc;
	u16	dur;
	u8	ra[ETH_ALEN];
	u8	ta[ETH_ALEN];
} WLAN_PACKED rts;
typedef struct { /* has no body, just a FCS */
	u16	fc;
	u16	dur;
	u8	ra[ETH_ALEN];
} WLAN_PACKED cts;
typedef struct { /* has no body, just a FCS */
	u16	fc;
	u16	dur;
	u8	ra[ETH_ALEN];
} WLAN_PACKED ack;
typedef struct { /* has no body, just a FCS */
	u16	fc;
	/* NB: this one holds Assoc ID in dur field: */
	u16	aid;
	u8	bssid[ETH_ALEN];
	u8	ta[ETH_ALEN];
} WLAN_PACKED pspoll;
typedef struct { /* has no body, just a FCS */
	u16	fc;
	u16	dur;
	u8	ra[ETH_ALEN];
	u8	bssid[ETH_ALEN];
} WLAN_PACKED cfend;
typedef struct { /* has no body, just a FCS */
	u16	fc;
	u16	dur;
	u8	ra[ETH_ALEN];
	u8	bssid[ETH_ALEN];
} WLAN_PACKED cfendcfack;
#endif

/* Prism header emulation (monitor mode) */
typedef struct wlanitem_u32 {
	u32	did;
	u16	status;
	u16	len;
	u32	data;
} WLAN_PACKED wlanitem_u32_t;
#define WLANITEM_STATUS_data_ok			0
#define WLANITEM_STATUS_no_value		1
#define WLANITEM_STATUS_invalid_itemname	2
#define WLANITEM_STATUS_invalid_itemdata	3
#define WLANITEM_STATUS_missing_itemdata	4
#define WLANITEM_STATUS_incomplete_itemdata	5
#define WLANITEM_STATUS_invalid_msg_did		6
#define WLANITEM_STATUS_invalid_mib_did		7
#define WLANITEM_STATUS_missing_conv_func	8
#define WLANITEM_STATUS_string_too_long		9
#define WLANITEM_STATUS_data_out_of_range	10
#define WLANITEM_STATUS_string_too_short	11
#define WLANITEM_STATUS_missing_valid_func	12
#define WLANITEM_STATUS_unknown			13
#define WLANITEM_STATUS_invalid_did		14
#define WLANITEM_STATUS_missing_print_func	15

#define WLAN_DEVNAMELEN_MAX	16
typedef struct wlansniffrm {
	u32		msgcode;
	u32		msglen;
	u8		devname[WLAN_DEVNAMELEN_MAX];
	wlanitem_u32_t	hosttime;
	wlanitem_u32_t	mactime;
	wlanitem_u32_t	channel;
	wlanitem_u32_t	rssi;
	wlanitem_u32_t	sq;
	wlanitem_u32_t	signal;
	wlanitem_u32_t	noise;
	wlanitem_u32_t	rate;
	wlanitem_u32_t	istx;	/* tx? 0:no 1:yes */
	wlanitem_u32_t	frmlen;
} WLAN_PACKED wlansniffrm_t;
#define WLANSNIFFFRM		0x0041
#define WLANSNIFFFRM_hosttime	0x1041
#define WLANSNIFFFRM_mactime	0x2041
#define WLANSNIFFFRM_channel	0x3041
#define WLANSNIFFFRM_rssi	0x4041
#define WLANSNIFFFRM_sq		0x5041
#define WLANSNIFFFRM_signal	0x6041
#define WLANSNIFFFRM_noise	0x7041
#define WLANSNIFFFRM_rate	0x8041
#define WLANSNIFFFRM_istx	0x9041
#define WLANSNIFFFRM_frmlen	0xA041
