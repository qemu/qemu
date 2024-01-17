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
#include "qemu/module.h"
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
        .name       = POWERPC_CPU_TYPE_NAME(_name),                           \
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
    /* PowerPC 405 family                                                    */
    /* PowerPC 405 cores                                                     */
    POWERPC_DEF("405d2",         CPU_POWERPC_405D2,                  405,
                "PowerPC 405 D2")
    POWERPC_DEF("405d4",         CPU_POWERPC_405D4,                  405,
                "PowerPC 405 D4")
    /* PowerPC 405 microcontrollers                                          */
    POWERPC_DEF("405cra",        CPU_POWERPC_405CRa,                 405,
                "PowerPC 405 CRa")
    POWERPC_DEF("405crb",        CPU_POWERPC_405CRb,                 405,
                "PowerPC 405 CRb")
    POWERPC_DEF("405crc",        CPU_POWERPC_405CRc,                 405,
                "PowerPC 405 CRc")
    POWERPC_DEF("405ep",         CPU_POWERPC_405EP,                  405,
                "PowerPC 405 EP")
    POWERPC_DEF("405ez",         CPU_POWERPC_405EZ,                  405,
                "PowerPC 405 EZ")
    POWERPC_DEF("405gpa",        CPU_POWERPC_405GPa,                 405,
                "PowerPC 405 GPa")
    POWERPC_DEF("405gpb",        CPU_POWERPC_405GPb,                 405,
                "PowerPC 405 GPb")
    POWERPC_DEF("405gpc",        CPU_POWERPC_405GPc,                 405,
                "PowerPC 405 GPc")
    POWERPC_DEF("405gpd",        CPU_POWERPC_405GPd,                 405,
                "PowerPC 405 GPd")
    POWERPC_DEF("405gpr",        CPU_POWERPC_405GPR,                 405,
                "PowerPC 405 GPR")
    POWERPC_DEF("405lp",         CPU_POWERPC_405LP,                  405,
                "PowerPC 405 LP")
    POWERPC_DEF("npe405h",       CPU_POWERPC_NPE405H,                405,
                "Npe405 H")
    POWERPC_DEF("npe405h2",      CPU_POWERPC_NPE405H2,               405,
                "Npe405 H2")
    POWERPC_DEF("npe405l",       CPU_POWERPC_NPE405L,                405,
                "Npe405 L")
    POWERPC_DEF("npe4gs3",       CPU_POWERPC_NPE4GS3,                405,
                "Npe4GS3")
    /* PowerPC 401/403/405 based set-top-box microcontrollers                */
    POWERPC_DEF("stb03",         CPU_POWERPC_STB03,                  405,
                "STB03xx")
    POWERPC_DEF("stb04",         CPU_POWERPC_STB04,                  405,
                "STB04xx")
    POWERPC_DEF("stb25",         CPU_POWERPC_STB25,                  405,
                "STB25xx")
    /* Xilinx PowerPC 405 cores                                              */
    POWERPC_DEF("x2vp4",         CPU_POWERPC_X2VP4,                  405,
                NULL)
    POWERPC_DEF("x2vp20",        CPU_POWERPC_X2VP20,                 405,
                NULL)
    /* PowerPC 440 family                                                    */
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440",           CPU_POWERPC_440,                    440GP,
                "Generic PowerPC 440")
#endif
    /* PowerPC 440 cores                                                     */
    POWERPC_DEF("440-xilinx",    CPU_POWERPC_440_XILINX,             440x5,
                "PowerPC 440 Xilinx 5")

    POWERPC_DEF("440-xilinx-w-dfpu",    CPU_POWERPC_440_XILINX, 440x5wDFPU,
                "PowerPC 440 Xilinx 5 With a Double Prec. FPU")
    /* PowerPC 440 microcontrollers                                          */
    POWERPC_DEF("440epa",        CPU_POWERPC_440EPa,                 440EP,
                "PowerPC 440 EPa")
    POWERPC_DEF("440epb",        CPU_POWERPC_440EPb,                 440EP,
                "PowerPC 440 EPb")
    POWERPC_DEF("440epx",        CPU_POWERPC_440EPX,                 440EP,
                "PowerPC 440 EPX")
    POWERPC_DEF("460exb",        CPU_POWERPC_460EXb,                 460EX,
                "PowerPC 460 EXb")
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440gpb",        CPU_POWERPC_440GPb,                 440GP,
                "PowerPC 440 GPb")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440gpc",        CPU_POWERPC_440GPc,                 440GP,
                "PowerPC 440 GPc")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440gra",        CPU_POWERPC_440GRa,                 440x5,
                "PowerPC 440 GRa")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440grx",        CPU_POWERPC_440GRX,                 440x5,
                "PowerPC 440 GRX")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440gxa",        CPU_POWERPC_440GXa,                 440EP,
                "PowerPC 440 GXa")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440gxb",        CPU_POWERPC_440GXb,                 440EP,
                "PowerPC 440 GXb")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440gxc",        CPU_POWERPC_440GXc,                 440EP,
                "PowerPC 440 GXc")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440gxf",        CPU_POWERPC_440GXf,                 440EP,
                "PowerPC 440 GXf")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440sp",         CPU_POWERPC_440SP,                  440EP,
                "PowerPC 440 SP")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440sp2",        CPU_POWERPC_440SP2,                 440EP,
                "PowerPC 440 SP2")
