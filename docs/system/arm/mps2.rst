Arm MPS2 and MPS3 boards (``mps2-an385``, ``mps2-an386``, ``mps2-an500``, ``mps2-an505``, ``mps2-an511``, ``mps2-an521``, ``mps3-an524``, ``mps3-an536``, ``mps3-an547``)
=========================================================================================================================================================================

These board models use Arm M-profile or R-profile CPUs.

The Arm MPS2, MPS2+ and MPS3 dev boards are FPGA based (the 2+ has a
bigger FPGA but is otherwise the same as the 2; the 3 has a bigger
FPGA again, can handle 4GB of RAM and has a USB controller and QSPI flash).

Since the CPU itself and most of the devices are in the FPGA, the
details of the board as seen by the guest depend significantly on the
FPGA image.

QEMU models the following FPGA images:

FPGA images using M-profile CPUs:

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

FPGA images using R-profile CPUs:

``mps3-an536``
  Dual Cortex-R52 on an MPS3, as documented in Arm Application Note AN536

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
- AN536 does not support runtime control of CPU reset and halt via
  the SCC CFG_REG0 register.
- AN536 does not support enabling or disabling the flash and ATCM
  interfaces via the SCC CFG_REG1 register.
- AN536 does not support setting of the initial vector table
  base address via the SCC CFG_REG6 and CFG_REG7 register config,
  and does not provide a mechanism for specifying these values at
  startup, so all guest images must be built to start from TCM
  (i.e. to expect the interrupt vector base at 0 from reset).
- AN536 defaults to only creating a single CPU; this is the equivalent
  of the way the real FPGA image usually runs with the second Cortex-R52
  held in halt via the initial SCC CFG_REG0 register setting. You can
  create the second CPU with ``-smp 2``; both CPUs will then start
  execution immediately on startup.

Note that for the AN536 the first UART is accessible only by
CPU0, and the second UART is accessible only by CPU1. The
first UART accessible shared between both CPUs is the third
UART. Guest software might therefore be built to use either
the first UART or the third UART; if you don't see any output
from the UART you are looking at, try one of the others.
(Even if the AN536 machine is started with a single CPU and so
no "CPU1-only UART", the UART numbering remains the same,
with the third UART being the first of the shared ones.)

Machine-specific options
""""""""""""""""""""""""

The following machine-specific options are supported:

remap
  Supported for ``mps3-an524`` only.
  Set ``BRAM``/``QSPI`` to select the initial memory mapping. The
  default is ``BRAM``.
