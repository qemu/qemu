/*
 * Copyright (c) 2004-2007 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2006-2007 Nick Kossifidis <mickflemm@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $Id$
 */

/*
 * HAL interface for Atheros Wireless LAN devices.
 *
 * ar5k is a free replacement of the binary-only HAL used by some drivers
 * for Atheros chipsets. While using a different ABI, it tries to be
 * source-compatible with the original (non-free) HAL interface.
 *
 * Many thanks to various contributors who supported the development of
 * ar5k with hard work and useful information. And, of course, for all the
 * people who encouraged me to continue this work which has been based
 * on my initial approach found on http://team.vantronix.net/ar5k/.
 */

#ifndef _AR5K_H
#define _AR5K_H




/*
 * Device tracing
 */

#define AR5K_TRACE_PRINTF(_trace)	AR5K_PRINTF(_trace)






/****************************\
  GENERIC DRIVER DEFINITIONS
\****************************/

/*
 * C doesn't support boolean ;-(
 * TODO: See if there is a bool definition somewere else
 * in the kernel, we shouldn't redefine it if it does...
 */
#ifndef TRUE
#define	TRUE	1
#endif
#ifndef FALSE
#define	FALSE	0
#endif
typedef u_int8_t AR5K_BOOL;

/*
 * Error codes reported from HAL to the driver
 */
typedef enum {
	AR5K_OK		= 0,	/* Everything went O.K.*/
	AR5K_ENOMEM	= 1,	/* Unable to allocate memory for ath_hal*/
	AR5K_EIO	= 2,	/* Hardware I/O Error*/
	AR5K_EELOCKED	= 3,	/* Unable to access EEPROM*/
	AR5K_EEBADSUM	= 4,	/* Invalid EEPROM checksum*/
	AR5K_EEREAD	= 5,	/* Unable to get device caps from EEPROM */
	AR5K_EEBADMAC	= 6,	/* Unable to read MAC address from EEPROM */
	AR5K_EINVAL	= 7,	/* Invalid parameter to function */
	AR5K_ENOTSUPP	= 8,	/* Hardware revision not supported */
	AR5K_EINPROGRESS= 9,	/* Unexpected error ocured during process */
} AR5K_STATUS;

/*
 * Some tuneable values (these should be changeable by the user)
 */
#define AR5K_TUNE_DMA_BEACON_RESP		2
#define AR5K_TUNE_SW_BEACON_RESP		10
#define AR5K_TUNE_ADDITIONAL_SWBA_BACKOFF	0
#define AR5K_TUNE_RADAR_ALERT			FALSE
#define AR5K_TUNE_MIN_TX_FIFO_THRES		1
#define AR5K_TUNE_MAX_TX_FIFO_THRES		((MAX_PDU_LENGTH / 64) + 1)
#define AR5K_TUNE_RSSI_THRES			1792
#define AR5K_TUNE_REGISTER_TIMEOUT		20000
#define AR5K_TUNE_REGISTER_DWELL_TIME		20000
#define AR5K_TUNE_BEACON_INTERVAL		100
#define AR5K_TUNE_AIFS				2
#define AR5K_TUNE_AIFS_11B			2
#define AR5K_TUNE_AIFS_XR			0
#define AR5K_TUNE_CWMIN				15
#define AR5K_TUNE_CWMIN_11B			31
#define AR5K_TUNE_CWMIN_XR			3
#define AR5K_TUNE_CWMAX				1023
#define AR5K_TUNE_CWMAX_11B			1023
#define AR5K_TUNE_CWMAX_XR			7
#define AR5K_TUNE_NOISE_FLOOR			-72
#define AR5K_TUNE_MAX_TXPOWER			60
#define AR5K_TUNE_DEFAULT_TXPOWER		30
#define AR5K_TUNE_TPC_TXPOWER			TRUE
#define AR5K_TUNE_ANT_DIVERSITY			TRUE
#define AR5K_TUNE_HWTXTRIES			4

/* token to use for aifs, cwmin, cwmax in MadWiFi */
#define	AR5K_TXQ_USEDEFAULT	((u_int32_t) -1)

