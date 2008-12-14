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


#include "hw/atheros_wlan.h"
#include "hw/atheros_wlan_crc32.h"
#include "hw/atheros_wlan_packet.h"

#define FRAME_INSERT(_8bit_data)	buf[i++] = _8bit_data


static int insertCRC(struct mac80211_frame *frame, uint32_t frame_length)
{
	unsigned long crc;
	unsigned char *fcs = (unsigned char*)frame;

	crc = crc32_ccitt(fcs, frame_length);
	memcpy(&fcs[frame_length], &crc, 4);

	return frame_length + 4;
}


void Atheros_WLAN_init_frame(Atheros_WLANState *s, struct mac80211_frame *frame)
{
	if (!frame)
		return;

	frame->sequence_control.sequence_number = s->inject_sequence_number++;
	memcpy(frame->source_address, s->ap_macaddr, 6);
	memcpy(frame->bssid_address, s->ap_macaddr, 6);

	frame->frame_length = insertCRC(frame, frame->frame_length);
}


struct mac80211_frame *Atheros_WLAN_create_beacon_frame()
{
	unsigned int i;
	unsigned char *buf;
	struct mac80211_frame *frame;

	frame = (struct mac80211_frame *)malloc(sizeof(struct mac80211_frame));
	if (!frame)
	{
		return NULL;
	}

	frame->next_frame = NULL;
	frame->frame_control.protocol_version = 0;
	frame->frame_control.type = IEEE80211_TYPE_MGT;
	frame->frame_control.sub_type = IEEE80211_TYPE_MGT_SUBTYPE_BEACON;
	frame->frame_control.flags = 0;
	frame->duration_id = 0;
	frame->sequence_control.fragment_number = 0;

	for (i=0; i<6; frame->destination_address[i] = 0xff, i++);

	i = 0;
	buf = (unsigned char*)frame->data_and_fcs;

	/*
	 * Fixed params... typical AP params (12 byte)
	 *
	 * They include
	 *  - Timestamp
	 *  - Beacon Interval
	 *  - Capability Information
	 */
	FRAME_INSERT(0x8d);
	FRAME_INSERT(0x61);
	FRAME_INSERT(0xa5);
	FRAME_INSERT(0x18);
	FRAME_INSERT(0x00);
	FRAME_INSERT(0x00);
	FRAME_INSERT(0x00);
	FRAME_INSERT(0x00);
	FRAME_INSERT(0x64);
	FRAME_INSERT(0x00);
	FRAME_INSERT(0x01);
	FRAME_INSERT(0x00);

	FRAME_INSERT(IEEE80211_BEACON_PARAM_SSID);
	FRAME_INSERT(4);	// length
	FRAME_INSERT('Q');	// SSID
	FRAME_INSERT('L');	// SSID
	FRAME_INSERT('a');	// SSID
	FRAME_INSERT('n');	// SSID

	FRAME_INSERT(IEEE80211_BEACON_PARAM_RATES);
	FRAME_INSERT(8);	// length
	FRAME_INSERT(0x82);
	FRAME_INSERT(0x84);
	FRAME_INSERT(0x8b);
	FRAME_INSERT(0x96);
	FRAME_INSERT(0x24);
	FRAME_INSERT(0x30);
	FRAME_INSERT(0x48);
	FRAME_INSERT(0x6c);

	FRAME_INSERT(IEEE80211_BEACON_PARAM_CHANNEL);
	FRAME_INSERT(1);	// length
	FRAME_INSERT(0x09);

	frame->frame_length = IEEE80211_HEADER_SIZE + i;
	return frame;
}

struct mac80211_frame *Atheros_WLAN_create_probe_response()
{
	unsigned int i;
	unsigned char *buf;
	struct mac80211_frame *frame;

	frame = (struct mac80211_frame *)malloc(sizeof(struct mac80211_frame));
	if (!frame)
	{
		return NULL;
	}

	frame->next_frame = NULL;
	frame->frame_control.protocol_version = 0;
	frame->frame_control.type = IEEE80211_TYPE_MGT;
	frame->frame_control.sub_type = IEEE80211_TYPE_MGT_SUBTYPE_PROBE_RESP;
	frame->frame_control.flags = 0;
	frame->duration_id = 314;
	frame->sequence_control.fragment_number = 0;

	i = 0;
	buf = (unsigned char*)frame->data_and_fcs;

	/*
	 * Fixed params... typical AP params (12 byte)
	 *
	 * They include
	 *  - Timestamp
	 *  - Beacon Interval
	 *  - Capability Information
	 */
	buf[i++] = 0x8d; buf[i++] = 0x61; buf[i++] = 0xa5; buf[i++] = 0x18;
	buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x00;
	buf[i++] = 0x64; buf[i++] = 0x00; buf[i++] = 0x01; buf[i++] = 0x00;

	FRAME_INSERT(IEEE80211_BEACON_PARAM_SSID);
	FRAME_INSERT(4);	// length
	FRAME_INSERT('Q');	// SSID
	FRAME_INSERT('L');	// SSID
	FRAME_INSERT('a');	// SSID
	FRAME_INSERT('n');	// SSID

