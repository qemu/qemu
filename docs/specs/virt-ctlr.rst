Virtual System Controller
=========================

The ``virt-ctrl`` device is a simple interface defined for the pure
virtual machine with no hardware reference implementation to allow the
guest kernel to send command to the host hypervisor.

The specification can evolve, the current state is defined as below.

This is a MMIO mapped device using 256 bytes.

Two 32bit registers are defined:

the features register (read-only, address 0x00)
   This register allows the device to report features supported by the
   controller.
   The only feature supported for the moment is power control (0x01).

the command register (write-only, address 0x04)
   This register allows the kernel to send the commands to the hypervisor.
   The implemented commands are part of the power control feature and
   are reset (1), halt (2) and panic (3).
   A basic command, no-op (0), is always present and can be used to test the
   register access. This command has no effect.