#define IEEE80211_ADDR_LEN      6       /* size of 802.11 address */
#define ETHER_ADDR_LEN          6       /* length of an Ethernet address */
static const u_char etherbroadcastaddr[ETHER_ADDR_LEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
//#define etherbroadcastaddr 0xff




/*****************************\
  GENERIC CHIPSET DEFINITIONS
\*****************************/

/* MAC Chips*/
enum ath5k_version {
	AR5K_AR5210	= 0,
	AR5K_AR5211	= 1,
	AR5K_AR5212	= 2,
};

/*PHY Chips*/
enum ath5k_radio {
	AR5K_AR5110	= 0,
	AR5K_AR5111	= 1,
	AR5K_AR5112	= 2,
};

/*
 * Common silicon revision/version values
 */
enum ath5k_srev_type {
	AR5K_VERSION_VER,
	AR5K_VERSION_REV,
	AR5K_VERSION_RAD,
	AR5K_VERSION_DEV
};

struct ath5k_srev_name {
	const char		*sr_name;
	enum ath5k_srev_type	sr_type;
	u_int			sr_val;
};

#define AR5K_SREV_NAME	{						\
	{ "5210",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5210 },	\
	{ "5311",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5311 },	\
	{ "5311a",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5311A },\
	{ "5311b",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5311B },\
	{ "5211",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5211 },	\
	{ "5212",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5212 },	\
	{ "5213",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5213 },	\
	{ "xxxx",	AR5K_VERSION_VER,	AR5K_SREV_UNKNOWN },	\
	{ "5110",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5110 },	\
	{ "5111",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5111 },	\
	{ "2111",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2111 },	\
	{ "5112",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5112 },	\
	{ "5112a",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5112A },	\
	{ "2112",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2112 },	\
	{ "2112a",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2112A },	\
	{ "xxxx",	AR5K_VERSION_RAD,	AR5K_SREV_UNKNOWN },	\
	{ "2413",	AR5K_VERSION_DEV,	AR5K_DEVID_AR2413 },	\
 	{ "5413",	AR5K_VERSION_DEV,	AR5K_DEVID_AR5413 },	\
 	{ "5424",	AR5K_VERSION_DEV,	AR5K_DEVID_AR5424 },	\
 	{ "xxxx",	AR5K_VERSION_DEV,	AR5K_SREV_UNKNOWN }	\
}

#define AR5K_SREV_UNKNOWN	0xffff

#define AR5K_SREV_VER_AR5210	0x00
#define AR5K_SREV_VER_AR5311	0x10
#define AR5K_SREV_VER_AR5311A	0x20
#define AR5K_SREV_VER_AR5311B	0x30
#define AR5K_SREV_VER_AR5211	0x40
#define AR5K_SREV_VER_AR5212	0x50
#define AR5K_SREV_VER_AR5213	0x55
#define AR5K_SREV_VER_UNSUPP	0x60

#define AR5K_SREV_RAD_5110	0x00
#define AR5K_SREV_RAD_5111	0x10
#define AR5K_SREV_RAD_5111A	0x15
#define AR5K_SREV_RAD_2111	0x20
#define AR5K_SREV_RAD_5112	0x30
#define AR5K_SREV_RAD_5112A	0x35
#define AR5K_SREV_RAD_2112	0x40
#define AR5K_SREV_RAD_2112A	0x45
#define AR5K_SREV_RAD_UNSUPP	0x50




/****************\
  TX DEFINITIONS
\****************/

/*
 * Tx Descriptor
 */
struct ath_tx_status {
	u_int16_t	ts_seqnum;
	u_int16_t	ts_tstamp;
	u_int8_t	ts_status;
	u_int8_t	ts_rate;
	int8_t		ts_rssi;
	u_int8_t	ts_shortretry;
	u_int8_t	ts_longretry;
	u_int8_t	ts_virtcol;
	u_int8_t	ts_antenna;
};

#define AR5K_TXSTAT_ALTRATE	0x80
#define AR5K_TXERR_XRETRY	0x01
#define AR5K_TXERR_FILT		0x02
#define AR5K_TXERR_FIFO		0x04

/*
 * Queue types used to classify tx queues.
 */
typedef enum {
	AR5K_TX_QUEUE_INACTIVE = 0,/*This queue is not used -see ath_hal_releasetxqueue*/
	AR5K_TX_QUEUE_DATA,	  /*A normal data queue*/
	AR5K_TX_QUEUE_XR_DATA,	  /*An XR-data queue*/
	AR5K_TX_QUEUE_BEACON,	  /*The beacon queue*/
	AR5K_TX_QUEUE_CAB,	  /*The ater-beacon queue*/
	AR5K_TX_QUEUE_UAPSD,	  /*Unscheduled Automatic Power Save Delivery queue*/
} AR5K_TX_QUEUE;

#define	AR5K_NUM_TX_QUEUES	10

/*
 * Queue syb-types to classify normal data queues.
 * These are the 4 Access Categories as defined in
 * WME spec. 0 is the lowest priority and 4 is the
 * highest. Normal data that hasn't been classified
 * goes to the Best Effort AC.
 */
typedef enum {
	AR5K_WME_AC_BK = 0,	/*Background traffic*/
	AR5K_WME_AC_BE, 	/*Best-effort (normal) traffic)*/
	AR5K_WME_AC_VI, 	/*Video traffic*/
	AR5K_WME_AC_VO, 	/*Voice traffic*/
} AR5K_TX_QUEUE_SUBTYPE;

/*
 * Queue ID numbers as returned by the HAL, each number
 * represents a hw queue. If hw does not support hw queues
 * (eg 5210/5211) all data goes in one queue. These match
 * d80211 definitions (net80211/MadWiFi don't use them).
 */
