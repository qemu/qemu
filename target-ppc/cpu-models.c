/*
 *  PowerPC CPU initialization for qemu.
 *
 *  Copyright (c) 2003-2007 Jocelyn Mayer
 *  Copyright 2011 Freescale Semiconductor, Inc.
 *  Copyright 2013 SUSE LINUX Products GmbH
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

/* A lot of PowerPC definition have been included here.
 * Most of them are not usable for now but have been kept
 * inside "#if defined(TODO) ... #endif" statements to make tests easier.
 */

#include "cpu.h"
#include "cpu-models.h"

#if defined(CONFIG_USER_ONLY)
#define TODO_USER_ONLY 1
#endif

/***************************************************************************/
/* PowerPC CPU definitions                                                 */
#define POWERPC_DEF_PREFIX(pvr, svr, type)                                  \
    glue(glue(glue(glue(pvr, _), svr), _), type)
#define POWERPC_DEF_SVR(_name, _desc, _pvr, _svr, _type)                    \
    static void                                                             \
    glue(POWERPC_DEF_PREFIX(_pvr, _svr, _type), _cpu_class_init)            \
    (ObjectClass *oc, void *data)                                           \
    {                                                                       \
        DeviceClass *dc = DEVICE_CLASS(oc);                                 \
        PowerPCCPUClass *pcc = POWERPC_CPU_CLASS(oc);                       \
                                                                            \
        pcc->pvr          = _pvr;                                           \
        pcc->svr          = _svr;                                           \
        dc->desc          = _desc;                                          \
    }                                                                       \
                                                                            \
    static const TypeInfo                                                   \
    glue(POWERPC_DEF_PREFIX(_pvr, _svr, _type), _cpu_type_info) = {         \
        .name       = _name "-" TYPE_POWERPC_CPU,                           \
        .parent     = stringify(_type) "-family-" TYPE_POWERPC_CPU,         \
        .class_init =                                                       \
            glue(POWERPC_DEF_PREFIX(_pvr, _svr, _type), _cpu_class_init),   \
    };                                                                      \
                                                                            \
    static void                                                             \
    glue(POWERPC_DEF_PREFIX(_pvr, _svr, _type), _cpu_register_types)(void)  \
    {                                                                       \
        type_register_static(                                               \
            &glue(POWERPC_DEF_PREFIX(_pvr, _svr, _type), _cpu_type_info));  \
    }                                                                       \
                                                                            \
    type_init(                                                              \
        glue(POWERPC_DEF_PREFIX(_pvr, _svr, _type), _cpu_register_types))

#define POWERPC_DEF(_name, _pvr, _type, _desc)                              \
    POWERPC_DEF_SVR(_name, _desc, _pvr, POWERPC_SVR_NONE, _type)

    /* Embedded PowerPC                                                      */
    /* PowerPC 401 family                                                    */
    POWERPC_DEF("401",           CPU_POWERPC_401,                    401,
                "Generic PowerPC 401")
    /* PowerPC 401 cores                                                     */
    POWERPC_DEF("401A1",         CPU_POWERPC_401A1,                  401,
                "PowerPC 401A1")
    POWERPC_DEF("401B2",         CPU_POWERPC_401B2,                  401x2,
                "PowerPC 401B2")
#if defined(TODO)
    POWERPC_DEF("401B3",         CPU_POWERPC_401B3,                  401x3,
                "PowerPC 401B3")
#endif
    POWERPC_DEF("401C2",         CPU_POWERPC_401C2,                  401x2,
                "PowerPC 401C2")
    POWERPC_DEF("401D2",         CPU_POWERPC_401D2,                  401x2,
                "PowerPC 401D2")
    POWERPC_DEF("401E2",         CPU_POWERPC_401E2,                  401x2,
                "PowerPC 401E2")
    POWERPC_DEF("401F2",         CPU_POWERPC_401F2,                  401x2,
                "PowerPC 401F2")
    /* XXX: to be checked */
    POWERPC_DEF("401G2",         CPU_POWERPC_401G2,                  401x2,
                "PowerPC 401G2")
    /* PowerPC 401 microcontrollers                                          */
#if defined(TODO)
    POWERPC_DEF("401GF",         CPU_POWERPC_401GF,                  401,
                "PowerPC 401GF")
#endif
    POWERPC_DEF("IOP480",        CPU_POWERPC_IOP480,                 IOP480,
                "IOP480 (401 microcontroller)")
    POWERPC_DEF("Cobra",         CPU_POWERPC_COBRA,                  401,
                "IBM Processor for Network Resources")
#if defined(TODO)
    POWERPC_DEF("Xipchip",       CPU_POWERPC_XIPCHIP,                401,
                NULL)
#endif
    /* PowerPC 403 family                                                    */
    /* PowerPC 403 microcontrollers                                          */
    POWERPC_DEF("403GA",         CPU_POWERPC_403GA,                  403,
                "PowerPC 403 GA")
    POWERPC_DEF("403GB",         CPU_POWERPC_403GB,                  403,
                "PowerPC 403 GB")
    POWERPC_DEF("403GC",         CPU_POWERPC_403GC,                  403,
                "PowerPC 403 GC")
    POWERPC_DEF("403GCX",        CPU_POWERPC_403GCX,                 403GCX,
                "PowerPC 403 GCX")
#if defined(TODO)
    POWERPC_DEF("403GP",         CPU_POWERPC_403GP,                  403,
                "PowerPC 403 GP")
#endif
    /* PowerPC 405 family                                                    */
    /* PowerPC 405 cores                                                     */
#if defined(TODO)
    POWERPC_DEF("405A3",         CPU_POWERPC_405A3,                  405,
                "PowerPC 405 A3")
#endif
#if defined(TODO)
    POWERPC_DEF("405A4",         CPU_POWERPC_405A4,                  405,
                "PowerPC 405 A4")
#endif
#if defined(TODO)
    POWERPC_DEF("405B3",         CPU_POWERPC_405B3,                  405,
                "PowerPC 405 B3")
#endif
#if defined(TODO)
    POWERPC_DEF("405B4",         CPU_POWERPC_405B4,                  405,
                "PowerPC 405 B4")
#endif
#if defined(TODO)
    POWERPC_DEF("405C3",         CPU_POWERPC_405C3,                  405,
                "PowerPC 405 C3")
#endif
#if defined(TODO)
    POWERPC_DEF("405C4",         CPU_POWERPC_405C4,                  405,
                "PowerPC 405 C4")
#endif
    POWERPC_DEF("405D2",         CPU_POWERPC_405D2,                  405,
                "PowerPC 405 D2")
#if defined(TODO)
    POWERPC_DEF("405D3",         CPU_POWERPC_405D3,                  405,
                "PowerPC 405 D3")
#endif
    POWERPC_DEF("405D4",         CPU_POWERPC_405D4,                  405,
                "PowerPC 405 D4")
#if defined(TODO)
    POWERPC_DEF("405D5",         CPU_POWERPC_405D5,                  405,
                "PowerPC 405 D5")
#endif
#if defined(TODO)
    POWERPC_DEF("405E4",         CPU_POWERPC_405E4,                  405,
                "PowerPC 405 E4")
#endif
#if defined(TODO)
    POWERPC_DEF("405F4",         CPU_POWERPC_405F4,                  405,
                "PowerPC 405 F4")
#endif
#if defined(TODO)
    POWERPC_DEF("405F5",         CPU_POWERPC_405F5,                  405,
                "PowerPC 405 F5")
#endif
#if defined(TODO)
    POWERPC_DEF("405F6",         CPU_POWERPC_405F6,                  405,
                "PowerPC 405 F6")
#endif
    /* PowerPC 405 microcontrollers                                          */
    POWERPC_DEF("405CRa",        CPU_POWERPC_405CRa,                 405,
                "PowerPC 405 CRa")
    POWERPC_DEF("405CRb",        CPU_POWERPC_405CRb,                 405,
                "PowerPC 405 CRb")
    POWERPC_DEF("405CRc",        CPU_POWERPC_405CRc,                 405,
                "PowerPC 405 CRc")
    POWERPC_DEF("405EP",         CPU_POWERPC_405EP,                  405,
                "PowerPC 405 EP")
#if defined(TODO)
    POWERPC_DEF("405EXr",        CPU_POWERPC_405EXr,                 405,
                "PowerPC 405 EXr")
#endif
    POWERPC_DEF("405EZ",         CPU_POWERPC_405EZ,                  405,
                "PowerPC 405 EZ")
#if defined(TODO)
    POWERPC_DEF("405FX",         CPU_POWERPC_405FX,                  405,
                "PowerPC 405 FX")
#endif
    POWERPC_DEF("405GPa",        CPU_POWERPC_405GPa,                 405,
                "PowerPC 405 GPa")
    POWERPC_DEF("405GPb",        CPU_POWERPC_405GPb,                 405,
                "PowerPC 405 GPb")
    POWERPC_DEF("405GPc",        CPU_POWERPC_405GPc,                 405,
                "PowerPC 405 GPc")
    POWERPC_DEF("405GPd",        CPU_POWERPC_405GPd,                 405,
                "PowerPC 405 GPd")
    POWERPC_DEF("405GPR",        CPU_POWERPC_405GPR,                 405,
                "PowerPC 405 GPR")
#if defined(TODO)
    POWERPC_DEF("405H",          CPU_POWERPC_405H,                   405,
                "PowerPC 405 H")
#endif
#if defined(TODO)
    POWERPC_DEF("405L",          CPU_POWERPC_405L,                   405,
                "PowerPC 405 L")
#endif
    POWERPC_DEF("405LP",         CPU_POWERPC_405LP,                  405,
                "PowerPC 405 LP")
#if defined(TODO)
    POWERPC_DEF("405PM",         CPU_POWERPC_405PM,                  405,
                "PowerPC 405 PM")
