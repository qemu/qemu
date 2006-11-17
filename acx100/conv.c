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
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>

#include "acx.h"


/***********************************************************************
** proto_is_stt
**
** Searches the 802.1h Selective Translation Table for a given
** protocol.
**
** prottype - protocol number (in host order) to search for.
**
** Returns:
**	1 - if the table is empty or a match is found.
**	0 - if the table is non-empty and a match is not found.
**
** Based largely on p80211conv.c of the linux-wlan-ng project
*/
static inline int
proto_is_stt(unsigned int proto)
{
	/* Always return found for now.  This is the behavior used by the */
	/* Zoom Win95 driver when 802.1h mode is selected */
	/* TODO: If necessary, add an actual search we'll probably
		 need this to match the CMAC's way of doing things.
		 Need to do some testing to confirm.
	*/

	if (proto == 0x80f3)  /* APPLETALK */
		return 1;

	return 0;
/*	return ((prottype == ETH_P_AARP) || (prottype == ETH_P_IPX)); */
}

/* Helpers */

static inline void
store_llc_snap(struct wlan_llc *llc)
{
	llc->dsap = 0xaa;	/* SNAP, see IEEE 802 */
	llc->ssap = 0xaa;
	llc->ctl = 0x03;
}
static inline int
llc_is_snap(const struct wlan_llc *llc)
{
	return (llc->dsap == 0xaa)
	&& (llc->ssap == 0xaa)
	&& (llc->ctl == 0x03);
}
static inline void
store_oui_rfc1042(struct wlan_snap *snap)
{
	snap->oui[0] = 0;
	snap->oui[1] = 0;
	snap->oui[2] = 0;
}
static inline int
oui_is_rfc1042(const struct wlan_snap *snap)
{
	return (snap->oui[0] == 0)
	&& (snap->oui[1] == 0)
	&& (snap->oui[2] == 0);
}
static inline void
store_oui_8021h(struct wlan_snap *snap)
{
	snap->oui[0] = 0;
	snap->oui[1] = 0;
	snap->oui[2] = 0xf8;
}
static inline int
oui_is_8021h(const struct wlan_snap *snap)
{
	return (snap->oui[0] == 0)
	&& (snap->oui[1] == 0)
	&& (snap->oui[2] == 0xf8);
}


