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
#ifndef TARGET_PPC_CPU_MODELS_H
#define TARGET_PPC_CPU_MODELS_H

/**
 * PowerPCCPUAlias:
 * @alias: The alias name.
 * @model: The CPU model @alias refers to, that directly resolves into CPU type
 *
 * A mapping entry from CPU @alias to CPU @model.
 */
typedef struct PowerPCCPUAlias {
    const char *alias;
    const char *model;
} PowerPCCPUAlias;

extern PowerPCCPUAlias ppc_cpu_aliases[];

/*****************************************************************************/
/* PVR definitions for most known PowerPC                                    */
enum {
    /* IBM Processor for Network Resources */
    CPU_POWERPC_COBRA              = 0x10100000, /* XXX: 405 ? */
    /* PowerPC 405 family */
    /* PowerPC 405 cores */
    CPU_POWERPC_405D2              = 0x20010000,
    CPU_POWERPC_405D4              = 0x41810000,
    /* PowerPC 405 microcontrolers */
    /* XXX: missing 0x200108a0 */
    CPU_POWERPC_405CRa             = 0x40110041,
    CPU_POWERPC_405CRb             = 0x401100C5,
    CPU_POWERPC_405CRc             = 0x40110145,
    CPU_POWERPC_405EP              = 0x51210950,
    CPU_POWERPC_405EZ              = 0x41511460, /* 0x51210950 ? */
    CPU_POWERPC_405GPa             = 0x40110000,
    CPU_POWERPC_405GPb             = 0x40110040,
    CPU_POWERPC_405GPc             = 0x40110082,
    CPU_POWERPC_405GPd             = 0x401100C4,
    CPU_POWERPC_405GPR             = 0x50910951,
    CPU_POWERPC_405LP              = 0x41F10000,
    /* IBM network processors */
    CPU_POWERPC_NPE405H            = 0x414100C0,
    CPU_POWERPC_NPE405H2           = 0x41410140,
    CPU_POWERPC_NPE405L            = 0x416100C0,
    CPU_POWERPC_NPE4GS3            = 0x40B10000,
    /* IBM STBxxx (PowerPC 401/403/405 core based microcontrollers) */
    CPU_POWERPC_STB03              = 0x40310000, /* 0x40130000 ? */
    CPU_POWERPC_STB04              = 0x41810000,
    CPU_POWERPC_STB25              = 0x51510950,
    /* Xilinx cores */
    CPU_POWERPC_X2VP4              = 0x20010820,
    CPU_POWERPC_X2VP20             = 0x20010860,
    /* PowerPC 440 family */
    /* Generic PowerPC 440 */
#define CPU_POWERPC_440              CPU_POWERPC_440GXf
    /* PowerPC 440 cores */
    CPU_POWERPC_440_XILINX         = 0x7ff21910,
    /* PowerPC 440 microcontrolers */
    CPU_POWERPC_440EPa             = 0x42221850,
    CPU_POWERPC_440EPb             = 0x422218D3,
    CPU_POWERPC_440GPb             = 0x40120440,
    CPU_POWERPC_440GPc             = 0x40120481,
#define CPU_POWERPC_440GRa           CPU_POWERPC_440EPb
    CPU_POWERPC_440GRX             = 0x200008D0,
#define CPU_POWERPC_440EPX           CPU_POWERPC_440GRX
    CPU_POWERPC_440GXa             = 0x51B21850,
    CPU_POWERPC_440GXb             = 0x51B21851,
    CPU_POWERPC_440GXc             = 0x51B21892,
    CPU_POWERPC_440GXf             = 0x51B21894,
    CPU_POWERPC_440SP              = 0x53221850,
    CPU_POWERPC_440SP2             = 0x53221891,
    CPU_POWERPC_440SPE             = 0x53421890,
    CPU_POWERPC_460EXb             = 0x130218A4, /* called 460 but 440 core */
    /* Freescale embedded PowerPC cores */
    /* PowerPC MPC 5xx cores (aka RCPU) */
    CPU_POWERPC_MPC5xx             = 0x00020020,
    /* PowerPC MPC 8xx cores (aka PowerQUICC) */
    CPU_POWERPC_MPC8xx             = 0x00500000,
    /* G2 cores (aka PowerQUICC-II) */
    CPU_POWERPC_G2                 = 0x00810011,
    CPU_POWERPC_G2H4               = 0x80811010,
    CPU_POWERPC_G2gp               = 0x80821010,
    CPU_POWERPC_G2ls               = 0x90810010,
    CPU_POWERPC_MPC603             = 0x00810100,
    CPU_POWERPC_G2_HIP3            = 0x00810101,
    CPU_POWERPC_G2_HIP4            = 0x80811014,
    /*   G2_LE core (aka PowerQUICC-II) */
    CPU_POWERPC_G2LE               = 0x80820010,
    CPU_POWERPC_G2LEgp             = 0x80822010,
    CPU_POWERPC_G2LEls             = 0xA0822010,
    CPU_POWERPC_G2LEgp1            = 0x80822011,
    CPU_POWERPC_G2LEgp3            = 0x80822013,
    /* MPC52xx microcontrollers  */
    /* XXX: MPC 5121 ? */
#define CPU_POWERPC_MPC5200_v10      CPU_POWERPC_G2LEgp1
#define CPU_POWERPC_MPC5200_v11      CPU_POWERPC_G2LEgp1
#define CPU_POWERPC_MPC5200_v12      CPU_POWERPC_G2LEgp1
#define CPU_POWERPC_MPC5200B_v20     CPU_POWERPC_G2LEgp1
#define CPU_POWERPC_MPC5200B_v21     CPU_POWERPC_G2LEgp1
    /* e200 family */
    /* e200 cores */
    CPU_POWERPC_e200z5             = 0x81000000,
    CPU_POWERPC_e200z6             = 0x81120000,
    /* e300 family */
    /* e300 cores */
    CPU_POWERPC_e300c1             = 0x00830010,
    CPU_POWERPC_e300c2             = 0x00840010,
    CPU_POWERPC_e300c3             = 0x00850010,
    CPU_POWERPC_e300c4             = 0x00860010,
    /* MPC83xx microcontrollers */
#define CPU_POWERPC_MPC834x          CPU_POWERPC_e300c1
#define CPU_POWERPC_MPC837x          CPU_POWERPC_e300c4
    /* e500 family */
    /* e500 cores  */
#define CPU_POWERPC_e500             CPU_POWERPC_e500v2_v22
    CPU_POWERPC_e500v1_v10         = 0x80200010,
    CPU_POWERPC_e500v1_v20         = 0x80200020,
    CPU_POWERPC_e500v2_v10         = 0x80210010,
    CPU_POWERPC_e500v2_v11         = 0x80210011,
    CPU_POWERPC_e500v2_v20         = 0x80210020,
    CPU_POWERPC_e500v2_v21         = 0x80210021,
    CPU_POWERPC_e500v2_v22         = 0x80210022,
    CPU_POWERPC_e500v2_v30         = 0x80210030,
    CPU_POWERPC_e500mc             = 0x80230020,
    CPU_POWERPC_e5500              = 0x80240020,
    CPU_POWERPC_e6500              = 0x80400020,
    /* MPC85xx microcontrollers */
#define CPU_POWERPC_MPC8533_v10      CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8533_v11      CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8533E_v10     CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8533E_v11     CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8540_v10      CPU_POWERPC_e500v1_v10
#define CPU_POWERPC_MPC8540_v20      CPU_POWERPC_e500v1_v20
#define CPU_POWERPC_MPC8540_v21      CPU_POWERPC_e500v1_v20
#define CPU_POWERPC_MPC8541_v10      CPU_POWERPC_e500v1_v20
#define CPU_POWERPC_MPC8541_v11      CPU_POWERPC_e500v1_v20
#define CPU_POWERPC_MPC8541E_v10     CPU_POWERPC_e500v1_v20
#define CPU_POWERPC_MPC8541E_v11     CPU_POWERPC_e500v1_v20
#define CPU_POWERPC_MPC8543_v10      CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8543_v11      CPU_POWERPC_e500v2_v11
#define CPU_POWERPC_MPC8543_v20      CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8543_v21      CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8543E_v10     CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8543E_v11     CPU_POWERPC_e500v2_v11
#define CPU_POWERPC_MPC8543E_v20     CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8543E_v21     CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8544_v10      CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8544_v11      CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8544E_v11     CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8544E_v10     CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8545_v10      CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8545_v20      CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8545_v21      CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8545E_v10     CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8545E_v20     CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8545E_v21     CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8547E_v10     CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8547E_v20     CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8547E_v21     CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8548_v10      CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8548_v11      CPU_POWERPC_e500v2_v11
#define CPU_POWERPC_MPC8548_v20      CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8548_v21      CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8548E_v10     CPU_POWERPC_e500v2_v10
#define CPU_POWERPC_MPC8548E_v11     CPU_POWERPC_e500v2_v11
#define CPU_POWERPC_MPC8548E_v20     CPU_POWERPC_e500v2_v20
#define CPU_POWERPC_MPC8548E_v21     CPU_POWERPC_e500v2_v21
#define CPU_POWERPC_MPC8555_v10      CPU_POWERPC_e500v1_v20
#define CPU_POWERPC_MPC8555_v11      CPU_POWERPC_e500v1_v20
#define CPU_POWERPC_MPC8555E_v10     CPU_POWERPC_e500v1_v20
#define CPU_POWERPC_MPC8555E_v11     CPU_POWERPC_e500v1_v20
#define CPU_POWERPC_MPC8560_v10      CPU_POWERPC_e500v1_v10
#define CPU_POWERPC_MPC8560_v20      CPU_POWERPC_e500v1_v20
#define CPU_POWERPC_MPC8560_v21      CPU_POWERPC_e500v1_v20
#define CPU_POWERPC_MPC8567          CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8567E         CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8568          CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8568E         CPU_POWERPC_e500v2_v22
#define CPU_POWERPC_MPC8572          CPU_POWERPC_e500v2_v30
#define CPU_POWERPC_MPC8572E         CPU_POWERPC_e500v2_v30
    /* e600 family */
    /* e600 cores */
    CPU_POWERPC_e600               = 0x80040010,
    /* MPC86xx microcontrollers */
#define CPU_POWERPC_MPC8610          CPU_POWERPC_e600
#define CPU_POWERPC_MPC8641          CPU_POWERPC_e600
#define CPU_POWERPC_MPC8641D         CPU_POWERPC_e600
    /* PowerPC 6xx cores */
    CPU_POWERPC_603                = 0x00030100,
    CPU_POWERPC_603E_v11           = 0x00060101,
    CPU_POWERPC_603E_v12           = 0x00060102,
    CPU_POWERPC_603E_v13           = 0x00060103,
    CPU_POWERPC_603E_v14           = 0x00060104,
    CPU_POWERPC_603E_v22           = 0x00060202,
    CPU_POWERPC_603E_v3            = 0x00060300,
    CPU_POWERPC_603E_v4            = 0x00060400,
    CPU_POWERPC_603E_v41           = 0x00060401,
    CPU_POWERPC_603E7t             = 0x00071201,
    CPU_POWERPC_603E7v             = 0x00070100,
    CPU_POWERPC_603E7v1            = 0x00070101,
    CPU_POWERPC_603E7v2            = 0x00070201,
    CPU_POWERPC_603E7              = 0x00070200,
    CPU_POWERPC_603P               = 0x00070000,
    /* XXX: missing 0x00040303 (604) */
    CPU_POWERPC_604                = 0x00040103,
    /* XXX: missing 0x00091203 */
    /* XXX: missing 0x00092110 */
    /* XXX: missing 0x00092120 */
    CPU_POWERPC_604E_v10           = 0x00090100,
    CPU_POWERPC_604E_v22           = 0x00090202,
    CPU_POWERPC_604E_v24           = 0x00090204,
    /* XXX: missing 0x000a0100 */
    /* XXX: missing 0x00093102 */
    CPU_POWERPC_604R               = 0x000a0101,
    /* PowerPC 740/750 cores (aka G3) */
    /* XXX: missing 0x00084202 */
    CPU_POWERPC_7x0_v10            = 0x00080100,
    CPU_POWERPC_7x0_v20            = 0x00080200,
    CPU_POWERPC_7x0_v21            = 0x00080201,
    CPU_POWERPC_7x0_v22            = 0x00080202,
    CPU_POWERPC_7x0_v30            = 0x00080300,
    CPU_POWERPC_7x0_v31            = 0x00080301,
    CPU_POWERPC_740E               = 0x00080100,
    CPU_POWERPC_750E               = 0x00080200,
    CPU_POWERPC_7x0P               = 0x10080000,
    /* XXX: missing 0x00087010 (CL ?) */
    CPU_POWERPC_750CL_v10          = 0x00087200,
    CPU_POWERPC_750CL_v20          = 0x00087210, /* aka rev E */
    CPU_POWERPC_750CX_v10          = 0x00082100,
    CPU_POWERPC_750CX_v20          = 0x00082200,
    CPU_POWERPC_750CX_v21          = 0x00082201,
    CPU_POWERPC_750CX_v22          = 0x00082202,
    CPU_POWERPC_750CXE_v21         = 0x00082211,
    CPU_POWERPC_750CXE_v22         = 0x00082212,
    CPU_POWERPC_750CXE_v23         = 0x00082213,
    CPU_POWERPC_750CXE_v24         = 0x00082214,
    CPU_POWERPC_750CXE_v24b        = 0x00083214,
    CPU_POWERPC_750CXE_v30         = 0x00082310,
    CPU_POWERPC_750CXE_v31         = 0x00082311,
    CPU_POWERPC_750CXE_v31b        = 0x00083311,
    CPU_POWERPC_750CXR             = 0x00083410,
    CPU_POWERPC_750FL              = 0x70000203,
    CPU_POWERPC_750FX_v10          = 0x70000100,
    CPU_POWERPC_750FX_v20          = 0x70000200,
    CPU_POWERPC_750FX_v21          = 0x70000201,
    CPU_POWERPC_750FX_v22          = 0x70000202,
    CPU_POWERPC_750FX_v23          = 0x70000203,
    CPU_POWERPC_750GL              = 0x70020102,
    CPU_POWERPC_750GX_v10          = 0x70020100,
    CPU_POWERPC_750GX_v11          = 0x70020101,
    CPU_POWERPC_750GX_v12          = 0x70020102,
    CPU_POWERPC_750L_v20           = 0x00088200,
    CPU_POWERPC_750L_v21           = 0x00088201,
    CPU_POWERPC_750L_v22           = 0x00088202,
    CPU_POWERPC_750L_v30           = 0x00088300,
    CPU_POWERPC_750L_v32           = 0x00088302,
    /* PowerPC 745/755 cores */
    CPU_POWERPC_7x5_v10            = 0x00083100,
    CPU_POWERPC_7x5_v11            = 0x00083101,
    CPU_POWERPC_7x5_v20            = 0x00083200,
    CPU_POWERPC_7x5_v21            = 0x00083201,
    CPU_POWERPC_7x5_v22            = 0x00083202, /* aka D */
    CPU_POWERPC_7x5_v23            = 0x00083203, /* aka E */
    CPU_POWERPC_7x5_v24            = 0x00083204,
    CPU_POWERPC_7x5_v25            = 0x00083205,
    CPU_POWERPC_7x5_v26            = 0x00083206,
    CPU_POWERPC_7x5_v27            = 0x00083207,
    CPU_POWERPC_7x5_v28            = 0x00083208,
    /* PowerPC 74xx cores (aka G4) */
    /* XXX: missing 0x000C1101 */
    CPU_POWERPC_7400_v10           = 0x000C0100,
    CPU_POWERPC_7400_v11           = 0x000C0101,
    CPU_POWERPC_7400_v20           = 0x000C0200,
    CPU_POWERPC_7400_v21           = 0x000C0201,
    CPU_POWERPC_7400_v22           = 0x000C0202,
    CPU_POWERPC_7400_v26           = 0x000C0206,
    CPU_POWERPC_7400_v27           = 0x000C0207,
    CPU_POWERPC_7400_v28           = 0x000C0208,
    CPU_POWERPC_7400_v29           = 0x000C0209,
    CPU_POWERPC_7410_v10           = 0x800C1100,
    CPU_POWERPC_7410_v11           = 0x800C1101,
    CPU_POWERPC_7410_v12           = 0x800C1102, /* aka C */
    CPU_POWERPC_7410_v13           = 0x800C1103, /* aka D */
    CPU_POWERPC_7410_v14           = 0x800C1104, /* aka E */
    CPU_POWERPC_7448_v10           = 0x80040100,
    CPU_POWERPC_7448_v11           = 0x80040101,
    CPU_POWERPC_7448_v20           = 0x80040200,
    CPU_POWERPC_7448_v21           = 0x80040201,
    CPU_POWERPC_7450_v10           = 0x80000100,
    CPU_POWERPC_7450_v11           = 0x80000101,
    CPU_POWERPC_7450_v12           = 0x80000102,
    CPU_POWERPC_7450_v20           = 0x80000200, /* aka A, B, C, D: 2.04 */
    CPU_POWERPC_7450_v21           = 0x80000201, /* aka E */
    CPU_POWERPC_74x1_v23           = 0x80000203, /* aka G: 2.3 */
    /* XXX: this entry might be a bug in some documentation */
    CPU_POWERPC_74x1_v210          = 0x80000210, /* aka G: 2.3 ? */
    CPU_POWERPC_74x5_v10           = 0x80010100,
    /* XXX: missing 0x80010200 */
    CPU_POWERPC_74x5_v21           = 0x80010201, /* aka C: 2.1 */
    CPU_POWERPC_74x5_v32           = 0x80010302,
    CPU_POWERPC_74x5_v33           = 0x80010303, /* aka F: 3.3 */
    CPU_POWERPC_74x5_v34           = 0x80010304, /* aka G: 3.4 */
    CPU_POWERPC_74x7_v10           = 0x80020100, /* aka A: 1.0 */
    CPU_POWERPC_74x7_v11           = 0x80020101, /* aka B: 1.1 */
    CPU_POWERPC_74x7_v12           = 0x80020102, /* aka C: 1.2 */
    CPU_POWERPC_74x7A_v10          = 0x80030100, /* aka A: 1.0 */
    CPU_POWERPC_74x7A_v11          = 0x80030101, /* aka B: 1.1 */
    CPU_POWERPC_74x7A_v12          = 0x80030102, /* aka C: 1.2 */
    /* 64 bits PowerPC */
#if defined(TARGET_PPC64)
    CPU_POWERPC_620                = 0x00140000,
    CPU_POWERPC_630                = 0x00400000,
    CPU_POWERPC_631                = 0x00410104,
    CPU_POWERPC_POWER4             = 0x00350000,
    CPU_POWERPC_POWER4P            = 0x00380000,
     /* XXX: missing 0x003A0201 */
    CPU_POWERPC_POWER5             = 0x003A0203,
    CPU_POWERPC_POWER5P_v21        = 0x003B0201,
    CPU_POWERPC_POWER6             = 0x003E0000,
    CPU_POWERPC_POWER_SERVER_MASK  = 0xFFFF0000,
    CPU_POWERPC_POWER7_BASE        = 0x003F0000,
    CPU_POWERPC_POWER7_v23         = 0x003F0203,
    CPU_POWERPC_POWER7P_BASE       = 0x004A0000,
    CPU_POWERPC_POWER7P_v21        = 0x004A0201,
    CPU_POWERPC_POWER8E_BASE       = 0x004B0000,
    CPU_POWERPC_POWER8E_v21        = 0x004B0201,
    CPU_POWERPC_POWER8_BASE        = 0x004D0000,
    CPU_POWERPC_POWER8_v20         = 0x004D0200,
    CPU_POWERPC_POWER8NVL_BASE     = 0x004C0000,
    CPU_POWERPC_POWER8NVL_v10      = 0x004C0100,
    CPU_POWERPC_POWER9_BASE        = 0x004E0000,
    CPU_POWERPC_POWER9_DD1         = 0x004E1100,
    CPU_POWERPC_POWER9_DD20        = 0x004E1200,
    CPU_POWERPC_POWER9_DD22        = 0x004E1202,
    CPU_POWERPC_POWER10_BASE       = 0x00800000,
    CPU_POWERPC_POWER10_DD1        = 0x00801100,
    CPU_POWERPC_POWER10_DD20       = 0x00801200,
    CPU_POWERPC_970_v22            = 0x00390202,
    CPU_POWERPC_970FX_v10          = 0x00391100,
    CPU_POWERPC_970FX_v20          = 0x003C0200,
    CPU_POWERPC_970FX_v21          = 0x003C0201,
    CPU_POWERPC_970FX_v30          = 0x003C0300,
    CPU_POWERPC_970FX_v31          = 0x003C0301,
    CPU_POWERPC_970MP_v10          = 0x00440100,
    CPU_POWERPC_970MP_v11          = 0x00440101,
#define CPU_POWERPC_CELL             CPU_POWERPC_CELL_v32
    CPU_POWERPC_CELL_v10           = 0x00700100,
    CPU_POWERPC_CELL_v20           = 0x00700400,
    CPU_POWERPC_CELL_v30           = 0x00700500,
    CPU_POWERPC_CELL_v31           = 0x00700501,
#define CPU_POWERPC_CELL_v32         CPU_POWERPC_CELL_v31
    CPU_POWERPC_RS64               = 0x00330000,
    CPU_POWERPC_RS64II             = 0x00340000,
    CPU_POWERPC_RS64III            = 0x00360000,
    CPU_POWERPC_RS64IV             = 0x00370000,
#endif /* defined(TARGET_PPC64) */
    /* Original POWER */
    /*
     * XXX: should be POWER (RIOS), RSC3308, RSC4608,
     * POWER2 (RIOS2) & RSC2 (P2SC) here
     */
    /* PA Semi core */
    CPU_POWERPC_PA6T               = 0x00900000,
};

