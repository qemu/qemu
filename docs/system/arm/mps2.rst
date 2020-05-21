Arm MPS2 boards (``mps2-an385``, ``mps2-an505``, ``mps2-an511``, ``mps2-an521``)
================================================================================

These board models all use Arm M-profile CPUs.

The Arm MPS2 and MPS2+ dev boards are FPGA based (the 2+ has a bigger
FPGA but is otherwise the same as the 2). Since the CPU itself
and most of the devices are in the FPGA, the details of the board
as seen by the guest depend significantly on the FPGA image.

QEMU models the following FPGA images:

``mps2-an385``
  Cortex-M3 as documented in ARM Application Note AN385
``mps2-an511``
  Cortex-M3 'DesignStart' as documented in AN511
``mps2-an505``
  Cortex-M33 as documented in ARM Application Note AN505
``mps2-an521``
  Dual Cortex-M33 as documented in Application Note AN521

Differences between QEMU and real hardware:

- AN385 remapping of low 16K of memory to either ZBT SSRAM1 or to
  block RAM is unimplemented (QEMU always maps this to ZBT SSRAM1, as
  if zbt_boot_ctrl is always zero)
- QEMU provides a LAN9118 ethernet rather than LAN9220; the only guest
  visible difference is that the LAN9118 doesn't support checksum
  offloading
