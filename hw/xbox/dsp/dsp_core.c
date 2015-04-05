/*
	DSP M56001 emulation
	Host/Emulator <-> DSP glue

	(C) 2003-2008 ARAnyM developer team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "dsp_core.h"
#include "dsp_cpu.h"
#include "ioMem.h"
#include "dsp.h"
#include "log.h"

/*--- the DSP core itself ---*/
dsp_core_t dsp_core;

/*--- Defines ---*/
#ifndef M_PI
#define M_PI	3.141592653589793238462643383279502
#endif

/*--- Functions prototypes ---*/
static void dsp_core_dsp2host(void);
static void dsp_core_host2dsp(void);

static void (*dsp_host_interrupt)(void);   /* Function to trigger host interrupt */

/* Init DSP emulation */
void dsp_core_init(void (*host_interrupt)(void))
{
	int i;

	LOG_TRACE(TRACE_DSP_STATE, "Dsp: core init\n");

	dsp_host_interrupt = host_interrupt;
	memset(&dsp_core, 0, sizeof(dsp_core_t));

	/* Initialize Y:rom[0x0100-0x01ff] with a sin table */
	for (i=0;i<256;i++) {
		float src = (((float) i)*M_PI)/128.0;
		Sint32 dest = (Sint32) (sin(src) * 8388608.0); /* 1<<23 */
		if (dest>8388607) {
			dest = 8388607;
		} else if (dest<-8388608) {
			dest = -8388608;
		}
		dsp_core.rom[DSP_SPACE_Y][0x100+i]=dest & 0x00ffffff;
	}

	/* Initialize X:rom[0x0100-0x017f] with a mu-law table */
	{
		const Uint16 mulaw_base[8]={
			0x7d7c, 0x3e7c, 0x1efc, 0x0f3c, 0x075c, 0x036c, 0x0174, 0x0078
		};

		Uint32 position = 0x0100;
		Uint32 offset = 0x040000;

		for(i=0;i<8;i++) {
			int j;
			Uint32 value = mulaw_base[i]<<8;

			for (j=0;j<16;j++) {
				dsp_core.rom[DSP_SPACE_X][position++]=value;
				value -= offset;
			}

			offset >>= 1;
		}
	}

	/* Initialize X:rom[0x0180-0x01ff] with a a-law table */
	{
		const Sint32 multiply_base[8]={
			0x1580, 0x0ac0, 0x5600, 0x2b00,
			0x1580, 0x0058, 0x0560, 0x02b0
		};
		const Sint32 multiply_col[4]={0x10, 0x01, 0x04, 0x02};
		const Sint32 multiply_line[4]={0x40, 0x04, 0x10, 0x08};
		const Sint32 base_values[4]={0, -1, 2, 1};
		Uint32 pos=0x0180;
		
		for (i=0;i<8;i++) {
			Sint32 alawbase, j;

			alawbase = multiply_base[i]<<8;
			for (j=0;j<4;j++) {
				Sint32 alawbase1, k;
				
				alawbase1 = alawbase + ((base_values[j]*multiply_line[i & 3])<<12);

				for (k=0;k<4;k++) {
					Sint32 alawbase2;

					alawbase2 = alawbase1 + ((base_values[k]*multiply_col[i & 3])<<12);

					dsp_core.rom[DSP_SPACE_X][pos++]=alawbase2;
				}
			}
		}
	}
}

/* Shutdown DSP emulation */
void dsp_core_shutdown(void)
{
	dsp_core.running = 0;
	LOG_TRACE(TRACE_DSP_STATE, "Dsp: core shutdown\n");
}