/* Logical PVR definitions for sPAPR */
enum {
    CPU_POWERPC_LOGICAL_2_04       = 0x0F000001,
    CPU_POWERPC_LOGICAL_2_05       = 0x0F000002,
    CPU_POWERPC_LOGICAL_2_06       = 0x0F000003,
    CPU_POWERPC_LOGICAL_2_06_PLUS  = 0x0F100003,
    CPU_POWERPC_LOGICAL_2_07       = 0x0F000004,
    CPU_POWERPC_LOGICAL_3_00       = 0x0F000005,
    CPU_POWERPC_LOGICAL_3_10       = 0x0F000006,
};

/* System version register (used on MPC 8xxx)                                */
enum {
    POWERPC_SVR_NONE               = 0x00000000,
    POWERPC_SVR_5200_v10           = 0x80110010,
    POWERPC_SVR_5200_v11           = 0x80110011,
    POWERPC_SVR_5200_v12           = 0x80110012,
    POWERPC_SVR_5200B_v20          = 0x80110020,
    POWERPC_SVR_5200B_v21          = 0x80110021,
#define POWERPC_SVR_55xx             POWERPC_SVR_5567
    POWERPC_SVR_8343               = 0x80570010,
    POWERPC_SVR_8343A              = 0x80570030,
    POWERPC_SVR_8343E              = 0x80560010,
    POWERPC_SVR_8343EA             = 0x80560030,
    POWERPC_SVR_8347P              = 0x80550010, /* PBGA package */
    POWERPC_SVR_8347T              = 0x80530010, /* TBGA package */
    POWERPC_SVR_8347AP             = 0x80550030, /* PBGA package */
    POWERPC_SVR_8347AT             = 0x80530030, /* TBGA package */
    POWERPC_SVR_8347EP             = 0x80540010, /* PBGA package */
    POWERPC_SVR_8347ET             = 0x80520010, /* TBGA package */
    POWERPC_SVR_8347EAP            = 0x80540030, /* PBGA package */
    POWERPC_SVR_8347EAT            = 0x80520030, /* TBGA package */
    POWERPC_SVR_8349               = 0x80510010,
    POWERPC_SVR_8349A              = 0x80510030,
    POWERPC_SVR_8349E              = 0x80500010,
    POWERPC_SVR_8349EA             = 0x80500030,
#define POWERPC_SVR_E500             0x40000000
    POWERPC_SVR_8377               = 0x80C70010 | POWERPC_SVR_E500,
    POWERPC_SVR_8377E              = 0x80C60010 | POWERPC_SVR_E500,
    POWERPC_SVR_8378               = 0x80C50010 | POWERPC_SVR_E500,
    POWERPC_SVR_8378E              = 0x80C40010 | POWERPC_SVR_E500,
    POWERPC_SVR_8379               = 0x80C30010 | POWERPC_SVR_E500,
    POWERPC_SVR_8379E              = 0x80C00010 | POWERPC_SVR_E500,
    POWERPC_SVR_8533_v10           = 0x80340010 | POWERPC_SVR_E500,
    POWERPC_SVR_8533_v11           = 0x80340011 | POWERPC_SVR_E500,
    POWERPC_SVR_8533E_v10          = 0x803C0010 | POWERPC_SVR_E500,
    POWERPC_SVR_8533E_v11          = 0x803C0011 | POWERPC_SVR_E500,
    POWERPC_SVR_8540_v10           = 0x80300010 | POWERPC_SVR_E500,
    POWERPC_SVR_8540_v20           = 0x80300020 | POWERPC_SVR_E500,
    POWERPC_SVR_8540_v21           = 0x80300021 | POWERPC_SVR_E500,
    POWERPC_SVR_8541_v10           = 0x80720010 | POWERPC_SVR_E500,
    POWERPC_SVR_8541_v11           = 0x80720011 | POWERPC_SVR_E500,
    POWERPC_SVR_8541E_v10          = 0x807A0010 | POWERPC_SVR_E500,
    POWERPC_SVR_8541E_v11          = 0x807A0011 | POWERPC_SVR_E500,
    POWERPC_SVR_8543_v10           = 0x80320010 | POWERPC_SVR_E500,
    POWERPC_SVR_8543_v11           = 0x80320011 | POWERPC_SVR_E500,
    POWERPC_SVR_8543_v20           = 0x80320020 | POWERPC_SVR_E500,
    POWERPC_SVR_8543_v21           = 0x80320021 | POWERPC_SVR_E500,
    POWERPC_SVR_8543E_v10          = 0x803A0010 | POWERPC_SVR_E500,
    POWERPC_SVR_8543E_v11          = 0x803A0011 | POWERPC_SVR_E500,
    POWERPC_SVR_8543E_v20          = 0x803A0020 | POWERPC_SVR_E500,
    POWERPC_SVR_8543E_v21          = 0x803A0021 | POWERPC_SVR_E500,
    POWERPC_SVR_8544_v10           = 0x80340110 | POWERPC_SVR_E500,
    POWERPC_SVR_8544_v11           = 0x80340111 | POWERPC_SVR_E500,
    POWERPC_SVR_8544E_v10          = 0x803C0110 | POWERPC_SVR_E500,
    POWERPC_SVR_8544E_v11          = 0x803C0111 | POWERPC_SVR_E500,
    POWERPC_SVR_8545_v20           = 0x80310220 | POWERPC_SVR_E500,
    POWERPC_SVR_8545_v21           = 0x80310221 | POWERPC_SVR_E500,
    POWERPC_SVR_8545E_v20          = 0x80390220 | POWERPC_SVR_E500,
    POWERPC_SVR_8545E_v21          = 0x80390221 | POWERPC_SVR_E500,
    POWERPC_SVR_8547E_v20          = 0x80390120 | POWERPC_SVR_E500,
    POWERPC_SVR_8547E_v21          = 0x80390121 | POWERPC_SVR_E500,
    POWERPC_SVR_8548_v10           = 0x80310010 | POWERPC_SVR_E500,
    POWERPC_SVR_8548_v11           = 0x80310011 | POWERPC_SVR_E500,
    POWERPC_SVR_8548_v20           = 0x80310020 | POWERPC_SVR_E500,
    POWERPC_SVR_8548_v21           = 0x80310021 | POWERPC_SVR_E500,
    POWERPC_SVR_8548E_v10          = 0x80390010 | POWERPC_SVR_E500,
    POWERPC_SVR_8548E_v11          = 0x80390011 | POWERPC_SVR_E500,
    POWERPC_SVR_8548E_v20          = 0x80390020 | POWERPC_SVR_E500,
    POWERPC_SVR_8548E_v21          = 0x80390021 | POWERPC_SVR_E500,
    POWERPC_SVR_8555_v10           = 0x80710010 | POWERPC_SVR_E500,
    POWERPC_SVR_8555_v11           = 0x80710011 | POWERPC_SVR_E500,
    POWERPC_SVR_8555E_v10          = 0x80790010 | POWERPC_SVR_E500,
    POWERPC_SVR_8555E_v11          = 0x80790011 | POWERPC_SVR_E500,
    POWERPC_SVR_8560_v10           = 0x80700010 | POWERPC_SVR_E500,
    POWERPC_SVR_8560_v20           = 0x80700020 | POWERPC_SVR_E500,
    POWERPC_SVR_8560_v21           = 0x80700021 | POWERPC_SVR_E500,
    POWERPC_SVR_8567               = 0x80750111 | POWERPC_SVR_E500,
    POWERPC_SVR_8567E              = 0x807D0111 | POWERPC_SVR_E500,
    POWERPC_SVR_8568               = 0x80750011 | POWERPC_SVR_E500,
    POWERPC_SVR_8568E              = 0x807D0011 | POWERPC_SVR_E500,
    POWERPC_SVR_8572               = 0x80E00010 | POWERPC_SVR_E500,
    POWERPC_SVR_8572E              = 0x80E80010 | POWERPC_SVR_E500,
    POWERPC_SVR_8610               = 0x80A00011,
    POWERPC_SVR_8641               = 0x80900021,
    POWERPC_SVR_8641D              = 0x80900121,
};

#endif
