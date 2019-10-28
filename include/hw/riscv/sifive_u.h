/*
 * SiFive U series machine interface
 *
 * Copyright (c) 2017 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_SIFIVE_U_H
#define HW_SIFIVE_U_H

#include "hw/net/cadence_gem.h"
#include "hw/riscv/riscv_hart.h"
#include "hw/riscv/sifive_cpu.h"
#include "hw/riscv/sifive_u_prci.h"
#include "hw/riscv/sifive_u_otp.h"

#define TYPE_RISCV_U_SOC "riscv.sifive.u.soc"
#define RISCV_U_SOC(obj) \
    OBJECT_CHECK(SiFiveUSoCState, (obj), TYPE_RISCV_U_SOC)

typedef struct SiFiveUSoCState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    CPUClusterState e_cluster;
    CPUClusterState u_cluster;
    RISCVHartArrayState e_cpus;
    RISCVHartArrayState u_cpus;
    DeviceState *plic;
    SiFiveUPRCIState prci;
    SiFiveUOTPState otp;
    CadenceGEMState gem;
} SiFiveUSoCState;

#define TYPE_RISCV_U_MACHINE MACHINE_TYPE_NAME("sifive_u")
#define RISCV_U_MACHINE(obj) \
    OBJECT_CHECK(SiFiveUState, (obj), TYPE_RISCV_U_MACHINE)

typedef struct SiFiveUState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    SiFiveUSoCState soc;

    void *fdt;
    int fdt_size;

    bool start_in_flash;
} SiFiveUState;

enum {
    SIFIVE_U_DEBUG,
    SIFIVE_U_MROM,
    SIFIVE_U_CLINT,
    SIFIVE_U_L2LIM,
    SIFIVE_U_PLIC,
    SIFIVE_U_PRCI,
    SIFIVE_U_UART0,
    SIFIVE_U_UART1,
    SIFIVE_U_OTP,
    SIFIVE_U_FLASH0,
    SIFIVE_U_DRAM,
    SIFIVE_U_GEM,
    SIFIVE_U_GEM_MGMT
};

enum {
    SIFIVE_U_UART0_IRQ = 4,
    SIFIVE_U_UART1_IRQ = 5,
    SIFIVE_U_GEM_IRQ = 0x35
};

enum {
    SIFIVE_U_HFCLK_FREQ = 33333333,
    SIFIVE_U_RTCCLK_FREQ = 1000000
};

#define SIFIVE_U_MANAGEMENT_CPU_COUNT   1
#define SIFIVE_U_COMPUTE_CPU_COUNT      4

#define SIFIVE_U_PLIC_HART_CONFIG "MS"
#define SIFIVE_U_PLIC_NUM_SOURCES 54
#define SIFIVE_U_PLIC_NUM_PRIORITIES 7
#define SIFIVE_U_PLIC_PRIORITY_BASE 0x04
#define SIFIVE_U_PLIC_PENDING_BASE 0x1000
#define SIFIVE_U_PLIC_ENABLE_BASE 0x2000
#define SIFIVE_U_PLIC_ENABLE_STRIDE 0x80
#define SIFIVE_U_PLIC_CONTEXT_BASE 0x200000
#define SIFIVE_U_PLIC_CONTEXT_STRIDE 0x1000

#endif
