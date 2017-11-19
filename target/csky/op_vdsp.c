/*
 *  CSKY helper routines
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "cpu.h"
#include "translate.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include <math.h>

void VDSP_HELPER(vadd64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt, i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] +
                                          env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] +
                                          env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] +
                                          env->vfp.reg[ry].dspi[i]);
        }
        break;
    }

}
void VDSP_HELPER(vadd128)(CPUCSKYState *env, uint32_t insn)
{
    int i, cnt;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] +
                                          env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] +
                                          env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] +
                                          env->vfp.reg[ry].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vadde64)(CPUCSKYState *env, uint32_t insn)
{
    int i, cnt;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspc[i] +
                                              env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dsps[i] +
                                              env->vfp.reg[ry].dsps[i]);
            }
            break;
        }

    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspc[i] +
                                               env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udsps[i] +
                                               env->vfp.reg[ry].udsps[i]);
            }
            break;
        }

    }

}
void VDSP_HELPER(vadde128)(CPUCSKYState *env, uint32_t insn)
{
    int i, cnt;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspc[i] +
                                              env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dsps[i] +
                                              env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspc[i] +
                                               env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udsps[i] +
                                               env->vfp.reg[ry].udsps[i]);
            }
            break;
        }

    }

}

void VDSP_HELPER(vcadd64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[i] = env->vfp.reg[rx].udspc[2 * i] +
                                           env->vfp.reg[rx].udspc[2 * i + 1];
            env->vfp.reg[rz].udspc[i + cnt] = env->vfp.reg[ry].udspc[2 * i] +
                                             env->vfp.reg[ry].udspc[2 * i + 1];
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[2 * i] +
                                           env->vfp.reg[rx].udsps[2 * i + 1]);
            env->vfp.reg[rz].udsps[i + cnt] = (env->vfp.reg[ry].udsps[2 * i] +
                                             env->vfp.reg[ry].udsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[2 * i] +
                                           env->vfp.reg[rx].udspi[2 * i + 1]);
            env->vfp.reg[rz].udspi[i + cnt] = (env->vfp.reg[ry].udspi[2 * i] +
                                             env->vfp.reg[ry].udspi[2 * i + 1]);
        }
        break;
    }
}
void VDSP_HELPER(vcadd128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[2 * i] +
                                         env->vfp.reg[rx].udspc[2 * i + 1]);
            env->vfp.reg[rz].udspc[i + cnt] = (env->vfp.reg[ry].udspc[2 * i] +
                                             env->vfp.reg[ry].udspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[2 * i] +
                                         env->vfp.reg[rx].udsps[2 * i + 1]);
            env->vfp.reg[rz].udsps[i + cnt] = (env->vfp.reg[ry].udsps[2 * i] +
                                        env->vfp.reg[ry].udsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[2 * i] +
                                         env->vfp.reg[rx].udspi[2 * i + 1]);
            env->vfp.reg[rz].udspi[i + cnt] = (env->vfp.reg[ry].udspi[2 * i] +
                                        env->vfp.reg[ry].udspi[2 * i + 1]);
        }
        break;
    }
}

void VDSP_HELPER(vcadde64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspc[2 * i] +
                                            env->vfp.reg[rx].dspc[2 * i + 1]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dsps[2 * i] +
                                            env->vfp.reg[rx].dsps[2 * i + 1]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspc[2 * i] +
                                             env->vfp.reg[rx].udspc[2 * i + 1]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udsps[2 * i] +
                                             env->vfp.reg[rx].udsps[2 * i + 1]);
            }
            break;
        }
    }
}
void VDSP_HELPER(vcadde128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspc[2 * i] +
                                            env->vfp.reg[rx].dspc[2 * i + 1]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dsps[2 * i] +
                                            env->vfp.reg[rx].dsps[2 * i + 1]);
            }
            break;
        }

    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspc[2 * i] +
                                             env->vfp.reg[rx].udspc[2 * i + 1]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udsps[2 * i] +
                                             env->vfp.reg[rx].udsps[2 * i + 1]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vaddxsl64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = (tmp + env->vfp.reg[ry].dspc[i]);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = (tmp + env->vfp.reg[ry].dsps[i]);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = (tmp + env->vfp.reg[ry].udspc[i]);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = (tmp + env->vfp.reg[ry].udsps[i]);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        }
    }
}
void VDSP_HELPER(vaddxsl128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = (tmp + env->vfp.reg[ry].dspc[i]);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = (tmp + env->vfp.reg[ry].dsps[i]);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = (tmp + env->vfp.reg[ry].udspc[i]);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = (tmp + env->vfp.reg[ry].udsps[i]);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vadds64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspc[i]);
                tmp = (tmp + env->vfp.reg[ry].dspc[i]);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = (tmp + env->vfp.reg[ry].dsps[i]);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = (tmp + env->vfp.reg[ry].dspi[i]);
                env->vfp.reg[rz].dspi[i] = tmp;
                if (tmp > 0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = 0x7fffffff;
                }
                if (tmp < -0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = -0x7fffffff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspc[i]);
                tmp = (tmp + env->vfp.reg[ry].udspc[i]);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = (tmp + env->vfp.reg[ry].udsps[i]);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = (tmp + env->vfp.reg[ry].udspi[i]);
                env->vfp.reg[rz].udspi[i] = tmp;
                if (tmp > 0xffffffff) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspi[i] = 1 - 1;
                }
            }
            break;
        }
    }
}
void VDSP_HELPER(vadds128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspc[i]);
                tmp = (tmp + env->vfp.reg[ry].dspc[i]);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = (tmp + env->vfp.reg[ry].dsps[i]);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = (tmp + env->vfp.reg[ry].dspi[i]);
                env->vfp.reg[rz].dspi[i] = tmp;
                if (tmp > 0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = 0x7fffffff;
                }
                if (tmp < -0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = -0x7fffffff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspc[i]);
                tmp = (tmp + env->vfp.reg[ry].udspc[i]);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = (tmp + env->vfp.reg[ry].udsps[i]);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = (tmp + env->vfp.reg[ry].udspi[i]);
                env->vfp.reg[rz].udspi[i] = tmp;
                if (tmp > 0xffffffff) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspi[i] = 1 - 1;
                }
            }
            break;
        }
    }

}

void VDSP_HELPER(vaddx64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] +
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] +
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] +
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] +
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vaddx128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] +
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] +
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] +
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] +
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vaddh64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] =
                    (((double)env->vfp.reg[rx].dspc[i] / 2) +
                     ((double)env->vfp.reg[ry].dspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] =
                    (((double)env->vfp.reg[rx].dsps[i] / 2) +
                     ((double)env->vfp.reg[ry].dsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] =
                    (((double)env->vfp.reg[rx].dspi[i] / 2) +
                     ((double)env->vfp.reg[ry].dspi[i] / 2));
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] =
                    (((double)env->vfp.reg[rx].udspc[i] / 2) +
                     ((double)env->vfp.reg[ry].udspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (((double)env->vfp.reg[rx].udsps[i] / 2) +
                     ((double)env->vfp.reg[ry].udsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] =
                    (((double)env->vfp.reg[rx].udspi[i] / 2) +
                     ((double)env->vfp.reg[ry].udspi[i] / 2));
            }
            break;
        }
    }
}

void VDSP_HELPER(vaddh128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] =
                    (((double)env->vfp.reg[rx].dspc[i] / 2) +
                     ((double)env->vfp.reg[ry].dspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] =
                    (((double)env->vfp.reg[rx].dsps[i] / 2) +
                     ((double)env->vfp.reg[ry].dsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] =
                    (((double)env->vfp.reg[rx].dspi[i] / 2) +
                     ((double)env->vfp.reg[ry].dspi[i] / 2));
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] =
                    (((double)env->vfp.reg[rx].udspc[i] / 2) +
                     ((double)env->vfp.reg[ry].udspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (((double)env->vfp.reg[rx].udsps[i] / 2) +
                     ((double)env->vfp.reg[ry].udsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] =
                    (((double)env->vfp.reg[rx].udspi[i] / 2) +
                     ((double)env->vfp.reg[ry].udspi[i] / 2));
            }
            break;
        }
    }
}

void VDSP_HELPER(vaddhr64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] =
                    (((double)env->vfp.reg[rx].dspc[i] / 2) + 0.5 +
                     ((double)env->vfp.reg[ry].dspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] =
                    (((double)env->vfp.reg[rx].dsps[i] / 2) + 0.5 +
                     ((double)env->vfp.reg[ry].dsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] =
                    (((double)env->vfp.reg[rx].dspi[i] / 2) + 0.5 +
                     ((double)env->vfp.reg[ry].dspi[i] / 2));
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] =
                    (((double)env->vfp.reg[rx].udspc[i] / 2) + 0.5 +
                     ((double)env->vfp.reg[ry].udspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (((double)env->vfp.reg[rx].udsps[i] / 2) + 0.5 +
                     ((double)env->vfp.reg[ry].udsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] =
                    (((double)env->vfp.reg[rx].udspi[i] / 2) + 0.5 +
                     ((double)env->vfp.reg[ry].udspi[i] / 2));
            }
            break;
        }
    }
}

void VDSP_HELPER(vaddhr128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] =
                    (((double)env->vfp.reg[rx].dspc[i] / 2) + 0.5 +
                     ((double)env->vfp.reg[ry].dspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] =
                    (((double)env->vfp.reg[rx].dsps[i] / 2) + 0.5 +
                     ((double)env->vfp.reg[ry].dsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] =
                    (((double)env->vfp.reg[rx].dspi[i] / 2) + 0.5 +
                     ((double)env->vfp.reg[ry].dspi[i] / 2));
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] =
                    (((double)env->vfp.reg[rx].udspc[i] / 2) + 0.5 +
                     ((double)env->vfp.reg[ry].udspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (((double)env->vfp.reg[rx].udsps[i] / 2) + 0.5 +
                     ((double)env->vfp.reg[ry].udsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] =
                    (((double)env->vfp.reg[rx].udspi[i] / 2) + 0.5 +
                     ((double)env->vfp.reg[ry].udspi[i] / 2));
            }
            break;
        }
    }
}

void VDSP_HELPER(vsub64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] -
                                        env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] -
                                        env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] -
                                        env->vfp.reg[ry].dspi[i]);
        }
        break;
    }
}
void VDSP_HELPER(vsub128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] -
                                        env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] -
                                        env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] -
                                        env->vfp.reg[ry].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vsube64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspc[i] -
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dsps[i] -
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspc[i] -
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udsps[i] -
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vsube128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspc[i] -
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dsps[i] -
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspc[i] -
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udsps[i] -
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vsabs64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = abs(env->vfp.reg[rx].dspc[i] -
                                               env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = abs(env->vfp.reg[rx].dsps[i] -
                                               env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = abs(env->vfp.reg[rx].dspi[i] -
                                               env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = env->vfp.reg[rx].udspc[i] -
                                            env->vfp.reg[ry].udspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = env->vfp.reg[rx].udsps[i] -
                                            env->vfp.reg[ry].udsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = env->vfp.reg[rx].udspi[i] -
                                            env->vfp.reg[ry].udspi[i];
            }
            break;
        }
    }
}

void VDSP_HELPER(vsabs128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = abs(env->vfp.reg[rx].dspc[i] -
                                               env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = abs(env->vfp.reg[rx].dsps[i] -
                                               env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = abs(env->vfp.reg[rx].dspi[i] -
                                               env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = env->vfp.reg[rx].udspc[i] -
                                            env->vfp.reg[ry].udspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = env->vfp.reg[rx].udsps[i] -
                                            env->vfp.reg[ry].udsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = env->vfp.reg[rx].udspi[i] -
                                            env->vfp.reg[ry].udspi[i];
            }
            break;
        }
    }
}

void VDSP_HELPER(vsabsa64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] += abs(env->vfp.reg[rx].dspc[i] -
                                                env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] += abs(env->vfp.reg[rx].dsps[i] -
                                                env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] += abs(env->vfp.reg[rx].dspi[i] -
                                               env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] += env->vfp.reg[rx].udspc[i] -
                                             env->vfp.reg[ry].udspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] += env->vfp.reg[rx].udsps[i] -
                                             env->vfp.reg[ry].udsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] += env->vfp.reg[rx].udspi[i] -
                                             env->vfp.reg[ry].udspi[i];
            }
            break;
        }
    }
}

void VDSP_HELPER(vsabsa128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] += abs(env->vfp.reg[rx].dspc[i] -
                                                env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] += abs(env->vfp.reg[rx].dsps[i] -
                                                env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] += abs(env->vfp.reg[rx].dspi[i] -
                                                env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] += env->vfp.reg[rx].udspc[i] -
                                             env->vfp.reg[ry].udspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] += env->vfp.reg[rx].udsps[i] -
                                             env->vfp.reg[ry].udsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] += env->vfp.reg[rx].udspi[i] -
                                             env->vfp.reg[ry].udspi[i];
            }
            break;
        }
    }
}

void VDSP_HELPER(vsabse64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = abs(env->vfp.reg[rx].dspc[i] -
                                               env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = abs(env->vfp.reg[rx].dsps[i] -
                                               env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = abs(env->vfp.reg[rx].udspc[i] -
                                                env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = abs(env->vfp.reg[rx].udsps[i] -
                                                env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vsabse128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = abs(env->vfp.reg[rx].dspc[i] -
                                               env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = abs(env->vfp.reg[rx].dsps[i] -
                                               env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = abs(env->vfp.reg[rx].udspc[i] -
                                                env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = abs(env->vfp.reg[rx].udsps[i] -
                                                env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vsabsae64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] += abs(env->vfp.reg[rx].dspc[i] -
                                                env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] += abs(env->vfp.reg[rx].dsps[i] -
                                                env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] += abs(env->vfp.reg[rx].udspc[i] -
                                                 env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] += abs(env->vfp.reg[rx].udsps[i] -
                                                 env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vsabsae128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] += abs(env->vfp.reg[rx].dspc[i] -
                                                env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] += abs(env->vfp.reg[rx].dsps[i] -
                                                env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] += abs(env->vfp.reg[rx].udspc[i] -
                                                 env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] += abs(env->vfp.reg[rx].udsps[i] -
                                                 env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vsubx64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] -
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] -
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] -
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] -
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vsubx128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] -
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] -
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] -
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] -
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vsubh64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] =
                    (((double)env->vfp.reg[rx].dspc[i] / 2) -
                     ((double)env->vfp.reg[ry].dspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] =
                    (((double)env->vfp.reg[rx].dsps[i] / 2) -
                     ((double)env->vfp.reg[ry].dsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] =
                    (((double)env->vfp.reg[rx].dspi[i] / 2) -
                     ((double)env->vfp.reg[ry].dspi[i] / 2));
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] =
                    (((double)env->vfp.reg[rx].udspc[i] / 2) -
                     ((double)env->vfp.reg[ry].udspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (((double)env->vfp.reg[rx].udsps[i] / 2) -
                     ((double)env->vfp.reg[ry].udsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] =
                    (((double)env->vfp.reg[rx].udspi[i] / 2) -
                     ((double)env->vfp.reg[ry].udspi[i] / 2));
            }
            break;
        }
    }
}

void VDSP_HELPER(vsubh128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] =
                    (((double)env->vfp.reg[rx].dspc[i] / 2) -
                     ((double)env->vfp.reg[ry].dspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] =
                    (((double)env->vfp.reg[rx].dsps[i] / 2) -
                     ((double)env->vfp.reg[ry].dsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] =
                    (((double)env->vfp.reg[rx].dspi[i] / 2) -
                     ((double)env->vfp.reg[ry].dspi[i] / 2));
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] =
                    (((double)env->vfp.reg[rx].udspc[i] / 2) -
                     ((double)env->vfp.reg[ry].udspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (((double)env->vfp.reg[rx].udsps[i] / 2) -
                     ((double)env->vfp.reg[ry].udsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] =
                    (((double)env->vfp.reg[rx].udspi[i] / 2) -
                     ((double)env->vfp.reg[ry].udspi[i] / 2));
            }
            break;
        }
    }
}

void VDSP_HELPER(vsubhr64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] =
                    (((double)env->vfp.reg[rx].dspc[i] / 2) + 0.5 -
                     ((double)env->vfp.reg[ry].dspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] =
                    (((double)env->vfp.reg[rx].dsps[i] / 2) + 0.5 -
                     ((double)env->vfp.reg[ry].dsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] =
                    (((double)env->vfp.reg[rx].dspi[i] / 2) + 0.5 -
                     ((double)env->vfp.reg[ry].dspi[i] / 2));
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] =
                    (((double)env->vfp.reg[rx].udspc[i] / 2) + 0.5 -
                     ((double)env->vfp.reg[ry].udspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (((double)env->vfp.reg[rx].udsps[i] / 2) + 0.5 -
                     ((double)env->vfp.reg[ry].udsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] =
                    (((double)env->vfp.reg[rx].udspi[i] / 2) + 0.5 -
                     ((double)env->vfp.reg[ry].udspi[i] / 2));
            }
            break;
        }
    }
}

void VDSP_HELPER(vsubhr128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] =
                    (((double)env->vfp.reg[rx].dspc[i] / 2) + 0.5 -
                     ((double)env->vfp.reg[ry].dspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] =
                    (((double)env->vfp.reg[rx].dsps[i] / 2) + 0.5 -
                     ((double)env->vfp.reg[ry].dsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] =
                    (((double)env->vfp.reg[rx].dspi[i] / 2) + 0.5 -
                     ((double)env->vfp.reg[ry].dspi[i] / 2));
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] =
                    (((double)env->vfp.reg[rx].udspc[i] / 2) + 0.5 -
                     ((double)env->vfp.reg[ry].udspc[i] / 2));
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (((double)env->vfp.reg[rx].udsps[i] / 2) + 0.5 -
                     ((double)env->vfp.reg[ry].udsps[i] / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] =
                    (((double)env->vfp.reg[rx].udspi[i] / 2) + 0.5 -
                     ((double)env->vfp.reg[ry].udspi[i] / 2));
            }
            break;
        }
    }
}

void VDSP_HELPER(vsubs64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspc[i]);
                tmp = (tmp - env->vfp.reg[ry].dspc[i]);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = (tmp - env->vfp.reg[ry].dsps[i]);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = (tmp - env->vfp.reg[ry].dspi[i]);
                env->vfp.reg[rz].dspi[i] = tmp;
                if (tmp > 0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = 0x7fffffff;
                }
                if (tmp < -0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = -0x7fffffff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspc[i]);
                tmp = (tmp - env->vfp.reg[ry].udspc[i]);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = (tmp - env->vfp.reg[ry].udsps[i]);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = (tmp - env->vfp.reg[ry].udspi[i]);
                env->vfp.reg[rz].udspi[i] = tmp;
                if (tmp > 0xffffffff) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspi[i] = 1 - 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vsubs128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspc[i]);
                tmp = (tmp - env->vfp.reg[ry].dspc[i]);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = (tmp - env->vfp.reg[ry].dsps[i]);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = (tmp - env->vfp.reg[ry].dspi[i]);
                env->vfp.reg[rz].dspi[i] = tmp;
                if (tmp > 0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = 0x7fffffff;
                }
                if (tmp < -0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = -0x7fffffff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspc[i]);
                tmp = (tmp - env->vfp.reg[ry].udspc[i]);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = (tmp - env->vfp.reg[ry].udsps[i]);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = (tmp - env->vfp.reg[ry].udspi[i]);
                env->vfp.reg[rz].udspi[i] = tmp;
                if (tmp > 0xffffffff) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspi[i] = 1 - 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vmul64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] *
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] *
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] *
                                            env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] *
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] *
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] *
                                             env->vfp.reg[ry].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmul128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] *
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] *
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] *
                                            env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] *
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] *
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] *
                                             env->vfp.reg[ry].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmule64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspc[i] *
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dsps[i] *
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspc[i] *
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udsps[i] *
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmule128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspc[i] *
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dsps[i] *
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspc[i] *
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udsps[i] *
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmula64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] += (env->vfp.reg[rx].dspc[i] *
                                             env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] += (env->vfp.reg[rx].dsps[i] *
                                             env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] += (env->vfp.reg[rx].dspi[i] *
                                             env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] += (env->vfp.reg[rx].udspc[i] *
                                              env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] += (env->vfp.reg[rx].udsps[i] *
                                              env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] += (env->vfp.reg[rx].udspi[i] *
                                              env->vfp.reg[ry].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmula128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] += (env->vfp.reg[rx].dspc[i] *
                                             env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] += (env->vfp.reg[rx].dsps[i] *
                                             env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] += (env->vfp.reg[rx].dspi[i] *
                                             env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] += (env->vfp.reg[rx].udspc[i] *
                                              env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] += (env->vfp.reg[rx].udsps[i] *
                                              env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] += (env->vfp.reg[rx].udspi[i] *
                                              env->vfp.reg[ry].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmulae64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] += (env->vfp.reg[rx].dspc[i] *
                                             env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] += (env->vfp.reg[rx].dsps[i] *
                                             env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] += (env->vfp.reg[rx].udspc[i] *
                                              env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] += (env->vfp.reg[rx].udsps[i] *
                                              env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmulae128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] += (env->vfp.reg[rx].dspc[i] *
                                             env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] += (env->vfp.reg[rx].dsps[i] *
                                             env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] += (env->vfp.reg[rx].udspc[i] *
                                              env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] += (env->vfp.reg[rx].udsps[i] *
                                              env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmuls64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] -= (env->vfp.reg[rx].dspc[i] *
                                             env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] -= (env->vfp.reg[rx].dsps[i] *
                                             env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] -= (env->vfp.reg[rx].dspi[i] *
                                             env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] -= (env->vfp.reg[rx].udspc[i] *
                                              env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] -= (env->vfp.reg[rx].udsps[i] *
                                              env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] -= (env->vfp.reg[rx].udspi[i] *
                                              env->vfp.reg[ry].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmuls128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] -= (env->vfp.reg[rx].dspc[i] *
                                             env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] -= (env->vfp.reg[rx].dsps[i] *
                                             env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] -= (env->vfp.reg[rx].dspi[i] *
                                             env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] -= (env->vfp.reg[rx].udspc[i] *
                                              env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] -= (env->vfp.reg[rx].udsps[i] *
                                              env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] -= (env->vfp.reg[rx].udspi[i] *
                                              env->vfp.reg[ry].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmulse64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] -= (env->vfp.reg[rx].dspc[i] *
                                             env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] -= (env->vfp.reg[rx].dsps[i] *
                                             env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] -= (env->vfp.reg[rx].udspc[i] *
                                              env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] -= (env->vfp.reg[rx].udsps[i] *
                                              env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmulse128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] -= (env->vfp.reg[rx].dspc[i] *
                                             env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] -= (env->vfp.reg[rx].dsps[i] *
                                             env->vfp.reg[ry].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] -= (env->vfp.reg[rx].udspc[i] *
                                              env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] -= (env->vfp.reg[rx].udsps[i] *
                                              env->vfp.reg[ry].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vshri64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;
    int immd;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;
    immd = ((insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK)
                        | (((insn > CSKY_VDSP_SOP_SHI_S) & 0x1) << 4);

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >> immd);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >> immd);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >> immd);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] >> immd);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] >> immd);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] >> immd);
            }
            break;
        }
    }
}

void VDSP_HELPER(vshri128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;
    int immd;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;
    immd = ((insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK)
                        | (((insn > CSKY_VDSP_SOP_SHI_S) & 0x1) << 4);

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >> immd);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >> immd);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >> immd);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] >> immd);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] >> immd);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] >> immd);
            }
            break;
        }
    }
}

void VDSP_HELPER(vshrir64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;
    int immd;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;
    immd = ((insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK)
                        | (((insn > CSKY_VDSP_SOP_SHI_S) & 0x1) << 4);

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >> immd);
                if (((env->vfp.reg[rz].dspc[i] >> (immd - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dspc[i] += 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >> immd);
                if (((env->vfp.reg[rz].dsps[i] >> (immd - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dsps[i] += 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >> immd);
                if (((env->vfp.reg[rz].dspi[i] >> (immd - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dspi[i] += 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] >> immd);
                if (((env->vfp.reg[rz].udspc[i] >> (immd - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udspc[i] += 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] >> immd);
                if (((env->vfp.reg[rz].udsps[i] >> (immd - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udsps[i] += 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] >> immd);
                if (((env->vfp.reg[rz].udspi[i] >> (immd - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udspi[i] += 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vshrir128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;
    int immd;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;
    immd = ((insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK)
                        | (((insn > CSKY_VDSP_SOP_SHI_S) & 0x1) << 4);

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >> immd);
                if (((env->vfp.reg[rz].dspc[i] >> (immd - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dspc[i] += 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >> immd);
                if (((env->vfp.reg[rz].dsps[i] >> (immd - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dsps[i] += 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >> immd);
                if (((env->vfp.reg[rz].dspi[i] >> (immd - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dspi[i] += 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] >> immd);
                if (((env->vfp.reg[rz].udspc[i] >> (immd - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udspc[i] += 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] >> immd);
                if (((env->vfp.reg[rz].udsps[i] >> (immd - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udsps[i] += 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] >> immd);
                if (((env->vfp.reg[rz].udspi[i] >> (immd - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udspi[i] += 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vshrr64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >>
                                            (env->vfp.reg[ry].dspc[i] & 0x1f));
                if (((env->vfp.reg[rz].dspc[i] >> ((env->vfp.reg[ry].dspc[i] &
                                                    0x1f) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dspc[i] += 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >>
                                            (env->vfp.reg[ry].dsps[i] & 0x1f));
                if (((env->vfp.reg[rz].dsps[i] >> ((env->vfp.reg[ry].dsps[i] &
                                                    0x1f) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dsps[i] += 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >>
                                            (env->vfp.reg[ry].dspi[i] & 0x1f));
                if (((env->vfp.reg[rz].dspi[i] >> ((env->vfp.reg[ry].dspi[i] &
                                                    0x1f) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dspi[i] += 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] >>
                                            (env->vfp.reg[ry].udspc[i] & 0x1f));
                if (((env->vfp.reg[rz].udspc[i] >> ((env->vfp.reg[ry].udspc[i] &
                                                     0x1f) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udspc[i] += 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] >>
                                            (env->vfp.reg[ry].udsps[i] & 0x1f));
                if (((env->vfp.reg[rz].udsps[i] >> ((env->vfp.reg[ry].udsps[i] &
                                                     0x1f) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udsps[i] += 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] >>
                                         (env->vfp.reg[ry].udspi[i] & 0x1f));
                if (((env->vfp.reg[rz].udspi[i] >> ((env->vfp.reg[ry].udspi[i] &
                                                     0x1f) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udspi[i] += 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vshrr128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >>
                                            (env->vfp.reg[ry].dspc[i] & 0x1f));
                if (((env->vfp.reg[rz].dspc[i] >> ((env->vfp.reg[ry].dspc[i] &
                                                    0x1f) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dspc[i] += 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >>
                                            (env->vfp.reg[ry].dsps[i] & 0x1f));
                if (((env->vfp.reg[rz].dsps[i] >> ((env->vfp.reg[ry].dsps[i] &
                                                    0x1f) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dsps[i] += 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >>
                                            (env->vfp.reg[ry].dspi[i] & 0x1f));
                if (((env->vfp.reg[rz].dspi[i] >> ((env->vfp.reg[ry].dspi[i] &
                                                    0x1f) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dspi[i] += 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] >>
                                            (env->vfp.reg[ry].udspc[i] & 0x1f));
                if (((env->vfp.reg[rz].udspc[i] >> ((env->vfp.reg[ry].udspc[i] &
                                                     0x1f) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udspc[i] += 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] >>
                                            (env->vfp.reg[ry].udsps[i] & 0x1f));
                if (((env->vfp.reg[rz].udsps[i] >> ((env->vfp.reg[ry].udsps[i] &
                                                     0x1f) - 1)) & 0x1) == 1){
                    env->vfp.reg[rz].udsps[i] += 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] >>
                                         (env->vfp.reg[ry].udspi[i] & 0x1f));
                if (((env->vfp.reg[rz].udspi[i] >> ((env->vfp.reg[ry].udspi[i] &
                                                     0x1f) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udspi[i] += 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vshls64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspc[i]);
                tmp = (tmp << (env->vfp.reg[ry].dspc[i] & 0x1f));
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = (tmp << (env->vfp.reg[ry].dsps[i] & 0x1f));
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = (tmp << (env->vfp.reg[ry].dspi[i] & 0x1f));
                env->vfp.reg[rz].dspi[i] = tmp;
                if (tmp > 0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = 0x7fffffff;
                }
                if (tmp < -0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = -0x7fffffff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspc[i]);
                tmp = (tmp << (env->vfp.reg[ry].udspc[i] & 0x1f));
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = (tmp << (env->vfp.reg[ry].udsps[i] & 0x1f));
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = (tmp << (env->vfp.reg[ry].udspi[i] & 0x1f));
                env->vfp.reg[rz].udspi[i] = tmp;
                if (tmp > 0xffffffff) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspi[i] = 1 - 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vshls128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspc[i]);
                tmp = (tmp << (env->vfp.reg[ry].dspc[i] & 0x1f));
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = (tmp << (env->vfp.reg[ry].dsps[i] & 0x1f));
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = (tmp << (env->vfp.reg[ry].dspi[i] & 0x1f));
                env->vfp.reg[rz].dspi[i] = tmp;
                if (tmp > 0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = 0x7fffffff;
                }
                if (tmp < -0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = -0x7fffffff - 1;
                }
            }
            break;

        }

    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspc[i]);
                tmp = (tmp << (env->vfp.reg[ry].udspc[i] & 0x1f));
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = (tmp << (env->vfp.reg[ry].udsps[i] & 0x1f));
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = (tmp << (env->vfp.reg[ry].udspi[i] & 0x1f));
                env->vfp.reg[rz].udspi[i] = tmp;
                if (tmp > 0xffffffff) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspi[i] = 1 - 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vshr64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >>
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >>
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >>
                                            env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] >>
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] >>
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] >>
                                             env->vfp.reg[ry].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vshr128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >>
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >>
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >>
                                            env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] >>
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] >>
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] >>
                                             env->vfp.reg[ry].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vshli64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;
    int immd;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;
    immd = ((insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK)
                        | (((insn > CSKY_VDSP_SOP_SHI_S) & 0x1) << 4);

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] << immd);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] << immd);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] << immd);
            }
            break;

        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] << immd);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] << immd);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] << immd);
            }
            break;
        }
    }
}

void VDSP_HELPER(vshli128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;
    int immd;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;
    immd = ((insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK)
                        | (((insn > CSKY_VDSP_SOP_SHI_S) & 0x1) << 4);

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] << immd);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] << immd);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] << immd);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] << immd);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] << immd);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] << immd);
            }
            break;
        }
    }
}

void VDSP_HELPER(vshlis64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, rz;
    int immd;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;
    immd = ((insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK)
                        | (((insn > CSKY_VDSP_SOP_SHI_S) & 0x1) << 4);

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspc[i]);
                tmp = (tmp << immd);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = (tmp << immd);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = (tmp << immd);
                env->vfp.reg[rz].dspi[i] = tmp;
                if (tmp > 0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = 0x7fffffff;
                }
                if (tmp < -0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = -0x7fffffff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspc[i]);
                tmp = (tmp << immd);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = (tmp << immd);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = (tmp << immd);
                env->vfp.reg[rz].udspi[i] = tmp;
                if (tmp > 0xffffffff) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspi[i] = 1 - 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vshlis128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, rz;
    int immd;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;
    immd = ((insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK)
                        | (((insn > CSKY_VDSP_SOP_SHI_S) & 0x1) << 4);

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspc[i]);
                tmp = (tmp << immd);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = (tmp << immd);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = (tmp << immd);
                env->vfp.reg[rz].dspi[i] = tmp;
                if (tmp > 0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = 0x7fffffff;
                }
                if (tmp < -0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = -0x7fffffff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspc[i]);
                tmp = (tmp << immd);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = (tmp << immd);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = (tmp << immd);
                env->vfp.reg[rz].udspi[i] = tmp;
                if (tmp > 0xffffffff) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspi[i] = 1 - 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vshl64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] <<
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] <<
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] <<
                                            env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] <<
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] <<
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] <<
                                             env->vfp.reg[ry].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vshl128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] <<
                                            env->vfp.reg[ry].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] <<
                                            env->vfp.reg[ry].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] <<
                                            env->vfp.reg[ry].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] <<
                                             env->vfp.reg[ry].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] <<
                                             env->vfp.reg[ry].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] <<
                                             env->vfp.reg[ry].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vcmphs64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >=
                                        env->vfp.reg[ry].dspc[i]) ? 0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >=
                                    env->vfp.reg[ry].dsps[i]) ? 0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >=
                                   env->vfp.reg[ry].dspi[i]) ? 0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] >=
                                        env->vfp.reg[ry].udspc[i]) ? 0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] >=
                                             env->vfp.reg[ry].udsps[i]) ?
                                             0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] >=
                                             env->vfp.reg[ry].udspi[i]) ?
                                             0xffffffff : 0x0;
            }
            break;
        }
    }
}

void VDSP_HELPER(vcmphs128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >=
                                            env->vfp.reg[ry].dspc[i]) ?
                                            0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >=
                                            env->vfp.reg[ry].dsps[i]) ?
                                            0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >=
                                            env->vfp.reg[ry].dspi[i]) ?
                                            0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] >=
                                             env->vfp.reg[ry].udspc[i]) ?
                                             0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] >=
                                             env->vfp.reg[ry].udsps[i]) ?
                                             0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] >=
                                             env->vfp.reg[ry].udspi[i]) ?
                                             0xffffffff : 0x0;
            }
            break;
        }
    }
}

void VDSP_HELPER(vcmplt64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] <
                                            env->vfp.reg[ry].dspc[i]) ?
                                            0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] <
                                            env->vfp.reg[ry].dsps[i]) ?
                                            0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] <
                                            env->vfp.reg[ry].dspi[i]) ?
                                            0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] <
                                             env->vfp.reg[ry].udspc[i]) ?
                                             0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] <
                                             env->vfp.reg[ry].udsps[i]) ?
                                             0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] <
                                             env->vfp.reg[ry].udspi[i]) ?
                                             0xffffffff : 0x0;
            }
            break;

        }

    }

}

void VDSP_HELPER(vcmplt128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] <
                                            env->vfp.reg[ry].dspc[i]) ?
                                            0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] <
                                            env->vfp.reg[ry].dsps[i]) ?
                                            0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] <
                                            env->vfp.reg[ry].dspi[i]) ?
                                            0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] <
                                             env->vfp.reg[ry].udspc[i]) ?
                                             0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] <
                                             env->vfp.reg[ry].udsps[i]) ?
                                             0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] <
                                             env->vfp.reg[ry].udspi[i]) ?
                                             0xffffffff : 0x0;
            }
            break;
        }
    }
}

void VDSP_HELPER(vcmpne64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] !=
                                            env->vfp.reg[ry].dspc[i]) ?
                                            0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] !=
                                            env->vfp.reg[ry].dsps[i]) ?
                                            0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] !=
                                            env->vfp.reg[ry].dspi[i]) ?
                                            0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] !=
                                             env->vfp.reg[ry].udspc[i]) ?
                                             0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] !=
                                             env->vfp.reg[ry].udsps[i]) ?
                                             0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] !=
                                             env->vfp.reg[ry].udspi[i]) ?
                                             0xffffffff : 0x0;
            }
            break;
        }
    }
}
void VDSP_HELPER(vcmpne128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] !=
                                            env->vfp.reg[ry].dspc[i]) ?
                                            0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] !=
                                            env->vfp.reg[ry].dsps[i]) ?
                                            0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] !=
                                            env->vfp.reg[ry].dspi[i]) ?
                                            0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] !=
                                             env->vfp.reg[ry].udspc[i]) ?
                                             0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] !=
                                             env->vfp.reg[ry].udsps[i]) ?
                                             0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] !=
                                             env->vfp.reg[ry].udspi[i]) ?
                                             0xffffffff : 0x0;
            }
            break;

        }

    }

}

void VDSP_HELPER(vcmphsz64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >= 0) ?
                                           0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >=
                                            0) ? 0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >=
                                            0) ? 0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udspc[i];
                env->vfp.reg[rz].udspc[i] = (tmp2 >= 0) ? 0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udsps[i];
                env->vfp.reg[rz].udsps[i] = (tmp2 >= 0) ? 0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udspi[i];
                env->vfp.reg[rz].udspi[i] = (tmp2 >= 0) ? 0xffffffff : 0x0;
            }
            break;
        }
    }
}

void VDSP_HELPER(vcmphsz128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >=
                                            0) ? 0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >=
                                            0) ? 0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >=
                                            0) ? 0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udspc[i];
                env->vfp.reg[rz].udspc[i] = (tmp2 >= 0) ? 0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udsps[i];
                env->vfp.reg[rz].udsps[i] = (tmp2 >= 0) ? 0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udspi[i];
                env->vfp.reg[rz].udspi[i] = (tmp2 >= 0) ? 0xffffffff : 0x0;
            }
            break;
        }
    }
}

void VDSP_HELPER(vcmpltz64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] <
                                            0) ? 0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] <
                                            0) ? 0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] <
                                            0) ? 0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udspc[i];
                env->vfp.reg[rz].udspc[i] = (tmp2 < 0) ? 0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udsps[i];
                env->vfp.reg[rz].udsps[i] = (tmp2 < 0) ? 0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udspi[i];
                env->vfp.reg[rz].udspi[i] = (tmp2 < 0) ? 0xffffffff : 0x0;
            }
            break;
        }
    }
}

void VDSP_HELPER(vcmpltz128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] <
                                            0) ? 0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] <
                                            0) ? 0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] <
                                            0) ? 0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udspc[i];
                env->vfp.reg[rz].udspc[i] = (tmp2 < 0) ? 0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udsps[i];
                env->vfp.reg[rz].udsps[i] = (tmp2 < 0) ? 0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udspi[i];
                env->vfp.reg[rz].udspi[i] = (tmp2 < 0) ? 0xffffffff : 0x0;
            }
            break;

        }

    }

}

void VDSP_HELPER(vcmpnez64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] !=
                                            0) ? 0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] !=
                                            0) ? 0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] !=
                                            0) ? 0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udspc[i];
                env->vfp.reg[rz].udspc[i] = (tmp2 != 0) ? 0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udsps[i];
                env->vfp.reg[rz].udsps[i] = (tmp2 != 0) ? 0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udspi[i];
                env->vfp.reg[rz].udspi[i] = (tmp2 != 0) ? 0xffffffff : 0x0;
            }
            break;
        }
    }
}
void VDSP_HELPER(vcmpnez128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] !=
                                            0) ? 0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] !=
                                            0) ? 0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] !=
                                            0) ? 0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udspc[i];
                env->vfp.reg[rz].udspc[i] = (tmp2 != 0) ? 0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udsps[i];
                env->vfp.reg[rz].udsps[i] = (tmp2 != 0) ? 0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                int64_t tmp2 = env->vfp.reg[rx].udspi[i];
                env->vfp.reg[rz].udspi[i] = (tmp2 != 0) ? 0xffffffff : 0x0;
            }
            break;
        }
    }
}

void VDSP_HELPER(vmax64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >
                                            env->vfp.reg[ry].dspc[i]) ?
                                            env->vfp.reg[rx].dspc[i] :
                                            env->vfp.reg[ry].dspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >
                                            env->vfp.reg[ry].dsps[i]) ?
                                            env->vfp.reg[rx].dsps[i] :
                                            env->vfp.reg[ry].dsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >
                                            env->vfp.reg[ry].dspi[i]) ?
                                            env->vfp.reg[rx].dspi[i] :
                                            env->vfp.reg[ry].dspi[i];
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] >
                                             env->vfp.reg[ry].udspc[i]) ?
                                             env->vfp.reg[rx].udspc[i] :
                                             env->vfp.reg[ry].udspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] >
                                             env->vfp.reg[ry].udsps[i]) ?
                                             env->vfp.reg[rx].udsps[i] :
                                             env->vfp.reg[ry].udsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] >
                                             env->vfp.reg[ry].udspi[i]) ?
                                             env->vfp.reg[rx].udspi[i] :
                                             env->vfp.reg[ry].udspi[i];
            }
            break;
        }
    }

}
void VDSP_HELPER(vmax128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] >
                                            env->vfp.reg[ry].dspc[i]) ?
                                            env->vfp.reg[rx].dspc[i] :
                                            env->vfp.reg[ry].dspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] >
                                            env->vfp.reg[ry].dsps[i]) ?
                                            env->vfp.reg[rx].dsps[i] :
                                            env->vfp.reg[ry].dsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] >
                                            env->vfp.reg[ry].dspi[i]) ?
                                            env->vfp.reg[rx].dspi[i] :
                                            env->vfp.reg[ry].dspi[i];
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] >
                                             env->vfp.reg[ry].udspc[i]) ?
                                             env->vfp.reg[rx].udspc[i] :
                                             env->vfp.reg[ry].udspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] >
                                             env->vfp.reg[ry].udsps[i]) ?
                                             env->vfp.reg[rx].udsps[i] :
                                             env->vfp.reg[ry].udsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] >
                                             env->vfp.reg[ry].udspi[i]) ?
                                             env->vfp.reg[rx].udspi[i] :
                                             env->vfp.reg[ry].udspi[i];
            }
            break;
        }
    }
}

void VDSP_HELPER(vmin64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] <
                                            env->vfp.reg[ry].dspc[i]) ?
                                            env->vfp.reg[rx].dspc[i] :
                                            env->vfp.reg[ry].dspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] <
                                            env->vfp.reg[ry].dsps[i]) ?
                                            env->vfp.reg[rx].dsps[i] :
                                            env->vfp.reg[ry].dsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] <
                                            env->vfp.reg[ry].dspi[i]) ?
                                            env->vfp.reg[rx].dspi[i] :
                                            env->vfp.reg[ry].dspi[i];
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] <
                                             env->vfp.reg[ry].udspc[i]) ?
                                             env->vfp.reg[rx].udspc[i] :
                                             env->vfp.reg[ry].udspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] <
                                             env->vfp.reg[ry].udsps[i]) ?
                                             env->vfp.reg[rx].udsps[i] :
                                             env->vfp.reg[ry].udsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] <
                                             env->vfp.reg[ry].udspi[i]) ?
                                             env->vfp.reg[rx].udspi[i] :
                                             env->vfp.reg[ry].udspi[i];
            }
            break;
        }
    }
}

void VDSP_HELPER(vmin128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] <
                                            env->vfp.reg[ry].dspc[i]) ?
                                            env->vfp.reg[rx].dspc[i] :
                                            env->vfp.reg[ry].dspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] <
                                            env->vfp.reg[ry].dsps[i]) ?
                                            env->vfp.reg[rx].dsps[i] :
                                            env->vfp.reg[ry].dsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] <
                                            env->vfp.reg[ry].dspi[i]) ?
                                            env->vfp.reg[rx].dspi[i] :
                                            env->vfp.reg[ry].dspi[i];
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] <
                                             env->vfp.reg[ry].udspc[i]) ?
                                             env->vfp.reg[rx].udspc[i] :
                                             env->vfp.reg[ry].udspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] <
                                             env->vfp.reg[ry].udsps[i]) ?
                                             env->vfp.reg[rx].udsps[i] :
                                             env->vfp.reg[ry].udsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] <
                                             env->vfp.reg[ry].udspi[i]) ?
                                             env->vfp.reg[rx].udspi[i] :
                                             env->vfp.reg[ry].udspi[i];
            }
            break;
        }
    }
}

void VDSP_HELPER(vcmax64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[2 * i] >
                                            env->vfp.reg[rx].dspc[2 * i + 1]) ?
                                            env->vfp.reg[rx].dspc[2 * i] :
                                            env->vfp.reg[rx].dspc[2 * i + 1];
                env->vfp.reg[rz].dspc[i + cnt] = (env->vfp.reg[ry].dspc[2 * i] >
                                            env->vfp.reg[ry].dspc[2 * i + 1]) ?
                                            env->vfp.reg[ry].dspc[2 * i] :
                                            env->vfp.reg[ry].dspc[2 * i + 1];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[2 * i] >
                                            env->vfp.reg[rx].dsps[2 * i + 1]) ?
                                            env->vfp.reg[rx].dsps[2 * i] :
                                            env->vfp.reg[rx].dsps[2 * i + 1];
                env->vfp.reg[rz].dsps[i + cnt] = (env->vfp.reg[ry].dsps[2 * i] >
                                            env->vfp.reg[ry].dsps[2 * i + 1]) ?
                                            env->vfp.reg[ry].dsps[2 * i] :
                                            env->vfp.reg[ry].dsps[2 * i + 1];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[2 * i] >
                                            env->vfp.reg[rx].dspi[2 * i + 1]) ?
                                            env->vfp.reg[rx].dspi[2 * i] :
                                            env->vfp.reg[rx].dspi[2 * i + 1];
                env->vfp.reg[rz].dspi[i + cnt] = (env->vfp.reg[ry].dspi[2 * i] >
                                            env->vfp.reg[ry].dspi[2 * i + 1]) ?
                                            env->vfp.reg[ry].dspi[2 * i] :
                                            env->vfp.reg[ry].dspi[2 * i + 1];
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[2 * i] >
                                            env->vfp.reg[rx].udspc[2 * i + 1]) ?
                                            env->vfp.reg[rx].udspc[2 * i] :
                                            env->vfp.reg[rx].udspc[2 * i + 1];
                env->vfp.reg[rz].udspc[i + cnt] =
                    (env->vfp.reg[ry].udspc[2 * i] >
                     env->vfp.reg[ry].udspc[2 * i + 1]) ?
                     env->vfp.reg[ry].udspc[2 * i] :
                     env->vfp.reg[ry].udspc[2 * i + 1];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (env->vfp.reg[rx].udsps[2 * i] >
                    env->vfp.reg[rx].udsps[2 * i + 1]) ?
                    env->vfp.reg[rx].udsps[2 * i] :
                    env->vfp.reg[rx].udsps[2 * i + 1];
                env->vfp.reg[rz].udsps[i + cnt] =
                    (env->vfp.reg[ry].udsps[2 * i] >
                    env->vfp.reg[ry].udsps[2 * i + 1]) ?
                    env->vfp.reg[ry].udsps[2 * i] :
                    env->vfp.reg[ry].udsps[2 * i + 1];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] =
                    (env->vfp.reg[rx].udspi[2 * i] >
                     env->vfp.reg[rx].udspi[2 * i + 1]) ?
                    env->vfp.reg[rx].udspi[2 * i] :
                    env->vfp.reg[rx].udspi[2 * i + 1];
                env->vfp.reg[rz].udspi[i + cnt] =
                    (env->vfp.reg[ry].udspi[2 * i] >
                     env->vfp.reg[ry].udspi[2 * i + 1]) ?
                    env->vfp.reg[ry].udspi[2 * i] :
                    env->vfp.reg[ry].udspi[2 * i + 1];
            }
            break;
        }
    }
}

void VDSP_HELPER(vcmax128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] =
                    (env->vfp.reg[rx].dspc[2 * i] >
                     env->vfp.reg[rx].dspc[2 * i + 1]) ?
                    env->vfp.reg[rx].dspc[2 * i] :
                    env->vfp.reg[rx].dspc[2 * i + 1];
                env->vfp.reg[rz].dspc[i + cnt] =
                    (env->vfp.reg[ry].dspc[2 * i] >
                     env->vfp.reg[ry].dspc[2 * i + 1]) ?
                    env->vfp.reg[ry].dspc[2 * i] :
                    env->vfp.reg[ry].dspc[2 * i + 1];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] =
                    (env->vfp.reg[rx].dsps[2 * i] >
                     env->vfp.reg[rx].dsps[2 * i + 1]) ?
                    env->vfp.reg[rx].dsps[2 * i] :
                    env->vfp.reg[rx].dsps[2 * i + 1];
                env->vfp.reg[rz].dsps[i + cnt] =
                    (env->vfp.reg[ry].dsps[2 * i] >
                     env->vfp.reg[ry].dsps[2 * i + 1]) ?
                    env->vfp.reg[ry].dsps[2 * i] :
                    env->vfp.reg[ry].dsps[2 * i + 1];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] =
                    (env->vfp.reg[rx].dspi[2 * i] >
                     env->vfp.reg[rx].dspi[2 * i + 1]) ?
                    env->vfp.reg[rx].dspi[2 * i] :
                    env->vfp.reg[rx].dspi[2 * i + 1];
                env->vfp.reg[rz].dspi[i + cnt] =
                    (env->vfp.reg[ry].dspi[2 * i] >
                     env->vfp.reg[ry].dspi[2 * i + 1]) ?
                    env->vfp.reg[ry].dspi[2 * i] :
                    env->vfp.reg[ry].dspi[2 * i + 1];
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] =
                    (env->vfp.reg[rx].udspc[2 * i] >
                     env->vfp.reg[rx].udspc[2 * i + 1]) ?
                    env->vfp.reg[rx].udspc[2 * i] :
                    env->vfp.reg[rx].udspc[2 * i + 1];
                env->vfp.reg[rz].udspc[i + cnt] =
                    (env->vfp.reg[ry].udspc[2 * i] >
                     env->vfp.reg[ry].udspc[2 * i + 1]) ?
                    env->vfp.reg[ry].udspc[2 * i] :
                    env->vfp.reg[ry].udspc[2 * i + 1];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (env->vfp.reg[rx].udsps[2 * i] >
                     env->vfp.reg[rx].udsps[2 * i + 1]) ?
                    env->vfp.reg[rx].udsps[2 * i] :
                    env->vfp.reg[rx].udsps[2 * i + 1];
                env->vfp.reg[rz].udsps[i + cnt] =
                    (env->vfp.reg[ry].udsps[2 * i] >
                     env->vfp.reg[ry].udsps[2 * i + 1]) ?
                    env->vfp.reg[ry].udsps[2 * i] :
                    env->vfp.reg[ry].udsps[2 * i + 1];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] =
                    (env->vfp.reg[rx].udspi[2 * i] >
                     env->vfp.reg[rx].udspi[2 * i + 1]) ?
                    env->vfp.reg[rx].udspi[2 * i] :
                    env->vfp.reg[rx].udspi[2 * i + 1];
                env->vfp.reg[rz].udspi[i + cnt] =
                    (env->vfp.reg[ry].udspi[2 * i] >
                     env->vfp.reg[ry].udspi[2 * i + 1]) ?
                    env->vfp.reg[ry].udspi[2 * i] :
                    env->vfp.reg[ry].udspi[2 * i + 1];
            }
            break;
        }
    }
}

void VDSP_HELPER(vcmin64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] =
                    (env->vfp.reg[rx].dspc[2 * i] <
                     env->vfp.reg[rx].dspc[2 * i + 1]) ?
                    env->vfp.reg[rx].dspc[2 * i] :
                    env->vfp.reg[rx].dspc[2 * i + 1];
                env->vfp.reg[rz].dspc[i + cnt] =
                    (env->vfp.reg[ry].dspc[2 * i] <
                     env->vfp.reg[ry].dspc[2 * i + 1]) ?
                    env->vfp.reg[ry].dspc[2 * i] :
                    env->vfp.reg[ry].dspc[2 * i + 1];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] =
                    (env->vfp.reg[rx].dsps[2 * i] <
                     env->vfp.reg[rx].dsps[2 * i + 1]) ?
                    env->vfp.reg[rx].dsps[2 * i] :
                    env->vfp.reg[rx].dsps[2 * i + 1];
                env->vfp.reg[rz].dsps[i + cnt] =
                    (env->vfp.reg[ry].dsps[2 * i] <
                     env->vfp.reg[ry].dsps[2 * i + 1]) ?
                    env->vfp.reg[ry].dsps[2 * i] :
                    env->vfp.reg[ry].dsps[2 * i + 1];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] =
                    (env->vfp.reg[rx].dspi[2 * i] <
                     env->vfp.reg[rx].dspi[2 * i + 1]) ?
                    env->vfp.reg[rx].dspi[2 * i] :
                    env->vfp.reg[rx].dspi[2 * i + 1];
                env->vfp.reg[rz].dspi[i + cnt] =
                    (env->vfp.reg[ry].dspi[2 * i] <
                     env->vfp.reg[ry].dspi[2 * i + 1]) ?
                    env->vfp.reg[ry].dspi[2 * i] :
                    env->vfp.reg[ry].dspi[2 * i + 1];
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] =
                    (env->vfp.reg[rx].udspc[2 * i] <
                     env->vfp.reg[rx].udspc[2 * i + 1]) ?
                    env->vfp.reg[rx].udspc[2 * i] :
                    env->vfp.reg[rx].udspc[2 * i + 1];
                env->vfp.reg[rz].udspc[i + cnt] =
                    (env->vfp.reg[ry].udspc[2 * i] <
                     env->vfp.reg[ry].udspc[2 * i + 1]) ?
                    env->vfp.reg[ry].udspc[2 * i] :
                    env->vfp.reg[ry].udspc[2 * i + 1];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (env->vfp.reg[rx].udsps[2 * i] <
                     env->vfp.reg[rx].udsps[2 * i + 1]) ?
                    env->vfp.reg[rx].udsps[2 * i] :
                    env->vfp.reg[rx].udsps[2 * i + 1];
                env->vfp.reg[rz].udsps[i + cnt] =
                    (env->vfp.reg[ry].udsps[2 * i] <
                     env->vfp.reg[ry].udsps[2 * i + 1]) ?
                    env->vfp.reg[ry].udsps[2 * i] :
                    env->vfp.reg[ry].udsps[2 * i + 1];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] =
                    (env->vfp.reg[rx].udspi[2 * i] <
                     env->vfp.reg[rx].udspi[2 * i + 1]) ?
                    env->vfp.reg[rx].udspi[2 * i] :
                    env->vfp.reg[rx].udspi[2 * i + 1];
                env->vfp.reg[rz].udspi[i + cnt] =
                    (env->vfp.reg[ry].udspi[2 * i] <
                     env->vfp.reg[ry].udspi[2 * i + 1]) ?
                    env->vfp.reg[ry].udspi[2 * i] :
                    env->vfp.reg[ry].udspi[2 * i + 1];
            }
            break;
        }
    }
}

void VDSP_HELPER(vcmin128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] =
                    (env->vfp.reg[rx].dspc[2 * i] <
                     env->vfp.reg[rx].dspc[2 * i + 1]) ?
                    env->vfp.reg[rx].dspc[2 * i] :
                    env->vfp.reg[rx].dspc[2 * i + 1];
                env->vfp.reg[rz].dspc[i + cnt] =
                    (env->vfp.reg[ry].dspc[2 * i] <
                     env->vfp.reg[ry].dspc[2 * i + 1]) ?
                    env->vfp.reg[ry].dspc[2 * i] :
                    env->vfp.reg[ry].dspc[2 * i + 1];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] =
                    (env->vfp.reg[rx].dsps[2 * i] <
                     env->vfp.reg[rx].dsps[2 * i + 1]) ?
                    env->vfp.reg[rx].dsps[2 * i] :
                    env->vfp.reg[rx].dsps[2 * i + 1];
                env->vfp.reg[rz].dsps[i + cnt] =
                    (env->vfp.reg[ry].dsps[2 * i] <
                     env->vfp.reg[ry].dsps[2 * i + 1]) ?
                    env->vfp.reg[ry].dsps[2 * i] :
                    env->vfp.reg[ry].dsps[2 * i + 1];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] =
                    (env->vfp.reg[rx].dspi[2 * i] <
                     env->vfp.reg[rx].dspi[2 * i + 1]) ?
                    env->vfp.reg[rx].dspi[2 * i] :
                    env->vfp.reg[rx].dspi[2 * i + 1];
                env->vfp.reg[rz].dspi[i + cnt] =
                    (env->vfp.reg[ry].dspi[2 * i] <
                     env->vfp.reg[ry].dspi[2 * i + 1]) ?
                    env->vfp.reg[ry].dspi[2 * i] :
                    env->vfp.reg[ry].dspi[2 * i + 1];
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] =
                    (env->vfp.reg[rx].udspc[2 * i] <
                     env->vfp.reg[rx].udspc[2 * i + 1]) ?
                    env->vfp.reg[rx].udspc[2 * i] :
                    env->vfp.reg[rx].udspc[2 * i + 1];
                env->vfp.reg[rz].udspc[i + cnt] =
                    (env->vfp.reg[ry].udspc[2 * i] <
                     env->vfp.reg[ry].udspc[2 * i + 1]) ?
                    env->vfp.reg[ry].udspc[2 * i] :
                    env->vfp.reg[ry].udspc[2 * i + 1];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (env->vfp.reg[rx].udsps[2 * i] <
                     env->vfp.reg[rx].udsps[2 * i + 1]) ?
                    env->vfp.reg[rx].udsps[2 * i] :
                    env->vfp.reg[rx].udsps[2 * i + 1];
                env->vfp.reg[rz].udsps[i + cnt] =
                    (env->vfp.reg[ry].udsps[2 * i] <
                     env->vfp.reg[ry].udsps[2 * i + 1]) ?
                    env->vfp.reg[ry].udsps[2 * i] :
                    env->vfp.reg[ry].udsps[2 * i + 1];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] =
                    (env->vfp.reg[rx].udspi[2 * i] <
                     env->vfp.reg[rx].udspi[2 * i + 1]) ?
                    env->vfp.reg[rx].udspi[2 * i] :
                    env->vfp.reg[rx].udspi[2 * i + 1];
                env->vfp.reg[rz].udspi[i + cnt] =
                    (env->vfp.reg[ry].udspi[2 * i] <
                     env->vfp.reg[ry].udspi[2 * i + 1]) ?
                    env->vfp.reg[ry].udspi[2 * i] :
                    env->vfp.reg[ry].udspi[2 * i + 1];
            }
            break;
        }
    }
}

void VDSP_HELPER(vand64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] &
                                        env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] &
                                        env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] &
                                        env->vfp.reg[ry].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vand128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] &
                                        env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] &
                                        env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] &
                                        env->vfp.reg[ry].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vandn64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] &
                                        ~env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] &
                                        ~env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] &
                                        ~env->vfp.reg[ry].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vandn128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] &
                                        ~env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] &
                                        ~env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] &
                                        ~env->vfp.reg[ry].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vor64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] |
                                        env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] |
                                        env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] |
                                        env->vfp.reg[ry].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vor128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] |
                                        env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] |
                                        env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] |
                                        env->vfp.reg[ry].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vnor64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = ~(env->vfp.reg[rx].dspc[i] |
                                         env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = ~(env->vfp.reg[rx].dsps[i] |
                                         env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = ~(env->vfp.reg[rx].dspi[i] |
                                         env->vfp.reg[ry].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vnor128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = ~(env->vfp.reg[rx].dspc[i] |
                                         env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = ~(env->vfp.reg[rx].dsps[i] |
                                         env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = ~(env->vfp.reg[rx].dspi[i] |
                                         env->vfp.reg[ry].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vxor64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] ^
                                        env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] ^
                                        env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] ^
                                        env->vfp.reg[ry].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vxor128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] ^
                                        env->vfp.reg[ry].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] ^
                                        env->vfp.reg[ry].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] ^
                                        env->vfp.reg[ry].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vtst64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] &
                                            env->vfp.reg[ry].dspc[i]) ?
                                            0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] &
                                            env->vfp.reg[ry].dsps[i]) ?
                                            0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] &
                                            env->vfp.reg[ry].dspi[i]) ?
                                            0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] &
                                             env->vfp.reg[ry].udspc[i]) ?
                                             0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] &
                                             env->vfp.reg[ry].udsps[i]) ?
                                             0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] &
                                             env->vfp.reg[ry].udspi[i]) ?
                                             0xffffffff : 0x0;
            }
            break;
        }
    }
}

void VDSP_HELPER(vtst128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i] &
                                            env->vfp.reg[ry].dspc[i]) ?
                                            0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i] &
                                            env->vfp.reg[ry].dsps[i]) ?
                                            0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i] &
                                            env->vfp.reg[ry].dspi[i]) ?
                                            0xffffffff : 0x0;
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[i] &
                                             env->vfp.reg[ry].udspc[i]) ?
                                             0xff : 0x0;
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[i] &
                                             env->vfp.reg[ry].udsps[i]) ?
                                             0xffff : 0x0;
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[i] &
                                             env->vfp.reg[ry].udspi[i]) ?
                                             0xffffffff : 0x0;
            }
            break;
        }
    }
}

void VDSP_HELPER(vmov64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vmov128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vmove64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmove128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = (env->vfp.reg[rx].dsps[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udsps[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmovh64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dsps[i] >>
                                            (lng / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspi[i] >>
                                            (lng / 2));
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udsps[i] >>
                                             (lng / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspi[i] >>
                                             (lng / 2));
            }
            break;
        }
    }
}

void VDSP_HELPER(vmovh128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dsps[i] >>
                                            (lng / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspi[i] >>
                                            (lng / 2));
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udsps[i] >>
                                             (lng / 2));
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspi[i] >>
                                             (lng / 2));
            }
            break;
        }
    }
}

void VDSP_HELPER(vmovrh64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dsps[i] >>
                                            (lng / 2));
                if (((env->vfp.reg[rz].dspc[i] >>
                      ((lng / 2) - 1)) & 0x1) == 1) {
                        env->vfp.reg[rz].dspc[i] += 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspi[i] >>
                                            (lng / 2));
                if (((env->vfp.reg[rz].dsps[i] >>
                      ((lng / 2) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dsps[i] += 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] =
                    (env->vfp.reg[rx].udsps[i] >> (lng / 2));

                if (((env->vfp.reg[rz].udspc[i] >>
                      ((lng / 2) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udspc[i] += 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (env->vfp.reg[rx].udspi[i] >> (lng / 2));

                if (((env->vfp.reg[rz].udsps[i] >>
                      ((lng / 2) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udsps[i] += 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vmovrh128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] =
                    (env->vfp.reg[rx].dsps[i] >> (lng / 2));

                if (((env->vfp.reg[rz].dspc[i] >>
                      ((lng / 2) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dspc[i] += 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] =
                    (env->vfp.reg[rx].dspi[i] >> (lng / 2));

                if (((env->vfp.reg[rz].dsps[i] >>
                      ((lng / 2) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].dsps[i] += 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] =
                    (env->vfp.reg[rx].udsps[i] >> (lng / 2));

                if (((env->vfp.reg[rz].udspc[i] >>
                      ((lng / 2) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udspc[i] += 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] =
                    (env->vfp.reg[rx].udspi[i] >> (lng / 2));

                if (((env->vfp.reg[rz].udsps[i] >>
                      ((lng / 2) - 1)) & 0x1) == 1) {
                    env->vfp.reg[rz].udsps[i] += 1;
                }
            }
            break;
        }
    }
}


void VDSP_HELPER(vmovl64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmovl128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = (env->vfp.reg[rx].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = (env->vfp.reg[rx].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vmovsl64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vmovsl128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vstousl64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udsps[i]);
            env->vfp.reg[rz].udspc[i] = tmp;
            if (tmp > 0xff) {
                env->vfp.reg[rz].udspc[i] = 0xff;
            }
            if (tmp < 1) {
                env->vfp.reg[rz].udspc[i] = 1 - 1;
            }
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udspi[i]);
            env->vfp.reg[rz].udsps[i] = tmp;
            if (tmp > 0xffff) {
                env->vfp.reg[rz].udsps[i] = 0xffff;
            }
            if (tmp < 1) {
                env->vfp.reg[rz].udsps[i] = 1 - 1;
            }
        }
        break;
    }
}

void VDSP_HELPER(vstousl128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udsps[i]);
            env->vfp.reg[rz].udspc[i] = tmp;
            if (tmp > 0xff) {
                env->vfp.reg[rz].udspc[i] = 0xff;
            }
            if (tmp < 1) {
                env->vfp.reg[rz].udspc[i] = 1 - 1;
            }
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udspi[i]);
            env->vfp.reg[rz].udsps[i] = tmp;
            if (tmp > 0xffff) {
                env->vfp.reg[rz].udsps[i] = 0xffff;
            }
            if (tmp < 1) {
                env->vfp.reg[rz].udsps[i] = 1 - 1;
            }
        }
        break;
    }
}

void VDSP_HELPER(vrev64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[cnt - i - 1] = (env->vfp.reg[rx].udspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[cnt - i - 1] = (env->vfp.reg[rx].udsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[cnt - i - 1] = (env->vfp.reg[rx].udspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vrev128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[cnt - i - 1] = (env->vfp.reg[rx].udspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[cnt - i - 1] = (env->vfp.reg[rx].udsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[cnt - i - 1] = (env->vfp.reg[rx].udspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vdup64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, rz, immd;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;
    immd = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[immd]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[immd]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[immd]);
        }
        break;
    }
}

void VDSP_HELPER(vdup128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, rz, immd;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;
    immd = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[immd]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[immd]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[immd]);
        }
        break;
    }
}

void VDSP_HELPER(vtrcl64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[2 * i] = (env->vfp.reg[rx].udspc[2 * i]);
            env->vfp.reg[rz].udspc[2 * i + 1] = (env->vfp.reg[ry].udspc[2 * i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[2 * i] = (env->vfp.reg[rx].udsps[2 * i]);
            env->vfp.reg[rz].udsps[2 * i + 1] = (env->vfp.reg[ry].udsps[2 * i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[2 * i] = (env->vfp.reg[rx].udspi[2 * i]);
            env->vfp.reg[rz].udspi[2 * i + 1] = (env->vfp.reg[ry].udspi[2 * i]);
        }
        break;
    }
}

void VDSP_HELPER(vtrcl128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[2 * i] = (env->vfp.reg[rx].udspc[2 * i]);
            env->vfp.reg[rz].udspc[2 * i + 1] = (env->vfp.reg[ry].udspc[2 * i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[2 * i] = (env->vfp.reg[rx].udsps[2 * i]);
            env->vfp.reg[rz].udsps[2 * i + 1] = (env->vfp.reg[ry].udsps[2 * i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[2 * i] = (env->vfp.reg[rx].udspi[2 * i]);
            env->vfp.reg[rz].udspi[2 * i + 1] = (env->vfp.reg[ry].udspi[2 * i]);
        }
        break;
    }
}

void VDSP_HELPER(vtrch64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[2 * i] = (env->vfp.reg[rx].udspc[2 * i + 1]);
            env->vfp.reg[rz].udspc[2 * i + 1] =
                (env->vfp.reg[ry].udspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[2 * i] = (env->vfp.reg[rx].udsps[2 * i + 1]);
            env->vfp.reg[rz].udsps[2 * i + 1] =
                (env->vfp.reg[ry].udsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[2 * i] = (env->vfp.reg[rx].udspi[2 * i + 1]);
            env->vfp.reg[rz].udspi[2 * i + 1] =
                (env->vfp.reg[ry].udspi[2 * i + 1]);
        }
        break;
    }
}

void VDSP_HELPER(vtrch128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[2 * i] = (env->vfp.reg[rx].udspc[2 * i + 1]);
            env->vfp.reg[rz].udspc[2 * i + 1] =
                (env->vfp.reg[ry].udspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[2 * i] = (env->vfp.reg[rx].udsps[2 * i + 1]);
            env->vfp.reg[rz].udsps[2 * i + 1] =
                (env->vfp.reg[ry].udsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[2 * i] = (env->vfp.reg[rx].udspi[2 * i + 1]);
            env->vfp.reg[rz].udspi[2 * i + 1] =
                (env->vfp.reg[ry].udspi[2 * i + 1]);
        }
        break;
    }
}

void VDSP_HELPER(vich64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[2 * i] = (env->vfp.reg[rx].udspc[i + cnt]);
            env->vfp.reg[rz].udspc[2 * i + 1] =
                (env->vfp.reg[ry].udspc[i + cnt]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[2 * i] = (env->vfp.reg[rx].udsps[i + cnt]);
            env->vfp.reg[rz].udsps[2 * i + 1] =
                (env->vfp.reg[ry].udsps[i + cnt]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[2 * i] = (env->vfp.reg[rx].udspi[i + cnt]);
            env->vfp.reg[rz].udspi[2 * i + 1] =
                (env->vfp.reg[ry].udspi[i + cnt]);
        }
        break;
    }
}

void VDSP_HELPER(vich128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[2 * i] = (env->vfp.reg[rx].udspc[i + cnt]);
            env->vfp.reg[rz].udspc[2 * i + 1] =
                (env->vfp.reg[ry].udspc[i + cnt]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[2 * i] = (env->vfp.reg[rx].udsps[i + cnt]);
            env->vfp.reg[rz].udsps[2 * i + 1] =
                (env->vfp.reg[ry].udsps[i + cnt]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[2 * i] = (env->vfp.reg[rx].udspi[i + cnt]);
            env->vfp.reg[rz].udspi[2 * i + 1] =
                (env->vfp.reg[ry].udspi[i + cnt]);
        }
        break;
    }
}

void VDSP_HELPER(vicl64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[2 * i] = (env->vfp.reg[rx].udspc[i]);
            env->vfp.reg[rz].udspc[2 * i + 1] = (env->vfp.reg[ry].udspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[2 * i] = (env->vfp.reg[rx].udsps[i]);
            env->vfp.reg[rz].udsps[2 * i + 1] = (env->vfp.reg[ry].udsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[2 * i] = (env->vfp.reg[rx].udspi[i]);
            env->vfp.reg[rz].udspi[2 * i + 1] = (env->vfp.reg[ry].udspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vicl128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[2 * i] = (env->vfp.reg[rx].udspc[i]);
            env->vfp.reg[rz].udspc[2 * i + 1] = (env->vfp.reg[ry].udspc[i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[2 * i] = (env->vfp.reg[rx].udsps[i]);
            env->vfp.reg[rz].udsps[2 * i + 1] = (env->vfp.reg[ry].udsps[i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[2 * i] = (env->vfp.reg[rx].udspi[i]);
            env->vfp.reg[rz].udspi[2 * i + 1] = (env->vfp.reg[ry].udspi[i]);
        }
        break;
    }
}

void VDSP_HELPER(vdch64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[2 * i + 1]);
            env->vfp.reg[rz].udspc[i + cnt] =
                (env->vfp.reg[ry].udspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[2 * i + 1]);
            env->vfp.reg[rz].udsps[i + cnt] =
                (env->vfp.reg[ry].udsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[2 * i + 1]);
            env->vfp.reg[rz].udspi[i + cnt] =
                (env->vfp.reg[ry].udspi[2 * i + 1]);
        }
        break;
    }
}

void VDSP_HELPER(vdch128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[2 * i + 1]);
            env->vfp.reg[rz].udspc[i + cnt] =
                (env->vfp.reg[ry].udspc[2 * i + 1]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[2 * i + 1]);
            env->vfp.reg[rz].udsps[i + cnt] =
                (env->vfp.reg[ry].udsps[2 * i + 1]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[2 * i + 1]);
            env->vfp.reg[rz].udspi[i + cnt] =
                (env->vfp.reg[ry].udspi[2 * i + 1]);
        }
        break;
    }
}

void VDSP_HELPER(vdcl64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[2 * i]);
            env->vfp.reg[rz].udspc[i + cnt] = (env->vfp.reg[ry].udspc[2 * i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[2 * i]);
            env->vfp.reg[rz].udsps[i + cnt] = (env->vfp.reg[ry].udsps[2 * i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[2 * i]);
            env->vfp.reg[rz].udspi[i + cnt] = (env->vfp.reg[ry].udspi[2 * i]);
        }
        break;
    }
}

void VDSP_HELPER(vdcl128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng / 2;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspc[i] = (env->vfp.reg[rx].udspc[2 * i]);
            env->vfp.reg[rz].udspc[i + cnt] = (env->vfp.reg[ry].udspc[2 * i]);
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udsps[i] = (env->vfp.reg[rx].udsps[2 * i]);
            env->vfp.reg[rz].udsps[i + cnt] = (env->vfp.reg[ry].udsps[2 * i]);
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            env->vfp.reg[rz].udspi[i] = (env->vfp.reg[rx].udspi[2 * i]);
            env->vfp.reg[rz].udspi[i + cnt] = (env->vfp.reg[ry].udspi[2 * i]);
        }
        break;
    }
}

void VDSP_HELPER(vabs64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = abs(env->vfp.reg[rx].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = abs(env->vfp.reg[rx].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = abs(env->vfp.reg[rx].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = env->vfp.reg[rx].udspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = env->vfp.reg[rx].udsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = env->vfp.reg[rx].udspi[i];
            }
            break;
        }
    }
}

void VDSP_HELPER(vabs128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = abs(env->vfp.reg[rx].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = abs(env->vfp.reg[rx].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = abs(env->vfp.reg[rx].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = env->vfp.reg[rx].udspc[i];
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = env->vfp.reg[rx].udsps[i];
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = env->vfp.reg[rx].udspi[i];
            }
            break;
        }
    }
}

void VDSP_HELPER(vneg64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = -(env->vfp.reg[rx].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = -(env->vfp.reg[rx].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = -(env->vfp.reg[rx].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = -(env->vfp.reg[rx].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = -(env->vfp.reg[rx].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = -(env->vfp.reg[rx].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vneg128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspc[i] = -(env->vfp.reg[rx].dspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dsps[i] = -(env->vfp.reg[rx].dsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].dspi[i] = -(env->vfp.reg[rx].dspi[i]);
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspc[i] = -(env->vfp.reg[rx].udspc[i]);
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udsps[i] = -(env->vfp.reg[rx].udsps[i]);
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                env->vfp.reg[rz].udspi[i] = -(env->vfp.reg[rx].udspi[i]);
            }
            break;
        }
    }
}

void VDSP_HELPER(vabss64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspc[i]);
                tmp = labs(tmp);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = labs(tmp);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = labs(tmp);
                env->vfp.reg[rz].dspi[i] = tmp;
                if (tmp > 0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = 0x7fffffff;
                }
                if (tmp < -0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = -0x7fffffff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspc[i]);
                tmp = labs(tmp);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = labs(tmp);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = labs(tmp);
                env->vfp.reg[rz].udspi[i] = tmp;
                if (tmp > 0xffffffff) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspi[i] = 1 - 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vabss128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspc[i]);
                tmp = labs(tmp);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = labs(tmp);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = labs(tmp);
                env->vfp.reg[rz].dspi[i] = tmp;
                if (tmp > 0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = 0x7fffffff;
                }
                if (tmp < -0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = -0x7fffffff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspc[i]);
                tmp = labs(tmp);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = labs(tmp);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = labs(tmp);
                env->vfp.reg[rz].udspi[i] = tmp;
                if (tmp > 0xffffffff) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspi[i] = 1 - 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vnegs64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspc[i]);
                tmp = -(tmp);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = -(tmp);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = -(tmp);
                env->vfp.reg[rz].dspi[i] = tmp;
                if (tmp > 0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = 0x7fffffff;
                }
                if (tmp < -0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = -0x7fffffff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspc[i]);
                tmp = -(tmp);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 1 - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = -(tmp);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 1 - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = -(tmp);
                env->vfp.reg[rz].udspi[i] = tmp;
                if (tmp > 0xffffffff) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspi[i] = 1 - 1;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vnegs128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    int64_t tmp;
    uint32_t wid, lng, sign, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    sign = (insn >> CSKY_VDSP_SIGN_SHI) & CSKY_VDSP_SIGN_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    if (sign) {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspc[i]);
                tmp = -(tmp);
                env->vfp.reg[rz].dspc[i] = tmp;
                if (tmp > 0x7f) {
                    env->vfp.reg[rz].dspc[i] = 0x7f;
                }
                if (tmp < -0x7f) {
                    env->vfp.reg[rz].dspc[i] = -0x7f - 1;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dsps[i]);
                tmp = -(tmp);
                env->vfp.reg[rz].dsps[i] = tmp;
                if (tmp > 0x7fff) {
                    env->vfp.reg[rz].dsps[i] = 0x7fff;
                }
                if (tmp < -0x7fff) {
                    env->vfp.reg[rz].dsps[i] = -0x7fff - 1;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].dspi[i]);
                tmp = -(tmp);
                env->vfp.reg[rz].dspi[i] = tmp;
                if (tmp > 0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = 0x7fffffff;
                }
                if (tmp < -0x7fffffff) {
                    env->vfp.reg[rz].dspi[i] = -0x7fffffff - 1;
                }
            }
            break;
        }
    } else {
        switch (lng) {
        case 8:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspc[i]);
                tmp = -(tmp);
                env->vfp.reg[rz].udspc[i] = tmp;
                if (tmp > 0xff) {
                    env->vfp.reg[rz].udspc[i] = 0xff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspc[i] = 0;
                }
            }
            break;
        case 16:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udsps[i]);
                tmp = -(tmp);
                env->vfp.reg[rz].udsps[i] = tmp;
                if (tmp > 0xffff) {
                    env->vfp.reg[rz].udsps[i] = 0xffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udsps[i] = 0;
                }
            }
            break;
        case 32:
            for (i = 0; i < cnt; i++) {
                tmp = (env->vfp.reg[rx].udspi[i]);
                tmp = -(tmp);
                env->vfp.reg[rz].udspi[i] = tmp;
                if (tmp > 0xffffffff) {
                    env->vfp.reg[rz].udspi[i] = 0xffffffff;
                }
                if (tmp < 1) {
                    env->vfp.reg[rz].udspi[i] = 0;
                }
            }
            break;
        }
    }
}

void VDSP_HELPER(vmfvru8)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t immd, rx, rz;

    immd = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    env->regs[rz] = env->vfp.reg[rx].udspc[immd];
}

void VDSP_HELPER(vmfvru16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t immd, rx, rz;

    immd = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    env->regs[rz] = env->vfp.reg[rx].udsps[immd];
}

void VDSP_HELPER(vmfvru32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t immd, rx, rz;

    immd = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    env->regs[rz] = env->vfp.reg[rx].udspi[immd];
}

void VDSP_HELPER(vmfvrs8)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t immd, rx, rz;

    immd = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    env->regs[rz] = env->vfp.reg[rx].dspc[immd];
}

void VDSP_HELPER(vmfvrs16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t immd, rx, rz;

    immd = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    env->regs[rz] = env->vfp.reg[rx].dsps[immd];
}

void VDSP_HELPER(vmtvru8)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t immd, rx, rz;

    immd = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    env->vfp.reg[rz].udspc[immd] = env->regs[rx];
}

void VDSP_HELPER(vmtvru16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t immd, rx, rz;

    immd = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    env->vfp.reg[rz].udsps[immd] = env->regs[rx];
}

void VDSP_HELPER(vmtvru32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t immd, rx, rz;

    immd = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    env->vfp.reg[rz].udspi[immd] = env->regs[rx];
}

void VDSP_HELPER(vins8)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, rz, immd, immdz;

    immd = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    immdz = (insn >> CSKY_VDSP_SOP_SHI_S) & CSKY_VDSP_REG_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    env->vfp.reg[rz].udspc[immdz] = (env->vfp.reg[rx].udspc[immd]);
}

void VDSP_HELPER(vins16)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, rz, immd, immdz;

    immd = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    immdz = (insn >> CSKY_VDSP_SOP_SHI_S) & CSKY_VDSP_REG_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    env->vfp.reg[rz].udsps[immdz] = (env->vfp.reg[rx].udsps[immd]);
}

void VDSP_HELPER(vins32)(CPUCSKYState *env, uint32_t insn)
{
    uint32_t rx, rz, immd, immdz;

    immd = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    immdz = (insn >> CSKY_VDSP_SOP_SHI_S) & CSKY_VDSP_REG_MASK;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    env->vfp.reg[rz].udspi[immdz] = (env->vfp.reg[rx].udspi[immd]);
}

void VDSP_HELPER(vcnt164)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i, j;
    int64_t tmp;
    uint32_t wid, lng, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    for (i = 0; i < cnt; i++) {
        tmp = env->vfp.reg[rx].udspc[i];
        for (j = 0; tmp > 0x0; ++j) {
            tmp &= (tmp - 1);
        }
        env->vfp.reg[rz].udspc[i] = j;
    }
}

void VDSP_HELPER(vcnt1128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i, j;
    int64_t tmp;
    uint32_t wid, lng, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    for (i = 0; i < cnt; i++) {
        tmp = env->vfp.reg[rx].udspc[i];
        for (j = 0; tmp > 0x0; ++j) {
            tmp &= (tmp - 1);
        }
        env->vfp.reg[rz].udspc[i] = j;
    }
}

void VDSP_HELPER(vbperm64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    for (i = 0; i < cnt; i++) {
        if (env->vfp.reg[ry].udspc[i] < 16) {
            env->vfp.reg[rz].udspc[i] =
                env->vfp.reg[rx].udspc[env->vfp.reg[ry].udspc[i]];
        } else {
            env->vfp.reg[rz].udspc[i] = 0xff;
        }
    }

}

void VDSP_HELPER(vbperm128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    for (i = 0; i < cnt; i++) {
        if (env->vfp.reg[ry].udspc[i] < 16) {
            env->vfp.reg[rz].udspc[i] =
                env->vfp.reg[rx].udspc[env->vfp.reg[ry].udspc[i]];
        } else {
            env->vfp.reg[rz].udspc[i] = 0xff;
        }
    }
}

void VDSP_HELPER(vbpermz64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    for (i = 0; i < cnt; i++) {
        if (env->vfp.reg[ry].udspc[i] < 16) {
            env->vfp.reg[rz].udspc[i] =
                env->vfp.reg[rx].udspc[env->vfp.reg[ry].udspc[i]];
        } else {
            env->vfp.reg[rz].udspc[i] = 0x0;
        }
    }

}

void VDSP_HELPER(vbpermz128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i;
    uint32_t wid, lng, rx, ry, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    ry = (insn >> CSKY_VDSP_REG_SHI_VRY) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    for (i = 0; i < cnt; i++) {
        if (env->vfp.reg[ry].udspc[i] < 16) {
            env->vfp.reg[rz].udspc[i] =
                env->vfp.reg[rx].udspc[env->vfp.reg[ry].udspc[i]];
        } else {
            env->vfp.reg[rz].udspc[i] = 0x0;
        }
    }
}

void VDSP_HELPER(vcls64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i, c;
    int64_t tmp;
    uint32_t wid, lng, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udspc[i] & 0x7f);
            for (c = 0; tmp < 0x40; tmp <<= 1) {
                c++;
            }
            env->vfp.reg[rz].udspc[i] = c;
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udsps[i] & 0x7fff);
            for (c = 0; tmp < 0x4000; tmp <<= 1) {
                c++;
            }
            env->vfp.reg[rz].udsps[i] = c;
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udspi[i] & 0x7fffffff);
            for (c = 0; tmp < 0x40000000; tmp <<= 1) {
                c++;
            }
            env->vfp.reg[rz].udspi[i] = c;
        }
        break;
    }
}

void VDSP_HELPER(vcls128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i, c;
    int64_t tmp;
    uint32_t wid, lng, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udspc[i] & 0x7f);
            for (c = 0; tmp < 0x40; tmp <<= 1) {
                c++;
            }
            env->vfp.reg[rz].udspc[i] = c;
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udsps[i] & 0x7fff);
            for (c = 0; tmp < 0x4000; tmp <<= 1) {
                c++;
            }
            env->vfp.reg[rz].udsps[i] = c;
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udspi[i] & 0x7fffffff);
            for (c = 0; tmp < 0x40000000; tmp <<= 1) {
                c++;
            }
            env->vfp.reg[rz].udspi[i] = c;
        }
        break;
    }
}

void VDSP_HELPER(vclz64)(CPUCSKYState *env, uint32_t insn)
{
    int cnt;
    int i, c;
    int64_t tmp;
    uint32_t wid, lng, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 64 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udspc[i]);
            for (c = 0; tmp < 0x80; tmp <<= 1) {
                c++;
            }
            env->vfp.reg[rz].udspc[i] = c;
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udsps[i]);
            for (c = 0; tmp < 0x8000; tmp <<= 1) {
                c++;
            }
            env->vfp.reg[rz].udsps[i] = c;
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udspi[i]);
            for (c = 0; tmp < 0x80000000; tmp <<= 1) {
                c++;
            }
            env->vfp.reg[rz].udspi[i] = c;
        }
        break;
    }
}

void VDSP_HELPER(vclz128)(CPUCSKYState *env, uint32_t insn)
{
    int cnt, i, c;
    int64_t tmp;
    uint32_t wid, lng, rx, rz;

    wid = ((insn >> CSKY_VDSP_WIDTH_BIT_HI & 0x2) |
           (insn >> CSKY_VDSP_WIDTH_BIT_LO & 0x1));
    lng = 8 * pow(2, wid);
    cnt = 128 / lng;
    rx = (insn >> CSKY_VDSP_REG_SHI_VRX) & CSKY_VDSP_REG_MASK;
    rz = insn & CSKY_VDSP_REG_MASK;

    switch (lng) {
    case 8:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udspc[i]);
            for (c = 0; tmp < 0x80; tmp <<= 1) {
                c++;
            }
            env->vfp.reg[rz].udspc[i] = c;
        }
        break;
    case 16:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udsps[i]);
            for (c = 0; tmp < 0x8000; tmp <<= 1) {
                c++;
            }
            env->vfp.reg[rz].udsps[i] = c;
        }
        break;
    case 32:
        for (i = 0; i < cnt; i++) {
            tmp = (env->vfp.reg[rx].udspi[i]);
            for (c = 0; tmp < 0x80000000; tmp <<= 1) {
                c++;
            }
            env->vfp.reg[rz].udspi[i] = c;
        }
        break;
    }
}
