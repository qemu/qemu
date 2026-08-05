.. SPDX-License-Identifier: GPL-2.0-or-later

.. _riscv-debug:

RISC-V Debug support for RISC-V CPUs
====================================

Up until QEMU version 11.1 RISC-V CPUs historically supports Debug version
0.13 via a "debug" flag that is enabled by default in the default rv64
CPU.

Starting on QEMU 11.2, RISC-V CPUs supports also Debug version 1.0, enabled
via the "sdtrig" flag like a regular extension.  The default setting for rv64
is still Debug 0.13 (debug = on, sdtrig = off),

The "sdtrig" flag precedes "debug", i.e. if both are enabled the CPU will
implement Debug 1.0.  In short:

.. list-table:: "debug" and "sdtrig" flags and enabled Debug version
   :widths: 25 25 25
   :header-rows: 1

   * - debug
     - sdtrig
     - Enabled Debug version
   * - on
     - off
     - 0.13
   * - on
     - on
     - 1.0
   * - off
     - on
     - 1.0
   * - off
     - off
     - All Debug versions disabled


Debug 0.13 to 1.0 code/design changes
-------------------------------------

The effort done in QEMU version 11.2 to support Debug versions 0.13 and
1.0 together was based in section 1.2.1.2 "Incompatible Changes from 0.13
to 1.0" from the ratified `1.0 spec`_.  Here's how the proposed changes in
that section were implemented in QEMU 11.2:

Changes that aren't applicable due to either being SBI related or not present/supported in QEMU:
    * "Make haltsum0 optional if there is only one hart."
    * "System bus autoincrement only happens if an access actually takes place (sbdata0)."
    * "Require debugger to poll dmactive after lowering it."

Already implemented in the legacy Debug 0.13 base code:
    * "When a selected trigger is disabled, tdata2 and tdata3 can be written with any value supported by any of the types this trigger supports."
    * "tcontrol fields only apply to breakpoint traps, not any trap."

All "hitN" fields are hardwired 0 thus no changes made:
    * "If version is greater than 0, then hit0 (previously called mcontrol6.hit) now contains 0 when a trigger fires more than one instruction after the instruction that matched. (This information is now reflected in hit1.)"

QEMU doesn't support encoding sizes greater than 64 bit so no change made:
    * "If version is greater than 0, then the encodings of size for sizes greater than 64 bit have changed."

Changes made in the 0.13 codebase to support Debug 1.0:
    * "Add pending to icount."
    * "If version is greater than 0, then bit 20 of mcontrol6 is no longer used for timing information. (Previously the bit was called mcontrol6.timing.)"


.. _1.0 spec: https://docs.riscv.org/reference/debug/v1.0/_attachments/riscv-debug-specification.pdf