#endif
#if defined(TODO)
    POWERPC_DEF("405PS",         CPU_POWERPC_405PS,                  405,
                "PowerPC 405 PS")
#endif
#if defined(TODO)
    POWERPC_DEF("405S",          CPU_POWERPC_405S,                   405,
                "PowerPC 405 S")
#endif
    POWERPC_DEF("Npe405H",       CPU_POWERPC_NPE405H,                405,
                "Npe405 H")
    POWERPC_DEF("Npe405H2",      CPU_POWERPC_NPE405H2,               405,
                "Npe405 H2")
    POWERPC_DEF("Npe405L",       CPU_POWERPC_NPE405L,                405,
                "Npe405 L")
    POWERPC_DEF("Npe4GS3",       CPU_POWERPC_NPE4GS3,                405,
                "Npe4GS3")
#if defined(TODO)
    POWERPC_DEF("Npcxx1",        CPU_POWERPC_NPCxx1,                 405,
                NULL)
#endif
#if defined(TODO)
    POWERPC_DEF("Npr161",        CPU_POWERPC_NPR161,                 405,
                NULL)
#endif
#if defined(TODO)
    POWERPC_DEF("LC77700",       CPU_POWERPC_LC77700,                405,
                "PowerPC LC77700 (Sanyo)")
#endif
    /* PowerPC 401/403/405 based set-top-box microcontrollers                */
#if defined(TODO)
    POWERPC_DEF("STB01000",      CPU_POWERPC_STB01000,               401x2,
                "STB010000")
#endif
#if defined(TODO)
    POWERPC_DEF("STB01010",      CPU_POWERPC_STB01010,               401x2,
                "STB01010")
#endif
#if defined(TODO)
    POWERPC_DEF("STB0210",       CPU_POWERPC_STB0210,                401x3,
                "STB0210")
#endif
    POWERPC_DEF("STB03",         CPU_POWERPC_STB03,                  405,
                "STB03xx")
#if defined(TODO)
    POWERPC_DEF("STB043",        CPU_POWERPC_STB043,                 405,
                "STB043x")
#endif
#if defined(TODO)
    POWERPC_DEF("STB045",        CPU_POWERPC_STB045,                 405,
                "STB045x")
#endif
    POWERPC_DEF("STB04",         CPU_POWERPC_STB04,                  405,
                "STB04xx")
    POWERPC_DEF("STB25",         CPU_POWERPC_STB25,                  405,
                "STB25xx")
#if defined(TODO)
    POWERPC_DEF("STB130",        CPU_POWERPC_STB130,                 405,
                "STB130")
#endif
    /* Xilinx PowerPC 405 cores                                              */
    POWERPC_DEF("x2vp4",         CPU_POWERPC_X2VP4,                  405,
                NULL)
    POWERPC_DEF("x2vp20",        CPU_POWERPC_X2VP20,                 405,
                NULL)
#if defined(TODO)
    POWERPC_DEF("zl10310",       CPU_POWERPC_ZL10310,                405,
                "Zarlink ZL10310")
#endif
#if defined(TODO)
    POWERPC_DEF("zl10311",       CPU_POWERPC_ZL10311,                405,
                "Zarlink ZL10311")
#endif
#if defined(TODO)
    POWERPC_DEF("zl10320",       CPU_POWERPC_ZL10320,                405,
                "Zarlink ZL10320")
#endif
#if defined(TODO)
    POWERPC_DEF("zl10321",       CPU_POWERPC_ZL10321,                405,
                "Zarlink ZL10321")
#endif
    /* PowerPC 440 family                                                    */
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440",           CPU_POWERPC_440,                    440GP,
                "Generic PowerPC 440")
#endif
    /* PowerPC 440 cores                                                     */
#if defined(TODO)
    POWERPC_DEF("440A4",         CPU_POWERPC_440A4,                  440x4,
                "PowerPC 440 A4")
#endif
    POWERPC_DEF("440-Xilinx",    CPU_POWERPC_440_XILINX,             440x5,
                "PowerPC 440 Xilinx 5")
#if defined(TODO)
    POWERPC_DEF("440A5",         CPU_POWERPC_440A5,                  440x5,
                "PowerPC 440 A5")
#endif
#if defined(TODO)
    POWERPC_DEF("440B4",         CPU_POWERPC_440B4,                  440x4,
                "PowerPC 440 B4")
#endif
#if defined(TODO)
    POWERPC_DEF("440G4",         CPU_POWERPC_440G4,                  440x4,
                "PowerPC 440 G4")
#endif
#if defined(TODO)
    POWERPC_DEF("440F5",         CPU_POWERPC_440F5,                  440x5,
                "PowerPC 440 F5")
#endif
#if defined(TODO)
    POWERPC_DEF("440G5",         CPU_POWERPC_440G5,                  440x5,
                "PowerPC 440 G5")
#endif
#if defined(TODO)
    POWERPC_DEF("440H4",         CPU_POWERPC_440H4,                  440x4,
                "PowerPC 440H4")
#endif
#if defined(TODO)
    POWERPC_DEF("440H6",         CPU_POWERPC_440H6,                  440Gx5,
                "PowerPC 440H6")
#endif
    /* PowerPC 440 microcontrollers                                          */
    POWERPC_DEF("440EPa",        CPU_POWERPC_440EPa,                 440EP,
                "PowerPC 440 EPa")
    POWERPC_DEF("440EPb",        CPU_POWERPC_440EPb,                 440EP,
                "PowerPC 440 EPb")
    POWERPC_DEF("440EPX",        CPU_POWERPC_440EPX,                 440EP,
                "PowerPC 440 EPX")
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440GPb",        CPU_POWERPC_440GPb,                 440GP,
                "PowerPC 440 GPb")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440GPc",        CPU_POWERPC_440GPc,                 440GP,
                "PowerPC 440 GPc")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440GRa",        CPU_POWERPC_440GRa,                 440x5,
                "PowerPC 440 GRa")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440GRX",        CPU_POWERPC_440GRX,                 440x5,
                "PowerPC 440 GRX")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440GXa",        CPU_POWERPC_440GXa,                 440EP,
                "PowerPC 440 GXa")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440GXb",        CPU_POWERPC_440GXb,                 440EP,
                "PowerPC 440 GXb")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440GXc",        CPU_POWERPC_440GXc,                 440EP,
                "PowerPC 440 GXc")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440GXf",        CPU_POWERPC_440GXf,                 440EP,
                "PowerPC 440 GXf")
#endif
#if defined(TODO)
    POWERPC_DEF("440S",          CPU_POWERPC_440S,                   440,
                "PowerPC 440 S")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440SP",         CPU_POWERPC_440SP,                  440EP,
                "PowerPC 440 SP")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440SP2",        CPU_POWERPC_440SP2,                 440EP,
                "PowerPC 440 SP2")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440SPE",        CPU_POWERPC_440SPE,                 440EP,
                "PowerPC 440 SPE")
#endif
    /* PowerPC 460 family                                                    */
#if defined(TODO)
    POWERPC_DEF("464",           CPU_POWERPC_464,                    460,
                "Generic PowerPC 464")
#endif
    /* PowerPC 464 microcontrollers                                          */
#if defined(TODO)
    POWERPC_DEF("464H90",        CPU_POWERPC_464H90,                 460,
                "PowerPC 464H90")
#endif
#if defined(TODO)
    POWERPC_DEF("464H90F",       CPU_POWERPC_464H90F,                460F,
                "PowerPC 464H90F")
#endif
    /* Freescale embedded PowerPC cores                                      */
    /* MPC5xx family (aka RCPU)                                              */
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("MPC5xx",        CPU_POWERPC_MPC5xx,                 MPC5xx,
                "Generic MPC5xx core")
#endif
    /* MPC8xx family (aka PowerQUICC)                                        */
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("MPC8xx",        CPU_POWERPC_MPC8xx,                 MPC8xx,
                "Generic MPC8xx core")
#endif
    /* MPC82xx family (aka PowerQUICC-II)                                    */
    POWERPC_DEF("G2",            CPU_POWERPC_G2,                     G2,
                "PowerPC G2 core")
    POWERPC_DEF("G2H4",          CPU_POWERPC_G2H4,                   G2,
                "PowerPC G2 H4 core")
    POWERPC_DEF("G2GP",          CPU_POWERPC_G2gp,                   G2,
                "PowerPC G2 GP core")
    POWERPC_DEF("G2LS",          CPU_POWERPC_G2ls,                   G2,
                "PowerPC G2 LS core")
    POWERPC_DEF("G2HiP3",        CPU_POWERPC_G2_HIP3,                G2,
                "PowerPC G2 HiP3 core")
    POWERPC_DEF("G2HiP4",        CPU_POWERPC_G2_HIP4,                G2,
                "PowerPC G2 HiP4 core")
    POWERPC_DEF("MPC603",        CPU_POWERPC_MPC603,                 603E,
                "PowerPC MPC603 core")
    POWERPC_DEF("G2le",          CPU_POWERPC_G2LE,                   G2LE,
        "PowerPC G2le core (same as G2 plus little-endian mode support)")
    POWERPC_DEF("G2leGP",        CPU_POWERPC_G2LEgp,                 G2LE,
                "PowerPC G2LE GP core")
    POWERPC_DEF("G2leLS",        CPU_POWERPC_G2LEls,                 G2LE,
                "PowerPC G2LE LS core")
    POWERPC_DEF("G2leGP1",       CPU_POWERPC_G2LEgp1,                G2LE,
                "PowerPC G2LE GP1 core")
    POWERPC_DEF("G2leGP3",       CPU_POWERPC_G2LEgp3,                G2LE,
                "PowerPC G2LE GP3 core")
    /* PowerPC G2 microcontrollers                                           */
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5121", "MPC5121",
                    CPU_POWERPC_MPC5121,      POWERPC_SVR_5121,      G2LE)