typedef enum {
	AR5K_TX_QUEUE_ID_DATA_MIN = 0, /*IEEE80211_TX_QUEUE_DATA0*/
	AR5K_TX_QUEUE_ID_DATA_MAX = 4, /*IEEE80211_TX_QUEUE_DATA4*/
	AR5K_TX_QUEUE_ID_DATA_SVP = 5, /*IEEE80211_TX_QUEUE_SVP - Spectralink Voice Protocol*/
	AR5K_TX_QUEUE_ID_CAB	  = 6, /*IEEE80211_TX_QUEUE_AFTER_BEACON*/
	AR5K_TX_QUEUE_ID_BEACON	  = 7, /*IEEE80211_TX_QUEUE_BEACON*/
	AR5K_TX_QUEUE_ID_UAPSD	  = 8,
	AR5K_TX_QUEUE_ID_XR_DATA  = 9,
} AR5K_TX_QUEUE_ID;


/*
 * Flags to set hw queue's parameters...
 */
#define AR5K_TXQ_FLAG_TXINT_ENABLE		0x0001	/* Enable TXOK and TXERR interrupts -not used- */
#define AR5K_TXQ_FLAG_TXDESCINT_ENABLE		0x0002	/* Enable TXDESC interrupt -not implemented- */
#define AR5K_TXQ_FLAG_BACKOFF_DISABLE		0x0004	/* Disable random post-backoff */
#define AR5K_TXQ_FLAG_COMPRESSION_ENABLE	0x0008	/* Enable hw compression -not implemented-*/
#define AR5K_TXQ_FLAG_RDYTIME_EXP_POLICY_ENABLE	0x0010	/* Enable ready time expiry policy (?)*/
#define AR5K_TXQ_FLAG_FRAG_BURST_BACKOFF_ENABLE	0x0020	/* Enable backoff while bursting */
#define AR5K_TXQ_FLAG_POST_FR_BKOFF_DIS		0x0040	/* Disable backoff while bursting */
#define AR5K_TXQ_FLAG_TXEOLINT_ENABLE		0x0080	/* Enable TXEOL interrupt -not implemented-*/

/*
 * A struct to hold tx queue's parameters
 */
typedef struct {
	AR5K_TX_QUEUE			tqi_type;	/* See AR5K_TX_QUEUE */
	AR5K_TX_QUEUE_SUBTYPE		tqi_subtype;	/* See AR5K_TX_QUEUE_SUBTYPE */
	u_int16_t			tqi_flags;	/* Tx queue flags (see above) */
	u_int32_t			tqi_aifs;	/* Arbitrated Interframe Space */
	int32_t				tqi_cw_min;	/* Minimum Contention Window */
	int32_t				tqi_cw_max;	/* Maximum Contention Window */
	u_int32_t			tqi_cbr_period; /* Constant bit rate period */
	u_int32_t			tqi_cbr_overflow_limit;
	u_int32_t			tqi_burst_time;
	u_int32_t			tqi_ready_time; /* Not used */
	u_int32_t			tqi_comp_buffer;/* Compression Buffer's phys addr */
} AR5K_TXQ_INFO;

/*
 * Transmit packet types.
 * These are not fully used inside OpenHAL yet
 */
typedef enum {
	AR5K_PKT_TYPE_NORMAL 		= 0,
	AR5K_PKT_TYPE_ATIM 		= 1,
	AR5K_PKT_TYPE_PSPOLL 		= 2,
	AR5K_PKT_TYPE_BEACON 		= 3,
	AR5K_PKT_TYPE_PROBE_RESP 	= 4,
	AR5K_PKT_TYPE_PIFS 		= 5,
} AR5K_PKT_TYPE;

/*
 * TX power and TPC settings
 */
#define AR5K_TXPOWER_OFDM(_r, _v)	(			\
	((0 & 1) << ((_v) + 6)) |				\
	(((hal->ah_txpower.txp_rates[(_r)]) & 0x3f) << (_v))	\
)

#define AR5K_TXPOWER_CCK(_r, _v)	(			\
	(hal->ah_txpower.txp_rates[(_r)] & 0x3f) << (_v)	\
)

/*
 * Used to compute TX times
 */
#define AR5K_CCK_SIFS_TIME		10
#define AR5K_CCK_PREAMBLE_BITS		144
#define AR5K_CCK_PLCP_BITS		48

#define AR5K_OFDM_SIFS_TIME		16
#define AR5K_OFDM_PREAMBLE_TIME		20
#define AR5K_OFDM_PLCP_BITS		22
#define AR5K_OFDM_SYMBOL_TIME		4

#define AR5K_TURBO_SIFS_TIME		8
#define AR5K_TURBO_PREAMBLE_TIME	14
#define AR5K_TURBO_PLCP_BITS		22
#define AR5K_TURBO_SYMBOL_TIME		4

#define AR5K_XR_SIFS_TIME		16
#define AR5K_XR_PLCP_BITS		22
#define AR5K_XR_SYMBOL_TIME		4

/* CCK */
#define AR5K_CCK_NUM_BITS(_frmlen) (_frmlen << 3)

#define AR5K_CCK_PHY_TIME(_sp) (_sp ?					\
	((AR5K_CCK_PREAMBLE_BITS + AR5K_CCK_PLCP_BITS) >> 1) :		\
	(AR5K_CCK_PREAMBLE_BITS + AR5K_CCK_PLCP_BITS))

#define AR5K_CCK_TX_TIME(_kbps, _frmlen, _sp)				\
	AR5K_CCK_PHY_TIME(_sp) +					\
	((AR5K_CCK_NUM_BITS(_frmlen) * 1000) / _kbps) +		\
	AR5K_CCK_SIFS_TIME

