/* SPDX-License-Identifier: MIT */
/*
 * libslirp
 *
 * Copyright (c) 2004-2008 Fabrice Bellard
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
 */
#include "slirp.h"
#include "vmstate.h"
#include "stream.h"

static int slirp_tcp_post_load(void *opaque, int version)
{
    tcp_template((struct tcpcb *)opaque);

    return 0;
}

static const VMStateDescription vmstate_slirp_tcp = {
    .name = "slirp-tcp",
    .version_id = 0,
    .post_load = slirp_tcp_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_INT16(t_state, struct tcpcb),
        VMSTATE_INT16_ARRAY(t_timer, struct tcpcb, TCPT_NTIMERS),
        VMSTATE_INT16(t_rxtshift, struct tcpcb),
        VMSTATE_INT16(t_rxtcur, struct tcpcb),
        VMSTATE_INT16(t_dupacks, struct tcpcb),
        VMSTATE_UINT16(t_maxseg, struct tcpcb),
        VMSTATE_UINT8(t_force, struct tcpcb),
        VMSTATE_UINT16(t_flags, struct tcpcb),
        VMSTATE_UINT32(snd_una, struct tcpcb),
        VMSTATE_UINT32(snd_nxt, struct tcpcb),
        VMSTATE_UINT32(snd_up, struct tcpcb),
        VMSTATE_UINT32(snd_wl1, struct tcpcb),
        VMSTATE_UINT32(snd_wl2, struct tcpcb),
        VMSTATE_UINT32(iss, struct tcpcb),
        VMSTATE_UINT32(snd_wnd, struct tcpcb),
        VMSTATE_UINT32(rcv_wnd, struct tcpcb),
        VMSTATE_UINT32(rcv_nxt, struct tcpcb),
        VMSTATE_UINT32(rcv_up, struct tcpcb),
        VMSTATE_UINT32(irs, struct tcpcb),
        VMSTATE_UINT32(rcv_adv, struct tcpcb),
        VMSTATE_UINT32(snd_max, struct tcpcb),
        VMSTATE_UINT32(snd_cwnd, struct tcpcb),
        VMSTATE_UINT32(snd_ssthresh, struct tcpcb),
        VMSTATE_INT16(t_idle, struct tcpcb),
        VMSTATE_INT16(t_rtt, struct tcpcb),
        VMSTATE_UINT32(t_rtseq, struct tcpcb),
        VMSTATE_INT16(t_srtt, struct tcpcb),
        VMSTATE_INT16(t_rttvar, struct tcpcb),
        VMSTATE_UINT16(t_rttmin, struct tcpcb),
        VMSTATE_UINT32(max_sndwnd, struct tcpcb),
        VMSTATE_UINT8(t_oobflags, struct tcpcb),
        VMSTATE_UINT8(t_iobc, struct tcpcb),
        VMSTATE_INT16(t_softerror, struct tcpcb),
        VMSTATE_UINT8(snd_scale, struct tcpcb),
        VMSTATE_UINT8(rcv_scale, struct tcpcb),
        VMSTATE_UINT8(request_r_scale, struct tcpcb),
        VMSTATE_UINT8(requested_s_scale, struct tcpcb),
        VMSTATE_UINT32(ts_recent, struct tcpcb),
        VMSTATE_UINT32(ts_recent_age, struct tcpcb),
        VMSTATE_UINT32(last_ack_sent, struct tcpcb),
        VMSTATE_END_OF_LIST()
    }
};

/* The sbuf has a pair of pointers that are migrated as offsets;
 * we calculate the offsets and restore the pointers using
 * pre_save/post_load on a tmp structure.
 */
struct sbuf_tmp {
    struct sbuf *parent;
    uint32_t roff, woff;
};

static int sbuf_tmp_pre_save(void *opaque)
{
    struct sbuf_tmp *tmp = opaque;
    tmp->woff = tmp->parent->sb_wptr - tmp->parent->sb_data;
    tmp->roff = tmp->parent->sb_rptr - tmp->parent->sb_data;

    return 0;
}

static int sbuf_tmp_post_load(void *opaque, int version)
{
    struct sbuf_tmp *tmp = opaque;
    uint32_t requested_len = tmp->parent->sb_datalen;

    /* Allocate the buffer space used by the field after the tmp */
    sbreserve(tmp->parent, tmp->parent->sb_datalen);

    if (tmp->parent->sb_datalen != requested_len) {
        return -ENOMEM;
    }
    if (tmp->woff >= requested_len ||
        tmp->roff >= requested_len) {
        g_critical("invalid sbuf offsets r/w=%u/%u len=%u",
                   tmp->roff, tmp->woff, requested_len);
        return -EINVAL;
    }

    tmp->parent->sb_wptr = tmp->parent->sb_data + tmp->woff;
    tmp->parent->sb_rptr = tmp->parent->sb_data + tmp->roff;

    return 0;
}