/***********************************************************************
** acx_ether_to_txbuf
**
** Uses the contents of the ether frame to build the elements of
** the 802.11 frame.
**
** We don't actually set up the frame header here.  That's the
** MAC's job.  We're only handling conversion of DIXII or 802.3+LLC
** frames to something that works with 802.11.
**
** Based largely on p80211conv.c of the linux-wlan-ng project
*/
int
acx_ether_to_txbuf(acx_device_t *adev, void *txbuf, const struct sk_buff *skb)
{
	struct wlan_hdr_a3 *w_hdr;
	struct wlan_ethhdr *e_hdr;
	struct wlan_llc *e_llc;
	struct wlan_snap *e_snap;
	const u8 *a1, *a3;
	int header_len, payload_len = -1;
	/* protocol type or data length, depending on whether
	 * DIX or 802.3 ethernet format */
	u16 proto;
	u16 fc;

	FN_ENTER;

	if (unlikely(!skb->len)) {
		log(L_DEBUG, "zero-length skb!\n");
		goto end;
	}

	w_hdr = (struct wlan_hdr_a3*)txbuf;

	switch (adev->mode) {
	case ACX_MODE_MONITOR:
		/* NB: one day we might want to play with DESC_CTL2_FCS
		** Will need to stop doing "- WLAN_FCS_LEN" here then */
		if (unlikely(skb->len >= WLAN_A4FR_MAXLEN_WEP_FCS - WLAN_FCS_LEN)) {
			printk("%s: can't tx oversized frame (%d bytes)\n",
				adev->ndev->name, skb->len);
			goto end;
		}
		memcpy(w_hdr, skb->data, skb->len);
		payload_len = skb->len;
		goto end;
	}

	/* step 1: classify ether frame, DIX or 802.3? */
	e_hdr = (wlan_ethhdr_t *)skb->data;
	proto = ntohs(e_hdr->type);
	if (proto <= 1500) {
		log(L_DEBUG, "tx: 802.3 len: %d\n", skb->len);
		/* codes <= 1500 reserved for 802.3 lengths */
		/* it's 802.3, pass ether payload unchanged, */
		/* trim off ethernet header and copy payload to txdesc */
		header_len = WLAN_HDR_A3_LEN;
	} else {
		/* it's DIXII, time for some conversion */
		/* Create 802.11 packet. Header also contains llc and snap. */

		log(L_DEBUG, "tx: DIXII len: %d\n", skb->len);

		/* size of header is 802.11 header + llc + snap */
		header_len = WLAN_HDR_A3_LEN + sizeof(wlan_llc_t) + sizeof(wlan_snap_t);
		/* llc is located behind the 802.11 header */
		e_llc = (wlan_llc_t*)(w_hdr + 1);
		/* snap is located behind the llc */
		e_snap = (wlan_snap_t*)(e_llc + 1);

		/* setup the LLC header */
		store_llc_snap(e_llc);

		/* setup the SNAP header */
		e_snap->type = htons(proto);
		if (proto_is_stt(proto)) {
			store_oui_8021h(e_snap);
		} else {
			store_oui_rfc1042(e_snap);
		}
	}
	/* trim off ethernet header and copy payload to txbuf */
	payload_len = skb->len - sizeof(wlan_ethhdr_t);
	/* TODO: can we just let acx DMA payload from skb instead? */
	memcpy((u8*)txbuf + header_len, skb->data + sizeof(wlan_ethhdr_t), payload_len);
	payload_len += header_len;

	/* Set up the 802.11 header */
	switch (adev->mode) {
	case ACX_MODE_0_ADHOC:
		fc = (WF_FTYPE_DATAi | WF_FSTYPE_DATAONLYi);
		a1 = e_hdr->daddr;
		a3 = adev->bssid;
		break;
	case ACX_MODE_2_STA:
		fc = (WF_FTYPE_DATAi | WF_FSTYPE_DATAONLYi | WF_FC_TODSi);
		a1 = adev->bssid;
		a3 = e_hdr->daddr;
		break;
	case ACX_MODE_3_AP:
		fc = (WF_FTYPE_DATAi | WF_FSTYPE_DATAONLYi | WF_FC_FROMDSi);
		a1 = e_hdr->daddr;
		a3 = e_hdr->saddr;
		break;
	default:
		printk("%s: error - converting eth to wlan in unknown mode\n",
				adev->ndev->name);
		payload_len = -1;
		goto end;
	}
	if (adev->wep_enabled)
		SET_BIT(fc, WF_FC_ISWEPi);

	w_hdr->fc = fc;
	w_hdr->dur = 0;
	MAC_COPY(w_hdr->a1, a1);
	MAC_COPY(w_hdr->a2, adev->dev_addr);
	MAC_COPY(w_hdr->a3, a3);
	w_hdr->seq = 0;

#ifdef DEBUG_CONVERT
	if (acx_debug & L_DATA) {
		printk("original eth frame [%d]: ", skb->len);
		acx_dump_bytes(skb->data, skb->len);
		printk("802.11 frame [%d]: ", payload_len);
		acx_dump_bytes(w_hdr, payload_len);
	}
#endif

end:
	FN_EXIT1(payload_len);
	return payload_len;
}


