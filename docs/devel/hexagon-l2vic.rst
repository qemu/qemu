Hexagon L2 Vectored Interrupt Controller
========================================


.. code-block:: none

              +-------+
              |       |             +----------------+
              | l2vic |             |  hexagon core  |
              |       |             |                |
              | +-----|             |                |
        ------> |VID0 >------------->irq2 -\         |
        ------> |     |             |      |         |
         ...  > |     |             |      |         |
        ------> |     |             | <int steering> |
              | +-----|             |   / |  | \     |
              |  ...  |             |  |  |  |  |    |
              | +-----|             | t0 t1 t2 t3 ...|
        ------> |VIDN |             |                |
        ------> |     |             |                |
        ------> |     |             |                |
        ------> |     |             |                |
              | +-----|             |                |
              |       |             |Global SREG File|
              | State |             |                |
              | [    ]|<============|=>[VID ]        |
              | [    ]|<============|=>[VID1]        |
              | [    ]|             |                |
              | [    ]|             |                |
              |       |             |                |
              +-------+             +----------------+

L2VIC/Core Integration
----------------------

* hexagon core supports 8 external interrupt sources
* l2vic supports 1024 input interrupts mapped among 4 output interrupts
* l2vic has four output signals: { VID0, VID1, VID2, VID3 }
* l2vic device has a bank of registers per-VID that can be used to query
  the status or assert new interrupts.
* Interrupts are 'steered' to threads based on { thread priority, 'EX' state,
  thread interrupt mask, thread interrupt enable, global interrupt enable,
  etc. }.
* Any hardware thread could conceivably handle any input interrupt, dependent
  on state.
* The system register transfer instruction can read the VID0-VID3 values from
  the l2vic when reading from hexagon core system registers "VID" and "VID1".
* When l2vic VID0 has multiple active interrupts, it pulses the VID0 output
  IRQ and stores the IRQ number for the VID0 register field.  Only after this
  interrupt is cleared can the l2vic pulse the VID0 output IRQ again and provide
  the next interrupt number on the VID0 register.
* The ``ciad`` instruction clears the l2vic input interrupt and un-disables the
  core interrupt.  If some/an l2vic VID0 interrupt is pending when this occurs,
  the next interrupt should fire and any subseqeunt reads of the VID register
  should reflect the newly raised interrupt.
* In QEMU, on an external interrupt or an unmasked-pending interrupt,
  all vCPUs are triggered (has_work==true) and each will grab the IO lock
  while considering the steering logic to determine whether they're the thread
  that must handle the interrupt.