static const VMStateDescription vmstate_slirp_sbuf_tmp = {
    .name = "slirp-sbuf-tmp",
    .post_load = sbuf_tmp_post_load,
    .pre_save  = sbuf_tmp_pre_save,
    .version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(woff, struct sbuf_tmp),
        VMSTATE_UINT32(roff, struct sbuf_tmp),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_slirp_sbuf = {
    .name = "slirp-sbuf",
    .version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(sb_cc, struct sbuf),
        VMSTATE_UINT32(sb_datalen, struct sbuf),
        VMSTATE_WITH_TMP(struct sbuf, struct sbuf_tmp, vmstate_slirp_sbuf_tmp),
        VMSTATE_VBUFFER_UINT32(sb_data, struct sbuf, 0, NULL, sb_datalen),
        VMSTATE_END_OF_LIST()
    }
};

static bool slirp_older_than_v4(void *opaque, int version_id)
{
    return version_id < 4;
}

static bool slirp_family_inet(void *opaque, int version_id)
{
    union slirp_sockaddr *ssa = (union slirp_sockaddr *)opaque;
    return ssa->ss.ss_family == AF_INET;
}

static int slirp_socket_pre_load(void *opaque)
{
    struct socket *so = opaque;
    if (tcp_attach(so) < 0) {
        return -ENOMEM;
    }
    /* Older versions don't load these fields */
    so->so_ffamily = AF_INET;
    so->so_lfamily = AF_INET;
    return 0;
}

#ifndef _WIN32
#define VMSTATE_SIN4_ADDR(f, s, t) VMSTATE_UINT32_TEST(f, s, t)
#else
/* Win uses u_long rather than uint32_t - but it's still 32bits long */
#define VMSTATE_SIN4_ADDR(f, s, t) VMSTATE_SINGLE_TEST(f, s, t, 0, \
                                       slirp_vmstate_info_uint32, u_long)
#endif

/* The OS provided ss_family field isn't that portable; it's size
 * and type varies (16/8 bit, signed, unsigned)
 * and the values it contains aren't fully portable.
 */
typedef struct SS_FamilyTmpStruct {
    union slirp_sockaddr    *parent;
    uint16_t                 portable_family;
} SS_FamilyTmpStruct;

#define SS_FAMILY_MIG_IPV4   2  /* Linux, BSD, Win... */
#define SS_FAMILY_MIG_IPV6  10  /* Linux */
#define SS_FAMILY_MIG_OTHER 0xffff

static int ss_family_pre_save(void *opaque)
{
    SS_FamilyTmpStruct *tss = opaque;

    tss->portable_family = SS_FAMILY_MIG_OTHER;

    if (tss->parent->ss.ss_family == AF_INET) {
        tss->portable_family = SS_FAMILY_MIG_IPV4;
    } else if (tss->parent->ss.ss_family == AF_INET6) {
        tss->portable_family = SS_FAMILY_MIG_IPV6;
    }

    return 0;
}

static int ss_family_post_load(void *opaque, int version_id)
{
    SS_FamilyTmpStruct *tss = opaque;

    switch (tss->portable_family) {
    case SS_FAMILY_MIG_IPV4:
        tss->parent->ss.ss_family = AF_INET;
        break;
    case SS_FAMILY_MIG_IPV6:
    case 23: /* compatibility: AF_INET6 from mingw */
    case 28: /* compatibility: AF_INET6 from FreeBSD sys/socket.h */
        tss->parent->ss.ss_family = AF_INET6;
        break;
    default:
        g_critical("invalid ss_family type %x", tss->portable_family);
        return -EINVAL;
    }

    return 0;
}

