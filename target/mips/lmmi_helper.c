/*
 *  Loongson Multimedia Instruction emulation helpers for QEMU.
 *
 *  Copyright (c) 2011  Richard Henderson <rth@twiddle.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"

/*
 * If the byte ordering doesn't matter, i.e. all columns are treated
 * identically, then this union can be used directly.  If byte ordering
 * does matter, we generally ignore dumping to memory.
 */
typedef union {
    uint8_t  ub[8];
    int8_t   sb[8];
    uint16_t uh[4];
    int16_t  sh[4];
    uint32_t uw[2];
    int32_t  sw[2];
    uint64_t d;
} LMIValue;

/* Some byte ordering issues can be mitigated by XORing in the following.  */
#ifdef HOST_WORDS_BIGENDIAN
# define BYTE_ORDER_XOR(N) N
#else
# define BYTE_ORDER_XOR(N) 0
#endif

#define SATSB(x)  (x < -0x80 ? -0x80 : x > 0x7f ? 0x7f : x)
#define SATUB(x)  (x > 0xff ? 0xff : x)

#define SATSH(x)  (x < -0x8000 ? -0x8000 : x > 0x7fff ? 0x7fff : x)
#define SATUH(x)  (x > 0xffff ? 0xffff : x)

#define SATSW(x) \
    (x < -0x80000000ll ? -0x80000000ll : x > 0x7fffffff ? 0x7fffffff : x)
#define SATUW(x)  (x > 0xffffffffull ? 0xffffffffull : x)

uint64_t helper_paddsb(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 8; ++i) {
        int r = vs.sb[i] + vt.sb[i];
        vs.sb[i] = SATSB(r);
    }
    return vs.d;
}

uint64_t helper_paddusb(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 8; ++i) {
        int r = vs.ub[i] + vt.ub[i];
        vs.ub[i] = SATUB(r);
    }
    return vs.d;
}

uint64_t helper_paddsh(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; ++i) {
        int r = vs.sh[i] + vt.sh[i];
        vs.sh[i] = SATSH(r);
    }
    return vs.d;
}

uint64_t helper_paddush(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; ++i) {
        int r = vs.uh[i] + vt.uh[i];
        vs.uh[i] = SATUH(r);
    }
    return vs.d;
}

uint64_t helper_paddb(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 8; ++i) {
        vs.ub[i] += vt.ub[i];
    }
    return vs.d;
}

uint64_t helper_paddh(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; ++i) {
        vs.uh[i] += vt.uh[i];
    }
    return vs.d;
}

uint64_t helper_paddw(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 2; ++i) {
        vs.uw[i] += vt.uw[i];
    }
    return vs.d;
}

uint64_t helper_psubsb(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 8; ++i) {
        int r = vs.sb[i] - vt.sb[i];
        vs.sb[i] = SATSB(r);
    }
    return vs.d;
}

uint64_t helper_psubusb(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 8; ++i) {
        int r = vs.ub[i] - vt.ub[i];
        vs.ub[i] = SATUB(r);
    }
    return vs.d;
}

uint64_t helper_psubsh(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; ++i) {
        int r = vs.sh[i] - vt.sh[i];
        vs.sh[i] = SATSH(r);
    }
    return vs.d;
}

uint64_t helper_psubush(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; ++i) {
        int r = vs.uh[i] - vt.uh[i];
        vs.uh[i] = SATUH(r);
    }
    return vs.d;
}

uint64_t helper_psubb(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 8; ++i) {
        vs.ub[i] -= vt.ub[i];
    }
    return vs.d;
}

uint64_t helper_psubh(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; ++i) {
        vs.uh[i] -= vt.uh[i];
    }
    return vs.d;
}

uint64_t helper_psubw(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned int i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 2; ++i) {
        vs.uw[i] -= vt.uw[i];
    }
    return vs.d;
}

