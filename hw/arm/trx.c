/*
 * trx.c
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

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <osmocom/core/bits.h>
#include <osmocom/core/select.h>
#include <osmocom/core/socket.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/utils.h>

#include <osmocom/gsm/gsm_utils.h>

#include <osmocom/bb/common/logging.h>

#include "app.h"
#include "l1ctl.h"
#include "l1ctl_link.h"
#include "trx.h"
#include "burst.h"


static int _trx_clk_read_cb(struct osmo_fd *ofd, unsigned int what);
static int _trx_ctrl_read_cb(struct osmo_fd *ofd, unsigned int what);
static int _trx_data_read_cb(struct osmo_fd *ofd, unsigned int what);


/* ------------------------------------------------------------------------ */
/* Init                                                                     */
/* ------------------------------------------------------------------------ */

int
_trx_udp_init(struct trx *trx,
              struct osmo_fd *ofd, const char *addr, uint16_t port,
              int (*cb)(struct osmo_fd *fd, unsigned int what))
{
	struct sockaddr_storage _sas;
	struct sockaddr *sa = (struct sockaddr *)&_sas;
	socklen_t sa_len;
	int rv;

	/* Init */
	ofd->fd = -1;
	ofd->cb = cb;
	ofd->data = trx;

	/* Listen / Binds */
	rv = osmo_sock_init_ofd(
		ofd,
		AF_UNSPEC, SOCK_DGRAM, 0, addr, port,
		OSMO_SOCK_F_BIND);
	if (rv < 0)
		goto err;

	/* Connect */
	sa_len = sizeof(struct sockaddr_storage);
	rv = getsockname(ofd->fd, sa, &sa_len);
	if (rv)
		goto err;

	if (sa->sa_family == AF_INET) {
	        struct sockaddr_in *sin = (struct sockaddr_in *)sa;
		sin->sin_port = htons(ntohs(sin->sin_port) + 100);
	} else if (sa->sa_family == AF_INET6) {
	        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
		sin6->sin6_port = htons(ntohs(sin6->sin6_port) + 100);
	} else {
		rv = -EINVAL;
		goto err;
	}

	rv = connect(ofd->fd, sa, sa_len);
	if (rv)
		goto err;

	return 0;

err:
	if (ofd->fd >= 0) {
		osmo_fd_unregister(ofd);
		close(ofd->fd);
	}

	return rv;
}

/* TDMA Clock timer callback - generates frame clock indications every 4.615ms */
static void
trx_clk_timer_cb(void *data)
{
	struct trx *trx = data;
	trx_clk_ind(trx, trx->clk_fn++);
	osmo_timer_schedule(&trx->clk_timer, 0, 4615);
}

struct trx *
trx_alloc(const char *addr, uint16_t base_port, struct app_state *as, int clock)
{
	struct trx *trx;
	int rv;

	/* Alloc */
	trx = talloc_zero(NULL, struct trx);
	if (!trx)
		return NULL;

	/* Init */
	trx->arfcn = ARFCN_INVAL;
	trx->bsic = BSIC_INVAL;
	trx->gain = 0; /* Best test results for broadest range of RX levels */
	trx->clk_fn = 0;

	/* L1 link */
	trx->as = as;

	/* Clock */
	if (clock) {
		rv = _trx_udp_init(trx, &trx->ofd_clk, addr, base_port++, _trx_clk_read_cb);
		if (rv)
			goto err;
	}

	/* Control */
	rv = _trx_udp_init(trx, &trx->ofd_ctrl, addr, base_port++, _trx_ctrl_read_cb);
	if (rv)
		goto err;

	/* Data */
	rv = _trx_udp_init(trx, &trx->ofd_data, addr, base_port++, _trx_data_read_cb);
	if (rv)
		goto err;

	/* Configure and start software TDMA clock */
	osmo_timer_setup(&trx->clk_timer, trx_clk_timer_cb, trx);
	osmo_timer_schedule(&trx->clk_timer, 0, 4615);

	return trx;

err:

	return NULL;
}

void
trx_free(struct trx *trx)
{
	/* Stop the software TDMA clock */
	osmo_timer_del(&trx->clk_timer);

	/* FIXME close and remove ofd */

	talloc_free(trx);
}