/* Reset */
void dsp_core_reset(void)
{
	int i;

	LOG_TRACE(TRACE_DSP_STATE, "Dsp: core reset\n");
	dsp_core_shutdown();

	/* Memory */
	memset((void*)dsp_core.periph, 0, sizeof(dsp_core.periph));
	memset(dsp_core.stack, 0, sizeof(dsp_core.stack));
	memset(dsp_core.registers, 0, sizeof(dsp_core.registers));
	dsp_core.dsp_host_rtx = 0;
	dsp_core.dsp_host_htx = 0;

	dsp_core.bootstrap_pos = 0;
	
	/* Registers */
	dsp_core.pc = 0x0000;
	dsp_core.registers[DSP_REG_OMR]=0x02;
	for (i=0;i<8;i++) {
		dsp_core.registers[DSP_REG_M0+i]=0x00ffff;
	}

	/* Interruptions */
	memset((void*)dsp_core.interrupt_isPending, 0, sizeof(dsp_core.interrupt_isPending));
	dsp_core.interrupt_state = DSP_INTERRUPT_NONE;
	dsp_core.interrupt_instr_fetch = -1;
	dsp_core.interrupt_save_pc = -1;
	dsp_core.interrupt_counter = 0;
	dsp_core.interrupt_pipeline_count = 0;
	for (i=0;i<5;i++) {
		dsp_core.interrupt_ipl[i] = 3;
	}
	for (i=5;i<12;i++) {
		dsp_core.interrupt_ipl[i] = -1;
	}

	/* host port init, dsp side */
	dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR]=(1<<DSP_HOST_HSR_HTDE);

	/* host port init, cpu side */
	dsp_core.hostport[CPU_HOST_ICR] = 0x0;
	dsp_core.hostport[CPU_HOST_CVR] = 0x12;
	dsp_core.hostport[CPU_HOST_ISR] = (1<<CPU_HOST_ISR_TRDY)|(1<<CPU_HOST_ISR_TXDE);
	dsp_core.hostport[CPU_HOST_IVR] = 0x0f;
	dsp_core.hostport[CPU_HOST_RX0] = 0x0;

	/* SSI registers */
	dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR]=1<<DSP_SSI_SR_TDE;
	dsp_core.ssi.waitFrameTX = 1;
	dsp_core.ssi.waitFrameRX = 1;
	dsp_core.ssi.TX = 0;
	dsp_core.ssi.RX = 0;
	dsp_core.ssi.dspPlay_handshakeMode_frame = 0;
	dsp_core_ssi_configure(DSP_SSI_CRA, 0);
	dsp_core_ssi_configure(DSP_SSI_CRB, 0);

	/* Other hardware registers */
	dsp_core.periph[DSP_SPACE_X][DSP_IPR]=0;
	dsp_core.periph[DSP_SPACE_X][DSP_BCR]=0xffff;

	/* Misc */
	dsp_core.loop_rep = 0;

	LOG_TRACE(TRACE_DSP_STATE, "Dsp: reset done\n");
	dsp56k_init_cpu();
}

/* 
	SSI INTERFACE processing
*/

/* Set PortC data register : send a frame order to the DMA in handshake mode */
void dsp_core_setPortCDataRegister(Uint32 value)
{
	/* if DSP Record is in handshake mode with DMA Play */
	if ((dsp_core.periph[DSP_SPACE_X][DSP_PCDDR] & 0x10) == 0x10) {
		if ((value & 0x10) == 0x10) {
			dsp_core.ssi.waitFrameRX = 0;
			DSP_SsiTransmit_SC1();
			LOG_TRACE(TRACE_DSP_HOST_SSI, "Dsp record in handshake mode: SSI send SC1 to crossbar\n");
		}
	}

	/* if DSP Play is in handshake mode with DMA Record, high or low frame sync */
	/* to allow / disable transfer of the data */
	if ((dsp_core.periph[DSP_SPACE_X][DSP_PCDDR] & 0x20) == 0x20) {
		if ((value & 0x20) == 0x20) {
			dsp_core.ssi.dspPlay_handshakeMode_frame = 1;
			dsp_core.ssi.waitFrameTX = 0;
			LOG_TRACE(TRACE_DSP_HOST_SSI, "Dsp play in handshake mode: frame = 1\n");
		}
		else {
			dsp_core.ssi.dspPlay_handshakeMode_frame = 0;
			DSP_SsiTransmit_SC2(0);
			LOG_TRACE(TRACE_DSP_HOST_SSI, "Dsp play in handshake mode: SSI send SC2 to crossbar, frame sync = 0\n");
		}
	}
}

