.. SPDX-License-Identifier: GPL-2.0-or-later

Board-specific boot firmware
============================

The CDSP/NSP machines can use the host firmware shipped in ``pc-bios``,
selected with ``-bios``. These images contain H2 and either the ``booter``
or ``loadlinux`` loader. Use ``-L`` to specify QEMU's firmware directory
when running an uninstalled build.

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

``hexagon_loadlinux_<suffix>``
  Boots a guest operating system written for the Hexagon VM interface.
  Despite the name, it is not specific to Linux. H2 starts the guest from
  an image that QEMU has placed at the ``-kernel`` address in the table
  above.

``hexagon_booter_<suffix>``
  Runs H2 user programs.  The booter creates a VM, loads a program
  that it reads from the host through semihosting, and waits for the
  program to exit::

    qemu-system-hexagon -M qcs6490-cdsp -serial stdio -display none \
        -bios hexagon_booter_qcs6490_cdsp \
        -semihosting-config enable=on,target=native,arg=booter,arg=prog.elf

  Programs are loaded at ``0xa2000000`` by default; the booter's
  ``--guest_base`` option changes this (``arg=--help`` lists its options).

  For more info, see <https://github.com/qualcomm/hexagon-hypervisor>.