uint64_t helper_pshufh(uint64_t fs, uint64_t ft)
{
    unsigned host = BYTE_ORDER_XOR(3);
    LMIValue vd, vs;
    unsigned i;

    vs.d = fs;
    vd.d = 0;
    for (i = 0; i < 4; i++, ft >>= 2) {
        vd.uh[i ^ host] = vs.uh[(ft & 3) ^ host];
    }
    return vd.d;
}

uint64_t helper_packsswh(uint64_t fs, uint64_t ft)
{
    uint64_t fd = 0;
    int64_t tmp;

    tmp = (int32_t)(fs >> 0);
    tmp = SATSH(tmp);
    fd |= (tmp & 0xffff) << 0;

    tmp = (int32_t)(fs >> 32);
    tmp = SATSH(tmp);
    fd |= (tmp & 0xffff) << 16;

    tmp = (int32_t)(ft >> 0);
    tmp = SATSH(tmp);
    fd |= (tmp & 0xffff) << 32;

    tmp = (int32_t)(ft >> 32);
    tmp = SATSH(tmp);
    fd |= (tmp & 0xffff) << 48;

    return fd;
}

uint64_t helper_packsshb(uint64_t fs, uint64_t ft)
{
    uint64_t fd = 0;
    unsigned int i;

    for (i = 0; i < 4; ++i) {
        int16_t tmp = fs >> (i * 16);
        tmp = SATSB(tmp);
        fd |= (uint64_t)(tmp & 0xff) << (i * 8);
    }
    for (i = 0; i < 4; ++i) {
        int16_t tmp = ft >> (i * 16);
        tmp = SATSB(tmp);
        fd |= (uint64_t)(tmp & 0xff) << (i * 8 + 32);
    }

    return fd;
}

uint64_t helper_packushb(uint64_t fs, uint64_t ft)
{
    uint64_t fd = 0;
    unsigned int i;

    for (i = 0; i < 4; ++i) {
        int16_t tmp = fs >> (i * 16);
        tmp = SATUB(tmp);
        fd |= (uint64_t)(tmp & 0xff) << (i * 8);
    }
    for (i = 0; i < 4; ++i) {
        int16_t tmp = ft >> (i * 16);
        tmp = SATUB(tmp);
        fd |= (uint64_t)(tmp & 0xff) << (i * 8 + 32);
    }

    return fd;
}

uint64_t helper_punpcklwd(uint64_t fs, uint64_t ft)
{
    return (fs & 0xffffffff) | (ft << 32);
}

uint64_t helper_punpckhwd(uint64_t fs, uint64_t ft)
{
    return (fs >> 32) | (ft & ~0xffffffffull);
}

uint64_t helper_punpcklhw(uint64_t fs, uint64_t ft)
{
    unsigned host = BYTE_ORDER_XOR(3);
    LMIValue vd, vs, vt;

    vs.d = fs;
    vt.d = ft;
    vd.uh[0 ^ host] = vs.uh[0 ^ host];
    vd.uh[1 ^ host] = vt.uh[0 ^ host];
    vd.uh[2 ^ host] = vs.uh[1 ^ host];
    vd.uh[3 ^ host] = vt.uh[1 ^ host];

    return vd.d;
}

uint64_t helper_punpckhhw(uint64_t fs, uint64_t ft)
{
    unsigned host = BYTE_ORDER_XOR(3);
    LMIValue vd, vs, vt;

    vs.d = fs;
    vt.d = ft;
    vd.uh[0 ^ host] = vs.uh[2 ^ host];
    vd.uh[1 ^ host] = vt.uh[2 ^ host];
    vd.uh[2 ^ host] = vs.uh[3 ^ host];
    vd.uh[3 ^ host] = vt.uh[3 ^ host];

    return vd.d;
}

