Hexagon Virtual Machine
=======================

The hexagon virtual machine is a hypervisor that can partition a single
Hexagon DSP among multiple guest operating systems, and abstracts the
specific details of a DSP architectural revision for the sake of consistency
among generations.

`minivm <https://github.com/quic/hexagonMVM>`_ is a reference implementation
of this VM interface.

Events
------

The guest operating system should register the Guest Event Vector Base
via the ``vmsetvec`` virtual instruction at system startup.  The vector table
and handlers are determined by the guest OS.

Guests return from event handlers with ``vmrte``.  This instruction will restore
the mode (user versus guest), interrupt enable state, PC, SP.

.. list-table:: Event types
   :header-rows: 1

   * - Number
     - Name
     - Description
     - Maskable
     - Detail
   * - 0
     - Reserved
     -
     -
     -
   * - 1
     - Machine check event
     - unrecoverable VM state
     - No
     - execution terminates if unhandled
   * - 2
     - General exception
     - internal hardware or software exception
     - No
     -
   * - 3-4
     - Reserved
     -
     -
     -
   * - 5
     - ``trap0``
     - ``trap0`` instruction
     - No
     -
   * - 6
     - Reserved
     -
     -
     -
   * - 7
     - Interrupt
     - external interrupts
     - Yes
     - increasing interrupt numbers have descending priority

Startup
-------
In order to transition to user-mode, the guest OS must set the ``UM`` bit in
the guest status register and specify the address to start executing in
user mode in the guest event link register.

Virtual Instructions
--------------------

.. list-table:: Virtual Instructions
   :header-rows: 1

   * - Instruction
     - Behavior
     - Operand
     - Input
     - Output
   * - vmversion
     - returns the VM version
     - 0x0
     - requested VM version
     - provided VM version
   * - vmrte
     - return from event
     - 0x1
     - Event info in g3:0
     - N/A
   * - vmsetvec
     - set event vector
     - 0x2
     - r0 is set to vector table addr
     - r0 is 0 on success, 1 otherwise
   * - vmsetie
     - set interrupt enabled
     - 0x3
     - r0 is set to 1 to enable, 0 to disable
     - previous IE bit is stored as LSB of r0
   * - vmgetie
     - get interrupt enabled
     - 0x4
     - N/A
     - current IE bit is stored as LSB of r0
   * - vmintop
     - interrupt operation
     - 0x5
     - r0 = Interrupt Op, r1-r4: Depends on Op
     - r0 - value depends on operation
   * - vmclrmap
     - clear virtual memory map
     - 0xa
     - r0 = Interrupt Op, r1-r4: Depends on Op
     - r0 - value depends on operation
   * - vmnewmap
     - set new virtual memory map
     - 0xb
     - + r0 contains logical address of new segment table
       + r1 = type of translations: 0 indicates a logical address of a zero-terminated linear list, 1 indicates a set of page tables.
     - r0 contains 0 on success, otherwise negative error code
   * - vmcache
     - VM cache control: not modeled
     - 0xd
     - + r0 contains the operation to be performed
       + r1 = Starting virtual address
       + r2 contains the length in bytes
     - r0 contains 0 on success, otherwise -1.  Cache behavior is not modeled so this operation always succeeds.
   * - vmgettime
     - Get virtual machine time
     - 0xe
     - N/A
     - r0 contains the least significant 32 bits of timestamp, r1 contains the  most significant 32 bits of timestamp
   * - vmsettime
     - Set virtual machine time
     - 0xf
     - r0 contains the least significant 32 bits of timestamp, r1 contains the  most significant 32 bits of timestamp
     - N/A
   * - vmwait
     - wait for interrupt
     - 0x10
     - N/A
     - r0 contains the interrupt number of the interrupt waking the guest
   * - vmyield
     - voluntarily yield VM task
     - 0x11
     - N/A
     - N/A
   * - vmstart
     - Create new virtual processor instance
     - 0x12
     - r0 contains the starting execution address, r1 contains the starting stack pointer
     - r0 contains the Virtual processor number of new virtual processor on success, otherwise -1
   * - vmstop
     - terminate current virtual processor instance
     - 0x13
     - N/A
     - N/A
   * - vmvpid
     - get the virtual processor ID
     - 0x14
     - N/A
     - r0 contains the virtual processor number of virtual processor executing the instruction
   * - vmsetregs
     - Set guest registers
     - 0x15
     - r0-3 hold g0-3 values
     - N/A
   * - vmgetregs
     - Get guest registers
     - 0x16
     - N/A
     - r0-3 hold g0-3 values
   * - vmtimerop
     - perform an operation on a system timer
     - 0x18
     - + getfreq = 0
       + getres = 1
       + gettime = 2
       + gettimeout = 3
       + settimeout = 4
       + deltatimeout = 5
     - r0 contains result of the timer operation call
   * - vmgetinfo
     - Get system info
     - 0x1a
     - Index of the system info parameter:

       + build_id = 0
       + info_boot_flags = 1
     - value of the indicated system info parameter
