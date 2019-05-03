/** @file
  Populate the BIOS_TABLES_TEST structure.

  Copyright (C) 2019, Red Hat, Inc.

  This program and the accompanying materials are licensed and made available
  under the terms and conditions of the BSD License that accompanies this
  distribution. The full text of the license may be found at
  <http://opensource.org/licenses/bsd-license.php>.

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS, WITHOUT
  WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
**/

#include <Guid/Acpi.h>
#include <Guid/BiosTablesTest.h>
#include <Guid/SmBios.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

/**
  Wait for a keypress with a message that the application is about to exit.
**/
STATIC
VOID
WaitForExitKeyPress (
  VOID
  )
{
  EFI_STATUS    Status;
  UINTN         Idx;
  EFI_INPUT_KEY Key;

  if (gST->ConIn == NULL) {
    return;
  }
  AsciiPrint ("%a: press any key to exit\n", gEfiCallerBaseName);
  Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Idx);
  if (EFI_ERROR (Status)) {
    return;
  }
  gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
}

EFI_STATUS
EFIAPI
BiosTablesTestMain (
  IN EFI_HANDLE       ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  VOID                          *Pages;
  volatile BIOS_TABLES_TEST     *BiosTablesTest;
  CONST VOID                    *Rsdp10;
  CONST VOID                    *Rsdp20;
  CONST VOID                    *Smbios21;
  CONST VOID                    *Smbios30;
  CONST EFI_CONFIGURATION_TABLE *ConfigTable;
  CONST EFI_CONFIGURATION_TABLE *ConfigTablesEnd;
  volatile EFI_GUID             *InverseSignature;
  UINTN                         Idx;

  Pages = AllocateAlignedPages (EFI_SIZE_TO_PAGES (sizeof *BiosTablesTest),
            SIZE_1MB);
  if (Pages == NULL) {
    AsciiErrorPrint ("%a: AllocateAlignedPages() failed\n",
      gEfiCallerBaseName);
    //
    // Assuming the application was launched by the boot manager as a boot
    // loader, exiting with error will cause the boot manager to proceed with
    // the remaining boot options. If there are no other boot options, the boot
    // manager menu will be pulled up. Give the user a chance to read the error
    // message.
    //
    WaitForExitKeyPress ();
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Locate all the gEfiAcpi10TableGuid, gEfiAcpi20TableGuid,
  // gEfiSmbiosTableGuid, gEfiSmbios3TableGuid config tables in one go.
  //
  Rsdp10 = NULL;
  Rsdp20 = NULL;
  Smbios21 = NULL;
  Smbios30 = NULL;
  ConfigTable = gST->ConfigurationTable;
  ConfigTablesEnd = gST->ConfigurationTable + gST->NumberOfTableEntries;
  while ((Rsdp10 == NULL || Rsdp20 == NULL ||
          Smbios21 == NULL || Smbios30 == NULL) &&
         ConfigTable < ConfigTablesEnd) {
    if (CompareGuid (&ConfigTable->VendorGuid, &gEfiAcpi10TableGuid)) {
      Rsdp10 = ConfigTable->VendorTable;
    } else if (CompareGuid (&ConfigTable->VendorGuid, &gEfiAcpi20TableGuid)) {
      Rsdp20 = ConfigTable->VendorTable;
    } else if (CompareGuid (&ConfigTable->VendorGuid, &gEfiSmbiosTableGuid)) {
      Smbios21 = ConfigTable->VendorTable;
    } else if (CompareGuid (&ConfigTable->VendorGuid, &gEfiSmbios3TableGuid)) {
      Smbios30 = ConfigTable->VendorTable;
    }
    ++ConfigTable;
  }

  AsciiPrint ("%a: BiosTablesTest=%p Rsdp10=%p Rsdp20=%p\n",
    gEfiCallerBaseName, Pages, Rsdp10, Rsdp20);
  AsciiPrint ("%a: Smbios21=%p Smbios30=%p\n", gEfiCallerBaseName, Smbios21,
    Smbios30);

  //
  // Store the config table addresses first, then the signature second.
  //
  BiosTablesTest = Pages;
  BiosTablesTest->Rsdp10 = (UINTN)Rsdp10;
  BiosTablesTest->Rsdp20 = (UINTN)Rsdp20;
  BiosTablesTest->Smbios21 = (UINTN)Smbios21;
  BiosTablesTest->Smbios30 = (UINTN)Smbios30;

  MemoryFence();

  InverseSignature = &BiosTablesTest->InverseSignatureGuid;
  InverseSignature->Data1  = gBiosTablesTestGuid.Data1;
  InverseSignature->Data1 ^= MAX_UINT32;
  InverseSignature->Data2  = gBiosTablesTestGuid.Data2;
  InverseSignature->Data2 ^= MAX_UINT16;
  InverseSignature->Data3  = gBiosTablesTestGuid.Data3;
  InverseSignature->Data3 ^= MAX_UINT16;
  for (Idx = 0; Idx < sizeof InverseSignature->Data4; ++Idx) {
    InverseSignature->Data4[Idx]  = gBiosTablesTestGuid.Data4[Idx];
    InverseSignature->Data4[Idx] ^= MAX_UINT8;
  }

  //
  // The wait below has dual purpose. First, it blocks the application without
  // wasting VCPU cycles while the hypervisor is scanning guest RAM. Second,
  // assuming the application was launched by the boot manager as a boot
  // loader, exiting the app with success causes the boot manager to pull up
  // the boot manager menu at once (regardless of other boot options); the wait
  // gives the user a chance to read the info printed above.
  //
  WaitForExitKeyPress ();
  return EFI_SUCCESS;
}
