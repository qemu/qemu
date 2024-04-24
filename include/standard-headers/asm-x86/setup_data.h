/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_X86_SETUP_DATA_H
#define _ASM_X86_SETUP_DATA_H

/* setup_data/setup_indirect types */
#define SETUP_NONE			0
#define SETUP_E820_EXT			1
#define SETUP_DTB			2
#define SETUP_PCI			3
#define SETUP_EFI			4
#define SETUP_APPLE_PROPERTIES		5
#define SETUP_JAILHOUSE			6
#define SETUP_CC_BLOB			7
#define SETUP_IMA			8
#define SETUP_RNG_SEED			9
#define SETUP_ENUM_MAX			SETUP_RNG_SEED

#define SETUP_INDIRECT			(1<<31)
#define SETUP_TYPE_MAX			(SETUP_ENUM_MAX | SETUP_INDIRECT)

#ifndef __ASSEMBLY__

#include "standard-headers/linux/types.h"

/* extensible setup data list node */
struct setup_data {
	uint64_t next;
	uint32_t type;
	uint32_t len;
	uint8_t data[];
};

/* extensible setup indirect data node */
struct setup_indirect {
	uint32_t type;
	uint32_t reserved;  /* Reserved, must be set to zero. */
	uint64_t len;
	uint64_t addr;
};

/*
 * The E820 memory region entry of the boot protocol ABI:
 */
struct boot_e820_entry {
	uint64_t addr;
	uint64_t size;
	uint32_t type;
} QEMU_PACKED;

/*
 * The boot loader is passing platform information via this Jailhouse-specific
 * setup data structure.
 */
struct jailhouse_setup_data {
	struct {
		uint16_t	version;
		uint16_t	compatible_version;
	} QEMU_PACKED hdr;
	struct {
		uint16_t	pm_timer_address;
		uint16_t	num_cpus;
		uint64_t	pci_mmconfig_base;
		uint32_t	tsc_khz;
		uint32_t	apic_khz;
		uint8_t	standard_ioapic;
		uint8_t	cpu_ids[255];
	} QEMU_PACKED v1;
	struct {
		uint32_t	flags;
	} QEMU_PACKED v2;
} QEMU_PACKED;

/*
 * IMA buffer setup data information from the previous kernel during kexec
 */
struct ima_setup_data {
	uint64_t addr;
	uint64_t size;
} QEMU_PACKED;

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_SETUP_DATA_H */