/* OFDM */
#define AR5K_OFDM_NUM_BITS(_frmlen) (AR5K_OFDM_PLCP_BITS + (_frmlen << 3))

#define AR5K_OFDM_NUM_BITS_PER_SYM(_kbps) ((_kbps *			\
	AR5K_OFDM_SYMBOL_TIME) / 1000)

#define AR5K_OFDM_NUM_BITS(_frmlen) (AR5K_OFDM_PLCP_BITS + (_frmlen << 3))

#define AR5K_OFDM_NUM_SYMBOLS(_kbps, _frmlen)				\
	howmany(AR5K_OFDM_NUM_BITS(_frmlen), AR5K_OFDM_NUM_BITS_PER_SYM(_kbps))

#define AR5K_OFDM_TX_TIME(_kbps, _frmlen)				\
	AR5K_OFDM_PREAMBLE_TIME + AR5K_OFDM_SIFS_TIME +			\
	(AR5K_OFDM_NUM_SYMBOLS(_kbps, _frmlen) * AR5K_OFDM_SYMBOL_TIME)

/* TURBO */
#define AR5K_TURBO_NUM_BITS(_frmlen) (AR5K_TURBO_PLCP_BITS + (_frmlen << 3))

#define AR5K_TURBO_NUM_BITS_PER_SYM(_kbps) (((_kbps << 1) *		\
	AR5K_TURBO_SYMBOL_TIME) / 1000)

#define AR5K_TURBO_NUM_BITS(_frmlen) (AR5K_TURBO_PLCP_BITS + (_frmlen << 3))

#define AR5K_TURBO_NUM_SYMBOLS(_kbps, _frmlen)				\
	howmany(AR5K_TURBO_NUM_BITS(_frmlen),				\
	AR5K_TURBO_NUM_BITS_PER_SYM(_kbps))

#define AR5K_TURBO_TX_TIME(_kbps, _frmlen)				\
	AR5K_TURBO_PREAMBLE_TIME + AR5K_TURBO_SIFS_TIME +		\
	(AR5K_TURBO_NUM_SYMBOLS(_kbps, _frmlen) * AR5K_TURBO_SYMBOL_TIME)

/* eXtendent Range (?)*/
#define AR5K_XR_PREAMBLE_TIME(_kbps) (((_kbps) < 1000) ? 173 : 76)

#define AR5K_XR_NUM_BITS_PER_SYM(_kbps) ((_kbps *			\
	AR5K_XR_SYMBOL_TIME) / 1000)

#define AR5K_XR_NUM_BITS(_frmlen) (AR5K_XR_PLCP_BITS + (_frmlen << 3))

#define AR5K_XR_NUM_SYMBOLS(_kbps, _frmlen)				\
	howmany(AR5K_XR_NUM_BITS(_frmlen), AR5K_XR_NUM_BITS_PER_SYM(_kbps))

#define AR5K_XR_TX_TIME(_kbps, _frmlen)				\
	AR5K_XR_PREAMBLE_TIME(_kbps) + AR5K_XR_SIFS_TIME +		\
	(AR5K_XR_NUM_SYMBOLS(_kbps, _frmlen) * AR5K_XR_SYMBOL_TIME)

/*
 * DMA size definitions (2^n+2)
 */
typedef enum {
	AR5K_DMASIZE_4B	= 0,
	AR5K_DMASIZE_8B,
	AR5K_DMASIZE_16B,
	AR5K_DMASIZE_32B,
	AR5K_DMASIZE_64B,
	AR5K_DMASIZE_128B,
	AR5K_DMASIZE_256B,
	AR5K_DMASIZE_512B
} ath5k_dmasize_t;



/****************\
  RX DEFINITIONS
\****************/

/*
 * Rx Descriptor
 */
struct ath_rx_status {
	u_int16_t	rs_datalen;
	u_int16_t	rs_tstamp;
	u_int8_t	rs_status;
	u_int8_t	rs_phyerr;
	int8_t		rs_rssi;
	u_int8_t	rs_keyix;
	u_int8_t	rs_rate;
	u_int8_t	rs_antenna;
	u_int8_t	rs_more;
};

#define AR5K_RXERR_CRC		0x01
#define AR5K_RXERR_PHY		0x02
#define AR5K_RXERR_FIFO		0x04
#define AR5K_RXERR_DECRYPT	0x08
#define AR5K_RXERR_MIC		0x10
#define AR5K_RXKEYIX_INVALID	((u_int8_t) - 1)
#define AR5K_TXKEYIX_INVALID	((u_int32_t) - 1)

/*
 * RX filters
 * Most of them are not yet used inside OpenHAL
 */
