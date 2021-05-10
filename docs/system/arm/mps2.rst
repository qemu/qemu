Arm MPS2 and MPS3 boards (``mps2-an385``, ``mps2-an386``, ``mps2-an500``, ``mps2-an505``, ``mps2-an511``, ``mps2-an521``, ``mps3-an524``, ``mps3-an547``)
=========================================================================================================================================================

These board models all use Arm M-profile CPUs.

The Arm MPS2, MPS2+ and MPS3 dev boards are FPGA based (the 2+ has a
bigger FPGA but is otherwise the same as the 2; the 3 has a bigger
FPGA again, can handle 4GB of RAM and has a USB controller and QSPI flash).

Since the CPU itself and most of the devices are in the FPGA, the
details of the board as seen by the guest depend significantly on the
FPGA image.

QEMU models the following FPGA images:

``mps2-an385``
  Cortex-M3 as documented in Arm Application Note AN385
``mps2-an386``
  Cortex-M4 as documented in Arm Application Note AN386
``mps2-an500``
  Cortex-M7 as documented in Arm Application Note AN500
``mps2-an505``
  Cortex-M33 as documented in Arm Application Note AN505
``mps2-an511``
  Cortex-M3 'DesignStart' as documented in Arm Application Note AN511
``mps2-an521``
  Dual Cortex-M33 as documented in Arm Application Note AN521
``mps3-an524``
  Dual Cortex-M33 on an MPS3, as documented in Arm Application Note AN524
``mps3-an547``
  Cortex-M55 on an MPS3, as documented in Arm Application Note AN547

Differences between QEMU and real hardware:

- AN385/AN386 remapping of low 16K of memory to either ZBT SSRAM1 or to
  block RAM is unimplemented (QEMU always maps this to ZBT SSRAM1, as
  if zbt_boot_ctrl is always zero)
- AN524 remapping of low memory to either BRAM or to QSPI flash is
  unimplemented (QEMU always maps this to BRAM, ignoring the
  SCC CFG_REG0 memory-remap bit)
- QEMU provides a LAN9118 ethernet rather than LAN9220; the only guest
  visible difference is that the LAN9118 doesn't support checksum
  offloading
- QEMU does not model the QSPI flash in MPS3 boards as real QSPI
  flash, but only as simple ROM, so attempting to rewrite the flash
  from the guest will fail
- QEMU does not model the USB controller in MPS3 boards

Machine-specific options
""""""""""""""""""""""""

The following machine-specific options are supported:

remap
  Supported for ``mps3-an524`` only.
  Set ``BRAM``/``QSPI`` to select the initial memory mapping. The
  default is ``BRAM``.
