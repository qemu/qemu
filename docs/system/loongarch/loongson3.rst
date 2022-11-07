:orphan:

==========================================
loongson3 virt generic platform (``virt``)
==========================================

The ``virt`` machine use gpex host bridge, and there are some
emulated devices on virt board, such as loongson7a RTC device,
IOAPIC device, ACPI device and so on.

Supported devices
-----------------

The ``virt`` machine supports:
- Gpex host bridge
- Ls7a RTC device
- Ls7a IOAPIC device
- ACPI GED device
- Fw_cfg device
- PCI/PCIe devices
- Memory device
- CPU device. Type: la464-loongarch-cpu.

CPU and machine Type
--------------------

The ``qemu-system-loongarch64`` provides emulation for virt
machine. You can specify the machine type ``virt`` and
cpu type ``la464-loongarch-cpu``.

Boot options
------------

We can boot the LoongArch virt machine by specifying the uefi bios,
initrd, and linux kernel. And those source codes and binary files
can be accessed by following steps.

(1) booting command:

.. code-block:: bash

  $ qemu-system-loongarch64 -machine virt -m 4G -cpu la464-loongarch-cpu \
      -smp 1 -bios QEMU_EFI.fd -kernel vmlinuz.efi -initrd initrd.img \
      -append "root=/dev/ram rdinit=/sbin/init console=ttyS0,115200" \
      --nographic

Note: The running speed may be a little slow, as the performance of our
qemu and uefi bios is not perfect, and it is being fixed.

(2) cross compiler tools:

.. code-block:: bash

  wget https://github.com/loongson/build-tools/releases/download/ \
  2022.05.29/loongarch64-clfs-5.0-cross-tools-gcc-full.tar.xz

  tar -vxf loongarch64-clfs-5.0-cross-tools-gcc-full.tar.xz

(3) qemu compile configure option:

.. code-block:: bash

  ./configure --disable-rdma --disable-pvrdma --prefix=usr \
              --target-list="loongarch64-softmmu" \
              --disable-libiscsi --disable-libnfs --disable-libpmem \
              --disable-glusterfs --enable-libusb --enable-usb-redir \
              --disable-opengl --disable-xen --enable-spice \
              --enable-debug --disable-capstone --disable-kvm \
              --enable-profiler
  make

(4) uefi bios source code and compile method:

.. code-block:: bash

  git clone https://github.com/loongson/edk2-LoongarchVirt.git

  cd edk2-LoongarchVirt

  git submodule update --init

  export PATH=$YOUR_COMPILER_PATH/bin:$PATH

  export WORKSPACE=`pwd`

  export PACKAGES_PATH=$WORKSPACE/edk2-LoongarchVirt

  export GCC5_LOONGARCH64_PREFIX=loongarch64-unknown-linux-gnu-

  edk2-LoongarchVirt/edksetup.sh

  make -C edk2-LoongarchVirt/BaseTools

  build --buildtarget=DEBUG --tagname=GCC5 --arch=LOONGARCH64  --platform=OvmfPkg/LoongArchQemu/Loongson.dsc

  build --buildtarget=RELEASE --tagname=GCC5 --arch=LOONGARCH64  --platform=OvmfPkg/LoongArchQemu/Loongson.dsc

The efi binary file path:

  Build/LoongArchQemu/DEBUG_GCC5/FV/QEMU_EFI.fd

  Build/LoongArchQemu/RELEASE_GCC5/FV/QEMU_EFI.fd

(5) linux kernel source code and compile method:

.. code-block:: bash

  git clone https://github.com/loongson/linux.git

  export PATH=$YOUR_COMPILER_PATH/bin:$PATH

  export LD_LIBRARY_PATH=$YOUR_COMPILER_PATH/lib:$LD_LIBRARY_PATH

  export LD_LIBRARY_PATH=$YOUR_COMPILER_PATH/loongarch64-unknown-linux-gnu/lib/:$LD_LIBRARY_PATH

  make ARCH=loongarch CROSS_COMPILE=loongarch64-unknown-linux-gnu- loongson3_defconfig

  make ARCH=loongarch CROSS_COMPILE=loongarch64-unknown-linux-gnu-

  make ARCH=loongarch CROSS_COMPILE=loongarch64-unknown-linux-gnu- install

  make ARCH=loongarch CROSS_COMPILE=loongarch64-unknown-linux-gnu- modules_install

Note: The branch of linux source code is loongarch-next.

(6) initrd file:

  You can use busybox tool and the linux modules to make a initrd file. Or you can access the
  binary files: https://github.com/yangxiaojuan-loongson/qemu-binary