#endif
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("440spe",        CPU_POWERPC_440SPE,                 440EP,
                "PowerPC 440 SPE")
#endif
    /* Freescale embedded PowerPC cores                                      */
    /* MPC5xx family (aka RCPU)                                              */
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("mpc5xx",        CPU_POWERPC_MPC5xx,                 MPC5xx,
                "Generic MPC5xx core")
#endif
    /* MPC8xx family (aka PowerQUICC)                                        */
#if defined(TODO_USER_ONLY)
    POWERPC_DEF("mpc8xx",        CPU_POWERPC_MPC8xx,                 MPC8xx,
                "Generic MPC8xx core")
#endif
    /* MPC82xx family (aka PowerQUICC-II)                                    */
    POWERPC_DEF("g2",            CPU_POWERPC_G2,                     G2,
                "PowerPC G2 core")
    POWERPC_DEF("g2h4",          CPU_POWERPC_G2H4,                   G2,
                "PowerPC G2 H4 core")
    POWERPC_DEF("g2gp",          CPU_POWERPC_G2gp,                   G2,
                "PowerPC G2 GP core")
    POWERPC_DEF("g2ls",          CPU_POWERPC_G2ls,                   G2,
                "PowerPC G2 LS core")
    POWERPC_DEF("g2hip3",        CPU_POWERPC_G2_HIP3,                G2,
                "PowerPC G2 HiP3 core")
    POWERPC_DEF("g2hip4",        CPU_POWERPC_G2_HIP4,                G2,
                "PowerPC G2 HiP4 core")
    POWERPC_DEF("mpc603",        CPU_POWERPC_MPC603,                 603E,
                "PowerPC MPC603 core")
    POWERPC_DEF("g2le",          CPU_POWERPC_G2LE,                   G2LE,
        "PowerPC G2le core (same as G2 plus little-endian mode support)")
    POWERPC_DEF("g2legp",        CPU_POWERPC_G2LEgp,                 G2LE,
                "PowerPC G2LE GP core")
    POWERPC_DEF("g2lels",        CPU_POWERPC_G2LEls,                 G2LE,
                "PowerPC G2LE LS core")
    POWERPC_DEF("g2legp1",       CPU_POWERPC_G2LEgp1,                G2LE,
                "PowerPC G2LE GP1 core")
    POWERPC_DEF("g2legp3",       CPU_POWERPC_G2LEgp3,                G2LE,
                "PowerPC G2LE GP3 core")
    /* PowerPC G2 microcontrollers                                           */
    POWERPC_DEF_SVR("mpc5200_v10", "MPC5200 v1.0",
                    CPU_POWERPC_MPC5200_v10,  POWERPC_SVR_5200_v10,  G2LE)
    POWERPC_DEF_SVR("mpc5200_v11", "MPC5200 v1.1",
                    CPU_POWERPC_MPC5200_v11,  POWERPC_SVR_5200_v11,  G2LE)
    POWERPC_DEF_SVR("mpc5200_v12", "MPC5200 v1.2",
                    CPU_POWERPC_MPC5200_v12,  POWERPC_SVR_5200_v12,  G2LE)
    POWERPC_DEF_SVR("mpc5200b_v20", "MPC5200B v2.0",
                    CPU_POWERPC_MPC5200B_v20, POWERPC_SVR_5200B_v20, G2LE)
    POWERPC_DEF_SVR("mpc5200b_v21", "MPC5200B v2.1",
                    CPU_POWERPC_MPC5200B_v21, POWERPC_SVR_5200B_v21, G2LE)
    /* e200 family                                                           */
    POWERPC_DEF("e200z5",        CPU_POWERPC_e200z5,                 e200,
                "PowerPC e200z5 core")
    POWERPC_DEF("e200z6",        CPU_POWERPC_e200z6,                 e200,
                "PowerPC e200z6 core")
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
    POWERPC_DEF_SVR("mpc8343", "MPC8343",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8343,      e300)
    POWERPC_DEF_SVR("mpc8343a", "MPC8343A",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8343A,     e300)
    POWERPC_DEF_SVR("mpc8343e", "MPC8343E",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8343E,     e300)
    POWERPC_DEF_SVR("mpc8343ea", "MPC8343EA",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8343EA,    e300)
    POWERPC_DEF_SVR("mpc8347t", "MPC8347T",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347T,     e300)
    POWERPC_DEF_SVR("mpc8347p", "MPC8347P",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347P,     e300)
    POWERPC_DEF_SVR("mpc8347at", "MPC8347AT",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347AT,    e300)
    POWERPC_DEF_SVR("mpc8347ap", "MPC8347AP",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347AP,    e300)
    POWERPC_DEF_SVR("mpc8347et", "MPC8347ET",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347ET,    e300)
    POWERPC_DEF_SVR("mpc8347ep", "MPC8343EP",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347EP,    e300)
    POWERPC_DEF_SVR("mpc8347eat", "MPC8347EAT",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347EAT,   e300)
    POWERPC_DEF_SVR("mpc8347eap", "MPC8343EAP",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8347EAP,   e300)
    POWERPC_DEF_SVR("mpc8349", "MPC8349",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8349,      e300)
    POWERPC_DEF_SVR("mpc8349a", "MPC8349A",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8349A,     e300)
    POWERPC_DEF_SVR("mpc8349e", "MPC8349E",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8349E,     e300)
    POWERPC_DEF_SVR("mpc8349ea", "MPC8349EA",
                    CPU_POWERPC_MPC834x,      POWERPC_SVR_8349EA,    e300)
    POWERPC_DEF_SVR("mpc8377", "MPC8377",
                    CPU_POWERPC_MPC837x,      POWERPC_SVR_8377,      e300)
    POWERPC_DEF_SVR("mpc8377e", "MPC8377E",
                    CPU_POWERPC_MPC837x,      POWERPC_SVR_8377E,     e300)
    POWERPC_DEF_SVR("mpc8378", "MPC8378",
                    CPU_POWERPC_MPC837x,      POWERPC_SVR_8378,      e300)
    POWERPC_DEF_SVR("mpc8378e", "MPC8378E",
                    CPU_POWERPC_MPC837x,      POWERPC_SVR_8378E,     e300)
    POWERPC_DEF_SVR("mpc8379", "MPC8379",
                    CPU_POWERPC_MPC837x,      POWERPC_SVR_8379,      e300)
    POWERPC_DEF_SVR("mpc8379e", "MPC8379E",
                    CPU_POWERPC_MPC837x,      POWERPC_SVR_8379E,     e300)
    /* e500 family                                                           */
    POWERPC_DEF_SVR("e500_v10", "PowerPC e500 v1.0 core",
                    CPU_POWERPC_e500v1_v10,   POWERPC_SVR_E500,      e500v1);
    POWERPC_DEF_SVR("e500_v20", "PowerPC e500 v2.0 core",
                    CPU_POWERPC_e500v1_v20,   POWERPC_SVR_E500,      e500v1);
    POWERPC_DEF_SVR("e500v2_v10", "PowerPC e500v2 v1.0 core",
                    CPU_POWERPC_e500v2_v10,   POWERPC_SVR_E500,      e500v2);
    POWERPC_DEF_SVR("e500v2_v20", "PowerPC e500v2 v2.0 core",
                    CPU_POWERPC_e500v2_v20,   POWERPC_SVR_E500,      e500v2);
    POWERPC_DEF_SVR("e500v2_v21", "PowerPC e500v2 v2.1 core",
                    CPU_POWERPC_e500v2_v21,   POWERPC_SVR_E500,      e500v2);
    POWERPC_DEF_SVR("e500v2_v22", "PowerPC e500v2 v2.2 core",
                    CPU_POWERPC_e500v2_v22,   POWERPC_SVR_E500,      e500v2);
    POWERPC_DEF_SVR("e500v2_v30", "PowerPC e500v2 v3.0 core",
                    CPU_POWERPC_e500v2_v30,   POWERPC_SVR_E500,      e500v2);
    POWERPC_DEF_SVR("e500mc", "e500mc",
                    CPU_POWERPC_e500mc,       POWERPC_SVR_E500,      e500mc)
