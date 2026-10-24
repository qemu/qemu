.. SPDX-License-Identifier: GPL-2.0-or-later

Compute DSP
===========

A Hexagon CDSP is designed as a computation offload device for an SoC.  The
``V66G_1024`` machine contains:

* L2VIC interrupt controller
* QTimer timer device

This machine will support any Hexagon CPU, but will default to ``v66``.

H2 boot firmware
----------------

These machines can be started through the H2 hypervisor images shipped in
``pc-bios``, selected with ``-bios``:

.. list-table::
   :header-rows: 1

   * - Machine
     - CPU
     - ``-kernel`` address
     - Image suffix
   * - ``qcs6490-cdsp``
     - ``v68``
     - ``0xa1000000``
     - ``qcs6490_cdsp``
   * - ``sa8775p-cdsp``
     - ``v73``
     - ``0xa0000000``
     - ``sa8775p_cdsp``
   * - ``sm8975-nsp``
     - ``v85``
     - ``0xa0000000``
     - ``sm8975_nsp``
   * - ``sc8480xp-nsp0``
     - ``v81``
     - ``0xa0000000``
     - ``sc8480xp_nsp0``
   * - ``sa8797p-nsp0``
     - ``v81``
     - ``0xa0000000``
     - ``sa8797p_nsp0``

``hexagon_booter_<suffix>``
  H2 plus a loader that reads guest ELF files from the host through QEMU
  semihosting::

    qemu-system-hexagon -M qcs6490-cdsp -serial stdio -display none \
        -bios hexagon_booter_qcs6490_cdsp \
        -semihosting-config enable=on,target=native,arg=booter,arg=guest.elf

  Guests are loaded at ``0xa2000000``.

``hexagon_loadlinux_<suffix>``
  H2 plus a loader that starts a kernel which QEMU has placed at the
  ``-kernel`` address in the table above.