static const VMStateDescription vmstate_slirp_ss_family = {
    .name = "slirp-socket-addr/ss_family",
    .pre_save  = ss_family_pre_save,
    .post_load = ss_family_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(portable_family, SS_FamilyTmpStruct),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_slirp_socket_addr = {
    .name = "slirp-socket-addr",
    .version_id = 4,
    .fields = (VMStateField[]) {
        VMSTATE_WITH_TMP(union slirp_sockaddr, SS_FamilyTmpStruct,
                            vmstate_slirp_ss_family),
        VMSTATE_SIN4_ADDR(sin.sin_addr.s_addr, union slirp_sockaddr,
                            slirp_family_inet),
        VMSTATE_UINT16_TEST(sin.sin_port, union slirp_sockaddr,
                            slirp_family_inet),

#if 0
        /* Untested: Needs checking by someone with IPv6 test */
        VMSTATE_BUFFER_TEST(sin6.sin6_addr, union slirp_sockaddr,
                            slirp_family_inet6),
        VMSTATE_UINT16_TEST(sin6.sin6_port, union slirp_sockaddr,
                            slirp_family_inet6),
        VMSTATE_UINT32_TEST(sin6.sin6_flowinfo, union slirp_sockaddr,
                            slirp_family_inet6),
        VMSTATE_UINT32_TEST(sin6.sin6_scope_id, union slirp_sockaddr,
                            slirp_family_inet6),
#endif

        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_slirp_socket = {
    .name = "slirp-socket",
    .version_id = 4,
    .pre_load = slirp_socket_pre_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(so_urgc, struct socket),
        /* Pre-v4 versions */
        VMSTATE_SIN4_ADDR(so_faddr.s_addr, struct socket,
                            slirp_older_than_v4),
        VMSTATE_SIN4_ADDR(so_laddr.s_addr, struct socket,
                            slirp_older_than_v4),
        VMSTATE_UINT16_TEST(so_fport, struct socket, slirp_older_than_v4),
        VMSTATE_UINT16_TEST(so_lport, struct socket, slirp_older_than_v4),
        /* v4 and newer */
        VMSTATE_STRUCT(fhost, struct socket, 4, vmstate_slirp_socket_addr,
                       union slirp_sockaddr),
        VMSTATE_STRUCT(lhost, struct socket, 4, vmstate_slirp_socket_addr,
                       union slirp_sockaddr),

        VMSTATE_UINT8(so_iptos, struct socket),
        VMSTATE_UINT8(so_emu, struct socket),
        VMSTATE_UINT8(so_type, struct socket),
        VMSTATE_INT32(so_state, struct socket),
        VMSTATE_STRUCT(so_rcv, struct socket, 0, vmstate_slirp_sbuf,
                       struct sbuf),
        VMSTATE_STRUCT(so_snd, struct socket, 0, vmstate_slirp_sbuf,
                       struct sbuf),
        VMSTATE_STRUCT_POINTER(so_tcpcb, struct socket, vmstate_slirp_tcp,
                       struct tcpcb),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_slirp_bootp_client = {
    .name = "slirp_bootpclient",
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(allocated, BOOTPClient),
        VMSTATE_BUFFER(macaddr, BOOTPClient),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_slirp = {
    .name = "slirp",
    .version_id = 4,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16_V(ip_id, Slirp, 2),
        VMSTATE_STRUCT_ARRAY(bootp_clients, Slirp, NB_BOOTP_CLIENTS, 3,
                             vmstate_slirp_bootp_client, BOOTPClient),
        VMSTATE_END_OF_LIST()
    }
};

void slirp_state_save(Slirp *slirp, SlirpWriteCb write_cb, void *opaque)
{
    struct gfwd_list *ex_ptr;
    SlirpOStream f = {
        .write_cb = write_cb,
        .opaque = opaque,
    };

    for (ex_ptr = slirp->guestfwd_list; ex_ptr; ex_ptr = ex_ptr->ex_next)
        if (ex_ptr->write_cb) {
            struct socket *so;
            so = slirp_find_ctl_socket(slirp, ex_ptr->ex_addr,
                                       ntohs(ex_ptr->ex_fport));
            if (!so) {
                continue;
            }

            slirp_ostream_write_u8(&f, 42);
            slirp_vmstate_save_state(&f, &vmstate_slirp_socket, so);
        }
    slirp_ostream_write_u8(&f, 0);

    slirp_vmstate_save_state(&f, &vmstate_slirp, slirp);
}


int slirp_state_load(Slirp *slirp, int version_id,
                     SlirpReadCb read_cb, void *opaque)
{
    struct gfwd_list *ex_ptr;
    SlirpIStream f = {
        .read_cb = read_cb,
        .opaque = opaque,
    };

    while (slirp_istream_read_u8(&f)) {
        int ret;
        struct socket *so = socreate(slirp);

        ret = slirp_vmstate_load_state(&f, &vmstate_slirp_socket, so, version_id);
        if (ret < 0) {
            return ret;
        }

        if ((so->so_faddr.s_addr & slirp->vnetwork_mask.s_addr) !=
            slirp->vnetwork_addr.s_addr) {
            return -EINVAL;
        }
        for (ex_ptr = slirp->guestfwd_list; ex_ptr; ex_ptr = ex_ptr->ex_next) {
            if (ex_ptr->write_cb &&
                so->so_faddr.s_addr == ex_ptr->ex_addr.s_addr &&
                so->so_fport == ex_ptr->ex_fport) {
                break;
            }
        }
        if (!ex_ptr) {
            return -EINVAL;
        }
    }

    return slirp_vmstate_load_state(&f, &vmstate_slirp, slirp, version_id);
}

int slirp_state_version(void)
{
    return 4;
}