#endif
    POWERPC_DEF_SVR("MPC5200_v10", "MPC5200 v1.0",
                    CPU_POWERPC_MPC5200_v10,  POWERPC_SVR_5200_v10,  G2LE)
    POWERPC_DEF_SVR("MPC5200_v11", "MPC5200 v1.1",
                    CPU_POWERPC_MPC5200_v11,  POWERPC_SVR_5200_v11,  G2LE)
    POWERPC_DEF_SVR("MPC5200_v12", "MPC5200 v1.2",
                    CPU_POWERPC_MPC5200_v12,  POWERPC_SVR_5200_v12,  G2LE)
    POWERPC_DEF_SVR("MPC5200B_v20", "MPC5200B v2.0",
                    CPU_POWERPC_MPC5200B_v20, POWERPC_SVR_5200B_v20, G2LE)
    POWERPC_DEF_SVR("MPC5200B_v21", "MPC5200B v2.1",
                    CPU_POWERPC_MPC5200B_v21, POWERPC_SVR_5200B_v21, G2LE)
    /* e200 family                                                           */
#if defined(TODO)
    POWERPC_DEF_SVR("MPC55xx", "Generic MPC55xx core",
                    CPU_POWERPC_MPC55xx,      POWERPC_SVR_55xx,      e200)
#endif
#if defined(TODO)
    POWERPC_DEF("e200z0",        CPU_POWERPC_e200z0,                 e200,
                "PowerPC e200z0 core")
#endif
#if defined(TODO)
    POWERPC_DEF("e200z1",        CPU_POWERPC_e200z1,                 e200,
                "PowerPC e200z1 core")
#endif
#if defined(TODO)
    POWERPC_DEF("e200z3",        CPU_POWERPC_e200z3,                 e200,
                "PowerPC e200z3 core")
#endif
    POWERPC_DEF("e200z5",        CPU_POWERPC_e200z5,                 e200,
                "PowerPC e200z5 core")
    POWERPC_DEF("e200z6",        CPU_POWERPC_e200z6,                 e200,
                "PowerPC e200z6 core")
    /* PowerPC e200 microcontrollers                                         */
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5514E", "MPC5514E",
                    CPU_POWERPC_MPC5514E,     POWERPC_SVR_5514E,     e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5514E_v0", "MPC5514E v0",
                    CPU_POWERPC_MPC5514E_v0,  POWERPC_SVR_5514E_v0,  e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5514E_v1", "MPC5514E v1",
                    CPU_POWERPC_MPC5514E_v1,  POWERPC_SVR_5514E_v1,  e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5514G", "MPC5514G",
                    CPU_POWERPC_MPC5514G,     POWERPC_SVR_5514G,     e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5514G_v0", "MPC5514G v0",
                    CPU_POWERPC_MPC5514G_v0,  POWERPC_SVR_5514G_v0,  e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5514G_v1", "MPC5514G v1",
                    CPU_POWERPC_MPC5514G_v1,  POWERPC_SVR_5514G_v1,  e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5515S", "MPC5515S",
                    CPU_POWERPC_MPC5515S,     POWERPC_SVR_5515S,     e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5516E", "MPC5516E",
                    CPU_POWERPC_MPC5516E,     POWERPC_SVR_5516E,     e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5516E_v0", "MPC5516E v0",
                    CPU_POWERPC_MPC5516E_v0,  POWERPC_SVR_5516E_v0,  e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5516E_v1", "MPC5516E v1",
                    CPU_POWERPC_MPC5516E_v1,  POWERPC_SVR_5516E_v1,  e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5516G", "MPC5516G",
                    CPU_POWERPC_MPC5516G,     POWERPC_SVR_5516G,     e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5516G_v0", "MPC5516G v0",
                    CPU_POWERPC_MPC5516G_v0,  POWERPC_SVR_5516G_v0,  e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5516G_v1", "MPC5516G v1",
                    CPU_POWERPC_MPC5516G_v1,  POWERPC_SVR_5516G_v1,  e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5516S", "MPC5516S",
                    CPU_POWERPC_MPC5516S,     POWERPC_SVR_5516S,     e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5533", "MPC5533",
                    CPU_POWERPC_MPC5533,      POWERPC_SVR_5533,      e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5534", "MPC5534",
                    CPU_POWERPC_MPC5534,      POWERPC_SVR_5534,      e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5553", "MPC5553",
                    CPU_POWERPC_MPC5553,      POWERPC_SVR_5553,      e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5554", "MPC5554",
                    CPU_POWERPC_MPC5554,      POWERPC_SVR_5554,      e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5561", "MPC5561",
                    CPU_POWERPC_MPC5561,      POWERPC_SVR_5561,      e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5565", "MPC5565",
                    CPU_POWERPC_MPC5565,      POWERPC_SVR_5565,      e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5566", "MPC5566",
                    CPU_POWERPC_MPC5566,      POWERPC_SVR_5566,      e200)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC5567", "MPC5567",
                    CPU_POWERPC_MPC5567,      POWERPC_SVR_5567,      e200)
#endif
    /* e300 family                                                           */
    POWERPC_DEF("e300c1",        CPU_POWERPC_e300c1,                 e300,
                "PowerPC e300c1 core")
    POWERPC_DEF("e300c2",        CPU_POWERPC_e300c2,                 e300,
                "PowerPC e300c2 core")
    POWERPC_DEF("e300c3",        CPU_POWERPC_e300c3,                 e300,
                "PowerPC e300c3 core")
    POWERPC_DEF("e300c4",        CPU_POWERPC_e300c4,                 e300,
                "PowerPC e300c4 core")
    /* PowerPC e300 microcontrollers                                         */
#if defined(TODO)
    POWERPC_DEF_SVR("MPC8313", "MPC8313",
                    CPU_POWERPC_MPC831x,      POWERPC_SVR_8313,      e300)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC8313E", "MPC8313E",
                    CPU_POWERPC_MPC831x,      POWERPC_SVR_8313E,     e300)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC8314", "MPC8314",
                    CPU_POWERPC_MPC831x,      POWERPC_SVR_8314,      e300)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC8314E", "MPC8314E",
                    CPU_POWERPC_MPC831x,      POWERPC_SVR_8314E,     e300)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC8315", "MPC8315",
                    CPU_POWERPC_MPC831x,      POWERPC_SVR_8315,      e300)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC8315E", "MPC8315E",
                    CPU_POWERPC_MPC831x,      POWERPC_SVR_8315E,     e300)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC8321", "MPC8321",
                    CPU_POWERPC_MPC832x,      POWERPC_SVR_8321,      e300)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC8321E", "MPC8321E",
                    CPU_POWERPC_MPC832x,      POWERPC_SVR_8321E,     e300)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC8323", "MPC8323",
                    CPU_POWERPC_MPC832x,      POWERPC_SVR_8323,      e300)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC8323E", "MPC8323E",
                    CPU_POWERPC_MPC832x,      POWERPC_SVR_8323E,     e300)
#endif
    POWERPC_DEF_SVR("MPC8343", "MPC8343",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8343,      e300)
    POWERPC_DEF_SVR("MPC8343A", "MPC8343A",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8343A,     e300)
    POWERPC_DEF_SVR("MPC8343E", "MPC8343E",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8343E,     e300)
    POWERPC_DEF_SVR("MPC8343EA", "MPC8343EA",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8343EA,    e300)
    POWERPC_DEF_SVR("MPC8347T", "MPC8347T",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347T,     e300)
    POWERPC_DEF_SVR("MPC8347P", "MPC8347P",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347P,     e300)
    POWERPC_DEF_SVR("MPC8347AT", "MPC8347AT",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347AT,    e300)
    POWERPC_DEF_SVR("MPC8347AP", "MPC8347AP",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347AP,    e300)
    POWERPC_DEF_SVR("MPC8347ET", "MPC8347ET",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347ET,    e300)
    POWERPC_DEF_SVR("MPC8347EP", "MPC8343EP",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347EP,    e300)
    POWERPC_DEF_SVR("MPC8347EAT", "MPC8347EAT",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347EAT,   e300)
    POWERPC_DEF_SVR("MPC8347EAP", "MPC8343EAP",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347EAP,   e300)
    POWERPC_DEF_SVR("MPC8349", "MPC8349",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8349,      e300)
    POWERPC_DEF_SVR("MPC8349A", "MPC8349A",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8349A,     e300)
    POWERPC_DEF_SVR("MPC8349E", "MPC8349E",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8349E,     e300)
    POWERPC_DEF_SVR("MPC8349EA", "MPC8349EA",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8349EA,    e300)
#if defined(TODO)
    POWERPC_DEF_SVR("MPC8358E", "MPC8358E",
                    CPU_POWERPC_MPC835x,      POWERPC_SVR_8358E,     e300)
#endif
#if defined(TODO)
    POWERPC_DEF_SVR("MPC8360E", "MPC8360E",
                    CPU_POWERPC_MPC836x,      POWERPC_SVR_8360E,     e300)