#ifdef TARGET_PPC64
    POWERPC_DEF_SVR("e5500", "e5500",
                    CPU_POWERPC_e5500,        POWERPC_SVR_E500,      e5500)
    POWERPC_DEF_SVR("e6500", "e6500",
                    CPU_POWERPC_e6500,        POWERPC_SVR_E500,      e6500)
#endif
    /* PowerPC e500 microcontrollers                                         */
    POWERPC_DEF_SVR("mpc8533_v10", "MPC8533 v1.0",
                    CPU_POWERPC_MPC8533_v10,  POWERPC_SVR_8533_v10,  e500v2)
    POWERPC_DEF_SVR("mpc8533_v11", "MPC8533 v1.1",
                    CPU_POWERPC_MPC8533_v11,  POWERPC_SVR_8533_v11,  e500v2)
    POWERPC_DEF_SVR("mpc8533e_v10", "MPC8533E v1.0",
                    CPU_POWERPC_MPC8533E_v10, POWERPC_SVR_8533E_v10, e500v2)
    POWERPC_DEF_SVR("mpc8533e_v11", "MPC8533E v1.1",
                    CPU_POWERPC_MPC8533E_v11, POWERPC_SVR_8533E_v11, e500v2)
    POWERPC_DEF_SVR("mpc8540_v10", "MPC8540 v1.0",
                    CPU_POWERPC_MPC8540_v10,  POWERPC_SVR_8540_v10,  e500v1)
    POWERPC_DEF_SVR("mpc8540_v20", "MPC8540 v2.0",
                    CPU_POWERPC_MPC8540_v20,  POWERPC_SVR_8540_v20,  e500v1)
    POWERPC_DEF_SVR("mpc8540_v21", "MPC8540 v2.1",
                    CPU_POWERPC_MPC8540_v21,  POWERPC_SVR_8540_v21,  e500v1)
    POWERPC_DEF_SVR("mpc8541_v10", "MPC8541 v1.0",
                    CPU_POWERPC_MPC8541_v10,  POWERPC_SVR_8541_v10,  e500v1)
    POWERPC_DEF_SVR("mpc8541_v11", "MPC8541 v1.1",
                    CPU_POWERPC_MPC8541_v11,  POWERPC_SVR_8541_v11,  e500v1)
    POWERPC_DEF_SVR("mpc8541e_v10", "MPC8541E v1.0",
                    CPU_POWERPC_MPC8541E_v10, POWERPC_SVR_8541E_v10, e500v1)
    POWERPC_DEF_SVR("mpc8541e_v11", "MPC8541E v1.1",
                    CPU_POWERPC_MPC8541E_v11, POWERPC_SVR_8541E_v11, e500v1)
    POWERPC_DEF_SVR("mpc8543_v10", "MPC8543 v1.0",
                    CPU_POWERPC_MPC8543_v10,  POWERPC_SVR_8543_v10,  e500v2)
    POWERPC_DEF_SVR("mpc8543_v11", "MPC8543 v1.1",
                    CPU_POWERPC_MPC8543_v11,  POWERPC_SVR_8543_v11,  e500v2)
    POWERPC_DEF_SVR("mpc8543_v20", "MPC8543 v2.0",
                    CPU_POWERPC_MPC8543_v20,  POWERPC_SVR_8543_v20,  e500v2)
    POWERPC_DEF_SVR("mpc8543_v21", "MPC8543 v2.1",
                    CPU_POWERPC_MPC8543_v21,  POWERPC_SVR_8543_v21,  e500v2)
    POWERPC_DEF_SVR("mpc8543e_v10", "MPC8543E v1.0",
                    CPU_POWERPC_MPC8543E_v10, POWERPC_SVR_8543E_v10, e500v2)
    POWERPC_DEF_SVR("mpc8543e_v11", "MPC8543E v1.1",
                    CPU_POWERPC_MPC8543E_v11, POWERPC_SVR_8543E_v11, e500v2)
    POWERPC_DEF_SVR("mpc8543e_v20", "MPC8543E v2.0",
                    CPU_POWERPC_MPC8543E_v20, POWERPC_SVR_8543E_v20, e500v2)
    POWERPC_DEF_SVR("mpc8543e_v21", "MPC8543E v2.1",
                    CPU_POWERPC_MPC8543E_v21, POWERPC_SVR_8543E_v21, e500v2)
    POWERPC_DEF_SVR("mpc8544_v10", "MPC8544 v1.0",
                    CPU_POWERPC_MPC8544_v10,  POWERPC_SVR_8544_v10,  e500v2)
    POWERPC_DEF_SVR("mpc8544_v11", "MPC8544 v1.1",
                    CPU_POWERPC_MPC8544_v11,  POWERPC_SVR_8544_v11,  e500v2)
    POWERPC_DEF_SVR("mpc8544e_v10", "MPC8544E v1.0",
                    CPU_POWERPC_MPC8544E_v10, POWERPC_SVR_8544E_v10, e500v2)
    POWERPC_DEF_SVR("mpc8544e_v11", "MPC8544E v1.1",
                    CPU_POWERPC_MPC8544E_v11, POWERPC_SVR_8544E_v11, e500v2)
    POWERPC_DEF_SVR("mpc8545_v20", "MPC8545 v2.0",
                    CPU_POWERPC_MPC8545_v20,  POWERPC_SVR_8545_v20,  e500v2)
    POWERPC_DEF_SVR("mpc8545_v21", "MPC8545 v2.1",
                    CPU_POWERPC_MPC8545_v21,  POWERPC_SVR_8545_v21,  e500v2)
    POWERPC_DEF_SVR("mpc8545e_v20", "MPC8545E v2.0",
                    CPU_POWERPC_MPC8545E_v20, POWERPC_SVR_8545E_v20, e500v2)
    POWERPC_DEF_SVR("mpc8545e_v21", "MPC8545E v2.1",
                    CPU_POWERPC_MPC8545E_v21, POWERPC_SVR_8545E_v21, e500v2)
    POWERPC_DEF_SVR("mpc8547e_v20", "MPC8547E v2.0",
                    CPU_POWERPC_MPC8547E_v20, POWERPC_SVR_8547E_v20, e500v2)
    POWERPC_DEF_SVR("mpc8547e_v21", "MPC8547E v2.1",
                    CPU_POWERPC_MPC8547E_v21, POWERPC_SVR_8547E_v21, e500v2)
    POWERPC_DEF_SVR("mpc8548_v10", "MPC8548 v1.0",
                    CPU_POWERPC_MPC8548_v10,  POWERPC_SVR_8548_v10,  e500v2)
    POWERPC_DEF_SVR("mpc8548_v11", "MPC8548 v1.1",
                    CPU_POWERPC_MPC8548_v11,  POWERPC_SVR_8548_v11,  e500v2)
    POWERPC_DEF_SVR("mpc8548_v20", "MPC8548 v2.0",
                    CPU_POWERPC_MPC8548_v20,  POWERPC_SVR_8548_v20,  e500v2)
    POWERPC_DEF_SVR("mpc8548_v21", "MPC8548 v2.1",
                    CPU_POWERPC_MPC8548_v21,  POWERPC_SVR_8548_v21,  e500v2)
    POWERPC_DEF_SVR("mpc8548e_v10", "MPC8548E v1.0",
                    CPU_POWERPC_MPC8548E_v10, POWERPC_SVR_8548E_v10, e500v2)
    POWERPC_DEF_SVR("mpc8548e_v11", "MPC8548E v1.1",
                    CPU_POWERPC_MPC8548E_v11, POWERPC_SVR_8548E_v11, e500v2)
    POWERPC_DEF_SVR("mpc8548e_v20", "MPC8548E v2.0",
                    CPU_POWERPC_MPC8548E_v20, POWERPC_SVR_8548E_v20, e500v2)
    POWERPC_DEF_SVR("mpc8548e_v21", "MPC8548E v2.1",
                    CPU_POWERPC_MPC8548E_v21, POWERPC_SVR_8548E_v21, e500v2)
    POWERPC_DEF_SVR("mpc8555_v10", "MPC8555 v1.0",
                    CPU_POWERPC_MPC8555_v10,  POWERPC_SVR_8555_v10,  e500v1)
    POWERPC_DEF_SVR("mpc8555_v11", "MPC8555 v1.1",
                    CPU_POWERPC_MPC8555_v11,  POWERPC_SVR_8555_v11,  e500v1)
    POWERPC_DEF_SVR("mpc8555e_v10", "MPC8555E v1.0",
                    CPU_POWERPC_MPC8555E_v10, POWERPC_SVR_8555E_v10, e500v1)
    POWERPC_DEF_SVR("mpc8555e_v11", "MPC8555E v1.1",
                    CPU_POWERPC_MPC8555E_v11, POWERPC_SVR_8555E_v11, e500v1)
    POWERPC_DEF_SVR("mpc8560_v10", "MPC8560 v1.0",
                    CPU_POWERPC_MPC8560_v10,  POWERPC_SVR_8560_v10,  e500v1)
    POWERPC_DEF_SVR("mpc8560_v20", "MPC8560 v2.0",
                    CPU_POWERPC_MPC8560_v20,  POWERPC_SVR_8560_v20,  e500v1)
    POWERPC_DEF_SVR("mpc8560_v21", "MPC8560 v2.1",
                    CPU_POWERPC_MPC8560_v21,  POWERPC_SVR_8560_v21,  e500v1)
    POWERPC_DEF_SVR("mpc8567", "MPC8567",
                    CPU_POWERPC_MPC8567,      POWERPC_SVR_8567,      e500v2)
    POWERPC_DEF_SVR("mpc8567e", "MPC8567E",
                    CPU_POWERPC_MPC8567E,     POWERPC_SVR_8567E,     e500v2)
    POWERPC_DEF_SVR("mpc8568", "MPC8568",
                    CPU_POWERPC_MPC8568,      POWERPC_SVR_8568,      e500v2)
    POWERPC_DEF_SVR("mpc8568e", "MPC8568E",
                    CPU_POWERPC_MPC8568E,     POWERPC_SVR_8568E,     e500v2)
    POWERPC_DEF_SVR("mpc8572", "MPC8572",
                    CPU_POWERPC_MPC8572,      POWERPC_SVR_8572,      e500v2)
    POWERPC_DEF_SVR("mpc8572e", "MPC8572E",
                    CPU_POWERPC_MPC8572E,     POWERPC_SVR_8572E,     e500v2)
    /* e600 family                                                           */
    POWERPC_DEF("e600",          CPU_POWERPC_e600,                   e600,
                "PowerPC e600 core")
    /* PowerPC e600 microcontrollers                                         */
    POWERPC_DEF_SVR("mpc8610", "MPC8610",
                    CPU_POWERPC_MPC8610,      POWERPC_SVR_8610,      e600)
    POWERPC_DEF_SVR("mpc8641", "MPC8641",
                    CPU_POWERPC_MPC8641,      POWERPC_SVR_8641,      e600)
    POWERPC_DEF_SVR("mpc8641d", "MPC8641D",
                    CPU_POWERPC_MPC8641D,     POWERPC_SVR_8641D,     e600)
    /* 32 bits "classic" PowerPC                                             */
    /* PowerPC 6xx family                                                    */
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
    POWERPC_DEF("7448_v1.0",     CPU_POWERPC_7448_v10,               7445,
                "PowerPC 7448 v1.0 (G4)")
    POWERPC_DEF("7448_v1.1",     CPU_POWERPC_7448_v11,               7445,
                "PowerPC 7448 v1.1 (G4)")
    POWERPC_DEF("7448_v2.0",     CPU_POWERPC_7448_v20,               7445,
                "PowerPC 7448 v2.0 (G4)")
    POWERPC_DEF("7448_v2.1",     CPU_POWERPC_7448_v21,               7445,
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
    POWERPC_DEF("7447a_v1.0",    CPU_POWERPC_74x7A_v10,              7445,
                "PowerPC 7447A v1.0 (G4)")
    POWERPC_DEF("7457a_v1.0",    CPU_POWERPC_74x7A_v10,              7455,
                "PowerPC 7457A v1.0 (G4)")
    POWERPC_DEF("7447a_v1.1",    CPU_POWERPC_74x7A_v11,              7445,
                "PowerPC 7447A v1.1 (G4)")
    POWERPC_DEF("7457a_v1.1",    CPU_POWERPC_74x7A_v11,              7455,
                "PowerPC 7457A v1.1 (G4)")
    POWERPC_DEF("7447a_v1.2",    CPU_POWERPC_74x7A_v12,              7445,
                "PowerPC 7447A v1.2 (G4)")
    POWERPC_DEF("7457a_v1.2",    CPU_POWERPC_74x7A_v12,              7455,
                "PowerPC 7457A v1.2 (G4)")
    /* 64 bits PowerPC                                                       */
#if defined(TARGET_PPC64)
    POWERPC_DEF("970_v2.2",      CPU_POWERPC_970_v22,                970,
                "PowerPC 970 v2.2")
    POWERPC_DEF("970fx_v1.0",    CPU_POWERPC_970FX_v10,              970,
                "PowerPC 970FX v1.0 (G5)")
    POWERPC_DEF("970fx_v2.0",    CPU_POWERPC_970FX_v20,              970,
                "PowerPC 970FX v2.0 (G5)")
    POWERPC_DEF("970fx_v2.1",    CPU_POWERPC_970FX_v21,              970,
                "PowerPC 970FX v2.1 (G5)")
    POWERPC_DEF("970fx_v3.0",    CPU_POWERPC_970FX_v30,              970,
                "PowerPC 970FX v3.0 (G5)")
    POWERPC_DEF("970fx_v3.1",    CPU_POWERPC_970FX_v31,              970,
                "PowerPC 970FX v3.1 (G5)")
    POWERPC_DEF("970mp_v1.0",    CPU_POWERPC_970MP_v10,              970,
                "PowerPC 970MP v1.0")
    POWERPC_DEF("970mp_v1.1",    CPU_POWERPC_970MP_v11,              970,
                "PowerPC 970MP v1.1")
    POWERPC_DEF("power5p_v2.1",  CPU_POWERPC_POWER5P_v21,            POWER5P,
                "POWER5+ v2.1")
    POWERPC_DEF("power7_v2.3",   CPU_POWERPC_POWER7_v23,             POWER7,
                "POWER7 v2.3")
    POWERPC_DEF("power7p_v2.1",  CPU_POWERPC_POWER7P_v21,            POWER7,
                "POWER7+ v2.1")
    POWERPC_DEF("power8e_v2.1",  CPU_POWERPC_POWER8E_v21,            POWER8,
                "POWER8E v2.1")
    POWERPC_DEF("power8_v2.0",   CPU_POWERPC_POWER8_v20,             POWER8,
                "POWER8 v2.0")
    POWERPC_DEF("power8nvl_v1.0", CPU_POWERPC_POWER8NVL_v10,         POWER8,
                "POWER8NVL v1.0")
    POWERPC_DEF("power9_v1.0",   CPU_POWERPC_POWER9_DD1,             POWER9,
                "POWER9 v1.0")
    POWERPC_DEF("power9_v2.0",   CPU_POWERPC_POWER9_DD20,            POWER9,
                "POWER9 v2.0")
    POWERPC_DEF("power9_v2.2",   CPU_POWERPC_POWER9_DD22,            POWER9,
                "POWER9 v2.2")
    POWERPC_DEF("power10_v1.0",  CPU_POWERPC_POWER10_DD1,            POWER10,
                "POWER10 v1.0")
    POWERPC_DEF("power10_v2.0",  CPU_POWERPC_POWER10_DD20,           POWER10,
                "POWER10 v2.0")
#endif /* defined (TARGET_PPC64) */

/***************************************************************************/
/* PowerPC CPU aliases                                                     */

PowerPCCPUAlias ppc_cpu_aliases[] = {
    { "405", "405d4" },
    { "405cr", "405crc" },
    { "405gp", "405gpd" },
    { "405gpe", "405crc" },
    { "x2vp7", "x2vp4" },
    { "x2vp50", "x2vp20" },

    { "440ep", "440epb" },
    { "460ex", "460exb" },
#if defined(TODO_USER_ONLY)
    { "440gp", "440gpc" },
    { "440gr", "440gra" },
    { "440gx", "440gxf" },

    { "rcpu", "mpc5xx" },
    /* MPC5xx microcontrollers */
    { "mgt560", "mpc5xx" },
    { "mpc509", "mpc5xx" },
    { "mpc533", "mpc5xx" },
    { "mpc534", "mpc5xx" },
    { "mpc555", "mpc5xx" },
    { "mpc556", "mpc5xx" },
    { "mpc560", "mpc5xx" },
    { "mpc561", "mpc5xx" },
    { "mpc562", "mpc5xx" },
    { "mpc563", "mpc5xx" },
    { "mpc564", "mpc5xx" },
    { "mpc565", "mpc5xx" },
    { "mpc566", "mpc5xx" },

    { "powerquicc", "mpc8xx" },
    /* MPC8xx microcontrollers */
    { "mgt823", "mpc8xx" },
    { "mpc821", "mpc8xx" },
    { "mpc823", "mpc8xx" },
    { "mpc850", "mpc8xx" },
    { "mpc852t", "mpc8xx" },
    { "mpc855t", "mpc8xx" },
    { "mpc857", "mpc8xx" },
    { "mpc859", "mpc8xx" },
    { "mpc860", "mpc8xx" },
    { "mpc862", "mpc8xx" },
    { "mpc866", "mpc8xx" },
    { "mpc870", "mpc8xx" },
    { "mpc875", "mpc8xx" },
    { "mpc880", "mpc8xx" },
    { "mpc885", "mpc8xx" },
#endif

    /* PowerPC MPC603 microcontrollers */
    { "mpc8240", "603" },

    { "mpc52xx", "mpc5200_v12" },
    { "mpc5200", "mpc5200_v12" },
    { "mpc5200b", "mpc5200b_v21" },

    { "mpc82xx", "g2legp3" },
    { "powerquicc-ii", "g2legp3" },
    { "mpc8241", "g2hip4" },
    { "mpc8245", "g2hip4" },
    { "mpc8247", "g2legp3" },
    { "mpc8248", "g2legp3" },
    { "mpc8250", "g2hip4" },
    { "mpc8250_hip3", "g2hip3" },
    { "mpc8250_hip4", "g2hip4" },
    { "mpc8255", "g2hip4" },
    { "mpc8255_hip3", "g2hip3" },
    { "mpc8255_hip4", "g2hip4" },
    { "mpc8260", "g2hip4" },
    { "mpc8260_hip3", "g2hip3" },
    { "mpc8260_hip4", "g2hip4" },
    { "mpc8264", "g2hip4" },
    { "mpc8264_hip3", "g2hip3" },
    { "mpc8264_hip4", "g2hip4" },
    { "mpc8265", "g2hip4" },
    { "mpc8265_hip3", "g2hip3" },
    { "mpc8265_hip4", "g2hip4" },
    { "mpc8266", "g2hip4" },
    { "mpc8266_hip3", "g2hip3" },
    { "mpc8266_hip4", "g2hip4" },
    { "mpc8270", "g2legp3" },
    { "mpc8271", "g2legp3" },
    { "mpc8272", "g2legp3" },
    { "mpc8275", "g2legp3" },
    { "mpc8280", "g2legp3" },
    { "e200", "e200z6" },
    { "e300", "e300c3" },
    { "mpc8347", "mpc8347t" },
    { "mpc8347a", "mpc8347at" },
    { "mpc8347e", "mpc8347et" },
    { "mpc8347ea", "mpc8347eat" },
    { "e500", "e500v2_v22" },
    { "e500v1", "e500_v20" },
    { "e500v2", "e500v2_v22" },
    { "mpc8533", "mpc8533_v11" },
    { "mpc8533e", "mpc8533e_v11" },
    { "mpc8540", "mpc8540_v21" },
    { "mpc8541", "mpc8541_v11" },
    { "mpc8541e", "mpc8541e_v11" },
    { "mpc8543", "mpc8543_v21" },
    { "mpc8543e", "mpc8543e_v21" },
    { "mpc8544", "mpc8544_v11" },
    { "mpc8544e", "mpc8544e_v11" },
    { "mpc8545", "mpc8545_v21" },
    { "mpc8545e", "mpc8545e_v21" },
    { "mpc8547e", "mpc8547e_v21" },
    { "mpc8548", "mpc8548_v21" },
    { "mpc8548e", "mpc8548e_v21" },
    { "mpc8555", "mpc8555_v11" },
    { "mpc8555e", "mpc8555e_v11" },
    { "mpc8560", "mpc8560_v21" },
    { "vanilla", "603" },
    { "603e", "603e_v4.1" },
    { "stretch", "603e_v4.1" },
    { "vaillant", "603e7v" },
    { "603r", "603e7t" },
    { "goldeneye", "603e7t" },
    { "604e", "604e_v2.4" },
    { "sirocco", "604e_v2.4" },
    { "mach5", "604r" },
    { "740", "740_v3.1" },
    { "arthur", "740_v3.1" },
    { "750", "750_v3.1" },
    { "typhoon", "750_v3.1" },
    { "g3",      "750_v3.1" },
    { "conan/doyle", "750p" },
    { "750cl", "750cl_v2.0" },
    { "750cx", "750cx_v2.2" },
    { "750cxe", "750cxe_v3.1b" },
    { "750fx", "750fx_v2.3" },
    { "750gx", "750gx_v1.2" },
    { "750l", "750l_v3.2" },
    { "lonestar", "750l_v3.2" },
    { "745", "745_v2.8" },
    { "755", "755_v2.8" },
    { "goldfinger", "755_v2.8" },
    { "7400", "7400_v2.9" },
    { "g4",  "7400_v2.9" },
    { "7410", "7410_v1.4" },
    { "nitro", "7410_v1.4" },
    { "7448", "7448_v2.1" },
    { "7450", "7450_v2.1" },
    { "vger", "7450_v2.1" },
    { "7441", "7441_v2.3" },
    { "7451", "7451_v2.3" },
    { "7445", "7445_v3.2" },
    { "7455", "7455_v3.2" },
    { "apollo6", "7455_v3.2" },
    { "7447", "7447_v1.1" },
    { "7457", "7457_v1.2" },
    { "apollo7", "7457_v1.2" },
    { "7447a", "7447a_v1.2" },
    { "7457a", "7457a_v1.2" },
    { "apollo7pm", "7457a_v1.0" },
#if defined(TARGET_PPC64)
    { "970", "970_v2.2" },
    { "970fx", "970fx_v3.1" },
    { "970mp", "970mp_v1.1" },
    { "power5+", "power5p_v2.1" },
    { "power5+_v2.1", "power5p_v2.1" },
    { "power5gs", "power5+_v2.1" },
    { "power7", "power7_v2.3" },
    { "power7+", "power7p_v2.1" },
    { "power7+_v2.1", "power7p_v2.1" },
    { "power8e", "power8e_v2.1" },
    { "power8", "power8_v2.0" },
    { "power8nvl", "power8nvl_v1.0" },
    { "power9", "power9_v2.2" },
    { "power10", "power10_v2.0" },
#endif

    /* Generic PowerPCs */
#if defined(TARGET_PPC64)
    { "ppc64", "970fx_v3.1" },
#endif
    { "ppc32", "604" },
    { "ppc", "604" },

    { NULL, NULL }
};