/* ------------------------------------------------------------------------ */
/* Clk interface                                                           */
/* ------------------------------------------------------------------------ */

static int
_trx_clk_read_cb(struct osmo_fd *ofd, unsigned int what)
{
	char buf[1500];
	int l;

	l = recv(ofd->fd, buf, sizeof(buf), 0);
	if (l <= 0)
		return l;

	LOGP(DTRX, LOGL_ERROR, "[!] Unexpected data on the CLK interface, discarding\n");

	return l;
}

int
trx_clk_ind(struct trx *trx, uint32_t fn)
{
	char buf[64];

	LOGP(DTRX, LOGL_DEBUG, "TRX CLK Indication %d\n", fn);

	snprintf(buf, sizeof(buf), "IND CLOCK %d", fn + 2); /* FIXME Dynamic adjust ? */
	send(trx->ofd_clk.fd, buf, strlen(buf)+1, 0);

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Control interface                                                        */
/* ------------------------------------------------------------------------ */

#define TRX_CMD_BUF_LEN	128

static int
_trx_ctrl_send_resp(struct trx *trx, const char *cmd, int status, const char *fmt, ...)
{
	va_list ap;
	char buf[TRX_CMD_BUF_LEN];
	int l;

	l = snprintf(buf, sizeof(buf)-1, "RSP %s %d ", cmd, status);

	if (fmt != NULL) {
		va_start(ap, fmt);
		l += vsnprintf(buf+l, sizeof(buf)-l-1, fmt, ap);
		va_end(ap);
	}

	buf[l] = '\0';

	LOGP(DTRX, LOGL_DEBUG, "TRX Control send: |%s|\n", buf);

	send(trx->ofd_ctrl.fd, buf, strlen(buf)+1, 0);

	return 0;
}

static int
_trx_ctrl_cmd_poweroff(struct trx *trx, const char *cmd, const char *args)
{
	int i;

	trx->power = 0;
	for (i = 0; i < 8; i++)
		if (trx->l1l[i])
			l1ctl_tx_bts_mode(trx->l1l[i], 0, trx->type, 0, 0, 0, 0, 0, trx->handover);

	return _trx_ctrl_send_resp(trx, cmd, 0, NULL);
}

static int
_trx_ctrl_cmd_poweron(struct trx *trx, const char *cmd, const char *args)
{
	int rv = 0;
	int i;

	if (trx->bsic == BSIC_INVAL || trx->arfcn == ARFCN_INVAL) {
		LOGP(DTRX, LOGL_ERROR,
			"TRX received POWERON when not fully configured\n");
		rv = -EINVAL;
	} else {
		trx->power = 1;
		memset(trx->handover, 0, sizeof(trx->handover));
		for (i = 0; i < 8; i++)
			if (trx->l1l[i])
				l1ctl_tx_bts_mode(trx->l1l[i], 1, trx->type, trx->bsic, trx->arfcn, trx->gain, trx->l1l[i]->tx_mask, trx->l1l[i]->rx_mask, trx->handover);
	}

	return _trx_ctrl_send_resp(trx, cmd, rv, NULL);
}

static int
_trx_ctrl_cmd_settsc(struct trx *trx, const char *cmd, const char *args)
{
	LOGP(DTRX, LOGL_ERROR,
		"TRX received SETTSC command ! "
		"OpenBTS should be configured to send SETBSIC command !\n");

	return _trx_ctrl_send_resp(trx, cmd, -1, "%s", args);
}

static int
_trx_ctrl_cmd_setbsic(struct trx *trx, const char *cmd, const char *args)
{
	int bsic = atoi(args);

	if (bsic >= 64) {
		LOGP(DTRX, LOGL_ERROR, "Invalid BSIC received\n");
		return _trx_ctrl_send_resp(trx, cmd, -1, "%d", bsic);
	}

	trx->bsic = bsic;

	return _trx_ctrl_send_resp(trx, cmd, 0, "%d", bsic);
}

static int
_trx_ctrl_cmd_setrxgain(struct trx *trx, const char *cmd, const char *args)
{
	int db = atoi(args);
	int i;

	if (db < 0 || db > 63) {
		LOGP(DTRX, LOGL_ERROR, "Invalid gain received\n");
		return _trx_ctrl_send_resp(trx, cmd, -1, "%d", db);
	}

	trx->gain = db;

	if (trx->power) {
		for (i = 0; i < 8; i++)
			if (trx->l1l[i])
				l1ctl_tx_bts_mode(trx->l1l[i], 1, trx->type, trx->bsic, trx->arfcn, trx->gain, trx->l1l[i]->tx_mask, trx->l1l[i]->rx_mask, trx->handover);
	}

	return _trx_ctrl_send_resp(trx, cmd, 0, "%d", db);
}

static int
_trx_ctrl_cmd_setpower(struct trx *trx, const char *cmd, const char *args)
{
	int db = atoi(args);

	return _trx_ctrl_send_resp(trx, cmd, 0, "%d", db);
}

static int
_trx_ctrl_cmd_setmaxdly(struct trx *trx, const char *cmd, const char *args)
{
	int dly = atoi(args);

	return _trx_ctrl_send_resp(trx, cmd, 0, "%d", dly);
}

static int
_trx_ctrl_cmd_setslot(struct trx *trx, const char *cmd, const char *args)
{
	int n, tn, type;

	n = sscanf(args, "%d %d", &tn, &type);

	if ((n != 2) || (tn < 0) || (tn > 7) || (type < 0) || ((type > 8) && (type != 13)))
		return _trx_ctrl_send_resp(trx, cmd, -1, "%d %d", tn, type);

	trx->type[tn] = type;

	if (trx->l1l[tn])
		l1ctl_tx_bts_mode(trx->l1l[tn], 1, trx->type, trx->bsic, trx->arfcn, trx->gain, trx->l1l[tn]->tx_mask, trx->l1l[tn]->rx_mask, trx->handover);

	return _trx_ctrl_send_resp(trx, cmd, 0, "%d %d", tn, type);
}

static int
_trx_ctrl_cmd_rxtune(struct trx *trx, const char *cmd, const char *args)
{
	int freq_khz = atoi(args);
	uint16_t arfcn;
	int rv = 0;

	arfcn = gsm_freq102arfcn(freq_khz / 100, 1);
	arfcn &= ~ARFCN_UPLINK;

	if ( arfcn == ARFCN_INVAL ||
	    (trx->arfcn != ARFCN_INVAL && trx->arfcn != arfcn)) {
		LOGP(DTRX, LOGL_ERROR, "RXTUNE called with invalid/inconsistent frequency\n");
		rv = -1;
		goto done;
	}

	if (trx->arfcn == ARFCN_INVAL) {
		LOGP(DTRX, LOGL_NOTICE, "Setting C0 ARFCN to %d (%s)\n",
			arfcn & ~ARFCN_FLAG_MASK, gsm_band_name(gsm_arfcn2band(arfcn)));
		trx->arfcn = arfcn;
	}

done:
	return _trx_ctrl_send_resp(trx, cmd, rv, "%d", freq_khz);
}

static int
_trx_ctrl_cmd_txtune(struct trx *trx, const char *cmd, const char *args)
{
	int freq_khz = atoi(args);
	uint16_t arfcn;
	int rv = 0;

	arfcn = gsm_freq102arfcn(freq_khz / 100, 0);

	if ( arfcn == ARFCN_INVAL ||
	    (trx->arfcn != ARFCN_INVAL && trx->arfcn != arfcn)) {
		LOGP(DTRX, LOGL_ERROR, "TXTUNE called with invalid/inconsistent frequency\n");
		rv = -1;
		goto done;
	}

	if (trx->arfcn == ARFCN_INVAL) {
		LOGP(DTRX, LOGL_NOTICE, "Setting C0 ARFCN to %d (%s)\n",
			arfcn & ~ARFCN_FLAG_MASK, gsm_band_name(gsm_arfcn2band(arfcn)));
		trx->arfcn = arfcn;
	}

done:
	return _trx_ctrl_send_resp(trx, cmd, rv, "%d", freq_khz);
}

static int
_trx_ctrl_cmd_handover(struct trx *trx, const char *cmd, const char *args)
{
	int n, tn, ss = 0;

	n = sscanf(args, "%d %d", &tn, &ss);

	if ((n < 1) || (tn < 0) || (tn > 7) || (ss < 0) || ((ss > 8)))
		return _trx_ctrl_send_resp(trx, cmd, -1, "%d %d", tn, ss);

	trx->handover[tn] |= (1 << ss);

	if (trx->l1l[tn])
		l1ctl_tx_bts_mode(trx->l1l[tn], 1, trx->type, trx->bsic, trx->arfcn, trx->gain, trx->l1l[tn]->tx_mask, trx->l1l[tn]->rx_mask, trx->handover);

	return _trx_ctrl_send_resp(trx, cmd, 0, "%d %d", tn, ss);
}

static int
_trx_ctrl_cmd_nohandover(struct trx *trx, const char *cmd, const char *args)
{
	int n, tn, ss = 0;

	n = sscanf(args, "%d %d", &tn, &ss);

	if ((n < 1) || (tn < 0) || (tn > 7) || (ss < 0) || ((ss > 8)))
		return _trx_ctrl_send_resp(trx, cmd, -1, "%d %d", tn, ss);

	trx->handover[tn] &= ~(1 << ss);

	if (trx->l1l[tn])
		l1ctl_tx_bts_mode(trx->l1l[tn], 1, trx->type, trx->bsic, trx->arfcn, trx->gain, trx->l1l[tn]->tx_mask, trx->l1l[tn]->rx_mask, trx->handover);

	return _trx_ctrl_send_resp(trx, cmd, 0, "%d %d", tn, ss);
}

struct trx_cmd_handler {
	const char *cmd;
	int (*handler)(struct trx *trx, const char *cmd, const char *args);
};

static const struct trx_cmd_handler trx_handlers[] = {
	{ "POWEROFF",	_trx_ctrl_cmd_poweroff },
	{ "POWERON",	_trx_ctrl_cmd_poweron },
	{ "SETTSC",	_trx_ctrl_cmd_settsc },
	{ "SETBSIC",	_trx_ctrl_cmd_setbsic },
	{ "SETPOWER",	_trx_ctrl_cmd_setpower },
	{ "SETRXGAIN",	_trx_ctrl_cmd_setrxgain },
	{ "SETMAXDLY",	_trx_ctrl_cmd_setmaxdly },
	{ "SETSLOT",	_trx_ctrl_cmd_setslot },
	{ "RXTUNE",	_trx_ctrl_cmd_rxtune },
	{ "TXTUNE",	_trx_ctrl_cmd_txtune },
	{ "HANDOVER",	_trx_ctrl_cmd_handover },
	{ "NOHANDOVER",	_trx_ctrl_cmd_nohandover },
	{ NULL, NULL }
};

static int
_trx_ctrl_read_cb(struct osmo_fd *ofd, unsigned int what)
{
	struct trx *trx = ofd->data;
	const struct trx_cmd_handler *ch;
	char buf[TRX_CMD_BUF_LEN];
	char *cmd, *args;
	ssize_t l;
	int rv;

	/* Get message */
	l = recv(ofd->fd, buf, sizeof(buf)-1, 0);
	if (l <= 0) {
		/* FIXME handle exception ... */
		return l;
	}

	/* Check 'CMD ' */
	if (strncmp(buf, "CMD ", 4))
		goto inval;

	/* Check length */
	if (buf[l-1] == '\0')
		l--;
	else
		buf[l] = '\0';	/* Safety */
	if (strlen(buf) != l)
		goto inval;

	/* Split command name and arguments */
	cmd = &buf[4];

	args = strchr(cmd, ' ');
	if (!args)
		args = &buf[l];
	else
		*args++ = '\0';

	LOGP(DTRX, LOGL_DEBUG, "TRX Control recv: |%s|%s|\n", cmd, args);

	/* Find handler */
	for (ch=trx_handlers; ch->cmd; ch++) {
		if (!strcmp(ch->cmd, cmd)) {
			rv = ch->handler(trx, cmd, args);
			if (rv)
				LOGP(DTRX, LOGL_ERROR, "[!] Processing failure for command '%s'\n", cmd);
			break;
		}
	}

	if (!ch->cmd) {
		LOGP(DTRX, LOGL_ERROR, "[!] No handlers found for command '%s'. Empty response\n", cmd);
		/* Mimic behaviour of OsmoTRX on receipt of an unknown command */
		_trx_ctrl_send_resp(trx, "ERR", 1, NULL);
	}

	/* Done ! */
	return l;

inval:
	LOGP(DTRX, LOGL_ERROR, "[!] Invalid command '%s' on CTRL interface, discarding\n", buf);
	return l;
}


/* ------------------------------------------------------------------------ */
/* Data interface                                                           */
/* ------------------------------------------------------------------------ */

#define TRX_DATA_BUF_LEN     256

static const ubit_t dummy_burst[] = {
	0,0,0,
	1,1,1,1,1,0,1,1,0,1,1,1,0,1,1,0,0,0,0,0,1,0,1,0,0,1,0,0,1,1,1,0,
	0,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,1,1,1,0,0,
	0,1,0,1,1,1,0,0,0,1,0,1,1,1,0,0,0,1,0,1,0,1,1,1,0,1,0,0,1,0,1,0,
	0,0,1,1,0,0,1,1,0,0,1,1,1,0,0,1,1,1,1,0,1,0,0,1,1,1,1,1,0,0,0,1,
	0,0,1,0,1,1,1,1,1,0,1,0,1,0,
	0,0,0,
};

static int
_trx_data_read_cb(struct osmo_fd *ofd, unsigned int what)
{
	struct trx *trx = ofd->data;
	uint8_t buf[TRX_DATA_BUF_LEN];
	uint32_t fn;
	int l, tn, pwr_att;
	ubit_t *data;
	struct burst_data _burst, *burst = &_burst;

	/* Get message */
	l = recv(ofd->fd, buf, sizeof(buf)-1, 0);
	if (l <= 0) {
		/* FIXME handle exception ... */
		return l;
	}

	/* Validate */
	if (l != 1+4+1+148)
		goto inval;

	/* Parse */
	tn = buf[0];
	fn = (buf[1] << 24) | (buf[2] << 16) | (buf[3] << 8) | buf[4];
	pwr_att = buf[5];
	data = buf+6;

	/* Ignore unallocated time slots */
	if (!trx->l1l[tn])
		goto skip;

	/* Ignore FCCH and SCH completely, they're handled internally */
	if ((trx->type[tn] >> 1) == 2 && ((fn % 51) % 10) < 2)
		goto skip;

	/* Detect dummy bursts */
	if (!memcmp(data, dummy_burst, sizeof(dummy_burst))) {
		LOGP(DTRX, LOGL_DEBUG, "TRX Data %u:%d:%d:DUMMY\n",
			fn, tn, pwr_att);
		goto skip;
	}

	/* Pack */
	burst->type = BURST_NB;
	burst->data[14] = 0x00;
	osmo_ubit2pbit_ext(burst->data,  0, data,  3, 58, 0);
	osmo_ubit2pbit_ext(burst->data, 58, data, 87, 58, 0);

	/* Send to L1 */
	l1ctl_tx_bts_burst_req(trx->l1l[tn], fn, tn, burst);

	/* Debug */
	LOGP(DTRX, LOGL_DEBUG, "TRX Data %u:%d:%d:%s\n",
		fn, tn, pwr_att, osmo_hexdump_nospc(burst->data, 15));

	/* Done ! */
skip:
	return l;

inval:
	LOGP(DTRX, LOGL_ERROR, "[!] Invalid data burst on DATA interface, discarding\n");
	return l;
}

int
trx_data_ind(struct trx *trx, uint32_t fn, uint8_t tn, sbit_t *data, float toa, int8_t rssi)
{
	char buf[158];
	short toa_int = (short)(toa * 256.0f);
	int i;

	LOGP(DTRX, LOGL_DEBUG, "TRX Data Indication (fn=%d, tn=%d, toa=%.2f)\n", fn, tn, toa);

	buf[0] = tn;

	buf[1] = (fn >> 24) & 0xff;
	buf[2] = (fn >> 16) & 0xff;
	buf[3] = (fn >>  8) & 0xff;
	buf[4] = (fn >>  0) & 0xff;

	/* RSSI */
	buf[5] = -rssi;

	/* TOA */
	buf[6] = (toa_int >> 8) & 0xff;
	buf[7] = (toa_int >> 0) & 0xff;

	/* Data bits */
	for (i=0; i<148; i++)
		buf[8+i] = (unsigned char)(127 - ((int)data[i]));

	/* End char ? */
	buf[9+148] = 0x00;

	/* Send result */
	send(trx->ofd_data.fd, buf, 158, 0);

	return 0;
}
