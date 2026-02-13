/*
 * trx.h
 *
 * OpenBTS TRX interface handling
 *
 * Copyright (C) 2013  Sylvain Munaut <tnt@246tNt.com>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __TRX_TRX_H__
#define __TRX_TRX_H__


#include <stdint.h>

#include <osmocom/core/bits.h>
#include <osmocom/core/select.h>
#include <osmocom/core/timer.h>


#define ARFCN_INVAL	0xffff
#define BSIC_INVAL	0xff


struct trx {
	/* UDP sockets */
	struct osmo_fd ofd_clk;
	struct osmo_fd ofd_ctrl;
	struct osmo_fd ofd_data;

	/* Link to app state */
	struct app_state *as;

	/* Link to L1CTL for each TN */
	struct l1ctl_link *l1l[8];

	/* TRX configuration */
	int power;
	uint16_t arfcn;
	uint8_t  bsic;
	int gain;
	uint8_t type[8];
	uint8_t handover[8];

	/* Software TDMA clock */
	struct osmo_timer_list clk_timer;
	uint32_t clk_fn;
};


struct trx *trx_alloc(const char *addr, uint16_t base_port,
                      struct app_state *as, int clock);
void trx_free(struct trx *trx);

int trx_clk_ind(struct trx *trx, uint32_t fn);
int trx_data_ind(struct trx *trx, uint32_t fn, uint8_t tn, sbit_t *data, float toa, int8_t rssi);


#endif /* __TRX_TRX_H__ */

// FIX: Synchronisation forcée avant l'horloge
