/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "target_elf.h"
#include "target/hexagon/cpu.h"


abi_ulong get_elf_hwcap(CPUState *cs)
{
    HexagonCPUClass *mcc = HEXAGON_CPU_GET_CLASS(cs);
    abi_ulong hwcaps;
    uint32_t hex_ver;

    if (!mcc->hex_def) {
        return 0;
    }

    hex_ver = mcc->hex_def->hex_version;
    switch (hex_ver) {
    case HEX_VER_V5:
        hwcaps = HWCAP_HEXAGON_ISA_V5;
        break;
    case HEX_VER_V55:
        hwcaps = HWCAP_HEXAGON_ISA_V55;
        break;
    case HEX_VER_V60:
    case HEX_VER_V61:
        hwcaps = HWCAP_HEXAGON_ISA_V60;
        break;
    case HEX_VER_V62:
        hwcaps = HWCAP_HEXAGON_ISA_V62;
        break;
    case HEX_VER_V65:
        hwcaps = HWCAP_HEXAGON_ISA_V65;
        break;
    case HEX_VER_V66:
        hwcaps = HWCAP_HEXAGON_ISA_V66;
        break;
    case HEX_VER_V67:
        hwcaps = HWCAP_HEXAGON_ISA_V67;
        break;
    case HEX_VER_V68:
        hwcaps = HWCAP_HEXAGON_ISA_V68;
        break;
    case HEX_VER_V69:
        hwcaps = HWCAP_HEXAGON_ISA_V69;
        break;
    case HEX_VER_V71:
        hwcaps = HWCAP_HEXAGON_ISA_V71;
        break;
    case HEX_VER_V73:
        hwcaps = HWCAP_HEXAGON_ISA_V73;
        break;
    default:
        return 0;
    }

    hwcaps |= HWCAP_HEXAGON_HVX;
    hwcaps |= HWCAP_HEXAGON_HVX_LENGTH_128B;
    if (hex_ver >= HEX_VER_V68) {
        hwcaps |= HWCAP_HEXAGON_HVX_IEEE_FP;
    }

    return hwcaps;
}

const char *get_elf_cpu_model(uint32_t eflags)
{
    static char buf[32];
    int err;

    switch (eflags) {
    case 0x04:
        return "v5";
    case 0x05:
        return "v55";
    case 0x60:
        return "v60";
    case 0x61:
        return "v61";
    case 0x62:
        return "v62";
    case 0x65:
        return "v65";
    case 0x66:
        return "v66";
    case 0x67:
    case 0x8067:        /* v67t */
        return "v67";
    case 0x68:
        return "v68";
    case 0x69:
        return "v69";
    case 0x71:
    case 0x8071:        /* v71t */
        return "v71";
    case 0x73:
        return "v73";
    }

    err = snprintf(buf, sizeof(buf), "unknown (0x%x)", eflags);
    return err >= 0 && err < sizeof(buf) ? buf : "unknown";
}