#define	AR5K_RX_FILTER_UCAST 		0x00000001	/* Don't filter unicast frames */
#define	AR5K_RX_FILTER_MCAST 		0x00000002	/* Don't filter multicast frames */
#define	AR5K_RX_FILTER_BCAST 		0x00000004	/* Don't filter broadcast frames */
#define	AR5K_RX_FILTER_CONTROL 		0x00000008	/* Don't filter control frames */
#define	AR5K_RX_FILTER_BEACON 		0x00000010	/* Don't filter beacon frames */
#define	AR5K_RX_FILTER_PROM 		0x00000020	/* Set promiscuous mode */
#define	AR5K_RX_FILTER_XRPOLL 		0x00000040	/* Don't filter XR poll frame */
#define	AR5K_RX_FILTER_PROBEREQ 	0x00000080	/* Don't filter probe requests */
#define	AR5K_RX_FILTER_PHYERROR		0x00000100	/* Don't filter phy errors */
#define	AR5K_RX_FILTER_PHYRADAR 	0x00000200	/* Don't filter phy radar errors*/

typedef struct {
	u_int32_t	ackrcv_bad;
	u_int32_t	rts_bad;
	u_int32_t	rts_good;
	u_int32_t	fcs_bad;
	u_int32_t	beacons;
} AR5K_MIB_STATS;




/**************************\
 BEACON TIMERS DEFINITIONS
\**************************/

#define AR5K_BEACON_PERIOD	0x0000ffff
#define AR5K_BEACON_ENA		0x00800000 /*enable beacon xmit*/
#define AR5K_BEACON_RESET_TSF	0x01000000 /*force a TSF reset*/

/*
 * Per-station beacon timer state.
 */
typedef struct {
	u_int32_t	bs_next_beacon;
	u_int32_t	bs_next_dtim;
	u_int32_t	bs_interval;		/*in TU's -see net80211/ieee80211_var.h-
						can also include the above flags*/
	u_int8_t	bs_dtim_period;
	u_int8_t	bs_cfp_period;
	u_int16_t	bs_cfp_max_duration;	/*if non-zero hw is setup to coexist with
						a Point Coordination Function capable AP*/
	u_int16_t	bs_cfp_du_remain;
	u_int16_t	bs_tim_offset;
	u_int16_t	bs_sleep_duration;
	u_int16_t	bs_bmiss_threshold;
	u_int32_t  	bs_cfp_next;
} AR5K_BEACON_STATE;




/********************\
  COMMON DEFINITIONS
\********************/

/*
 * Atheros descriptor
 */
struct __attribute__ ((__packed__)) ath_desc {
	u_int32_t	ds_link;
	u_int32_t	ds_data;
	u_int32_t	ds_ctl0;
	u_int32_t	ds_ctl1;
	u_int32_t	ds_hw[4];

	union {
		struct ath_rx_status rx;
		struct ath_tx_status tx;
	} ds_us;

#define ds_rxstat ds_us.rx
#define ds_txstat ds_us.tx

};

#define AR5K_RXDESC_INTREQ	0x0020

#define AR5K_TXDESC_CLRDMASK	0x0001
#define AR5K_TXDESC_NOACK	0x0002
#define AR5K_TXDESC_RTSENA	0x0004
#define AR5K_TXDESC_CTSENA	0x0008
#define AR5K_TXDESC_INTREQ	0x0010
#define AR5K_TXDESC_VEOL	0x0020

/*
 * 802.11 operating modes...
 */
#define AR5K_MODE_11A  	0x01
#define AR5K_MODE_11B  	0x02
#define AR5K_MODE_11G  	0x04
#define AR5K_MODE_TURBO	0x08
#define AR5K_MODE_108G 	0x16
#define AR5K_MODE_XR	0x32
#define AR5K_MODE_ALL 	(AR5K_MODE_11A|	\
			AR5K_MODE_11B|	\
			AR5K_MODE_11G|	\
			AR5K_MODE_TURBO|\
			AR5K_MODE_108G|	\
			AR5K_MODE_XR)

/*
 * Channel definitions
 */
typedef struct {
	u_int16_t	freq;		/* setting in Mhz */
	u_int16_t	channel_flags;
	u_int8_t	private_flags;	/* not used in OpenHAL yet*/
} AR5K_CHANNEL;

#define AR5K_SLOT_TIME_9	396
#define AR5K_SLOT_TIME_20	880
#define AR5K_SLOT_TIME_MAX	0xffff

/* channel_flags */
#define	CHANNEL_CW_INT	0x0008	/* Contention Window interference detected */
#define	CHANNEL_TURBO	0x0010	/* Turbo Channel */
#define	CHANNEL_CCK	0x0020	/* CCK channel */
#define	CHANNEL_OFDM	0x0040	/* OFDM channel */
#define	CHANNEL_2GHZ	0x0080	/* 2GHz channel. */
#define	CHANNEL_5GHZ	0x0100	/* 5GHz channel */
#define	CHANNEL_PASSIVE	0x0200	/* Only passive scan allowed */
#define	CHANNEL_DYN	0x0400	/* Dynamic CCK-OFDM channel (for g operation)*/
#define	CHANNEL_XR	0x0800	/* XR channel */

