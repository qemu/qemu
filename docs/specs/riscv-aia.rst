.. _riscv-aia:

RISC-V AIA support for RISC-V machines
======================================

AIA (Advanced Interrupt Architecture) support is implemented in the ``virt``
RISC-V machine for TCG and KVM accelerators.

The support consists of two main modes:

- "aia=aplic": adds one or more APLIC (Advanced Platform Level Interrupt Controller)
  devices
- "aia=aplic-imsic": adds one or more APLIC device and an IMSIC (Incoming MSI
   Controller) device for each CPU

From an user standpoint, these modes will behave the same regardless of the accelerator
used.  From a developer standpoint the accelerator settings will change what it being
emulated in userspace versus what is being emulated by an in-kernel irqchip.

When running TCG, all controllers are emulated in userspace, including machine mode
(m-mode) APLIC and IMSIC (when applicable).

When running KVM:

- no m-mode is provided, so there is no m-mode APLIC or IMSIC emulation regardless of
  the AIA mode chosen
- with "aia=aplic", s-mode APLIC will be emulated by userspace
- with "aia=aplic-imsic" there are two possibilities.  If no additional KVM option
  is provided there will be no APLIC or IMSIC emulation in userspace, and the virtual
  machine will use the provided in-kernel APLIC and IMSIC controllers.  If the user
  chooses to use the irqchip in split mode via "-accel kvm,kernel-irqchip=split",
  s-mode APLIC will be emulated while using the s-mode IMSIC from the irqchip

The following table summarizes how the AIA and accelerator options defines what
we will emulate in userspace:


.. list-table:: How AIA and accel options changes controller emulation
   :widths: 25 25 25 25 25 25 25
   :header-rows: 1

   * - Accel
     - Accel props
     - AIA type
     - APLIC m-mode
     - IMSIC m-mode
     - APLIC s-mode
     - IMSIC s-mode
   * - tcg
     - ---
     - aplic
     - emul
     - n/a
     - emul
     - n/a
   * - tcg
     - ---
     - aplic-imsic
     - emul
     - emul
     - emul
     - emul
   * - kvm
     - ---
     - aplic
     - n/a
     - n/a
     - emul
     - n/a
   * - kvm
     - none
     - aplic-imsic
     - n/a
     - n/a
     - in-kernel
     - in-kernel
   * - kvm
     - irqchip=split
     - aplic-imsic
     - n/a
     - n/a
     - emul
     - in-kernel
