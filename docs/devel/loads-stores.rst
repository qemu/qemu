..
   Copyright (c) 2017 Linaro Limited
   Written by Peter Maydell

===================
Load and Store APIs
===================

QEMU internally has multiple families of functions for performing
loads and stores. This document attempts to enumerate them all
and indicate when to use them. It does not provide detailed
documentation of each API -- for that you should look at the
documentation comments in the relevant header files.


``ld*_p and st*_p``
~~~~~~~~~~~~~~~~~~~

These functions operate on a host pointer, and should be used
when you already have a pointer into host memory (corresponding
to guest ram or a local buffer). They deal with doing accesses
with the desired endianness and with correctly handling
potentially unaligned pointer values.

Function names follow the pattern:

load: ``ld{sign}{size}_{endian}_p(ptr)``

store: ``st{size}_{endian}_p(ptr, val)``

``sign``
 - (empty) : for 32 or 64 bit sizes
 - ``u`` : unsigned
 - ``s`` : signed

``size``
 - ``b`` : 8 bits
 - ``w`` : 16 bits
 - ``24`` : 24 bits
 - ``l`` : 32 bits
 - ``q`` : 64 bits

``endian``
 - ``he`` : host endian
 - ``be`` : big endian
 - ``le`` : little endian

The ``_{endian}`` infix is omitted for target-endian accesses.

The target endian accessors are only available to source
files which are built per-target.

There are also functions which take the size as an argument:

load: ``ldn{endian}_p(ptr, sz)``

which performs an unsigned load of ``sz`` bytes from ``ptr``
as an ``{endian}`` order value and returns it in a uint64_t.

store: ``stn{endian}_p(ptr, sz, val)``

which stores ``val`` to ``ptr`` as an ``{endian}`` order value
of size ``sz`` bytes.


Regexes for git grep:
 - ``\<ld[us]\?[bwlq]\(_[hbl]e\)\?_p\>``
 - ``\<st[bwlq]\(_[hbl]e\)\?_p\>``
 - ``\<st24\(_[hbl]e\)\?_p\>``
 - ``\<ldn_\([hbl]e\)\?_p\>``
 - ``\<stn_\([hbl]e\)\?_p\>``

``cpu_{ld,st}*_mmu``
~~~~~~~~~~~~~~~~~~~~

These functions operate on a guest virtual address, plus a context
known as a "mmu index" which controls how that virtual address is
translated, plus a ``MemOp`` which contains alignment requirements
among other things.  The ``MemOp`` and mmu index are combined into
a single argument of type ``MemOpIdx``.

The meaning of the indexes are target specific, but specifying a
particular index might be necessary if, for instance, the helper
requires a "always as non-privileged" access rather than the
default access for the current state of the guest CPU.

These functions may cause a guest CPU exception to be taken
(e.g. for an alignment fault or MMU fault) which will result in
guest CPU state being updated and control longjmp'ing out of the
function call.  They should therefore only be used in code that is
implementing emulation of the guest CPU.