#define	CHANNEL_A	(CHANNEL_5GHZ|CHANNEL_OFDM)
#define	CHANNEL_B	(CHANNEL_2GHZ|CHANNEL_CCK)
#define	CHANNEL_PUREG	(CHANNEL_2GHZ|CHANNEL_OFDM)
//#ifdef notdef
#define	CHANNEL_G	(CHANNEL_2GHZ|CHANNEL_DYN)
//#else
//#define	CHANNEL_G	(CHANNEL_2GHZ|CHANNEL_OFDM)
//#endif
#define	CHANNEL_T	(CHANNEL_5GHZ|CHANNEL_OFDM|CHANNEL_TURBO)
#define	CHANNEL_TG	(CHANNEL_2GHZ|CHANNEL_OFDM|CHANNEL_TURBO)
#define	CHANNEL_108A	CHANNEL_T
#define	CHANNEL_108G	CHANNEL_TG
#define	CHANNEL_X	(CHANNEL_5GHZ|CHANNEL_OFDM|CHANNEL_XR)

#define	CHANNEL_ALL 	(CHANNEL_OFDM|CHANNEL_CCK| CHANNEL_2GHZ |\
			 CHANNEL_5GHZ | CHANNEL_TURBO)

#define	CHANNEL_ALL_NOTURBO 	(CHANNEL_ALL &~ CHANNEL_TURBO)
#define CHANNEL_MODES	CHANNEL_ALL

/*
 * Used internaly in OpenHAL (ar5211.c/ar5212.c
 * for reset_tx_queue). Also see struct AR5K_CHANNEL.
 */
#define IS_CHAN_XR(_c) \
        ((_c.channel_flags & CHANNEL_XR) != 0)

#define IS_CHAN_B(_c) \
        ((_c.channel_flags & CHANNEL_B) != 0)

typedef enum {
	AR5K_CHIP_5GHZ = CHANNEL_5GHZ,
	AR5K_CHIP_2GHZ = CHANNEL_2GHZ,
} AR5K_CHIP;

/*
 * The following structure will be used to map 2GHz channels to
 * 5GHz Atheros channels.
 */
struct ath5k_athchan_2ghz {
	u_int32_t	a2_flags;
	u_int16_t	a2_athchan;
};

/*
 * Rate definitions
 */

#define AR5K_MAX_RATES	32 /*max number of rates on the rate table*/

typedef struct {
	u_int8_t	valid;		/* Valid for rate control */
	u_int32_t	modulation;
	u_int16_t	rate_kbps;		
	u_int8_t	rate_code;	/* Rate mapping for h/w descriptors */
	u_int8_t	dot11_rate;
	u_int8_t	control_rate;
	u_int16_t	lp_ack_duration;/* long preamble ACK duration */
	u_int16_t	sp_ack_duration;/* short preamble ACK duration*/
} AR5K_RATE;

typedef struct {
	u_int16_t	rate_count;				
	u_int8_t	rate_code_to_index[AR5K_MAX_RATES];	/* Back-mapping */
	AR5K_RATE	rates[AR5K_MAX_RATES];
} AR5K_RATE_TABLE;

/*
 * Rate tables...
 */
#define AR5K_RATES_11A { 8, {					\
	255, 255, 255, 255, 255, 255, 255, 255, 6, 4, 2, 0,	\
	7, 5, 3, 1, 255, 255, 255, 255, 255, 255, 255, 255,	\
	255, 255, 255, 255, 255, 255, 255, 255 }, {		\
	{ 1, MODULATION_OFDM, 6000, 11, 140, 0 },		\
	{ 1, MODULATION_OFDM, 9000, 15, 18, 0 },		\
	{ 1, MODULATION_OFDM, 12000, 10, 152, 2 },		\
	{ 1, MODULATION_OFDM, 18000, 14, 36, 2 },		\
	{ 1, MODULATION_OFDM, 24000, 9, 176, 4 },		\
	{ 1, MODULATION_OFDM, 36000, 13, 72, 4 },		\
	{ 1, MODULATION_OFDM, 48000, 8, 96, 4 },		\
	{ 1, MODULATION_OFDM, 54000, 12, 108, 4 } }		\
}

#define AR5K_RATES_11B { 4, {						\
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,	\
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,	\
	3, 2, 1, 0, 255, 255, 255, 255 }, {				\
	{ 1, MODULATION_CCK, 1000, 27, 130, 0 },	\
	{ 1, MODULATION_CCK, 2000, 26, 132, 1 },	\
	{ 1, MODULATION_CCK, 5500, 25, 139, 1 },	\
	{ 1, MODULATION_CCK, 11000, 24, 150, 1 } }	\
}

#define AR5K_RATES_11G { 12, {					\
	255, 255, 255, 255, 255, 255, 255, 255, 10, 8, 6, 4,	\
	11, 9, 7, 5, 255, 255, 255, 255, 255, 255, 255, 255,	\
	3, 2, 1, 0, 255, 255, 255, 255 }, {			\
	{ 1, MODULATION_CCK, 1000, 27, 2, 0 },		\
	{ 1, MODULATION_CCK, 2000, 26, 4, 1 },		\
	{ 1, MODULATION_CCK, 5500, 25, 11, 1 },		\
	{ 1, MODULATION_CCK, 11000, 24, 22, 1 },	\
	{ 0, MODULATION_OFDM, 6000, 11, 12, 4 },	\
	{ 0, MODULATION_OFDM, 9000, 15, 18, 4 },	\
	{ 1, MODULATION_OFDM, 12000, 10, 24, 6 },	\
	{ 1, MODULATION_OFDM, 18000, 14, 36, 6 },	\
	{ 1, MODULATION_OFDM, 24000, 9, 48, 8 },	\
	{ 1, MODULATION_OFDM, 36000, 13, 72, 8 },	\
	{ 1, MODULATION_OFDM, 48000, 8, 96, 8 },	\
	{ 1, MODULATION_OFDM, 54000, 12, 108, 8 } }	\
}

