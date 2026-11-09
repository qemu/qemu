.. SPDX-License-Identifier: GPL-2.0-or-later

.. _Hexagon Emulation:

Hexagon CPU architecture support
================================

QEMU's TCG emulation includes support for v65, v66, v67, v68, v69, v71, v73,
v75, v77, v79, v81, v83, v85.
It also has support for the following architecture extensions:

- HVX (Hexagon Vector eXtensions)
- `HVX floating point <https://docs.qualcomm.com/doc/80-N2040-61/topic/hvx-floating-point.html>`_.
- HLX (see the `HLX reference manual
  <https://docs.qualcomm.com/doc/80-N3040-23/topic/hlx.html>`_).

User-DMA and HMX are not yet implemented.

Machine configurations also select the available HVX contexts, VTCM,
and HMX/HLX resources. See :doc:`machines` for the machine configurations.

For information on the specifics of the HVX extension, please refer
to the `Qualcomm Hexagon V79 HVX Programmer's Reference Manual
<https://docs.qualcomm.com/doc/80-N2040-61/>`_.

Emulation limits
----------------

The system models target functional execution.

HVX uses 128-byte vectors; 64-byte mode is not implemented.

See :doc:`../../devel/hexagon-sys` and
:doc:`../../devel/hexagon-l2vic` for system-register, TLB, and interrupt
details relevant to OS and firmware authors.