The ``retaddr`` parameter is used to control unwinding of the
guest CPU state in case of a guest CPU exception.  This is passed
to ``cpu_restore_state()``.  Therefore the value should either be 0,
to indicate that the guest CPU state is already synchronized, or
the result of ``GETPC()`` from the top level ``HELPER(foo)``
function, which is a return address into the generated code\ [#gpc]_.

.. [#gpc] Note that ``GETPC()`` should be used with great care: calling
          it in other functions that are *not* the top level
          ``HELPER(foo)`` will cause unexpected behavior. Instead, the
          value of ``GETPC()`` should be read from the helper and passed
          if needed to the functions that the helper calls.

Function names follow the pattern:

load: ``cpu_ld{size}{end}_mmu(env, ptr, oi, retaddr)``

store: ``cpu_st{size}{end}_mmu(env, ptr, val, oi, retaddr)``

``size``
 - ``b`` : 8 bits
 - ``w`` : 16 bits
 - ``l`` : 32 bits
 - ``q`` : 64 bits

``end``
 - (empty) : for target endian, or 8 bit sizes
 - ``_be`` : big endian
 - ``_le`` : little endian

Regexes for git grep:
 - ``\<cpu_ld[bwlq]\(_[bl]e\)\?_mmu\>``
 - ``\<cpu_st[bwlq]\(_[bl]e\)\?_mmu\>``


``cpu_{ld,st}*_mmuidx_ra``
~~~~~~~~~~~~~~~~~~~~~~~~~~

These functions work like the ``cpu_{ld,st}_mmu`` functions except
that the ``mmuidx`` parameter is not combined with a ``MemOp``,
and therefore there is no required alignment supplied or enforced.

Function names follow the pattern:

load: ``cpu_ld{sign}{size}{end}_mmuidx_ra(env, ptr, mmuidx, retaddr)``

store: ``cpu_st{size}{end}_mmuidx_ra(env, ptr, val, mmuidx, retaddr)``

``sign``
 - (empty) : for 32 or 64 bit sizes
 - ``u`` : unsigned
 - ``s`` : signed

``size``
 - ``b`` : 8 bits
 - ``w`` : 16 bits
 - ``l`` : 32 bits
 - ``q`` : 64 bits

``end``
 - (empty) : for target endian, or 8 bit sizes
 - ``_be`` : big endian
 - ``_le`` : little endian

Regexes for git grep:
 - ``\<cpu_ld[us]\?[bwlq]\(_[bl]e\)\?_mmuidx_ra\>``
 - ``\<cpu_st[bwlq]\(_[bl]e\)\?_mmuidx_ra\>``

``cpu_{ld,st}*_data_ra``
~~~~~~~~~~~~~~~~~~~~~~~~

These functions work like the ``cpu_{ld,st}_mmuidx_ra`` functions
except that the ``mmuidx`` parameter is taken from the current mode
of the guest CPU, as determined by ``cpu_mmu_index(env, false)``.

These are generally the preferred way to do accesses by guest
virtual address from helper functions, unless the access should
be performed with a context other than the default, or alignment
should be enforced for the access.

Function names follow the pattern:

load: ``cpu_ld{sign}{size}{end}_data_ra(env, ptr, ra)``

store: ``cpu_st{size}{end}_data_ra(env, ptr, val, ra)``

``sign``
 - (empty) : for 32 or 64 bit sizes
 - ``u`` : unsigned
 - ``s`` : signed

``size``
 - ``b`` : 8 bits
 - ``w`` : 16 bits
 - ``l`` : 32 bits
 - ``q`` : 64 bits

``end``
 - (empty) : for target endian, or 8 bit sizes
 - ``_be`` : big endian
 - ``_le`` : little endian

Regexes for git grep:
 - ``\<cpu_ld[us]\?[bwlq]\(_[bl]e\)\?_data_ra\>``
 - ``\<cpu_st[bwlq]\(_[bl]e\)\?_data_ra\>``

``cpu_{ld,st}*_data``
~~~~~~~~~~~~~~~~~~~~~

These functions work like the ``cpu_{ld,st}_data_ra`` functions
except that the ``retaddr`` parameter is 0, and thus does not
unwind guest CPU state.

This means they must only be used from helper functions where the
translator has saved all necessary CPU state.  These functions are
the right choice for calls made from hooks like the CPU ``do_interrupt``
hook or when you know for certain that the translator had to save all
the CPU state anyway.

Function names follow the pattern:

load: ``cpu_ld{sign}{size}{end}_data(env, ptr)``

store: ``cpu_st{size}{end}_data(env, ptr, val)``

``sign``
 - (empty) : for 32 or 64 bit sizes
 - ``u`` : unsigned
 - ``s`` : signed

``size``
 - ``b`` : 8 bits
 - ``w`` : 16 bits
 - ``l`` : 32 bits
 - ``q`` : 64 bits

``end``
 - (empty) : for target endian, or 8 bit sizes
 - ``_be`` : big endian
 - ``_le`` : little endian

Regexes for git grep:
 - ``\<cpu_ld[us]\?[bwlq]\(_[bl]e\)\?_data\>``
 - ``\<cpu_st[bwlq]\(_[bl]e\)\?_data\+\>``

``cpu_ld*_code``
~~~~~~~~~~~~~~~~

These functions perform a read for instruction execution.  The ``mmuidx``
parameter is taken from the current mode of the guest CPU, as determined
by ``cpu_mmu_index(env, true)``.  The ``retaddr`` parameter is 0, and
thus does not unwind guest CPU state, because CPU state is always
synchronized while translating instructions.  Any guest CPU exception
that is raised will indicate an instruction execution fault rather than
a data read fault.

In general these functions should not be used directly during translation.
There are wrapper functions that are to be used which also take care of
plugins for tracing.

Function names follow the pattern:

load: ``cpu_ld{sign}{size}_code(env, ptr)``

``sign``
 - (empty) : for 32 or 64 bit sizes
 - ``u`` : unsigned
 - ``s`` : signed

``size``
 - ``b`` : 8 bits
 - ``w`` : 16 bits
 - ``l`` : 32 bits
 - ``q`` : 64 bits

Regexes for git grep:
 - ``\<cpu_ld[us]\?[bwlq]_code\>``

``translator_ld*``
~~~~~~~~~~~~~~~~~~

These functions are a wrapper for ``cpu_ld*_code`` which also perform
any actions required by any tracing plugins.  They are only to be
called during the translator callback ``translate_insn``.

There is a set of functions ending in ``_swap`` which, if the parameter
is true, returns the value in the endianness that is the reverse of
the guest native endianness, as determined by ``TARGET_BIG_ENDIAN``.

Function names follow the pattern:

load: ``translator_ld{sign}{size}(env, ptr)``

swap: ``translator_ld{sign}{size}_swap(env, ptr, swap)``

``sign``
 - (empty) : for 32 or 64 bit sizes
 - ``u`` : unsigned
 - ``s`` : signed

``size``
 - ``b`` : 8 bits
 - ``w`` : 16 bits
 - ``l`` : 32 bits
 - ``q`` : 64 bits

Regexes for git grep:
 - ``\<translator_ld[us]\?[bwlq]\(_swap\)\?\>``

``helper_{ld,st}*_mmu``
~~~~~~~~~~~~~~~~~~~~~~~~~

These functions are intended primarily to be called by the code
generated by the TCG backend.  Like the ``cpu_{ld,st}_mmu`` functions
they perform accesses by guest virtual address, with a given ``MemOpIdx``.

They differ from ``cpu_{ld,st}_mmu`` in that they take the endianness
of the operation only from the MemOpIdx, and loads extend the return
value to the size of a host general register (``tcg_target_ulong``).

load: ``helper_ld{sign}{size}_mmu(env, addr, opindex, retaddr)``

store: ``helper_{size}_mmu(env, addr, val, opindex, retaddr)``

``sign``
 - (empty) : for 32 or 64 bit sizes
 - ``u`` : unsigned
 - ``s`` : signed

``size``
 - ``b`` : 8 bits
 - ``w`` : 16 bits
 - ``l`` : 32 bits
 - ``q`` : 64 bits

Regexes for git grep:
 - ``\<helper_ld[us]\?[bwlq]_mmu\>``
 - ``\<helper_st[bwlq]_mmu\>``

``address_space_*``
~~~~~~~~~~~~~~~~~~~

These functions are the primary ones to use when emulating CPU
or device memory accesses. They take an AddressSpace, which is the
way QEMU defines the view of memory that a device or CPU has.
(They generally correspond to being the "master" end of a hardware bus
or bus fabric.)

Each CPU has an AddressSpace. Some kinds of CPU have more than
one AddressSpace (for instance Arm guest CPUs have an AddressSpace
for the Secure world and one for NonSecure if they implement TrustZone).
Devices which can do DMA-type operations should generally have an
AddressSpace. There is also a "system address space" which typically
has all the devices and memory that all CPUs can see. (Some older
device models use the "system address space" rather than properly
modelling that they have an AddressSpace of their own.)

Functions are provided for doing byte-buffer reads and writes,
and also for doing one-data-item loads and stores.

In all cases the caller provides a MemTxAttrs to specify bus
transaction attributes, and can check whether the memory transaction
succeeded using a MemTxResult return code.

``address_space_read(address_space, addr, attrs, buf, len)``

``address_space_write(address_space, addr, attrs, buf, len)``

``address_space_rw(address_space, addr, attrs, buf, len, is_write)``

``address_space_ld{sign}{size}_{endian}(address_space, addr, attrs, txresult)``

``address_space_st{size}_{endian}(address_space, addr, val, attrs, txresult)``

``sign``
 - (empty) : for 32 or 64 bit sizes
 - ``u`` : unsigned

(No signed load operations are provided.)

``size``
 - ``b`` : 8 bits
 - ``w`` : 16 bits
 - ``l`` : 32 bits
 - ``q`` : 64 bits

``endian``
 - ``le`` : little endian
 - ``be`` : big endian

The ``_{endian}`` suffix is omitted for byte accesses.

Regexes for git grep:
 - ``\<address_space_\(read\|write\|rw\)\>``
 - ``\<address_space_ldu\?[bwql]\(_[lb]e\)\?\>``
 - ``\<address_space_st[bwql]\(_[lb]e\)\?\>``

``address_space_write_rom``
~~~~~~~~~~~~~~~~~~~~~~~~~~~

This function performs a write by physical address like
``address_space_write``, except that if the write is to a ROM then
the ROM contents will be modified, even though a write by the guest
CPU to the ROM would be ignored. This is used for non-guest writes
like writes from the gdb debug stub or initial loading of ROM contents.

Note that portions of the write which attempt to write data to a
device will be silently ignored -- only real RAM and ROM will
be written to.

Regexes for git grep:
 - ``address_space_write_rom``

``{ld,st}*_phys``
~~~~~~~~~~~~~~~~~

These are functions which are identical to
``address_space_{ld,st}*``, except that they always pass
``MEMTXATTRS_UNSPECIFIED`` for the transaction attributes, and ignore
whether the transaction succeeded or failed.

The fact that they ignore whether the transaction succeeded means
they should not be used in new code, unless you know for certain
that your code will only be used in a context where the CPU or
device doing the access has no way to report such an error.

``load: ld{sign}{size}_{endian}_phys``

``store: st{size}_{endian}_phys``

``sign``
 - (empty) : for 32 or 64 bit sizes
 - ``u`` : unsigned

(No signed load operations are provided.)

``size``
 - ``b`` : 8 bits
 - ``w`` : 16 bits
 - ``l`` : 32 bits
 - ``q`` : 64 bits

``endian``
 - ``le`` : little endian
 - ``be`` : big endian

The ``_{endian}_`` infix is omitted for byte accesses.

Regexes for git grep:
 - ``\<ldu\?[bwlq]\(_[bl]e\)\?_phys\>``
 - ``\<st[bwlq]\(_[bl]e\)\?_phys\>``

``cpu_physical_memory_*``
~~~~~~~~~~~~~~~~~~~~~~~~~

These are convenience functions which are identical to
``address_space_*`` but operate specifically on the system address space,
always pass a ``MEMTXATTRS_UNSPECIFIED`` set of memory attributes and
ignore whether the memory transaction succeeded or failed.
For new code they are better avoided:

* there is likely to be behaviour you need to model correctly for a
  failed read or write operation
* a device should usually perform operations on its own AddressSpace
  rather than using the system address space

``cpu_physical_memory_read``

``cpu_physical_memory_write``

``cpu_physical_memory_rw``

Regexes for git grep:
 - ``\<cpu_physical_memory_\(read\|write\|rw\)\>``

``cpu_memory_rw_debug``
~~~~~~~~~~~~~~~~~~~~~~~

Access CPU memory by virtual address for debug purposes.

This function is intended for use by the GDB stub and similar code.
It takes a virtual address, converts it to a physical address via
an MMU lookup using the current settings of the specified CPU,
and then performs the access (using ``address_space_rw`` for
reads or ``cpu_physical_memory_write_rom`` for writes).
This means that if the access is a write to a ROM then this
function will modify the contents (whereas a normal guest CPU access
would ignore the write attempt).

``cpu_memory_rw_debug``

``dma_memory_*``
~~~~~~~~~~~~~~~~

These behave like ``address_space_*``, except that they perform a DMA
barrier operation first.

**TODO**: We should provide guidance on when you need the DMA
barrier operation and when it's OK to use ``address_space_*``, and
make sure our existing code is doing things correctly.

``dma_memory_read``

``dma_memory_write``

``dma_memory_rw``

Regexes for git grep:
 - ``\<dma_memory_\(read\|write\|rw\)\>``
 - ``\<ldu\?[bwlq]\(_[bl]e\)\?_dma\>``
 - ``\<st[bwlq]\(_[bl]e\)\?_dma\>``

``pci_dma_*`` and ``{ld,st}*_pci_dma``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

These functions are specifically for PCI device models which need to
perform accesses where the PCI device is a bus master. You pass them a
``PCIDevice *`` and they will do ``dma_memory_*`` operations on the
correct address space for that device.

``pci_dma_read``

``pci_dma_write``

``pci_dma_rw``

``load: ld{sign}{size}_{endian}_pci_dma``

``store: st{size}_{endian}_pci_dma``

``sign``
 - (empty) : for 32 or 64 bit sizes
 - ``u`` : unsigned

(No signed load operations are provided.)

``size``
 - ``b`` : 8 bits
 - ``w`` : 16 bits
 - ``l`` : 32 bits
 - ``q`` : 64 bits

``endian``
 - ``le`` : little endian
 - ``be`` : big endian

The ``_{endian}_`` infix is omitted for byte accesses.

Regexes for git grep:
 - ``\<pci_dma_\(read\|write\|rw\)\>``
 - ``\<ldu\?[bwlq]\(_[bl]e\)\?_pci_dma\>``
 - ``\<st[bwlq]\(_[bl]e\)\?_pci_dma\>``