/***********************************************************************
** acx_rxbuf_to_ether
**
** Uses the contents of a received 802.11 frame to build an ether
** frame.
**
** This function extracts the src and dest address from the 802.11
** frame to use in the construction of the eth frame.
**
** Based largely on p80211conv.c of the linux-wlan-ng project
*/
struct sk_buff*
acx_rxbuf_to_ether(acx_device_t *adev, rxbuffer_t *rxbuf)
{
	struct wlan_hdr *w_hdr;
	struct wlan_ethhdr *e_hdr;
	struct wlan_llc *e_llc;
	struct wlan_snap *e_snap;
	struct sk_buff *skb;
	const u8 *daddr;
	const u8 *saddr;
	const u8 *e_payload;
	int buflen, payload_length;
	unsigned int payload_offset, mtu;
	u16 fc;

	FN_ENTER;

	/* This looks complex because it must handle possible
	** phy header in rxbuff */
	w_hdr = acx_get_wlan_hdr(adev, rxbuf);
	payload_offset = WLAN_HDR_A3_LEN; /* it is relative to w_hdr */
	payload_length = RXBUF_BYTES_USED(rxbuf) /* entire rxbuff... */
		- ((u8*)w_hdr - (u8*)rxbuf) /* minus space before 802.11 frame */
		- WLAN_HDR_A3_LEN; /* minus 802.11 header */

	/* setup some vars for convenience */
	fc = w_hdr->fc;
	switch (WF_FC_FROMTODSi & fc) {
	case 0:
		daddr = w_hdr->a1;
		saddr = w_hdr->a2;
		break;
	case WF_FC_FROMDSi:
		daddr = w_hdr->a1;
		saddr = w_hdr->a3;
		break;
	case WF_FC_TODSi:
		daddr = w_hdr->a3;
		saddr = w_hdr->a2;
		break;
	default: /* WF_FC_FROMTODSi */
		payload_offset += (WLAN_HDR_A4_LEN - WLAN_HDR_A3_LEN);
		payload_length -= (WLAN_HDR_A4_LEN - WLAN_HDR_A3_LEN);
		daddr = w_hdr->a3;
		saddr = w_hdr->a4;
	}

	if ((WF_FC_ISWEPi & fc) && IS_ACX100(adev)) {
		/* chop off the IV+ICV WEP header and footer */
		log(L_DATA|L_DEBUG, "rx: WEP packet, "
			"chopping off IV and ICV\n");
		payload_offset += WLAN_WEP_IV_LEN;
		payload_length -= WLAN_WEP_IV_LEN + WLAN_WEP_ICV_LEN;
	}

	if (unlikely(payload_length < 0)) {
		printk("%s: rx frame too short, ignored\n", adev->ndev->name);
		goto ret_null;
	}

	e_hdr = (wlan_ethhdr_t*) ((u8*) w_hdr + payload_offset);
	e_llc = (wlan_llc_t*) e_hdr;
	e_snap = (wlan_snap_t*) (e_llc + 1);
	mtu = adev->ndev->mtu;
	e_payload = (u8*) (e_snap + 1);

	log(L_DATA, "rx: payload_offset %d, payload_length %d\n",
		payload_offset, payload_length);
	log(L_XFER|L_DATA,
		"rx: frame info: llc=%02X%02X%02X "
		"snap.oui=%02X%02X%02X snap.type=%04X\n",
		e_llc->dsap, e_llc->ssap, e_llc->ctl,
		e_snap->oui[0],	e_snap->oui[1], e_snap->oui[2],
		ntohs(e_snap->type));

	/* Test for the various encodings */
	if ((payload_length >= sizeof(wlan_ethhdr_t))
	 && ((e_llc->dsap != 0xaa) || (e_llc->ssap != 0xaa))
	 && (   (mac_is_equal(daddr, e_hdr->daddr))
	     || (mac_is_equal(saddr, e_hdr->saddr))
	    )
	) {
	/* 802.3 Encapsulated: */
	/* wlan frame body contains complete eth frame (header+body) */
		log(L_DEBUG|L_DATA, "rx: 802.3 ENCAP len=%d\n", payload_length);

		if (unlikely(payload_length > (mtu + ETH_HLEN))) {
			printk("%s: rx: ENCAP frame too large (%d > %d)\n",
				adev->ndev->name,
				payload_length, mtu + ETH_HLEN);
			goto ret_null;
		}

		/* allocate space and setup host buffer */
		buflen = payload_length;
		/* Attempt to align IP header (14 bytes eth header + 2 = 16) */
		skb = dev_alloc_skb(buflen + 2);
		if (unlikely(!skb))
			goto no_skb;
		skb_reserve(skb, 2);
		skb_put(skb, buflen);		/* make room */

		/* now copy the data from the 80211 frame */
		memcpy(skb->data, e_hdr, payload_length);

	} else if ( (payload_length >= sizeof(wlan_llc_t)+sizeof(wlan_snap_t))
		 && llc_is_snap(e_llc) ) {
	/* wlan frame body contains: AA AA 03 ... (it's a SNAP) */

		if ( !oui_is_rfc1042(e_snap)
		 || (proto_is_stt(ieee2host16(e_snap->type)) /* && (ethconv == WLAN_ETHCONV_8021h) */)) {
			log(L_DEBUG|L_DATA, "rx: SNAP+RFC1042 len=%d\n", payload_length);
	/* wlan frame body contains: AA AA 03 !(00 00 00) ... -or- */
	/* wlan frame body contains: AA AA 03 00 00 00 0x80f3 ... */
	/* build eth hdr, type = len, copy AA AA 03... as eth body */
			/* it's a SNAP + RFC1042 frame && protocol is in STT */

			if (unlikely(payload_length > mtu)) {
				printk("%s: rx: SNAP frame too large (%d > %d)\n",
					adev->ndev->name,
					payload_length, mtu);
				goto ret_null;
			}

			/* allocate space and setup host buffer */
			buflen = payload_length + ETH_HLEN;
			skb = dev_alloc_skb(buflen + 2);
			if (unlikely(!skb))
				goto no_skb;
			skb_reserve(skb, 2);
			skb_put(skb, buflen);		/* make room */

			/* create 802.3 header */
			e_hdr = (wlan_ethhdr_t*) skb->data;
			MAC_COPY(e_hdr->daddr, daddr);
			MAC_COPY(e_hdr->saddr, saddr);
			e_hdr->type = htons(payload_length);

			/* Now copy the data from the 80211 frame.
			   Make room in front for the eth header, and keep the
			   llc and snap from the 802.11 payload */
			memcpy(skb->data + ETH_HLEN,
					e_llc, payload_length);

		} else {
	/* wlan frame body contains: AA AA 03 00 00 00 [type] [tail] */
	/* build eth hdr, type=[type], copy [tail] as eth body */
			log(L_DEBUG|L_DATA, "rx: 802.1h/RFC1042 len=%d\n",
				payload_length);
			/* it's an 802.1h frame (an RFC1042 && protocol is not in STT) */
			/* build a DIXII + RFC894 */

			payload_length -= sizeof(wlan_llc_t) + sizeof(wlan_snap_t);
			if (unlikely(payload_length > mtu)) {
				printk("%s: rx: DIXII frame too large (%d > %d)\n",
					adev->ndev->name,
					payload_length,	mtu);
				goto ret_null;
			}

			/* allocate space and setup host buffer */
			buflen = payload_length + ETH_HLEN;
			skb = dev_alloc_skb(buflen + 2);
			if (unlikely(!skb))
				goto no_skb;
			skb_reserve(skb, 2);
			skb_put(skb, buflen);		/* make room */

			/* create 802.3 header */
			e_hdr = (wlan_ethhdr_t *) skb->data;
			MAC_COPY(e_hdr->daddr, daddr);
			MAC_COPY(e_hdr->saddr, saddr);
			e_hdr->type = e_snap->type;

			/* Now copy the data from the 80211 frame.
			   Make room in front for the eth header, and cut off the
			   llc and snap from the 802.11 payload */
			memcpy(skb->data + ETH_HLEN,
					e_payload, payload_length);
		}

	} else {
		log(L_DEBUG|L_DATA, "rx: NON-ENCAP len=%d\n", payload_length);
	/* build eth hdr, type=len, copy wlan body as eth body */
		/* any NON-ENCAP */
		/* it's a generic 80211+LLC or IPX 'Raw 802.3' */
		/* build an 802.3 frame */

		if (unlikely(payload_length > mtu)) {
			printk("%s: rx: OTHER frame too large (%d > %d)\n",
				adev->ndev->name, payload_length, mtu);
			goto ret_null;
		}

		/* allocate space and setup host buffer */
		buflen = payload_length + ETH_HLEN;
		skb = dev_alloc_skb(buflen + 2);
		if (unlikely(!skb))
			goto no_skb;
		skb_reserve(skb, 2);
		skb_put(skb, buflen);		/* make room */

		/* set up the 802.3 header */
		e_hdr = (wlan_ethhdr_t *) skb->data;
		MAC_COPY(e_hdr->daddr, daddr);
		MAC_COPY(e_hdr->saddr, saddr);
		e_hdr->type = htons(payload_length);

		/* now copy the data from the 80211 frame */
		memcpy(skb->data + ETH_HLEN, e_llc, payload_length);
	}

	skb->dev = adev->ndev;
	skb->protocol = eth_type_trans(skb, adev->ndev);

#ifdef DEBUG_CONVERT
	if (acx_debug & L_DATA) {
		int len = RXBUF_BYTES_RCVD(adev, rxbuf);
		printk("p802.11 frame [%d]: ", len);
		acx_dump_bytes(w_hdr, len);
		printk("eth frame [%d]: ", skb->len);
		acx_dump_bytes(skb->data, skb->len);
	}
#endif

	FN_EXIT0;
	return skb;

no_skb:
	printk("%s: rx: no memory for skb (%d bytes)\n",
			adev->ndev->name, buflen + 2);
ret_null:
	FN_EXIT1((int)NULL);
	return NULL;
}