uint64_t helper_punpcklbh(uint64_t fs, uint64_t ft)
{
    unsigned host = BYTE_ORDER_XOR(7);
    LMIValue vd, vs, vt;

    vs.d = fs;
    vt.d = ft;
    vd.ub[0 ^ host] = vs.ub[0 ^ host];
    vd.ub[1 ^ host] = vt.ub[0 ^ host];
    vd.ub[2 ^ host] = vs.ub[1 ^ host];
    vd.ub[3 ^ host] = vt.ub[1 ^ host];
    vd.ub[4 ^ host] = vs.ub[2 ^ host];
    vd.ub[5 ^ host] = vt.ub[2 ^ host];
    vd.ub[6 ^ host] = vs.ub[3 ^ host];
    vd.ub[7 ^ host] = vt.ub[3 ^ host];

    return vd.d;
}

uint64_t helper_punpckhbh(uint64_t fs, uint64_t ft)
{
    unsigned host = BYTE_ORDER_XOR(7);
    LMIValue vd, vs, vt;

    vs.d = fs;
    vt.d = ft;
    vd.ub[0 ^ host] = vs.ub[4 ^ host];
    vd.ub[1 ^ host] = vt.ub[4 ^ host];
    vd.ub[2 ^ host] = vs.ub[5 ^ host];
    vd.ub[3 ^ host] = vt.ub[5 ^ host];
    vd.ub[4 ^ host] = vs.ub[6 ^ host];
    vd.ub[5 ^ host] = vt.ub[6 ^ host];
    vd.ub[6 ^ host] = vs.ub[7 ^ host];
    vd.ub[7 ^ host] = vt.ub[7 ^ host];

    return vd.d;
}

uint64_t helper_pavgh(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; i++) {
        vs.uh[i] = (vs.uh[i] + vt.uh[i] + 1) >> 1;
    }
    return vs.d;
}

uint64_t helper_pavgb(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 8; i++) {
        vs.ub[i] = (vs.ub[i] + vt.ub[i] + 1) >> 1;
    }
    return vs.d;
}

uint64_t helper_pmaxsh(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; i++) {
        vs.sh[i] = (vs.sh[i] >= vt.sh[i] ? vs.sh[i] : vt.sh[i]);
    }
    return vs.d;
}

uint64_t helper_pminsh(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; i++) {
        vs.sh[i] = (vs.sh[i] <= vt.sh[i] ? vs.sh[i] : vt.sh[i]);
    }
    return vs.d;
}

uint64_t helper_pmaxub(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; i++) {
        vs.ub[i] = (vs.ub[i] >= vt.ub[i] ? vs.ub[i] : vt.ub[i]);
    }
    return vs.d;
}

uint64_t helper_pminub(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; i++) {
        vs.ub[i] = (vs.ub[i] <= vt.ub[i] ? vs.ub[i] : vt.ub[i]);
    }
    return vs.d;
}

uint64_t helper_pcmpeqw(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 2; i++) {
        vs.uw[i] = -(vs.uw[i] == vt.uw[i]);
    }
    return vs.d;
}

uint64_t helper_pcmpgtw(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 2; i++) {
        vs.uw[i] = -(vs.uw[i] > vt.uw[i]);
    }
    return vs.d;
}

uint64_t helper_pcmpeqh(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; i++) {
        vs.uh[i] = -(vs.uh[i] == vt.uh[i]);
    }
    return vs.d;
}

uint64_t helper_pcmpgth(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; i++) {
        vs.uh[i] = -(vs.uh[i] > vt.uh[i]);
    }
    return vs.d;
}

uint64_t helper_pcmpeqb(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 8; i++) {
        vs.ub[i] = -(vs.ub[i] == vt.ub[i]);
    }
    return vs.d;
}

uint64_t helper_pcmpgtb(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 8; i++) {
        vs.ub[i] = -(vs.ub[i] > vt.ub[i]);
    }
    return vs.d;
}

uint64_t helper_psllw(uint64_t fs, uint64_t ft)
{
    LMIValue vs;
    unsigned i;

    ft &= 0x7f;
    if (ft > 31) {
        return 0;
    }
    vs.d = fs;
    for (i = 0; i < 2; ++i) {
        vs.uw[i] <<= ft;
    }
    return vs.d;
}