/* SSI set TX register */
void dsp_core_ssi_writeTX(Uint32 value)
{
	/* Clear SSI TDE bit */
	dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] &= 0xff-(1<<DSP_SSI_SR_TDE);
	dsp_core.ssi.TX = value;
	LOG_TRACE(TRACE_DSP_HOST_SSI, "Dsp set TX register: 0x%06x\n", value);

	/* if DSP Play is in handshake mode with DMA Record, send frame sync */
	/* to allow transfer of the data */
	if (dsp_core.ssi.dspPlay_handshakeMode_frame) {
		DSP_SsiTransmit_SC2(1);
		LOG_TRACE(TRACE_DSP_HOST_SSI, "Dsp play in handshake mode: SSI send SC2 to crossbar, frame sync = 1\n");
	}
}

/* SSI set TDE register (dummy write) */
void dsp_core_ssi_writeTSR(void)
{
	/* Dummy write : Just clear SSI TDE bit */
	dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] &= 0xff-(1<<DSP_SSI_SR_TDE);
}

/* SSI get RX register */
Uint32 dsp_core_ssi_readRX(void)
{
	/* Clear SSI RDF bit */
	dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] &= 0xff-(1<<DSP_SSI_SR_RDF);
	LOG_TRACE(TRACE_DSP_HOST_SSI, "Dsp read RX register: 0x%06x\n", dsp_core.ssi.RX);
	return dsp_core.ssi.RX;
}


/**
 * SSI receive serial clock.
 *
 */
void dsp_core_ssi_Receive_SC0(void)
{
	Uint32 value, i, temp=0;

	/* Receive data from crossbar to SSI */
	value = dsp_core.ssi.received_value;

	/* adjust value to receive size word */
	value <<= (24 - dsp_core.ssi.cra_word_length);
	value &= 0xffffff;

	/* if bit SHFD in CRB is set, swap received data */
	if (dsp_core.ssi.crb_shifter) {
		temp=0;
		for (i=0; i<dsp_core.ssi.cra_word_length; i++) {
			temp += value & 1;
			temp <<= 1;
			value >>= 1;
		}
		value = temp;
	}

	LOG_TRACE(TRACE_DSP_HOST_SSI, "Dsp SSI received value from crossbar: 0x%06x\n", value);

	if (dsp_core.ssi.crb_re && dsp_core.ssi.waitFrameRX == 0) {
		/* Send value to DSP receive */
		dsp_core.ssi.RX = value;

		/* generate interrupt ? */
		if (dsp_core.periph[DSP_SPACE_X][DSP_SSI_CRB] & (1<<DSP_SSI_CRB_RIE)) {
			if (dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] & (1<<DSP_SSI_SR_RDF)) {
				dsp_add_interrupt(DSP_INTER_SSI_RCV_DATA);
			} else {
				dsp_add_interrupt(DSP_INTER_SSI_RCV_DATA);
			}
		}
	}else{
		dsp_core.ssi.RX = 0;
	}

	/* set RDF */
	dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] |= 1<<DSP_SSI_SR_RDF;
}

/**
 * SSI receive SC1 bit : frame sync for receiver
 *     value = 1 : beginning of a new frame
 *     value = 0 : not beginning of a new frame
 */
void dsp_core_ssi_Receive_SC1(Uint32 value)
{
	/* SSI runs in network mode ? */
	if (dsp_core.ssi.crb_mode) {
		if (value) {
			/* Beginning of a new frame */
			dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] |= (1<<DSP_SSI_SR_RFS);
			dsp_core.ssi.waitFrameRX = 0;
		}else{
			dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] &= 0xff-(1<<DSP_SSI_SR_RFS);
		}
	}else{
		/* SSI runs in normal mode */
		dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] |= (1<<DSP_SSI_SR_RFS);
	}

	LOG_TRACE(TRACE_DSP_HOST_SSI, "Dsp SSI receive frame sync: 0x%01x\n", value);
}

/**
 * SSI receive SC2 bit : frame sync for transmitter
 *     value = 1 : beginning of a new frame
 *     value = 0 : not beginning of a new frame
 */
