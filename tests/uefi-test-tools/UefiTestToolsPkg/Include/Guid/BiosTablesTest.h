/** @file
  Expose the address(es) of the ACPI RSD PTR table(s) and the SMBIOS entry
  point(s) in a MB-aligned structure to the hypervisor.

  The hypervisor locates the MB-aligned structure based on the signature GUID
  that is at offset 0 in the structure. Once the RSD PTR and SMBIOS anchor
  address(es) are retrieved, the hypervisor may perform various ACPI and SMBIOS
  checks.

  This feature is a development aid, for supporting ACPI and SMBIOS table unit
  tests in hypervisors. Do not enable in production builds.

  Copyright (C) 2019, Red Hat, Inc.

  This program and the accompanying materials are licensed and made available
  under the terms and conditions of the BSD License that accompanies this
  distribution. The full text of the license may be found at
  <http://opensource.org/licenses/bsd-license.php>.

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS, WITHOUT
  WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#ifndef BIOSTABLESTEST_H
#define BIOSTABLESTEST_H

#include <Uefi/UefiBaseType.h>

#define BIOS_TABLES_TEST_GUID                          \
  {                                                    \
    0x5478594e,                                        \
    0xdfcb,                                            \
    0x425f,                                            \
    { 0x8e, 0x42, 0xc8, 0xaf, 0xf8, 0x8a, 0x88, 0x7a } \
  }

extern EFI_GUID gBiosTablesTestGuid;

//
// The following structure must be allocated in Boot Services Data type memory,
// aligned at a 1MB boundary.
//
#pragma pack (1)
typedef struct {
  //
  // The signature GUID is written to the MB-aligned structure from
  // gBiosTablesTestGuid, but with all bits inverted. That's the actual GUID
  // value that the hypervisor should look for at each MB boundary, looping
  // over all guest RAM pages with that alignment, until a match is found. The
  // bit-flipping occurs in order not to store the actual GUID in any UEFI
  // executable, which might confuse guest memory analysis. Note that EFI_GUID
  // has little endian representation.
  //
  EFI_GUID             InverseSignatureGuid;
  //
  // The Rsdp10 and Rsdp20 fields may be read when the signature GUID matches.
  // Rsdp10 is the guest-physical address of the ACPI 1.0 specification RSD PTR
  // table, in 8-byte little endian representation. Rsdp20 is the same, for the
  // ACPI 2.0 or later specification RSD PTR table. Each of these fields may be
  // zero (independently of the other) if the UEFI System Table does not
  // provide the corresponding UEFI Configuration Table.
  //
  EFI_PHYSICAL_ADDRESS Rsdp10;
  EFI_PHYSICAL_ADDRESS Rsdp20;
  //
  // The Smbios21 and Smbios30 fields may be read when the signature GUID
  // matches. Smbios21 is the guest-physical address of the SMBIOS 2.1 (32-bit)
  // Entry Point Structure from the SMBIOS v3.2.0 specification, in 8-byte
  // little endian representation. Smbios30 is the guest-physical address of
  // the SMBIOS 3.0 (64-bit) Entry Point Structure from the same specification,
  // in the same representation. Each of these fields may be zero
  // (independently of the other) if the UEFI System Table does not provide the
  // corresponding UEFI Configuration Table.
  //
  EFI_PHYSICAL_ADDRESS Smbios21;
  EFI_PHYSICAL_ADDRESS Smbios30;
} BIOS_TABLES_TEST;
#pragma pack ()

#endif /* BIOSTABLESTEST_H */
