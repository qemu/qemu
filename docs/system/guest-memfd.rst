.. _guest-memfd:

guest-memfd support
===================

Recent kernels allow for the creation of a guest-memfd file
descriptor, which can be used to back VMs in a similar manner as a
memfd file descriptor, but is intended specifically for this purpose
and allows for closer coordination between KVM and the management of
this memory to enable more advanced/VM-specific use-cases.

Initially this additional functionality centered around providing a
common/centralized place for managing kernel-side memory handing
requirements for various Confidential Guest architectures. (For more
on Confidential Guests, see :ref:`confidential-guest-support`).

guest_memfd has since evolved to become a more general-purpose way to
allocate/manage guest memory and potentially allow for things like
providing additional memory isolation within the kernel[1] and support
for persisting a guest's state across kexec to allow for live-updating
the host kernel with minimal guest downtime[2].

Usage
-----

For Confidential Guests, guest-memfd is currently utilized internally
by QEMU to handle private guest memory, independently of whatever
memory backend the user has configured for normal/non-private/shared
guest memory. To avoid doubling memory, QEMU discards memory in
response to the guest converting GPA ranges between shared/private.
(e.g. if GPA X is converted from private to shared, the guest-memfd FD
offset corresponding to GPA x will be truncated since the memory will
be provided by the memory backend the user configured for shared
memory, and vice-versa). This is handled automatically/internally for
Confidential Guests that rely on this handling and is not directly
exposed by QEMU command-line options.

For non-Confidential guests, guest-memfd can be used in a manner that
is somewhat interchangeable with a normal memfd. Currently, this is
handled by using the same memory-backend implementation as memfd, but
with an additional 'guest-memfd=on' option. E.g.::

    qemu ... \
      -object memory-backend-memfd,id=ID,size=SIZE,share=on,guest-memfd=on

Note that the share=on option is required for guest-memfd, since it
does not support anonymous memory allocations or COW-like semantics.

Also note that there are a couple of limitations in using guest_memfd
in this way compared to a normal memfd, which is why the option is
exposed as an experimental for the time being:

 * guest_memfd does not currently support the hugetlb=on option
 * guest_memfd does not currently support Transparent Huge Pages

References
----------

- `[1] directmap removal <https://lore.kernel.org/kvm/20260317141031.514-1-kalyazin@amazon.com/>`__
- `[2] LUO <https://lore.kernel.org/kvm/20260728121138.1103610-1-tarunsahu@google.com/>`__