void dsp_core_ssi_Receive_SC2(Uint32 value)
{
	/* SSI runs in network mode ? */
	if (dsp_core.ssi.crb_mode) {
		if (value) {
			/* Beginning of a new frame */
			dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] |= (1<<DSP_SSI_SR_TFS);
			dsp_core.ssi.waitFrameTX = 0;
		}else{
			dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] &= 0xff-(1<<DSP_SSI_SR_TFS);
		}
	}else{
		/* SSI runs in normal mode */
		dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] |= (1<<DSP_SSI_SR_TFS);
	}

	LOG_TRACE(TRACE_DSP_HOST_SSI, "Dsp SSI transmit frame sync: 0x%01x\n", value);
}

/**
 * SSI transmit serial clock.
 *
 */
void dsp_core_ssi_Receive_SCK(void)
{
	Uint32 value, i, temp=0;

	value = dsp_core.ssi.TX;

	/* Transfer data from SSI to crossbar*/

	/* adjust value to transnmit size word */
	value >>= (24 - dsp_core.ssi.cra_word_length);
	value &= dsp_core.ssi.cra_word_mask;

	/* if bit SHFD in CRB is set, swap data to transmit */
	if (dsp_core.ssi.crb_shifter) {
		for (i=0; i<dsp_core.ssi.cra_word_length; i++) {
			temp += value & 1;
			temp <<= 1;
			value >>= 1;
		}
		value = temp;
	}

	LOG_TRACE(TRACE_DSP_HOST_SSI, "Dsp SSI transmit value to crossbar: 0x%06x\n", value);

	/* Transmit the data */
	if (dsp_core.ssi.crb_te && dsp_core.ssi.waitFrameTX == 0) {
		/* Send value to crossbar */
		dsp_core.ssi.transmit_value = value;

		/* generate interrupt ? */
		if (dsp_core.periph[DSP_SPACE_X][DSP_SSI_CRB] & (1<<DSP_SSI_CRB_TIE)) {
			if (dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] & (1<<DSP_SSI_SR_TDE)) {
				dsp_add_interrupt(DSP_INTER_SSI_TRX_DATA);
			} else {
				dsp_add_interrupt(DSP_INTER_SSI_TRX_DATA);
			}
		}
	}else{
		dsp_core.ssi.transmit_value = 0;
	}

	/* set TDE */
	dsp_core.periph[DSP_SPACE_X][DSP_SSI_SR] |= (1<<DSP_SSI_SR_TDE);
}


/* SSI initialisations and state management */
void dsp_core_ssi_configure(Uint32 address, Uint32 value)
{
	Uint32 crb_te, crb_re;

	switch (address) {
		case DSP_SSI_CRA:
			dsp_core.periph[DSP_SPACE_X][DSP_SSI_CRA] = value;
			/* get word size for transfers */
			switch ((value>>DSP_SSI_CRA_WL0) & 3) {
				case 0:
					dsp_core.ssi.cra_word_length = 8;
					dsp_core.ssi.cra_word_mask = 0xff;
					break;
				case 1:
					dsp_core.ssi.cra_word_length = 12;
					dsp_core.ssi.cra_word_mask = 0xfff;
					break;
				case 2:
					dsp_core.ssi.cra_word_length = 16;
					dsp_core.ssi.cra_word_mask = 0xffff;
					break;
				case 3:
					dsp_core.ssi.cra_word_length = 24;
					dsp_core.ssi.cra_word_mask = 0xffffff;
					break;
			}

			LOG_TRACE(TRACE_DSP_HOST_SSI, "Dsp SSI CRA write: 0x%06x\n", value);

			/* Get the Frame rate divider ( 2 < value <32) */
			dsp_core.ssi.cra_frame_rate_divider = ((value >> DSP_SSI_CRA_DC0) & 0x1f)+1;
			break;
		case DSP_SSI_CRB:
			crb_te = dsp_core.periph[DSP_SPACE_X][DSP_SSI_CRB] & (1<<DSP_SSI_CRB_TE);
			crb_re = dsp_core.periph[DSP_SPACE_X][DSP_SSI_CRB] & (1<<DSP_SSI_CRB_RE);
			dsp_core.periph[DSP_SPACE_X][DSP_SSI_CRB] = value;
	
			dsp_core.ssi.crb_src_clock = (value>>DSP_SSI_CRB_SCKD) & 1;
			dsp_core.ssi.crb_shifter   = (value>>DSP_SSI_CRB_SHFD) & 1;
			dsp_core.ssi.crb_synchro   = (value>>DSP_SSI_CRB_SYN) & 1;
			dsp_core.ssi.crb_mode      = (value>>DSP_SSI_CRB_MOD) & 1;
			dsp_core.ssi.crb_te        = (value>>DSP_SSI_CRB_TE) & 1;
			dsp_core.ssi.crb_re        = (value>>DSP_SSI_CRB_RE) & 1;
			dsp_core.ssi.crb_tie       = (value>>DSP_SSI_CRB_TIE) & 1;
			dsp_core.ssi.crb_rie       = (value>>DSP_SSI_CRB_RIE) & 1;

			if (crb_te == 0 && dsp_core.ssi.crb_te) {
				dsp_core.ssi.waitFrameTX = 1;
			}
			if (crb_re == 0 && dsp_core.ssi.crb_re) {
				dsp_core.ssi.waitFrameRX = 1;
			}

			LOG_TRACE(TRACE_DSP_HOST_SSI, "Dsp SSI CRB write: 0x%06x\n", value);

			break;
	}
}