uint64_t helper_psrlw(uint64_t fs, uint64_t ft)
{
    LMIValue vs;
    unsigned i;

    ft &= 0x7f;
    if (ft > 31) {
        return 0;
    }
    vs.d = fs;
    for (i = 0; i < 2; ++i) {
        vs.uw[i] >>= ft;
    }
    return vs.d;
}

uint64_t helper_psraw(uint64_t fs, uint64_t ft)
{
    LMIValue vs;
    unsigned i;

    ft &= 0x7f;
    if (ft > 31) {
        ft = 31;
    }
    vs.d = fs;
    for (i = 0; i < 2; ++i) {
        vs.sw[i] >>= ft;
    }
    return vs.d;
}

uint64_t helper_psllh(uint64_t fs, uint64_t ft)
{
    LMIValue vs;
    unsigned i;

    ft &= 0x7f;
    if (ft > 15) {
        return 0;
    }
    vs.d = fs;
    for (i = 0; i < 4; ++i) {
        vs.uh[i] <<= ft;
    }
    return vs.d;
}

uint64_t helper_psrlh(uint64_t fs, uint64_t ft)
{
    LMIValue vs;
    unsigned i;

    ft &= 0x7f;
    if (ft > 15) {
        return 0;
    }
    vs.d = fs;
    for (i = 0; i < 4; ++i) {
        vs.uh[i] >>= ft;
    }
    return vs.d;
}

uint64_t helper_psrah(uint64_t fs, uint64_t ft)
{
    LMIValue vs;
    unsigned i;

    ft &= 0x7f;
    if (ft > 15) {
        ft = 15;
    }
    vs.d = fs;
    for (i = 0; i < 4; ++i) {
        vs.sh[i] >>= ft;
    }
    return vs.d;
}

uint64_t helper_pmullh(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; ++i) {
        vs.sh[i] *= vt.sh[i];
    }
    return vs.d;
}

uint64_t helper_pmulhh(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; ++i) {
        int32_t r = vs.sh[i] * vt.sh[i];
        vs.sh[i] = r >> 16;
    }
    return vs.d;
}

uint64_t helper_pmulhuh(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 4; ++i) {
        uint32_t r = vs.uh[i] * vt.uh[i];
        vs.uh[i] = r >> 16;
    }
    return vs.d;
}

uint64_t helper_pmaddhw(uint64_t fs, uint64_t ft)
{
    unsigned host = BYTE_ORDER_XOR(3);
    LMIValue vs, vt;
    uint32_t p0, p1;

    vs.d = fs;
    vt.d = ft;
    p0  = vs.sh[0 ^ host] * vt.sh[0 ^ host];
    p0 += vs.sh[1 ^ host] * vt.sh[1 ^ host];
    p1  = vs.sh[2 ^ host] * vt.sh[2 ^ host];
    p1 += vs.sh[3 ^ host] * vt.sh[3 ^ host];

    return ((uint64_t)p1 << 32) | p0;
}

uint64_t helper_pasubub(uint64_t fs, uint64_t ft)
{
    LMIValue vs, vt;
    unsigned i;

    vs.d = fs;
    vt.d = ft;
    for (i = 0; i < 8; ++i) {
        int r = vs.ub[i] - vt.ub[i];
        vs.ub[i] = (r < 0 ? -r : r);
    }
    return vs.d;
}

uint64_t helper_biadd(uint64_t fs)
{
    unsigned i, fd;

    for (i = fd = 0; i < 8; ++i) {
        fd += (fs >> (i * 8)) & 0xff;
    }
    return fd & 0xffff;
}

uint64_t helper_pmovmskb(uint64_t fs)
{
    unsigned fd = 0;

    fd |= ((fs >>  7) & 1) << 0;
    fd |= ((fs >> 15) & 1) << 1;
    fd |= ((fs >> 23) & 1) << 2;
    fd |= ((fs >> 31) & 1) << 3;
    fd |= ((fs >> 39) & 1) << 4;
    fd |= ((fs >> 47) & 1) << 5;
    fd |= ((fs >> 55) & 1) << 6;
    fd |= ((fs >> 63) & 1) << 7;

    return fd & 0xff;
}
