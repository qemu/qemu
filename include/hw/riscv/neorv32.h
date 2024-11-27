/*
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
#ifndef HW_NEORV32_H
#define HW_NEORV32_H

#include "hw/riscv/riscv_hart.h"
#include "hw/boards.h"

#if defined(TARGET_RISCV32)
#define NEORV32_CPU TYPE_RISCV_CPU_NEORV32
#endif

#define TYPE_RISCV_NEORV32_SOC "riscv.neorv32.soc"
#define RISCV_NEORV32_SOC(obj) \
    OBJECT_CHECK(Neorv32SoCState, (obj), TYPE_RISCV_NEORV32_SOC)

typedef struct Neorv32SoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
    MemoryRegion imem_region;
    MemoryRegion bootloader_rom;
} Neorv32SoCState;

typedef struct Neorv32State {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    Neorv32SoCState soc;
} Neorv32State;

#define TYPE_NEORV32_MACHINE MACHINE_TYPE_NAME("neorv32")
#define NEORV32_MACHINE(obj) \
    OBJECT_CHECK(Neorv32State, (obj), TYPE_NEORV32_MACHINE)

enum {
	NEORV32_IMEM,
	NEORV32_BOOTLOADER_ROM,
	NEORV32_DMEM,
	NEORV32_SYSINFO,
	NEORV32_UART0,
	NEORV32_SPI0,
};

#endif //HW_NEORV32_H