/* 
	HOST INTERFACE processing
*/

static void dsp_core_hostport_update_trdy(void)
{
	int trdy;

	/* Clear/set TRDY bit */
	dsp_core.hostport[CPU_HOST_ISR] &= 0xff-(1<<CPU_HOST_ISR_TRDY);
	trdy = (dsp_core.hostport[CPU_HOST_ISR]>>CPU_HOST_ISR_TXDE)
		& ~(dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR]>>DSP_HOST_HSR_HRDF);
	dsp_core.hostport[CPU_HOST_ISR] |= (trdy & 1)<< CPU_HOST_ISR_TRDY;
}

static void dsp_core_hostport_update_hreq(void)
{
	int hreq;

	hreq = (dsp_core.hostport[CPU_HOST_ICR] & dsp_core.hostport[CPU_HOST_ISR]) & 0x3;

	/* Trigger host interrupt? */
	if (hreq && (dsp_core.hostport[CPU_HOST_ISR] & (1<<CPU_HOST_ISR_HREQ)) == 0) {
		dsp_host_interrupt();
	}

	/* Set HREQ bit in hostport */
	dsp_core.hostport[CPU_HOST_ISR] &= 0x7f;
	dsp_core.hostport[CPU_HOST_ISR] |= (hreq?1:0) << CPU_HOST_ISR_HREQ;
} 

/* Host port transfer ? (dsp->host) */
static void dsp_core_dsp2host(void)
{
	/* RXDF = 1 ==> host hasn't read the last value yet */
	if (dsp_core.hostport[CPU_HOST_ISR] & (1<<CPU_HOST_ISR_RXDF)) {
		return;
	}

	/* HTDE = 1 ==> nothing to tranfert from DSP port */
	if (dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR] & (1<<DSP_HOST_HSR_HTDE)) {
		return;
	}

	dsp_core.hostport[CPU_HOST_RXL] = dsp_core.dsp_host_htx;
	dsp_core.hostport[CPU_HOST_RXM] = dsp_core.dsp_host_htx>>8;
	dsp_core.hostport[CPU_HOST_RXH] = dsp_core.dsp_host_htx>>16;

	/* Set HTDE bit to say that DSP can write */
	dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR] |= 1<<DSP_HOST_HSR_HTDE;

	/* Is there an interrupt to send ? */
	if (dsp_core.periph[DSP_SPACE_X][DSP_HOST_HCR] & (1<<DSP_HOST_HCR_HTIE)) {
		dsp_add_interrupt(DSP_INTER_HOST_TRX_DATA);
	}

	/* Set RXDF bit to say that host can read */
	dsp_core.hostport[CPU_HOST_ISR] |= 1<<CPU_HOST_ISR_RXDF;
	dsp_core_hostport_update_hreq();

	LOG_TRACE(TRACE_DSP_HOST_INTERFACE, "Dsp: (DSP->Host): Transfer 0x%06x, Dsp HTDE=1, Host RXDF=1\n", dsp_core.dsp_host_htx);
}