	FRAME_INSERT(IEEE80211_BEACON_PARAM_RATES);
	FRAME_INSERT(8);	// length
	FRAME_INSERT(0x82);
	FRAME_INSERT(0x84);
	FRAME_INSERT(0x8b);
	FRAME_INSERT(0x96);
	FRAME_INSERT(0x24);
	FRAME_INSERT(0x30);
	FRAME_INSERT(0x48);
	FRAME_INSERT(0x6c);

	FRAME_INSERT(IEEE80211_BEACON_PARAM_CHANNEL);
	FRAME_INSERT(1);	// length
	FRAME_INSERT(0x09);

	frame->frame_length = IEEE80211_HEADER_SIZE + i;
	return frame;
}

struct mac80211_frame *Atheros_WLAN_create_authentication()
{
	unsigned int i;
	unsigned char *buf;
	struct mac80211_frame *frame;

	frame = (struct mac80211_frame *)malloc(sizeof(struct mac80211_frame));
	if (!frame)
	{
		return NULL;
	}

	frame->next_frame = NULL;
	frame->frame_control.protocol_version = 0;
	frame->frame_control.type = IEEE80211_TYPE_MGT;
	frame->frame_control.sub_type = IEEE80211_TYPE_MGT_SUBTYPE_AUTHENTICATION;
	frame->frame_control.flags = 0;
	frame->duration_id = 314;
	frame->sequence_control.fragment_number = 0;

	i = 0;
	buf = (unsigned char*)frame->data_and_fcs;

	/*
	 * Fixed params... typical AP params (6 byte)
	 *
	 * They include
	 *  - Authentication Algorithm (here: Open System)
	 *  - Authentication SEQ
	 *  - Status code (successful 0x0)
	 */
	FRAME_INSERT(0x00);
	FRAME_INSERT(0x00);
	FRAME_INSERT(0x02);
	FRAME_INSERT(0x00);
	FRAME_INSERT(0x00);
	FRAME_INSERT(0x00);

	FRAME_INSERT(IEEE80211_BEACON_PARAM_SSID);
	FRAME_INSERT(4);	// length
	FRAME_INSERT('Q');	// SSID
	FRAME_INSERT('L');	// SSID
	FRAME_INSERT('a');	// SSID
	FRAME_INSERT('n');	// SSID

	frame->frame_length = IEEE80211_HEADER_SIZE + i;
	return frame;
}

struct mac80211_frame *Atheros_WLAN_create_deauthentication()
{
	unsigned int i;
	unsigned char *buf;
	struct mac80211_frame *frame;

	frame = (struct mac80211_frame *)malloc(sizeof(struct mac80211_frame));
	if (!frame)
	{
		return NULL;
	}

	frame->next_frame = NULL;
	frame->frame_control.protocol_version = 0;
	frame->frame_control.type = IEEE80211_TYPE_MGT;
	frame->frame_control.sub_type = IEEE80211_TYPE_MGT_SUBTYPE_DEAUTHENTICATION;
	frame->frame_control.flags = 0;
	frame->duration_id = 314;
	frame->sequence_control.fragment_number = 0;

	i = 0;
	buf = (unsigned char*)frame->data_and_fcs;

	/*
	 * Inser reason code:
	 *  "Deauthentication because sending STA is leaving"
	 */
	FRAME_INSERT(0x03);
	FRAME_INSERT(0x00);

	frame->frame_length = IEEE80211_HEADER_SIZE + i;
	return frame;
}

struct mac80211_frame *Atheros_WLAN_create_association_response()
{
	unsigned int i;
	unsigned char *buf;
	struct mac80211_frame *frame;

	frame = (struct mac80211_frame *)malloc(sizeof(struct mac80211_frame));
	if (!frame)
	{
		return NULL;
	}

	frame->next_frame = NULL;
	frame->frame_control.protocol_version = 0;
	frame->frame_control.type = IEEE80211_TYPE_MGT;
	frame->frame_control.sub_type = IEEE80211_TYPE_MGT_SUBTYPE_ASSOCIATION_RESP;
	frame->frame_control.flags = 0;
	// frame->duration_id = 314;
	frame->sequence_control.fragment_number = 0;

	i = 0;
	buf = (unsigned char*)frame->data_and_fcs;

	/*
	 * Fixed params... typical AP params (6 byte)
	 *
	 * They include
	 *  - Capability Information
	 *  - Status code (successful 0x0)
	 *  - Association ID
	 */
	FRAME_INSERT(0x01);
	FRAME_INSERT(0x00);
	FRAME_INSERT(0x00);
	FRAME_INSERT(0x00);
	FRAME_INSERT(0x01);
	FRAME_INSERT(0xc0);

	FRAME_INSERT(IEEE80211_BEACON_PARAM_SSID);
	FRAME_INSERT(4);	// length
	FRAME_INSERT('Q');	// SSID
	FRAME_INSERT('L');	// SSID
	FRAME_INSERT('a');	// SSID
	FRAME_INSERT('n');	// SSID