#define AR5K_RATES_TURBO { 8, {					\
	255, 255, 255, 255, 255, 255, 255, 255, 6, 4, 2, 0,	\
	7, 5, 3, 1, 255, 255, 255, 255, 255, 255, 255, 255,	\
	255, 255, 255, 255, 255, 255, 255, 255 }, {		\
	{ 1, MODULATION_TURBO, 6000, 11, 140, 0 },	\
	{ 1, MODULATION_TURBO, 9000, 15, 18, 0 },	\
	{ 1, MODULATION_TURBO, 12000, 10, 152, 2 },	\
	{ 1, MODULATION_TURBO, 18000, 14, 36, 2 },	\
	{ 1, MODULATION_TURBO, 24000, 9, 176, 4 },	\
	{ 1, MODULATION_TURBO, 36000, 13, 72, 4 },	\
	{ 1, MODULATION_TURBO, 48000, 8, 96, 4 },	\
	{ 1, MODULATION_TURBO, 54000, 12, 108, 4 } }	\
}

#define AR5K_RATES_XR { 12, {					\
	255, 3, 1, 255, 255, 255, 2, 0, 10, 8, 6, 4,		\
	11, 9, 7, 5, 255, 255, 255, 255, 255, 255, 255, 255,	\
	255, 255, 255, 255, 255, 255, 255, 255 }, {		\
	{ 1, MODULATION_XR, 500, 7, 129, 0 },		\
	{ 1, MODULATION_XR, 1000, 2, 139, 1 },		\
	{ 1, MODULATION_XR, 2000, 6, 150, 2 },		\
	{ 1, MODULATION_XR, 3000, 1, 150, 3 },		\
	{ 1, MODULATION_OFDM, 6000, 11, 140, 4 },	\
	{ 1, MODULATION_OFDM, 9000, 15, 18, 4 },	\
	{ 1, MODULATION_OFDM, 12000, 10, 152, 6 },	\
	{ 1, MODULATION_OFDM, 18000, 14, 36, 6 },	\
	{ 1, MODULATION_OFDM, 24000, 9, 176, 8 },	\
	{ 1, MODULATION_OFDM, 36000, 13, 72, 8 },	\
	{ 1, MODULATION_OFDM, 48000, 8, 96, 8 },	\
	{ 1, MODULATION_OFDM, 54000, 12, 108, 8 } }	\
}

/*
 * Crypto definitions
 */

/* key types */
typedef enum {
	AR5K_CIPHER_WEP		= 0,
	AR5K_CIPHER_AES_OCB	= 1,
	AR5K_CIPHER_AES_CCM	= 2,
	AR5K_CIPHER_CKIP	= 3,
	AR5K_CIPHER_TKIP	= 4,
	AR5K_CIPHER_CLR		= 5,	/* no encryption */
	AR5K_CIPHER_MIC		= 127	/* used for Message 
					   Integrity Code */
} AR5K_CIPHER;

#define AR5K_KEYVAL_LENGTH_40	5
#define AR5K_KEYVAL_LENGTH_104	13
#define AR5K_KEYVAL_LENGTH_128	16
#define AR5K_KEYVAL_LENGTH_MAX	AR5K_KEYVAL_LENGTH_128

typedef struct {
	int		wk_len;		/* key's length */
	u_int8_t	wk_key[AR5K_KEYVAL_LENGTH_MAX];
	u_int8_t	wk_type;	/* see above */
	u_int8_t	wk_mic[8];	/* TKIP MIC key */
} AR5K_KEYVAL;



/***********************\
 HW RELATED DEFINITIONS
\***********************/

/*
 * Misc definitions
 */
#define	AR5K_RSSI_EP_MULTIPLIER	(1<<7)

#define AR5K_ASSERT_ENTRY(_e, _s) do {		\
	if (_e >= _s)				\
		return (FALSE);			\
} while (0)


typedef struct {
	u_int32_t	ns_avgbrssi;	/* average beacon rssi */
	u_int32_t	ns_avgrssi;	/* average data rssi */
	u_int32_t	ns_avgtxrssi;	/* average tx rssi */
} AR5K_NODE_STATS;

typedef enum {
	AR5K_ANT_VARIABLE	= 0,	/* variable by programming */
	AR5K_ANT_FIXED_A	= 1,	/* fixed to 11a frequencies */
	AR5K_ANT_FIXED_B	= 2,	/* fixed to 11b frequencies */
	AR5K_ANT_MAX		= 3,
} AR5K_ANT_SETTING;

/*
 * HAL interrupt abstraction
 */

