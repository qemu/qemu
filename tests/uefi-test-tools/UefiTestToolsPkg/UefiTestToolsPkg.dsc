## @file
# edk2 platform description for the test helper UEFI applications that run in
# guests.
#
# Copyright (C) 2019, Red Hat, Inc.
#
# This program and the accompanying materials are licensed and made available
# under the terms and conditions of the BSD License that accompanies this
# distribution. The full text of the license may be found at
# <http://opensource.org/licenses/bsd-license.php>.
#
# THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS, WITHOUT
# WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
##

[Defines]
  DSC_SPECIFICATION       = 1.28
  PLATFORM_GUID           = 6750ccc1-8365-49f0-8437-948e516a9f55
  PLATFORM_VERSION        = 0.1
  PLATFORM_NAME           = UefiTestTools
  SKUID_IDENTIFIER        = DEFAULT
  SUPPORTED_ARCHITECTURES = ARM|AARCH64|IA32|X64|RISCV64
  BUILD_TARGETS           = DEBUG

[BuildOptions.IA32]
  GCC:*_*_IA32_CC_FLAGS = -mno-mmx -mno-sse

[BuildOptions.X64]
  GCC:*_*_X64_CC_FLAGS = -mno-mmx -mno-sse

[BuildOptions.ARM.EDKII.UEFI_APPLICATION]
  GCC:*_*_ARM_DLINK_FLAGS = -z common-page-size=0x1000

[BuildOptions.AARCH64.EDKII.UEFI_APPLICATION]
  GCC:*_*_AARCH64_DLINK_FLAGS = -z common-page-size=0x1000

[BuildOptions]
  GCC:*_*_*_CC_FLAGS = -D DISABLE_NEW_DEPRECATED_INTERFACES

[SkuIds]
  0|DEFAULT

[LibraryClasses]
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  DebugLib|MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLibDevicePathProtocol/UefiDevicePathLibDevicePathProtocol.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf

[LibraryClasses.ARM, LibraryClasses.AARCH64]
  BaseMemoryLib|MdePkg/Library/BaseMemoryLibOptDxe/BaseMemoryLibOptDxe.inf
  NULL|ArmPkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf
  NULL|MdePkg/Library/BaseStackCheckLib/BaseStackCheckLib.inf

[LibraryClasses.IA32, LibraryClasses.X64]
  BaseMemoryLib|MdePkg/Library/BaseMemoryLibRepStr/BaseMemoryLibRepStr.inf
  RegisterFilterLib|MdePkg/Library/RegisterFilterLibNull/RegisterFilterLibNull.inf

[LibraryClasses.RISCV64]
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf

[PcdsFixedAtBuild]
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8040004F
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2F

[Components]
  UefiTestToolsPkg/BiosTablesTest/BiosTablesTest.inf