#endif
    POWERPC_DEF_SVR("MPC8377", "MPC8377",
                    CPU_POWERPC_MPC837x,      POWERPC_SVR_8377,      e300)
    POWERPC_DEF_SVR("MPC8377E", "MPC8377E",
                    CPU_POWERPC_MPC837x,      POWERPC_SVR_8377E,     e300)
    POWERPC_DEF_SVR("MPC8378", "MPC8378",
                    CPU_POWERPC_MPC837x,      POWERPC_SVR_8378,      e300)
    POWERPC_DEF_SVR("MPC8378E", "MPC8378E",
                    CPU_POWERPC_MPC837x,      POWERPC_SVR_8378E,     e300)
    POWERPC_DEF_SVR("MPC8379", "MPC8379",
                    CPU_POWERPC_MPC837x,      POWERPC_SVR_8379,      e300)
    POWERPC_DEF_SVR("MPC8379E", "MPC8379E",
                    CPU_POWERPC_MPC837x,      POWERPC_SVR_8379E,     e300)
    /* e500 family                                                           */
    POWERPC_DEF("e500_v10",      CPU_POWERPC_e500v1_v10,             e500v1,
                "PowerPC e500 v1.0 core")
    POWERPC_DEF("e500_v20",      CPU_POWERPC_e500v1_v20,             e500v1,
                "PowerPC e500 v2.0 core")
    POWERPC_DEF("e500v2_v10",    CPU_POWERPC_e500v2_v10,             e500v2,
                "PowerPC e500v2 v1.0 core")
    POWERPC_DEF("e500v2_v20",    CPU_POWERPC_e500v2_v20,             e500v2,
                "PowerPC e500v2 v2.0 core")
    POWERPC_DEF("e500v2_v21",    CPU_POWERPC_e500v2_v21,             e500v2,
                "PowerPC e500v2 v2.1 core")
    POWERPC_DEF("e500v2_v22",    CPU_POWERPC_e500v2_v22,             e500v2,
                "PowerPC e500v2 v2.2 core")
    POWERPC_DEF("e500v2_v30",    CPU_POWERPC_e500v2_v30,             e500v2,
                "PowerPC e500v2 v3.0 core")
    POWERPC_DEF_SVR("e500mc", "e500mc",
                    CPU_POWERPC_e500mc,       POWERPC_SVR_E500,      e500mc)
#ifdef TARGET_PPC64
    POWERPC_DEF_SVR("e5500", "e5500",
                    CPU_POWERPC_e5500,        POWERPC_SVR_E500,      e5500)
#endif
    /* PowerPC e500 microcontrollers                                         */
    POWERPC_DEF_SVR("MPC8533_v10", "MPC8533 v1.0",
                    CPU_POWERPC_MPC8533_v10,  POWERPC_SVR_8533_v10,  e500v2)
    POWERPC_DEF_SVR("MPC8533_v11", "MPC8533 v1.1",
                    CPU_POWERPC_MPC8533_v11,  POWERPC_SVR_8533_v11,  e500v2)
    POWERPC_DEF_SVR("MPC8533E_v10", "MPC8533E v1.0",
                    CPU_POWERPC_MPC8533E_v10, POWERPC_SVR_8533E_v10, e500v2)
    POWERPC_DEF_SVR("MPC8533E_v11", "MPC8533E v1.1",
                    CPU_POWERPC_MPC8533E_v11, POWERPC_SVR_8533E_v11, e500v2)
    POWERPC_DEF_SVR("MPC8540_v10", "MPC8540 v1.0",
                    CPU_POWERPC_MPC8540_v10,  POWERPC_SVR_8540_v10,  e500v1)
    POWERPC_DEF_SVR("MPC8540_v20", "MPC8540 v2.0",
                    CPU_POWERPC_MPC8540_v20,  POWERPC_SVR_8540_v20,  e500v1)
    POWERPC_DEF_SVR("MPC8540_v21", "MPC8540 v2.1",
                    CPU_POWERPC_MPC8540_v21,  POWERPC_SVR_8540_v21,  e500v1)
    POWERPC_DEF_SVR("MPC8541_v10", "MPC8541 v1.0",
                    CPU_POWERPC_MPC8541_v10,  POWERPC_SVR_8541_v10,  e500v1)
    POWERPC_DEF_SVR("MPC8541_v11", "MPC8541 v1.1",
                    CPU_POWERPC_MPC8541_v11,  POWERPC_SVR_8541_v11,  e500v1)
    POWERPC_DEF_SVR("MPC8541E_v10", "MPC8541E v1.0",
                    CPU_POWERPC_MPC8541E_v10, POWERPC_SVR_8541E_v10, e500v1)
    POWERPC_DEF_SVR("MPC8541E_v11", "MPC8541E v1.1",
                    CPU_POWERPC_MPC8541E_v11, POWERPC_SVR_8541E_v11, e500v1)
    POWERPC_DEF_SVR("MPC8543_v10", "MPC8543 v1.0",
                    CPU_POWERPC_MPC8543_v10,  POWERPC_SVR_8543_v10,  e500v2)
    POWERPC_DEF_SVR("MPC8543_v11", "MPC8543 v1.1",
                    CPU_POWERPC_MPC8543_v11,  POWERPC_SVR_8543_v11,  e500v2)
    POWERPC_DEF_SVR("MPC8543_v20", "MPC8543 v2.0",
                    CPU_POWERPC_MPC8543_v20,  POWERPC_SVR_8543_v20,  e500v2)
    POWERPC_DEF_SVR("MPC8543_v21", "MPC8543 v2.1",
                    CPU_POWERPC_MPC8543_v21,  POWERPC_SVR_8543_v21,  e500v2)
    POWERPC_DEF_SVR("MPC8543E_v10", "MPC8543E v1.0",
                    CPU_POWERPC_MPC8543E_v10, POWERPC_SVR_8543E_v10, e500v2)
    POWERPC_DEF_SVR("MPC8543E_v11", "MPC8543E v1.1",
                    CPU_POWERPC_MPC8543E_v11, POWERPC_SVR_8543E_v11, e500v2)
    POWERPC_DEF_SVR("MPC8543E_v20", "MPC8543E v2.0",
                    CPU_POWERPC_MPC8543E_v20, POWERPC_SVR_8543E_v20, e500v2)
    POWERPC_DEF_SVR("MPC8543E_v21", "MPC8543E v2.1",
                    CPU_POWERPC_MPC8543E_v21, POWERPC_SVR_8543E_v21, e500v2)
    POWERPC_DEF_SVR("MPC8544_v10", "MPC8544 v1.0",
                    CPU_POWERPC_MPC8544_v10,  POWERPC_SVR_8544_v10,  e500v2)
    POWERPC_DEF_SVR("MPC8544_v11", "MPC8544 v1.1",
                    CPU_POWERPC_MPC8544_v11,  POWERPC_SVR_8544_v11,  e500v2)
    POWERPC_DEF_SVR("MPC8544E_v10", "MPC8544E v1.0",
                    CPU_POWERPC_MPC8544E_v10, POWERPC_SVR_8544E_v10, e500v2)
    POWERPC_DEF_SVR("MPC8544E_v11", "MPC8544E v1.1",
                    CPU_POWERPC_MPC8544E_v11, POWERPC_SVR_8544E_v11, e500v2)
    POWERPC_DEF_SVR("MPC8545_v20", "MPC8545 v2.0",
                    CPU_POWERPC_MPC8545_v20,  POWERPC_SVR_8545_v20,  e500v2)
    POWERPC_DEF_SVR("MPC8545_v21", "MPC8545 v2.1",
                    CPU_POWERPC_MPC8545_v21,  POWERPC_SVR_8545_v21,  e500v2)
    POWERPC_DEF_SVR("MPC8545E_v20", "MPC8545E v2.0",
                    CPU_POWERPC_MPC8545E_v20, POWERPC_SVR_8545E_v20, e500v2)
    POWERPC_DEF_SVR("MPC8545E_v21", "MPC8545E v2.1",
                    CPU_POWERPC_MPC8545E_v21, POWERPC_SVR_8545E_v21, e500v2)
    POWERPC_DEF_SVR("MPC8547E_v20", "MPC8547E v2.0",
                    CPU_POWERPC_MPC8547E_v20, POWERPC_SVR_8547E_v20, e500v2)
    POWERPC_DEF_SVR("MPC8547E_v21", "MPC8547E v2.1",
                    CPU_POWERPC_MPC8547E_v21, POWERPC_SVR_8547E_v21, e500v2)
    POWERPC_DEF_SVR("MPC8548_v10", "MPC8548 v1.0",
                    CPU_POWERPC_MPC8548_v10,  POWERPC_SVR_8548_v10,  e500v2)
    POWERPC_DEF_SVR("MPC8548_v11", "MPC8548 v1.1",
                    CPU_POWERPC_MPC8548_v11,  POWERPC_SVR_8548_v11,  e500v2)
    POWERPC_DEF_SVR("MPC8548_v20", "MPC8548 v2.0",
                    CPU_POWERPC_MPC8548_v20,  POWERPC_SVR_8548_v20,  e500v2)
    POWERPC_DEF_SVR("MPC8548_v21", "MPC8548 v2.1",
                    CPU_POWERPC_MPC8548_v21,  POWERPC_SVR_8548_v21,  e500v2)
    POWERPC_DEF_SVR("MPC8548E_v10", "MPC8548E v1.0",
                    CPU_POWERPC_MPC8548E_v10, POWERPC_SVR_8548E_v10, e500v2)
    POWERPC_DEF_SVR("MPC8548E_v11", "MPC8548E v1.1",
                    CPU_POWERPC_MPC8548E_v11, POWERPC_SVR_8548E_v11, e500v2)
    POWERPC_DEF_SVR("MPC8548E_v20", "MPC8548E v2.0",
                    CPU_POWERPC_MPC8548E_v20, POWERPC_SVR_8548E_v20, e500v2)
    POWERPC_DEF_SVR("MPC8548E_v21", "MPC8548E v2.1",
                    CPU_POWERPC_MPC8548E_v21, POWERPC_SVR_8548E_v21, e500v2)
    POWERPC_DEF_SVR("MPC8555_v10", "MPC8555 v1.0",
                    CPU_POWERPC_MPC8555_v10,  POWERPC_SVR_8555_v10,  e500v2)
    POWERPC_DEF_SVR("MPC8555_v11", "MPC8555 v1.1",
                    CPU_POWERPC_MPC8555_v11,  POWERPC_SVR_8555_v11,  e500v2)
    POWERPC_DEF_SVR("MPC8555E_v10", "MPC8555E v1.0",
                    CPU_POWERPC_MPC8555E_v10, POWERPC_SVR_8555E_v10, e500v2)
    POWERPC_DEF_SVR("MPC8555E_v11", "MPC8555E v1.1",
                    CPU_POWERPC_MPC8555E_v11, POWERPC_SVR_8555E_v11, e500v2)
    POWERPC_DEF_SVR("MPC8560_v10", "MPC8560 v1.0",
                    CPU_POWERPC_MPC8560_v10,  POWERPC_SVR_8560_v10,  e500v2)
    POWERPC_DEF_SVR("MPC8560_v20", "MPC8560 v2.0",
                    CPU_POWERPC_MPC8560_v20,  POWERPC_SVR_8560_v20,  e500v2)
    POWERPC_DEF_SVR("MPC8560_v21", "MPC8560 v2.1",
                    CPU_POWERPC_MPC8560_v21,  POWERPC_SVR_8560_v21,  e500v2)
    POWERPC_DEF_SVR("MPC8567", "MPC8567",
                    CPU_POWERPC_MPC8567,      POWERPC_SVR_8567,      e500v2)
    POWERPC_DEF_SVR("MPC8567E", "MPC8567E",
                    CPU_POWERPC_MPC8567E,     POWERPC_SVR_8567E,     e500v2)
    POWERPC_DEF_SVR("MPC8568", "MPC8568",
                    CPU_POWERPC_MPC8568,      POWERPC_SVR_8568,      e500v2)
    POWERPC_DEF_SVR("MPC8568E", "MPC8568E",
                    CPU_POWERPC_MPC8568E,     POWERPC_SVR_8568E,     e500v2)
    POWERPC_DEF_SVR("MPC8572", "MPC8572",
                    CPU_POWERPC_MPC8572,      POWERPC_SVR_8572,      e500v2)
    POWERPC_DEF_SVR("MPC8572E", "MPC8572E",
                    CPU_POWERPC_MPC8572E,     POWERPC_SVR_8572E,     e500v2)
    /* e600 family                                                           */
    POWERPC_DEF("e600",          CPU_POWERPC_e600,                   7400,
                "PowerPC e600 core")
    /* PowerPC e600 microcontrollers                                         */
