- SeaBIOS (bios.bin) is the successor of pc bios.
  See http://www.seabios.org/ for more information.

- The VGA BIOS and the Cirrus VGA BIOS come from the LGPL VGA bios
  project (http://www.nongnu.org/vgabios/).

- OpenBIOS (http://www.openbios.org/) is a free (GPL v2) portable
  firmware implementation. The goal is to implement a 100% IEEE
  1275-1994 (referred to as Open Firmware) compliant firmware.
  The included images for PowerPC (for 32 and 64 bit PPC CPUs),
  Sparc32 (including QEMU,tcx.bin and QEMU,cgthree.bin) and Sparc64 are built
  from OpenBIOS SVN revision 1280.

- SLOF (Slimline Open Firmware) is a free IEEE 1275 Open Firmware
  implementation for certain IBM POWER hardware.  The sources are at
  https://gitlab.com/slof/slof, and the image currently in qemu is
  built from git tag qemu-slof-20241106.

- VOF (Virtual Open Firmware) is a minimalistic firmware to work with
  -machine pseries,x-vof=on. When enabled, the firmware acts as a slim shim and
  QEMU implements parts of the IEEE 1275 Open Firmware interface.

- The PXE roms come from the iPXE project. Built with BANNER_TIME 0.
  Sources available at http://ipxe.org.  Vendor:Device ID -> ROM mapping:

	8086:100e -> pxe-e1000.rom
	8086:1209 -> pxe-eepro100.rom
	1050:0940 -> pxe-ne2k_pci.rom
	1022:2000 -> pxe-pcnet.rom
	10ec:8139 -> pxe-rtl8139.rom
	1af4:1000 -> pxe-virtio.rom

- The sources for the Alpha palcode image is available from:
  https://github.com/rth7680/qemu-palcode.git

- The u-boot binary for e500 comes from the upstream denx u-boot project where
  it was compiled using the qemu-ppce500 target.
  A git mirror is available at: https://gitlab.com/qemu-project/u-boot.git
  The hash used to compile the current version is: 2072e72

- Skiboot (https://github.com/open-power/skiboot/) is an OPAL
  (OpenPower Abstraction Layer) firmware for OpenPOWER systems. It can
  run an hypervisor OS or simply a host OS on the "baremetal"
  platform, also known as the PowerNV (Non-Virtualized) platform.

- pnv-pnor.bin is a non-volatile RAM image used by PowerNV, which stores
  NVRAM BIOS settings among other things. This image was created with the
  following command (the ffspart tool can be found in the skiboot source tree):

  ffspart -s 0x1000 -c 34 -i pnv-pnor.in -p pnv-pnor.bin

  Where pnv-pnor.in contains the two lines (no leading whitespace):

  NVRAM,0x01000,0x00020000,,,/dev/zero
  VERSION,0x21000,0x00001000,,,/dev/zero

  skiboot is then booted once to format the NVRAM partition.

- QemuMacDrivers (https://github.com/ozbenh/QemuMacDrivers) is a project to
  provide virtualised drivers for PPC MacOS guests.

- The "edk2-*.fd.bz2" images are platform firmware binaries and matching UEFI
  variable store templates built from the TianoCore community's EFI Development
  Kit II project
  <https://github.com/tianocore/tianocore.github.io/wiki/EDK-II>. The images
  were built at git tag "edk2-stable202302". The firmware binaries bundle parts
  of the OpenSSL project, at git tag "OpenSSL_1_1_1s" (the OpenSSL tag is a
  function of the edk2 tag). Parts of the Berkeley SoftFloat library are
  bundled as well, at Release 3e plus a subsequent typo fix (commit
  b64af41c3276f97f0e181920400ee056b9c88037), as an OpenSSL dependency on 32-bit
  ARM. Licensing information is given in "edk2-licenses.txt". The image files
  are described by the JSON documents in the "pc-bios/descriptors" directory,
  which conform to the "docs/interop/firmware.json" schema.

- OpenSBI (https://github.com/riscv/opensbi) aims to provide an open-source
  reference implementation of the RISC-V Supervisor Binary Interface (SBI)
  specifications for platform-specific firmwares executing in M-mode. For all
  supported platforms, OpenSBI provides several runtime firmware examples.
  These example firmwares can be used to replace the legacy riscv-pk bootloader
  and enable the use of well-known bootloaders such as U-Boot.
  OpenSBI is distributed under the terms of the BSD 2-clause license
  ("Simplified BSD License" or "FreeBSD License", SPDX: BSD-2-Clause). OpenSBI
  source code also contains code reused from other projects described here:
  https://github.com/riscv/opensbi/blob/master/ThirdPartyNotices.md.

- npcm{7xx,8xx}_bootrom.bin is a simplified, free (Apache 2.0) boot ROM for
  Nuvoton NPCM7xx/8xx BMC devices. It currently implements the bare minimum to
  load, parse, initialize and run boot images stored in SPI flash, but may grow
  more features over time as needed. The source code is available at:
  https://github.com/google/vbootrom

- hppa-firmware.img (32-bit) and hppa-firmware64.img (64-bit) are firmware
  files for the HP-PARISC (hppa) architecture.
  They are built form the SeaBIOS-hppa sources, which is a fork of SeaBIOS
  adapted for hppa.
  SeaBIOS-hppa is available at https://github.com/hdeller/seabios-hppa