/* Host port transfer ? (host->dsp) */
static void dsp_core_host2dsp(void)
{
	/* TXDE = 1 ==> nothing to tranfert from host port */
	if (dsp_core.hostport[CPU_HOST_ISR] & (1<<CPU_HOST_ISR_TXDE)) {
		return;
	}
	
	/* HRDF = 1 ==> DSP hasn't read the last value yet */
	if (dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR] & (1<<DSP_HOST_HSR_HRDF)) {
		return;
	}

	dsp_core.dsp_host_rtx = dsp_core.hostport[CPU_HOST_TXL];
	dsp_core.dsp_host_rtx |= dsp_core.hostport[CPU_HOST_TXM]<<8;
	dsp_core.dsp_host_rtx |= dsp_core.hostport[CPU_HOST_TXH]<<16;

	/* Set HRDF bit to say that DSP can read */
	dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR] |= 1<<DSP_HOST_HSR_HRDF;

	/* Is there an interrupt to send ? */
	if (dsp_core.periph[DSP_SPACE_X][DSP_HOST_HCR] & (1<<DSP_HOST_HCR_HRIE)) {
		dsp_add_interrupt(DSP_INTER_HOST_RCV_DATA);
	}

	/* Set TXDE bit to say that host can write */
	dsp_core.hostport[CPU_HOST_ISR] |= 1<<CPU_HOST_ISR_TXDE;
	dsp_core_hostport_update_hreq();

	LOG_TRACE(TRACE_DSP_HOST_INTERFACE, "Dsp: (Host->DSP): Transfer 0x%06x, Dsp HRDF=1, Host TXDE=1\n", dsp_core.dsp_host_rtx);

	dsp_core_hostport_update_trdy();
}

void dsp_core_hostport_dspread(void)
{
	/* Clear HRDF bit to say that DSP has read */
	dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR] &= 0xff-(1<<DSP_HOST_HSR_HRDF);

	LOG_TRACE(TRACE_DSP_HOST_INTERFACE, "Dsp: (Host->DSP): Dsp HRDF cleared\n");

	dsp_core_hostport_update_trdy();
	dsp_core_host2dsp();
}

void dsp_core_hostport_dspwrite(void)
{
	/* Clear HTDE bit to say that DSP has written */
	dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR] &= 0xff-(1<<DSP_HOST_HSR_HTDE);

	LOG_TRACE(TRACE_DSP_HOST_INTERFACE, "Dsp: (DSP->Host): Dsp HTDE cleared\n");

	dsp_core_dsp2host();
}

/* Read/writes on host port */
Uint8 dsp_core_read_host(int addr)
{
	Uint8 value;

	value = dsp_core.hostport[addr];
	if (addr == CPU_HOST_TRXL) {
		/* Clear RXDF bit to say that CPU has read */
		dsp_core.hostport[CPU_HOST_ISR] &= 0xff-(1<<CPU_HOST_ISR_RXDF);
		dsp_core_dsp2host();
		dsp_core_hostport_update_hreq();
		
		LOG_TRACE(TRACE_DSP_HOST_INTERFACE, "Dsp: (DSP->Host): Host RXDF=0\n");
	}
	return value;
}