#if defined(TODO)
    POWERPC_DEF_SVR("MPC8610", "MPC8610",
                    CPU_POWERPC_MPC8610,      POWERPC_SVR_8610,      7400)
#endif
    POWERPC_DEF_SVR("MPC8641", "MPC8641",
                    CPU_POWERPC_MPC8641,      POWERPC_SVR_8641,      7400)
    POWERPC_DEF_SVR("MPC8641D", "MPC8641D",
                    CPU_POWERPC_MPC8641D,     POWERPC_SVR_8641D,     7400)
    /* 32 bits "classic" PowerPC                                             */
    /* PowerPC 6xx family                                                    */
    POWERPC_DEF("601_v0",        CPU_POWERPC_601_v0,                 601,
                "PowerPC 601v0")
    POWERPC_DEF("601_v1",        CPU_POWERPC_601_v1,                 601,
                "PowerPC 601v1")
    POWERPC_DEF("601_v2",        CPU_POWERPC_601_v2,                 601v,
                "PowerPC 601v2")
    POWERPC_DEF("602",           CPU_POWERPC_602,                    602,
                "PowerPC 602")
    POWERPC_DEF("603",           CPU_POWERPC_603,                    603,
                "PowerPC 603")
    POWERPC_DEF("603e_v1.1",     CPU_POWERPC_603E_v11,               603E,
                "PowerPC 603e v1.1")
    POWERPC_DEF("603e_v1.2",     CPU_POWERPC_603E_v12,               603E,
                "PowerPC 603e v1.2")
    POWERPC_DEF("603e_v1.3",     CPU_POWERPC_603E_v13,               603E,
                "PowerPC 603e v1.3")
    POWERPC_DEF("603e_v1.4",     CPU_POWERPC_603E_v14,               603E,
                "PowerPC 603e v1.4")
    POWERPC_DEF("603e_v2.2",     CPU_POWERPC_603E_v22,               603E,
                "PowerPC 603e v2.2")
    POWERPC_DEF("603e_v3",       CPU_POWERPC_603E_v3,                603E,
                "PowerPC 603e v3")
    POWERPC_DEF("603e_v4",       CPU_POWERPC_603E_v4,                603E,
                "PowerPC 603e v4")
    POWERPC_DEF("603e_v4.1",     CPU_POWERPC_603E_v41,               603E,
                "PowerPC 603e v4.1")
    POWERPC_DEF("603e7",         CPU_POWERPC_603E7,                  603E,
                "PowerPC 603e (aka PID7)")
    POWERPC_DEF("603e7t",        CPU_POWERPC_603E7t,                 603E,
                "PowerPC 603e7t")
    POWERPC_DEF("603e7v",        CPU_POWERPC_603E7v,                 603E,
                "PowerPC 603e7v")
    POWERPC_DEF("603e7v1",       CPU_POWERPC_603E7v1,                603E,
                "PowerPC 603e7v1")
    POWERPC_DEF("603e7v2",       CPU_POWERPC_603E7v2,                603E,
                "PowerPC 603e7v2")
    POWERPC_DEF("603p",          CPU_POWERPC_603P,                   603E,
                "PowerPC 603p (aka PID7v)")
    POWERPC_DEF("604",           CPU_POWERPC_604,                    604,
                "PowerPC 604")
    POWERPC_DEF("604e_v1.0",     CPU_POWERPC_604E_v10,               604E,
                "PowerPC 604e v1.0")
    POWERPC_DEF("604e_v2.2",     CPU_POWERPC_604E_v22,               604E,
                "PowerPC 604e v2.2")
    POWERPC_DEF("604e_v2.4",     CPU_POWERPC_604E_v24,               604E,
                "PowerPC 604e v2.4")
    POWERPC_DEF("604r",          CPU_POWERPC_604R,                   604E,
                "PowerPC 604r (aka PIDA)")
#if defined(TODO)
    POWERPC_DEF("604ev",         CPU_POWERPC_604EV,                  604E,
                "PowerPC 604ev")
