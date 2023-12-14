CPU Features
============

The QEMU emulation of the OpenRISC architecture provides following built in
features.

- Shadow GPRs
- MMU TLB with 128 entries, 1 way
- Power Management (PM)
- Programmable Interrupt Controller (PIC)
- Tick Timer

These features are on by default and the presence can be confirmed by checking
the contents of the Unit Presence Register (``UPR``) and CPU Configuration
Register (``CPUCFGR``).