/*
 * These are maped to take advantage of some common bits
 * between the MAC chips, to be able to set intr properties
 * easier. Some of them are not used yet inside OpenHAL.
 */
typedef enum {
	AR5K_INT_RX	= 0x00000001,
	AR5K_INT_RXDESC	= 0x00000002,
	AR5K_INT_RXNOFRM = 0x00000008,
	AR5K_INT_RXEOL	= 0x00000010,
	AR5K_INT_RXORN	= 0x00000020,
	AR5K_INT_TX	= 0x00000040,
	AR5K_INT_TXDESC	= 0x00000080,
	AR5K_INT_TXURN	= 0x00000800,
	AR5K_INT_MIB	= 0x00001000,
	AR5K_INT_RXPHY	= 0x00004000,
	AR5K_INT_RXKCM	= 0x00008000,
	AR5K_INT_SWBA	= 0x00010000,
	AR5K_INT_BMISS	= 0x00040000,
	AR5K_INT_BNR	= 0x00100000,
	AR5K_INT_GPIO	= 0x01000000,
	AR5K_INT_FATAL	= 0x40000000,
	AR5K_INT_GLOBAL	= 0x80000000,

	/*A sum of all the common bits*/
	AR5K_INT_COMMON  = AR5K_INT_RXNOFRM
			| AR5K_INT_RXDESC
			| AR5K_INT_RXEOL
			| AR5K_INT_RXORN
			| AR5K_INT_TXURN
			| AR5K_INT_TXDESC
			| AR5K_INT_MIB
			| AR5K_INT_RXPHY
			| AR5K_INT_RXKCM
			| AR5K_INT_SWBA
			| AR5K_INT_BMISS
			| AR5K_INT_GPIO,
	AR5K_INT_NOCARD	= 0xffffffff /*Declare that the card 
				       has been removed*/
} AR5K_INT;

/*
 * Power management
 */
typedef enum {
	AR5K_PM_UNDEFINED = 0,
	AR5K_PM_AUTO,
	AR5K_PM_AWAKE,
	AR5K_PM_FULL_SLEEP,
	AR5K_PM_NETWORK_SLEEP,
} AR5K_POWER_MODE;


/*
 * LED states
 */
typedef int AR5K_LED_STATE;

/*
 * These match net80211 definitions (not used in
 * d80211).
 */
#define AR5K_LED_INIT	0 /*IEEE80211_S_INIT*/
#define AR5K_LED_SCAN	1 /*IEEE80211_S_SCAN*/
#define AR5K_LED_AUTH	2 /*IEEE80211_S_AUTH*/
#define AR5K_LED_ASSOC	3 /*IEEE80211_S_ASSOC*/
#define AR5K_LED_RUN	4 /*IEEE80211_S_RUN*/

/* GPIO-controlled software LED */
#define AR5K_SOFTLED_PIN	0
#define AR5K_SOFTLED_ON		0
#define AR5K_SOFTLED_OFF	1

/*
 * Chipset capabilities -see ath_hal_getcapability-
 * get_capability function is not yet fully implemented
 * in OpenHAL so most of these don't work yet...
 */
typedef enum {
	AR5K_CAP_REG_DMN		= 0,	/* Used to get current reg. domain id */
	AR5K_CAP_CIPHER			= 1,	/* Can handle encryption */
	AR5K_CAP_TKIP_MIC		= 2,	/* Can handle TKIP MIC in hardware */
	AR5K_CAP_TKIP_SPLIT		= 3,	/* TKIP uses split keys */
	AR5K_CAP_PHYCOUNTERS		= 4,	/* PHY error counters */
	AR5K_CAP_DIVERSITY		= 5,	/* Supports fast diversity */
	AR5K_CAP_NUM_TXQUEUES		= 6,	/* Used to get max number of hw txqueues */
	AR5K_CAP_VEOL			= 7,	/* Supports virtual EOL */
	AR5K_CAP_COMPRESSION		= 8,	/* Supports compression */
	AR5K_CAP_BURST			= 9,	/* Supports packet bursting */
	AR5K_CAP_FASTFRAME		= 10,	/* Supports fast frames */
	AR5K_CAP_TXPOW			= 11,	/* Used to get global tx power limit */
	AR5K_CAP_TPC			= 12,	/* Can do per-packet tx power control (needed for 802.11a) */
	AR5K_CAP_BSSIDMASK		= 13,	/* Supports bssid mask */
	AR5K_CAP_MCAST_KEYSRCH		= 14,	/* Supports multicast key search */
	AR5K_CAP_TSF_ADJUST		= 15,	/* Supports beacon tsf adjust */
	AR5K_CAP_XR			= 16,	/* Supports XR mode */
	AR5K_CAP_WME_TKIPMIC 		= 17,	/* Supports TKIP MIC when using WMM */
	AR5K_CAP_CHAN_HALFRATE 		= 18,	/* Supports half rate channels */
	AR5K_CAP_CHAN_QUARTERRATE 	= 19,	/* Supports quarter rate channels */
	AR5K_CAP_RFSILENT		= 20,	/* Supports RFsilent */
} AR5K_CAPABILITY_TYPE;


#endif /* _AR5K_H */