#endif
    /* PowerPC 7xx family                                                    */
    POWERPC_DEF("740_v1.0",      CPU_POWERPC_7x0_v10,                740,
                "PowerPC 740 v1.0 (G3)")
    POWERPC_DEF("750_v1.0",      CPU_POWERPC_7x0_v10,                750,
                "PowerPC 750 v1.0 (G3)")
    POWERPC_DEF("740_v2.0",      CPU_POWERPC_7x0_v20,                740,
                "PowerPC 740 v2.0 (G3)")
    POWERPC_DEF("750_v2.0",      CPU_POWERPC_7x0_v20,                750,
                "PowerPC 750 v2.0 (G3)")
    POWERPC_DEF("740_v2.1",      CPU_POWERPC_7x0_v21,                740,
                "PowerPC 740 v2.1 (G3)")
    POWERPC_DEF("750_v2.1",      CPU_POWERPC_7x0_v21,                750,
                "PowerPC 750 v2.1 (G3)")
    POWERPC_DEF("740_v2.2",      CPU_POWERPC_7x0_v22,                740,
                "PowerPC 740 v2.2 (G3)")
    POWERPC_DEF("750_v2.2",      CPU_POWERPC_7x0_v22,                750,
                "PowerPC 750 v2.2 (G3)")
    POWERPC_DEF("740_v3.0",      CPU_POWERPC_7x0_v30,                740,
                "PowerPC 740 v3.0 (G3)")
    POWERPC_DEF("750_v3.0",      CPU_POWERPC_7x0_v30,                750,
                "PowerPC 750 v3.0 (G3)")
    POWERPC_DEF("740_v3.1",      CPU_POWERPC_7x0_v31,                740,
                "PowerPC 740 v3.1 (G3)")
    POWERPC_DEF("750_v3.1",      CPU_POWERPC_7x0_v31,                750,
                "PowerPC 750 v3.1 (G3)")
    POWERPC_DEF("740e",          CPU_POWERPC_740E,                   740,
                "PowerPC 740E (G3)")
    POWERPC_DEF("750e",          CPU_POWERPC_750E,                   750,
                "PowerPC 750E (G3)")
    POWERPC_DEF("740p",          CPU_POWERPC_7x0P,                   740,
                "PowerPC 740P (G3)")
    POWERPC_DEF("750p",          CPU_POWERPC_7x0P,                   750,
                "PowerPC 750P (G3)")
    POWERPC_DEF("750cl_v1.0",    CPU_POWERPC_750CL_v10,              750cl,
                "PowerPC 750CL v1.0")
    POWERPC_DEF("750cl_v2.0",    CPU_POWERPC_750CL_v20,              750cl,
                "PowerPC 750CL v2.0")
    POWERPC_DEF("750cx_v1.0",    CPU_POWERPC_750CX_v10,              750cx,
                "PowerPC 750CX v1.0 (G3 embedded)")
    POWERPC_DEF("750cx_v2.0",    CPU_POWERPC_750CX_v20,              750cx,
                "PowerPC 750CX v2.1 (G3 embedded)")
    POWERPC_DEF("750cx_v2.1",    CPU_POWERPC_750CX_v21,              750cx,
                "PowerPC 750CX v2.1 (G3 embedded)")
    POWERPC_DEF("750cx_v2.2",    CPU_POWERPC_750CX_v22,              750cx,
                "PowerPC 750CX v2.2 (G3 embedded)")
    POWERPC_DEF("750cxe_v2.1",   CPU_POWERPC_750CXE_v21,             750cx,
                "PowerPC 750CXe v2.1 (G3 embedded)")
    POWERPC_DEF("750cxe_v2.2",   CPU_POWERPC_750CXE_v22,             750cx,
                "PowerPC 750CXe v2.2 (G3 embedded)")
    POWERPC_DEF("750cxe_v2.3",   CPU_POWERPC_750CXE_v23,             750cx,
                "PowerPC 750CXe v2.3 (G3 embedded)")
    POWERPC_DEF("750cxe_v2.4",   CPU_POWERPC_750CXE_v24,             750cx,
                "PowerPC 750CXe v2.4 (G3 embedded)")
    POWERPC_DEF("750cxe_v2.4b",  CPU_POWERPC_750CXE_v24b,            750cx,
                "PowerPC 750CXe v2.4b (G3 embedded)")
    POWERPC_DEF("750cxe_v3.0",   CPU_POWERPC_750CXE_v30,             750cx,
                "PowerPC 750CXe v3.0 (G3 embedded)")
    POWERPC_DEF("750cxe_v3.1",   CPU_POWERPC_750CXE_v31,             750cx,
                "PowerPC 750CXe v3.1 (G3 embedded)")
    POWERPC_DEF("750cxe_v3.1b",  CPU_POWERPC_750CXE_v31b,            750cx,
                "PowerPC 750CXe v3.1b (G3 embedded)")
    POWERPC_DEF("750cxr",        CPU_POWERPC_750CXR,                 750cx,
                "PowerPC 750CXr (G3 embedded)")
    POWERPC_DEF("750fl",         CPU_POWERPC_750FL,                  750fx,
                "PowerPC 750FL (G3 embedded)")
    POWERPC_DEF("750fx_v1.0",    CPU_POWERPC_750FX_v10,              750fx,
                "PowerPC 750FX v1.0 (G3 embedded)")
    POWERPC_DEF("750fx_v2.0",    CPU_POWERPC_750FX_v20,              750fx,
                "PowerPC 750FX v2.0 (G3 embedded)")
    POWERPC_DEF("750fx_v2.1",    CPU_POWERPC_750FX_v21,              750fx,
                "PowerPC 750FX v2.1 (G3 embedded)")
    POWERPC_DEF("750fx_v2.2",    CPU_POWERPC_750FX_v22,              750fx,
                "PowerPC 750FX v2.2 (G3 embedded)")
    POWERPC_DEF("750fx_v2.3",    CPU_POWERPC_750FX_v23,              750fx,
                "PowerPC 750FX v2.3 (G3 embedded)")
    POWERPC_DEF("750gl",         CPU_POWERPC_750GL,                  750gx,
                "PowerPC 750GL (G3 embedded)")
    POWERPC_DEF("750gx_v1.0",    CPU_POWERPC_750GX_v10,              750gx,
                "PowerPC 750GX v1.0 (G3 embedded)")
    POWERPC_DEF("750gx_v1.1",    CPU_POWERPC_750GX_v11,              750gx,
                "PowerPC 750GX v1.1 (G3 embedded)")
    POWERPC_DEF("750gx_v1.2",    CPU_POWERPC_750GX_v12,              750gx,
                "PowerPC 750GX v1.2 (G3 embedded)")
    POWERPC_DEF("750l_v2.0",     CPU_POWERPC_750L_v20,               750,
                "PowerPC 750L v2.0 (G3 embedded)")
    POWERPC_DEF("750l_v2.1",     CPU_POWERPC_750L_v21,               750,
                "PowerPC 750L v2.1 (G3 embedded)")
    POWERPC_DEF("750l_v2.2",     CPU_POWERPC_750L_v22,               750,
                "PowerPC 750L v2.2 (G3 embedded)")
    POWERPC_DEF("750l_v3.0",     CPU_POWERPC_750L_v30,               750,
                "PowerPC 750L v3.0 (G3 embedded)")
    POWERPC_DEF("750l_v3.2",     CPU_POWERPC_750L_v32,               750,
                "PowerPC 750L v3.2 (G3 embedded)")
    POWERPC_DEF("745_v1.0",      CPU_POWERPC_7x5_v10,                745,
                "PowerPC 745 v1.0")
    POWERPC_DEF("755_v1.0",      CPU_POWERPC_7x5_v10,                755,
                "PowerPC 755 v1.0")
    POWERPC_DEF("745_v1.1",      CPU_POWERPC_7x5_v11,                745,
                "PowerPC 745 v1.1")
    POWERPC_DEF("755_v1.1",      CPU_POWERPC_7x5_v11,                755,
                "PowerPC 755 v1.1")
    POWERPC_DEF("745_v2.0",      CPU_POWERPC_7x5_v20,                745,
                "PowerPC 745 v2.0")
    POWERPC_DEF("755_v2.0",      CPU_POWERPC_7x5_v20,                755,
                "PowerPC 755 v2.0")
    POWERPC_DEF("745_v2.1",      CPU_POWERPC_7x5_v21,                745,
                "PowerPC 745 v2.1")
    POWERPC_DEF("755_v2.1",      CPU_POWERPC_7x5_v21,                755,
                "PowerPC 755 v2.1")
    POWERPC_DEF("745_v2.2",      CPU_POWERPC_7x5_v22,                745,
                "PowerPC 745 v2.2")
    POWERPC_DEF("755_v2.2",      CPU_POWERPC_7x5_v22,                755,
                "PowerPC 755 v2.2")
    POWERPC_DEF("745_v2.3",      CPU_POWERPC_7x5_v23,                745,
                "PowerPC 745 v2.3")
    POWERPC_DEF("755_v2.3",      CPU_POWERPC_7x5_v23,                755,
                "PowerPC 755 v2.3")
    POWERPC_DEF("745_v2.4",      CPU_POWERPC_7x5_v24,                745,
                "PowerPC 745 v2.4")
    POWERPC_DEF("755_v2.4",      CPU_POWERPC_7x5_v24,                755,
                "PowerPC 755 v2.4")
    POWERPC_DEF("745_v2.5",      CPU_POWERPC_7x5_v25,                745,
                "PowerPC 745 v2.5")
    POWERPC_DEF("755_v2.5",      CPU_POWERPC_7x5_v25,                755,
                "PowerPC 755 v2.5")
    POWERPC_DEF("745_v2.6",      CPU_POWERPC_7x5_v26,                745,
                "PowerPC 745 v2.6")
    POWERPC_DEF("755_v2.6",      CPU_POWERPC_7x5_v26,                755,
                "PowerPC 755 v2.6")
    POWERPC_DEF("745_v2.7",      CPU_POWERPC_7x5_v27,                745,
                "PowerPC 745 v2.7")
    POWERPC_DEF("755_v2.7",      CPU_POWERPC_7x5_v27,                755,
                "PowerPC 755 v2.7")
    POWERPC_DEF("745_v2.8",      CPU_POWERPC_7x5_v28,                745,
                "PowerPC 745 v2.8")
    POWERPC_DEF("755_v2.8",      CPU_POWERPC_7x5_v28,                755,
                "PowerPC 755 v2.8")
#if defined(TODO)
    POWERPC_DEF("745p",          CPU_POWERPC_7x5P,                   745,
                "PowerPC 745P (G3)")
    POWERPC_DEF("755p",          CPU_POWERPC_7x5P,                   755,
                "PowerPC 755P (G3)")
