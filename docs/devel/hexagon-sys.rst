.. SPDX-License-Identifier: GPL-2.0-or-later

.. _Hexagon-System-arch:

Hexagon System Architecture
===========================

The hexagon architecture has some unique elements which are described here.

Interrupts
----------
When interrupts arrive at a Hexagon DSP core, they are priority-steered to
be handled by an eligible hardware thread with the lowest priority.

Memory
------
Each hardware thread has an ``SSR.ASID`` field that contains its Address
Space Identifier.  This value is catenated with a 32-bit virtual address -
the MMU can then resolve this extended virtual address to a physical address.

TLBs
----
The format of a TLB entry is shown below.

.. note::
    The Small Core DSPs have a different TLB format which is not yet
    supported.

.. admonition:: Diagram

 .. code:: text

             6                   5                   4               3
       3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |v|g|x|A|A|             |                                       |
      |a|l|P|1|0|     ASID    |             Virtual Page              |
      |l|b| | | |             |                                       |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

         3                   2                   1                   0
       1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      | | | | |       |                                             | |
      |x|w|r|u|Cacheab|               Physical Page                 |S|
      | | | | |       |                                             | |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


* ASID: the address-space identifier
* A1, A0: the behavior of these cache line attributes are not modeled by QEMU.
* xP: the extra-physical bit is the most significant physical address bit.
* S: the S bit and the LSBs of the physical page indicate the page size
* val: this is the 'valid' bit, when set it indicates that page matching
  should consider this entry.

.. list-table:: Page sizes
   :widths: 25 25 50
   :header-rows: 1

   * - S-bit
     - Phys page LSBs
     - Page size
   * - 1
     - N/A
     - 4kb
   * - 0
     - 0b1
     - 16kb
   * - 0
     - 0b10
     - 64kb
   * - 0
     - 0b100
     - 256kb
   * - 0
     - 0b1000
     - 1MB
   * - 0
     - 0b10000
     - 4MB
   * - 0
     - 0b100000
     - 16MB

* glb: if the global bit is set, the ASID is not considered when matching
  TLBs.
* Cacheab: the cacheability attributes of TLBs are not modeled, these bits
  are ignored.
* RWX: read-, write-, execute-, enable bits.  Indicates if user programs
  are permitted to read/write/execute the given page.
* U: indicates if user programs can access this page.

Scheduler
---------
The Hexagon system architecture has a feature to assist the guest OS
task scheduler.  The guest OS can enable this feature by setting
``SCHEDCFG.EN``.  The ``BESTWAIT`` register is programmed by the guest OS
to indicate the priority of the highest priority task waiting to run on a
hardware thread.  The reschedule interrupt is triggered when any hardware
thread's priority in ``STID.PRIO`` is worse than the ``BESTWAIT``.  When
it is triggered, the ``BESTWAIT.PRIO`` value is reset to 0x1ff.

HVX Coprocessor
---------------
The Supervisor Status Register field ``SSR.XA`` binds a DSP hardware thread
to one of the eight possible HVX contexts.  The guest OS is responsible for
managing this resource.

.. seealso::

    ``target/hexagon/README`` in the QEMU source tree for more info about Hexagon.
