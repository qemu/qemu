Inter-VM Shared Memory Flat Device
----------------------------------

The ivshmem-flat device is meant to be used on machines that lack a PCI bus,
making them unsuitable for the use of the traditional ivshmem device modeled as
a PCI device. Machines like those with a Cortex-M MCU are good candidates to use
the ivshmem-flat device. Also, since the flat version maps the control and
status registers directly to the memory, it requires a quite tiny "device
driver" to interact with other VMs, which is useful in some RTOSes, like
Zephyr, which usually run on constrained resource targets.

Similar to the ivshmem device, the ivshmem-flat device supports both peer
notification via HW interrupts and Inter-VM shared memory. This allows the
device to be used together with the traditional ivshmem, enabling communication
between, for instance, an aarch64 VM  (using the traditional ivshmem device and
running Linux), and an arm VM (using the ivshmem-flat device and running Zephyr
instead).

The ivshmem-flat device does not support the use of a ``memdev`` option (see
ivshmem.rst for more details). It relies on the ivshmem server to create and
distribute the proper shared memory file descriptor and the eventfd(s) to notify
(interrupt) the peers. Therefore, to use this device, it is always necessary to
have an ivshmem server up and running for proper device creation.

Although the ivshmem-flat supports both peer notification (interrupts) and
shared memory, the interrupt mechanism is optional. If no input IRQ is
specified for the device it is disabled, preventing the VM from notifying or
being notified by other VMs (a warning will be displayed to the user to inform
the IRQ mechanism is disabled). The shared memory region is always present.

The MMRs (INTRMASK, INTRSTATUS, IVPOSITION, and DOORBELL registers) offsets at
the MMR region, and their functions, follow the ivshmem spec, so they work
exactly as in the ivshmem PCI device (see ./specs/ivshmem-spec.txt).