#endif
    /* PowerPC 74xx family                                                   */
    POWERPC_DEF("7400_v1.0",     CPU_POWERPC_7400_v10,               7400,
                "PowerPC 7400 v1.0 (G4)")
    POWERPC_DEF("7400_v1.1",     CPU_POWERPC_7400_v11,               7400,
                "PowerPC 7400 v1.1 (G4)")
    POWERPC_DEF("7400_v2.0",     CPU_POWERPC_7400_v20,               7400,
                "PowerPC 7400 v2.0 (G4)")
    POWERPC_DEF("7400_v2.1",     CPU_POWERPC_7400_v21,               7400,
                "PowerPC 7400 v2.1 (G4)")
    POWERPC_DEF("7400_v2.2",     CPU_POWERPC_7400_v22,               7400,
                "PowerPC 7400 v2.2 (G4)")
    POWERPC_DEF("7400_v2.6",     CPU_POWERPC_7400_v26,               7400,
                "PowerPC 7400 v2.6 (G4)")
    POWERPC_DEF("7400_v2.7",     CPU_POWERPC_7400_v27,               7400,
                "PowerPC 7400 v2.7 (G4)")
    POWERPC_DEF("7400_v2.8",     CPU_POWERPC_7400_v28,               7400,
                "PowerPC 7400 v2.8 (G4)")
    POWERPC_DEF("7400_v2.9",     CPU_POWERPC_7400_v29,               7400,
                "PowerPC 7400 v2.9 (G4)")
    POWERPC_DEF("7410_v1.0",     CPU_POWERPC_7410_v10,               7410,
                "PowerPC 7410 v1.0 (G4)")
    POWERPC_DEF("7410_v1.1",     CPU_POWERPC_7410_v11,               7410,
                "PowerPC 7410 v1.1 (G4)")
    POWERPC_DEF("7410_v1.2",     CPU_POWERPC_7410_v12,               7410,
                "PowerPC 7410 v1.2 (G4)")
    POWERPC_DEF("7410_v1.3",     CPU_POWERPC_7410_v13,               7410,
                "PowerPC 7410 v1.3 (G4)")
    POWERPC_DEF("7410_v1.4",     CPU_POWERPC_7410_v14,               7410,
                "PowerPC 7410 v1.4 (G4)")
    POWERPC_DEF("7448_v1.0",     CPU_POWERPC_7448_v10,               7400,
                "PowerPC 7448 v1.0 (G4)")
    POWERPC_DEF("7448_v1.1",     CPU_POWERPC_7448_v11,               7400,
                "PowerPC 7448 v1.1 (G4)")
    POWERPC_DEF("7448_v2.0",     CPU_POWERPC_7448_v20,               7400,
                "PowerPC 7448 v2.0 (G4)")
    POWERPC_DEF("7448_v2.1",     CPU_POWERPC_7448_v21,               7400,
                "PowerPC 7448 v2.1 (G4)")
    POWERPC_DEF("7450_v1.0",     CPU_POWERPC_7450_v10,               7450,
                "PowerPC 7450 v1.0 (G4)")
    POWERPC_DEF("7450_v1.1",     CPU_POWERPC_7450_v11,               7450,
                "PowerPC 7450 v1.1 (G4)")
    POWERPC_DEF("7450_v1.2",     CPU_POWERPC_7450_v12,               7450,
                "PowerPC 7450 v1.2 (G4)")
    POWERPC_DEF("7450_v2.0",     CPU_POWERPC_7450_v20,               7450,
                "PowerPC 7450 v2.0 (G4)")
    POWERPC_DEF("7450_v2.1",     CPU_POWERPC_7450_v21,               7450,
                "PowerPC 7450 v2.1 (G4)")
    POWERPC_DEF("7441_v2.1",     CPU_POWERPC_7450_v21,               7440,
                "PowerPC 7441 v2.1 (G4)")
    POWERPC_DEF("7441_v2.3",     CPU_POWERPC_74x1_v23,               7440,
                "PowerPC 7441 v2.3 (G4)")
    POWERPC_DEF("7451_v2.3",     CPU_POWERPC_74x1_v23,               7450,
                "PowerPC 7451 v2.3 (G4)")
    POWERPC_DEF("7441_v2.10",    CPU_POWERPC_74x1_v210,              7440,
                "PowerPC 7441 v2.10 (G4)")
    POWERPC_DEF("7451_v2.10",    CPU_POWERPC_74x1_v210,              7450,
                "PowerPC 7451 v2.10 (G4)")
    POWERPC_DEF("7445_v1.0",     CPU_POWERPC_74x5_v10,               7445,
                "PowerPC 7445 v1.0 (G4)")
    POWERPC_DEF("7455_v1.0",     CPU_POWERPC_74x5_v10,               7455,
                "PowerPC 7455 v1.0 (G4)")
    POWERPC_DEF("7445_v2.1",     CPU_POWERPC_74x5_v21,               7445,
                "PowerPC 7445 v2.1 (G4)")
    POWERPC_DEF("7455_v2.1",     CPU_POWERPC_74x5_v21,               7455,
                "PowerPC 7455 v2.1 (G4)")
    POWERPC_DEF("7445_v3.2",     CPU_POWERPC_74x5_v32,               7445,
                "PowerPC 7445 v3.2 (G4)")
    POWERPC_DEF("7455_v3.2",     CPU_POWERPC_74x5_v32,               7455,
                "PowerPC 7455 v3.2 (G4)")
    POWERPC_DEF("7445_v3.3",     CPU_POWERPC_74x5_v33,               7445,
                "PowerPC 7445 v3.3 (G4)")
    POWERPC_DEF("7455_v3.3",     CPU_POWERPC_74x5_v33,               7455,
                "PowerPC 7455 v3.3 (G4)")
    POWERPC_DEF("7445_v3.4",     CPU_POWERPC_74x5_v34,               7445,
                "PowerPC 7445 v3.4 (G4)")
    POWERPC_DEF("7455_v3.4",     CPU_POWERPC_74x5_v34,               7455,
                "PowerPC 7455 v3.4 (G4)")
    POWERPC_DEF("7447_v1.0",     CPU_POWERPC_74x7_v10,               7445,
                "PowerPC 7447 v1.0 (G4)")
    POWERPC_DEF("7457_v1.0",     CPU_POWERPC_74x7_v10,               7455,
                "PowerPC 7457 v1.0 (G4)")
    POWERPC_DEF("7447_v1.1",     CPU_POWERPC_74x7_v11,               7445,
                "PowerPC 7447 v1.1 (G4)")
    POWERPC_DEF("7457_v1.1",     CPU_POWERPC_74x7_v11,               7455,
                "PowerPC 7457 v1.1 (G4)")
    POWERPC_DEF("7457_v1.2",     CPU_POWERPC_74x7_v12,               7455,
                "PowerPC 7457 v1.2 (G4)")
    POWERPC_DEF("7447A_v1.0",    CPU_POWERPC_74x7A_v10,              7445,
                "PowerPC 7447A v1.0 (G4)")
    POWERPC_DEF("7457A_v1.0",    CPU_POWERPC_74x7A_v10,              7455,
                "PowerPC 7457A v1.0 (G4)")
    POWERPC_DEF("7447A_v1.1",    CPU_POWERPC_74x7A_v11,              7445,
                "PowerPC 7447A v1.1 (G4)")
    POWERPC_DEF("7457A_v1.1",    CPU_POWERPC_74x7A_v11,              7455,
                "PowerPC 7457A v1.1 (G4)")
    POWERPC_DEF("7447A_v1.2",    CPU_POWERPC_74x7A_v12,              7445,
                "PowerPC 7447A v1.2 (G4)")
    POWERPC_DEF("7457A_v1.2",    CPU_POWERPC_74x7A_v12,              7455,
                "PowerPC 7457A v1.2 (G4)")
    /* 64 bits PowerPC                                                       */
#if defined (TARGET_PPC64)
#if defined(TODO)
    POWERPC_DEF("620",           CPU_POWERPC_620,                    620,
                "PowerPC 620")
    POWERPC_DEF("630",           CPU_POWERPC_630,                    630,
                "PowerPC 630 (POWER3)")
#endif
#if defined(TODO)
    POWERPC_DEF("631",           CPU_POWERPC_631,                    631,
                "PowerPC 631 (Power 3+)")
#endif
#if defined(TODO)
    POWERPC_DEF("POWER4",        CPU_POWERPC_POWER4,                 POWER4,
                "POWER4")
#endif
#if defined(TODO)
    POWERPC_DEF("POWER4+",       CPU_POWERPC_POWER4P,                POWER4P,
                "POWER4p")
#endif
#if defined(TODO)
    POWERPC_DEF("POWER5",        CPU_POWERPC_POWER5,                 POWER5,
                "POWER5")
    POWERPC_DEF("POWER5gr",      CPU_POWERPC_POWER5GR,               POWER5,
                "POWER5GR")
#endif
#if defined(TODO)
    POWERPC_DEF("POWER5+",       CPU_POWERPC_POWER5P,                POWER5P,
                "POWER5+")
    POWERPC_DEF("POWER5gs",      CPU_POWERPC_POWER5GS,               POWER5P,
                "POWER5GS")
#endif
#if defined(TODO)
    POWERPC_DEF("POWER6",        CPU_POWERPC_POWER6,                 POWER6,
                "POWER6")
    POWERPC_DEF("POWER6_5",      CPU_POWERPC_POWER6_5,               POWER5,
                "POWER6 running in POWER5 mode")
    POWERPC_DEF("POWER6A",       CPU_POWERPC_POWER6A,                POWER6,
                "POWER6A")
#endif
    POWERPC_DEF("POWER7_v2.0",   CPU_POWERPC_POWER7_v20,             POWER7,
                "POWER7 v2.0")
    POWERPC_DEF("POWER7_v2.1",   CPU_POWERPC_POWER7_v21,             POWER7,
                "POWER7 v2.1")
    POWERPC_DEF("POWER7_v2.3",   CPU_POWERPC_POWER7_v23,             POWER7,
                "POWER7 v2.3")
    POWERPC_DEF("970",           CPU_POWERPC_970,                    970,
                "PowerPC 970")
    POWERPC_DEF("970fx_v1.0",    CPU_POWERPC_970FX_v10,              970FX,
                "PowerPC 970FX v1.0 (G5)")
    POWERPC_DEF("970fx_v2.0",    CPU_POWERPC_970FX_v20,              970FX,
                "PowerPC 970FX v2.0 (G5)")
    POWERPC_DEF("970fx_v2.1",    CPU_POWERPC_970FX_v21,              970FX,
                "PowerPC 970FX v2.1 (G5)")
    POWERPC_DEF("970fx_v3.0",    CPU_POWERPC_970FX_v30,              970FX,
                "PowerPC 970FX v3.0 (G5)")
    POWERPC_DEF("970fx_v3.1",    CPU_POWERPC_970FX_v31,              970FX,
                "PowerPC 970FX v3.1 (G5)")
    POWERPC_DEF("970gx",         CPU_POWERPC_970GX,                  970GX,
                "PowerPC 970GX (G5)")
    POWERPC_DEF("970mp_v1.0",    CPU_POWERPC_970MP_v10,              970MP,
                "PowerPC 970MP v1.0")
    POWERPC_DEF("970mp_v1.1",    CPU_POWERPC_970MP_v11,              970MP,
                "PowerPC 970MP v1.1")
#if defined(TODO)
    POWERPC_DEF("Cell",          CPU_POWERPC_CELL,                   970,
                "PowerPC Cell")
#endif
#if defined(TODO)
    POWERPC_DEF("Cell_v1.0",     CPU_POWERPC_CELL_v10,               970,
                "PowerPC Cell v1.0")
#endif
#if defined(TODO)
    POWERPC_DEF("Cell_v2.0",     CPU_POWERPC_CELL_v20,               970,
                "PowerPC Cell v2.0")
#endif
#if defined(TODO)
    POWERPC_DEF("Cell_v3.0",     CPU_POWERPC_CELL_v30,               970,
                "PowerPC Cell v3.0")
#endif
#if defined(TODO)
    POWERPC_DEF("Cell_v3.1",     CPU_POWERPC_CELL_v31,               970,
                "PowerPC Cell v3.1")