	FRAME_INSERT(IEEE80211_BEACON_PARAM_RATES);
	FRAME_INSERT(8);	// length
	FRAME_INSERT(0x82);
	FRAME_INSERT(0x84);
	FRAME_INSERT(0x8b);
	FRAME_INSERT(0x96);
	FRAME_INSERT(0x24);
	FRAME_INSERT(0x30);
	FRAME_INSERT(0x48);
	FRAME_INSERT(0x6c);

	FRAME_INSERT(IEEE80211_BEACON_PARAM_EXTENDED_RATES);
	FRAME_INSERT(4);	// length
	FRAME_INSERT(0x0c);
	FRAME_INSERT(0x12);
	FRAME_INSERT(0x18);
	FRAME_INSERT(0x60);

	frame->frame_length = IEEE80211_HEADER_SIZE + i;
	return frame;
}

struct mac80211_frame *Atheros_WLAN_create_disassociation()
{
	unsigned int i;
	unsigned char *buf;
	struct mac80211_frame *frame;

	frame = (struct mac80211_frame *)malloc(sizeof(struct mac80211_frame));
	if (!frame)
	{
		return NULL;
	}

	frame->next_frame = NULL;
	frame->frame_control.protocol_version = 0;
	frame->frame_control.type = IEEE80211_TYPE_MGT;
	frame->frame_control.sub_type = IEEE80211_TYPE_MGT_SUBTYPE_DISASSOCIATION;
	frame->frame_control.flags = 0;
	frame->duration_id = 314;
	frame->sequence_control.fragment_number = 0;

	i = 0;
	buf = (unsigned char*)frame->data_and_fcs;

	/*
	 * Inser reason code:
	 *  "Disassociation because sending STA is leaving"
	 */
	FRAME_INSERT(0x03);
	FRAME_INSERT(0x00);

	frame->frame_length = IEEE80211_HEADER_SIZE + i;
	return frame;
}

struct mac80211_frame *Atheros_WLAN_create_data_packet(Atheros_WLANState *s, const uint8_t *buf, int size)
{
	struct mac80211_frame *frame;

	frame = (struct mac80211_frame *)malloc(sizeof(struct mac80211_frame));
	if (!frame)
	{
		return NULL;
	}

	frame->next_frame = NULL;
	frame->frame_control.protocol_version = 0;
	frame->frame_control.type = IEEE80211_TYPE_DATA;
	frame->frame_control.sub_type = IEEE80211_TYPE_DATA_SUBTYPE_DATA;
	frame->frame_control.flags = 0x2; // from station back to station via AP
	frame->duration_id = 44;
	frame->sequence_control.fragment_number = 0;

	// send message to wlan-device
	memcpy(frame->destination_address, s->macaddr, 6);

	size -= 12; // remove old 803.2 header
	size += 6; // add new 803.11 header
	if (size > sizeof(frame->data_and_fcs))
	{
		// sanitize memcpy
		size = sizeof(frame->data_and_fcs);
	}

	// LLC
	frame->data_and_fcs[0] = 0xaa;
	frame->data_and_fcs[1] = 0xaa;
	frame->data_and_fcs[2] = 0x03;
	frame->data_and_fcs[3] = 0x00;
	frame->data_and_fcs[4] = 0x00;
	frame->data_and_fcs[5] = 0x00;

	memcpy(&frame->data_and_fcs[6], &buf[12], size);
	frame->frame_length = IEEE80211_HEADER_SIZE + size;

	return frame;
}

int Atheros_WLAN_dumpFrame(struct mac80211_frame *frame, int frame_len, char *filename)
{
        int i = 0, j;
        unsigned char buf[56];
        unsigned int frame_total_length = frame_len + 16;
        unsigned char *l = (unsigned char *)&frame_total_length;

        // Wireshark header
        buf[i++] = 0xd4; buf[i++] = 0xc3; buf[i++] = 0xb2; buf[i++] = 0xa1; buf[i++] = 0x02; buf[i++] = 0x00; buf[i++] = 0x04; buf[i++] = 0x00;
        buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x00;
        buf[i++] = 0x60; buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x7f; buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x00;
        buf[i++] = 0xd1; buf[i++] = 0x75; buf[i++] = 0x5d; buf[i++] = 0x46; buf[i++] = 0x76; buf[i++] = 0x8b; buf[i++] = 0x06; buf[i++] = 0x00;

        // total frame length
        for (j=0; j<4; buf[i++] = l[j++]);
        // captured frame length
        for (j=0; j<4; buf[i++] = l[j++]);

        // Radiotap header
        buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x10; buf[i++] = 0x00; buf[i++] = 0x0e; buf[i++] = 0x18; buf[i++] = 0x00; buf[i++] = 0x00;
        buf[i++] = 0x10; buf[i++] = 0x02; buf[i++] = 0x94; buf[i++] = 0x09; buf[i++] = 0xa0; buf[i++] = 0x00; buf[i++] = 0x00; buf[i++] = 0x26;

        FILE *fp;
        fp = fopen(filename, "w");
        if (!fp)
        {
                return 1;
        }

        fwrite(buf, 1, sizeof(buf), fp);
        fwrite(frame, 1, frame_len, fp);

        fclose(fp);

        return 0;
}

#endif /* CONFIG_WIN32 */
