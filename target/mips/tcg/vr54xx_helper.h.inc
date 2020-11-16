/*
 * MIPS NEC Vr54xx instruction emulation helpers for QEMU.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

DEF_HELPER_3(muls, tl, env, tl, tl)
DEF_HELPER_3(mulsu, tl, env, tl, tl)
DEF_HELPER_3(macc, tl, env, tl, tl)
DEF_HELPER_3(maccu, tl, env, tl, tl)
DEF_HELPER_3(msac, tl, env, tl, tl)
DEF_HELPER_3(msacu, tl, env, tl, tl)
DEF_HELPER_3(mulhi, tl, env, tl, tl)
DEF_HELPER_3(mulhiu, tl, env, tl, tl)
DEF_HELPER_3(mulshi, tl, env, tl, tl)
DEF_HELPER_3(mulshiu, tl, env, tl, tl)
DEF_HELPER_3(macchi, tl, env, tl, tl)
DEF_HELPER_3(macchiu, tl, env, tl, tl)
DEF_HELPER_3(msachi, tl, env, tl, tl)
DEF_HELPER_3(msachiu, tl, env, tl, tl)