#endif
#if defined(TODO)
    POWERPC_DEF("Cell_v3.2",     CPU_POWERPC_CELL_v32,               970,
                "PowerPC Cell v3.2")
#endif
#if defined(TODO)
    /* This one seems to support the whole POWER2 instruction set
     * and the PowerPC 64 one.
     */
    /* What about A10 & A30 ? */
    POWERPC_DEF("RS64",          CPU_POWERPC_RS64,                   RS64,
                "RS64 (Apache/A35)")
#endif
#if defined(TODO)
    POWERPC_DEF("RS64-II",       CPU_POWERPC_RS64II,                 RS64,
                "RS64-II (NorthStar/A50)")
#endif
#if defined(TODO)
    POWERPC_DEF("RS64-III",      CPU_POWERPC_RS64III,                RS64,
                "RS64-III (Pulsar)")
#endif
#if defined(TODO)
    POWERPC_DEF("RS64-IV",       CPU_POWERPC_RS64IV,                 RS64,
                "RS64-IV (IceStar/IStar/SStar)")
#endif
#endif /* defined (TARGET_PPC64) */
    /* POWER                                                                 */
#if defined(TODO)
    POWERPC_DEF("POWER",         CPU_POWERPC_POWER,                  POWER,
                "Original POWER")
#endif
#if defined(TODO)
    POWERPC_DEF("POWER2",        CPU_POWERPC_POWER2,                 POWER,
                "POWER2")
#endif
    /* PA semi cores                                                         */
#if defined(TODO)
    POWERPC_DEF("PA6T",          CPU_POWERPC_PA6T,                   PA6T,
                "PA PA6T")
#endif


/***************************************************************************/
/* PowerPC CPU aliases                                                     */

const PowerPCCPUAlias ppc_cpu_aliases[] = {
    { "403", "403GC" },
    { "405", "405D4" },
    { "405CR", "405CRc" },
    { "405GP", "405GPd" },
    { "405GPe", "405CRc" },
    { "x2vp7", "x2vp4" },
    { "x2vp50", "x2vp20" },

    { "440EP", "440EPb" },
    { "440GP", "440GPc" },
    { "440GR", "440GRa" },
    { "440GX", "440GXf" },

    { "RCPU", "MPC5xx" },
    /* MPC5xx microcontrollers */
    { "MGT560", "MPC5xx" },
    { "MPC509", "MPC5xx" },
    { "MPC533", "MPC5xx" },
    { "MPC534", "MPC5xx" },
    { "MPC555", "MPC5xx" },
    { "MPC556", "MPC5xx" },
    { "MPC560", "MPC5xx" },
    { "MPC561", "MPC5xx" },
    { "MPC562", "MPC5xx" },
    { "MPC563", "MPC5xx" },
    { "MPC564", "MPC5xx" },
    { "MPC565", "MPC5xx" },
    { "MPC566", "MPC5xx" },

    { "PowerQUICC", "MPC8xx" },
    /* MPC8xx microcontrollers */
    { "MGT823", "MPC8xx" },
    { "MPC821", "MPC8xx" },
    { "MPC823", "MPC8xx" },
    { "MPC850", "MPC8xx" },
    { "MPC852T", "MPC8xx" },
    { "MPC855T", "MPC8xx" },
    { "MPC857", "MPC8xx" },
    { "MPC859", "MPC8xx" },
    { "MPC860", "MPC8xx" },
    { "MPC862", "MPC8xx" },
    { "MPC866", "MPC8xx" },
    { "MPC870", "MPC8xx" },
    { "MPC875", "MPC8xx" },
    { "MPC880", "MPC8xx" },
    { "MPC885", "MPC8xx" },

    /* PowerPC MPC603 microcontrollers */
    { "MPC8240", "603" },

    { "MPC52xx", "MPC5200" },
    { "MPC5200", "MPC5200_v12" },
    { "MPC5200B", "MPC5200B_v21" },

    { "MPC82xx", "MPC8280" },
    { "PowerQUICC-II", "MPC82xx" },
    { "MPC8241", "G2HiP4" },
    { "MPC8245", "G2HiP4" },
    { "MPC8247", "G2leGP3" },
    { "MPC8248", "G2leGP3" },
    { "MPC8250", "MPC8250_HiP4" },
    { "MPC8250_HiP3", "G2HiP3" },
    { "MPC8250_HiP4", "G2HiP4" },
    { "MPC8255", "MPC8255_HiP4" },
    { "MPC8255_HiP3", "G2HiP3" },
    { "MPC8255_HiP4", "G2HiP4" },
    { "MPC8260", "MPC8260_HiP4" },
    { "MPC8260_HiP3", "G2HiP3" },
    { "MPC8260_HiP4", "G2HiP4" },
    { "MPC8264", "MPC8264_HiP4" },
    { "MPC8264_HiP3", "G2HiP3" },
    { "MPC8264_HiP4", "G2HiP4" },
    { "MPC8265", "MPC8265_HiP4" },
    { "MPC8265_HiP3", "G2HiP3" },
    { "MPC8265_HiP4", "G2HiP4" },
    { "MPC8266", "MPC8266_HiP4" },
    { "MPC8266_HiP3", "G2HiP3" },
    { "MPC8266_HiP4", "G2HiP4" },
    { "MPC8270", "G2leGP3" },
    { "MPC8271", "G2leGP3" },
    { "MPC8272", "G2leGP3" },
    { "MPC8275", "G2leGP3" },
    { "MPC8280", "G2leGP3" },
    { "e200", "e200z6" },
    { "e300", "e300c3" },
    { "MPC8347", "MPC8347T" },
    { "MPC8347A", "MPC8347AT" },
    { "MPC8347E", "MPC8347ET" },
    { "MPC8347EA", "MPC8347EAT" },
    { "e500", "e500v2_v22" },
    { "e500v1", "e500_v20" },
    { "e500v2", "e500v2_v22" },
    { "MPC8533", "MPC8533_v11" },
    { "MPC8533E", "MPC8533E_v11" },
    { "MPC8540", "MPC8540_v21" },
    { "MPC8541", "MPC8541_v11" },
    { "MPC8541E", "MPC8541E_v11" },
    { "MPC8543", "MPC8543_v21" },
    { "MPC8543E", "MPC8543E_v21" },
    { "MPC8544", "MPC8544_v11" },
    { "MPC8544E", "MPC8544E_v11" },
    { "MPC8545", "MPC8545_v21" },
    { "MPC8545E", "MPC8545E_v21" },
    { "MPC8547E", "MPC8547E_v21" },
    { "MPC8548", "MPC8548_v21" },
    { "MPC8548E", "MPC8548E_v21" },
    { "MPC8555", "MPC8555_v11" },
    { "MPC8555E", "MPC8555E_v11" },
    { "MPC8560", "MPC8560_v21" },
    { "601",  "601_v2" },
    { "601v", "601_v2" },
    { "Vanilla", "603" },
    { "603e", "603e_v4.1" },
    { "Stretch", "603e" },
    { "Vaillant", "603e7v" },
    { "603r", "603e7t" },
    { "Goldeneye", "603r" },
    { "604e", "604e_v2.4" },
    { "Sirocco", "604e" },
    { "Mach5", "604r" },
    { "740", "740_v3.1" },
    { "Arthur", "740" },
    { "750", "750_v3.1" },
    { "Typhoon", "750" },
    { "G3",      "750" },
    { "Conan/Doyle", "750p" },
    { "750cl", "750cl_v2.0" },
    { "750cx", "750cx_v2.2" },
    { "750cxe", "750cxe_v3.1b" },
    { "750fx", "750fx_v2.3" },
    { "750gx", "750gx_v1.2" },
    { "750l", "750l_v3.2" },
    { "LoneStar", "750l" },
    { "745", "745_v2.8" },
    { "755", "755_v2.8" },
    { "Goldfinger", "755" },
    { "7400", "7400_v2.9" },
    { "Max", "7400" },
    { "G4",  "7400" },
    { "7410", "7410_v1.4" },
    { "Nitro", "7410" },
    { "7448", "7448_v2.1" },
    { "7450", "7450_v2.1" },
    { "Vger", "7450" },
    { "7441", "7441_v2.3" },
    { "7451", "7451_v2.3" },
    { "7445", "7445_v3.2" },
    { "7455", "7455_v3.2" },
    { "Apollo6", "7455" },
    { "7447", "7447_v1.2" },
    { "7457", "7457_v1.2" },
    { "Apollo7", "7457" },
    { "7447A", "7447A_v1.2" },
    { "7457A", "7457A_v1.2" },
    { "Apollo7PM", "7457A_v1.0" },
#if defined(TARGET_PPC64)
    { "Trident", "620" },
    { "POWER3", "630" },
    { "Boxer", "POWER3" },
    { "Dino",  "POWER3" },
    { "POWER3+", "631" },
    { "POWER7", "POWER7_v2.3" },
    { "970fx", "970fx_v3.1" },
    { "970mp", "970mp_v1.1" },
    { "Apache", "RS64" },
    { "A35",    "RS64" },
    { "NorthStar", "RS64-II" },
    { "A50",       "RS64-II" },
    { "Pulsar", "RS64-III" },
    { "IceStar", "RS64-IV" },
    { "IStar",   "RS64-IV" },
    { "SStar",   "RS64-IV" },
#endif
    { "RIOS",    "POWER" },
    { "RSC",     "POWER" },
    { "RSC3308", "POWER" },
    { "RSC4608", "POWER" },
    { "RSC2", "POWER2" },
    { "P2SC", "POWER2" },

    /* Generic PowerPCs */
#if defined(TARGET_PPC64)
    { "ppc64", "970fx" },
#endif
    { "ppc32", "604" },
    { "ppc", "ppc32" },
    { "default", "ppc" },
    { NULL, NULL }
};
