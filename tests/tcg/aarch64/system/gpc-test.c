/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 *
 * Copyright (c) 2026 Linaro Ltd
 *
 */

#include <stdbool.h>
#include <stdint.h>
#include <minilib.h>
#include "boot.h"

#define ID_AA64PFR0_EL1 "S3_0_C0_C4_0"

#define GPTBR_EL3 "S3_6_C2_C1_4"
#define GPCBW_EL3 "S3_6_C2_C1_5"
#define GPCCR_EL3 "S3_6_C2_C1_6"
#define VBAR_EL3 "S3_6_C12_C0_0"

#define get_sys_reg(register_name, dest) \
        asm("mrs %[reg], " register_name "\n\t" : [reg] "=r" (dest))
#define set_sys_reg(register_name, value) \
        asm("msr " register_name ", %[reg]\n\r" : : [reg] "r" (value))

const uint32_t gpc_granule_size = 4096;
const uint32_t gpis_per_64_bits = 16;

int main(uint64_t sp)
{
    uint64_t out;
    uint64_t pfr0;
    uint64_t gpt_base;
    uint64_t rme_status;
    uint64_t currentel_raw;
    uint64_t currentel;
    uint64_t gpcbw;
    uint64_t gpt_table0_addr = (uint64_t) realms_gpt0;
    uint64_t gpt_table1_addr = (uint64_t) realms_gpt1;

    /* Mask is FNG1, FNG0, and A2 */
    const uint64_t feature_mask = (1ULL << 18 | 1ULL << 17 | 1ULL << 16);
    const uint64_t in = feature_mask;

    get_sys_reg("CurrentEL", currentel_raw);
    currentel = (currentel_raw >> 2) & 0x3;

    if (currentel < 3) {
        ml_printf("FAIL: Test must be run at EL3 (it is %d)\n", currentel);
        return 1;
    }

    get_sys_reg(ID_AA64PFR0_EL1, pfr0);

    /* rme_status is 1 for RME, 2 for RME + GPC2, 3 for RME+GPC3 */
    rme_status = (pfr0 >> 52) & 0xF;
    if (rme_status < 2) {
        ml_printf("SKIP: System does not support RME (RME=%ld)\n", rme_status);
        return 0;
    }

    /* Configure the level 0 table for the first 4GB of memory */
    realms_gpt0[0] = gpt_table1_addr | 0x3; /* Covers GB 0; table descriptor */
    realms_gpt0[1] = 0xf1; /* Covers GB 1; full access */
    realms_gpt0[2] = 0xf1; /* Covers GB 2; full access */
    realms_gpt0[3] = 0xf1; /* Covers GB 3; full access */

    /* Pick an artibtrary location to read inside the first 1GB. */
    uint64_t fault_location = 0x10202008;
    uint32_t gpi_index = fault_location / gpc_granule_size;
    realms_gpt1[gpi_index / gpis_per_64_bits] = 0;

    gpt_base = gpt_table0_addr >> 12;
    set_sys_reg(GPTBR_EL3, gpt_base);

    /*
     * Default values:
     * PPS=0:     GPC table 0 protects 4GB.
     * RLPAD=0:   Realm physical address spaces are normal
     * NSPAD=0:   Non-secure physical address spaces are normal
     * SPAD=0:    Secure physical address spaces are normal
     * IRGN=0:    Inner non-cacheable
     * ORGN=0:    Outer non-cacheable
     * PGS=0:     Physical granule size is 4KB.
     * GPCP=0:    All GPC faults reported
     * TBGPCP=0:  Trace buffer rejects trace
     * L0GPTSZ=0: Each entry in table 0 protects 1GB.
     * APPSAA=0:  Accesses above 4GB must be to Non-secure PAs
     * GPCBW=0:   Bypass windows disabled.
     * NA6, NA7, NSP, SA, NSO are all reserved values for GPI.
     */
    uint64_t gpccr = 0;

    /* Switch on granule protection check */
    gpccr |= 1 << 16; /* GPC enabled. */
    gpccr |= 0b10 << 12; /* SH = Outer shareable */
    set_sys_reg(GPCCR_EL3, gpccr);

    /* Access some memory outside the GPC forbidden region */
    uint64_t x = *(unsigned int *) (fault_location + 4096 * 16);
    ml_printf("Fault address: %lx\n", exception_fault_address);
    if (exception_fault_address != 0) {
        ml_printf("FAIL: Memory access was blocked by GPC, "
                  "and should not have been\n");
        return 1;
    }

    /* Access the GPC forbidden region */
    x = *(unsigned int *) fault_location;

    ml_printf("Fault address: %lx\n", exception_fault_address);
    if (exception_fault_address != fault_location) {
        ml_printf("FAIL: Memory access was not blocked by GPC, "
                  "and should have been\n");
        return 1;
    }

    rme_status = (pfr0 >> 52) & 0xF;
    if (rme_status < 3) {
        ml_printf("SKIP: System does not support GPC3 (RME=%ld)\n", rme_status);
        return 0;
    }

    /* Clear the exception record */
    exception_fault_address = 0;

    /* Enable bypass windows */
    gpccr |= 1 << 29; /* GPC Bypass windows enabled */
    set_sys_reg(GPCCR_EL3, gpccr);

    gpcbw = 0; /* Base 0GB, Size 1GB, Stride 1TB */
    set_sys_reg(GPCBW_EL3, gpcbw);
    ml_printf("GPCBW configured\n");

    /* Access the GPC forbidden region again */
    x = *(unsigned int *) fault_location;

    ml_printf("Fault address: %lx\n", exception_fault_address);
    if (exception_fault_address != 0) {
        ml_printf("FAIL: Memory access was blocked by GPC, "
                  "and should have been allowed by bypass window. code=%lx\n",
                  exception_type_code);
        return 1;
    }

    /* Clear the exception record */
    exception_fault_address = 0;
    /* Reconfigure GPCBW to 1GB start */
    gpcbw = 1; /* Base 1GB, Size 1GB, Stride 1TB */
    set_sys_reg(GPCBW_EL3, gpcbw);
    ml_printf("GPCBW reconfigured for 1GB start\n");

    /* Access the GPC forbidden region again */
    x = *(unsigned int *) fault_location;

    ml_printf("Fault address: %lx\n", exception_fault_address);
    if (exception_fault_address != fault_location) {
        ml_printf("FAIL: Memory access was allowed by GPC, "
                  "and should not have been allowed by bypass window. code=%lx\n",
                  exception_type_code);
        return 1;
    }

    return 0;
}