void dsp_core_write_host(int addr, Uint8 value)
{
	switch(addr) {
		case CPU_HOST_ICR:
			dsp_core.hostport[CPU_HOST_ICR]=value & 0xfb;
			/* Set HF1 and HF0 accordingly on the host side */
			dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR] &=
					0xff-((1<<DSP_HOST_HSR_HF1)|(1<<DSP_HOST_HSR_HF0));
			dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR] |=
					dsp_core.hostport[CPU_HOST_ICR] & ((1<<DSP_HOST_HSR_HF1)|(1<<DSP_HOST_HSR_HF0));
			dsp_core_hostport_update_hreq();
			break;
		case CPU_HOST_CVR:
			dsp_core.hostport[CPU_HOST_CVR]=value & 0x9f;
			/* if bit 7=1, host command . HSR(bit HCP) is set*/
			if (value & (1<<7)) {
				dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR] |= (1<<DSP_HOST_HSR_HCP);
				/* Is there an interrupt to send ? */
				if (dsp_core.periph[DSP_SPACE_X][DSP_HOST_HCR] & (1<<DSP_HOST_HCR_HCIE)) {
					dsp_add_interrupt(DSP_INTER_HOST_COMMAND);
				}
			}
			else{
				dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR] &= 0xff - (1<<DSP_HOST_HSR_HCP);
			}

			LOG_TRACE(TRACE_DSP_HOST_COMMAND, "Dsp: (Host->DSP): Host command = %06x\n", value & 0x9f);

			break;
		case CPU_HOST_ISR:
		case CPU_HOST_TRX0:
			/* Read only */
			break;
		case CPU_HOST_IVR:
			dsp_core.hostport[CPU_HOST_IVR]=value;
			break;
		case CPU_HOST_TRXH:
			dsp_core.hostport[CPU_HOST_TXH]=value;
			break;
		case CPU_HOST_TRXM:
			dsp_core.hostport[CPU_HOST_TXM]=value;
			break;
		case CPU_HOST_TRXL:
			dsp_core.hostport[CPU_HOST_TXL]=value;

			if (!dsp_core.running) {
				dsp_core.ramint[DSP_SPACE_P][dsp_core.bootstrap_pos] =
					(dsp_core.hostport[CPU_HOST_TXH]<<16) |
					(dsp_core.hostport[CPU_HOST_TXM]<<8) |
					 dsp_core.hostport[CPU_HOST_TXL];
				
				LOG_TRACE(TRACE_DSP_STATE, "Dsp: bootstrap p:0x%04x = 0x%06x\n",
								dsp_core.bootstrap_pos,
								dsp_core.ramint[DSP_SPACE_P][dsp_core.bootstrap_pos]);

				if (++dsp_core.bootstrap_pos == 0x200) {
					LOG_TRACE(TRACE_DSP_STATE, "Dsp: wait bootstrap done\n");
					dsp_core.running = 1;
				}
			} else {

				/* If TRDY is set, the tranfert is direct to DSP (Burst mode) */
				if (dsp_core.hostport[CPU_HOST_ISR] & (1<<CPU_HOST_ISR_TRDY)){
					dsp_core.dsp_host_rtx = dsp_core.hostport[CPU_HOST_TXL];
					dsp_core.dsp_host_rtx |= dsp_core.hostport[CPU_HOST_TXM]<<8;
					dsp_core.dsp_host_rtx |= dsp_core.hostport[CPU_HOST_TXH]<<16;

					LOG_TRACE(TRACE_DSP_HOST_INTERFACE, "Dsp: (Host->DSP): Direct Transfer 0x%06x\n", dsp_core.dsp_host_rtx);

					/* Set HRDF bit to say that DSP can read */
					dsp_core.periph[DSP_SPACE_X][DSP_HOST_HSR] |= 1<<DSP_HOST_HSR_HRDF;

					/* Is there an interrupt to send ? */
					if (dsp_core.periph[DSP_SPACE_X][DSP_HOST_HCR] & (1<<DSP_HOST_HCR_HRIE)) {
						dsp_add_interrupt(DSP_INTER_HOST_RCV_DATA);
					}

					LOG_TRACE(TRACE_DSP_HOST_INTERFACE, "Dsp: (Host->DSP): Dsp HRDF set\n");
				}
				else{
					/* Clear TXDE to say that CPU has written */
					dsp_core.hostport[CPU_HOST_ISR] &= 0xff-(1<<CPU_HOST_ISR_TXDE);
					dsp_core_hostport_update_hreq();

					LOG_TRACE(TRACE_DSP_HOST_INTERFACE, "Dsp: (Host->DSP): Host TXDE cleared\n");
				}
				dsp_core_hostport_update_trdy();
				dsp_core_host2dsp();
			}
			break;
	}
}

/*
vim:ts=4:sw=4:
*/
