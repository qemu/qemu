=========================================================
User Space Accelerator Development Kit (UADK) Compression
=========================================================
UADK is a general-purpose user space accelerator framework that uses shared
virtual addressing (SVA) to provide a unified programming interface for
hardware acceleration of cryptographic and compression algorithms.

UADK includes Unified/User-space-access-intended Accelerator Framework (UACCE),
which enables hardware accelerators from different vendors that support SVA to
adapt to UADK.

Currently, HiSilicon Kunpeng hardware accelerators have been registered with
UACCE. Through the UADK framework, users can run cryptographic and compression
algorithms using hardware accelerators instead of CPUs, freeing up CPU
computing power and improving computing performance.

https://github.com/Linaro/uadk/tree/master/docs

UADK Framework
==============
UADK consists of UACCE, vendors' drivers, and an algorithm layer. UADK requires
the hardware accelerator to support SVA, and the operating system to support
IOMMU and SVA. Hardware accelerators from different vendors are registered as
different character devices with UACCE by using kernel-mode drivers of the
vendors. A user can access the hardware accelerators by performing user-mode
operations on the character devices.

::

          +----------------------------------+
          |                apps              |
          +----+------------------------+----+
               |                        |
               |                        |
       +-------+--------+       +-------+-------+
       |   scheduler    |       | alg libraries |
       +-------+--------+       +-------+-------+
               |                         |
               |                         |
               |                         |
               |                +--------+------+
               |                | vendor drivers|
               |                +-+-------------+
               |                  |
               |                  |
            +--+------------------+--+
            |         libwd          |
    User    +----+-------------+-----+
    --------------------------------------------------
    Kernel    +--+-----+   +------+
              | uacce  |   | smmu |
              +---+----+   +------+
                  |
              +---+------------------+
              | vendor kernel driver |
              +----------------------+
    --------------------------------------------------
             +----------------------+
             |   HW Accelerators    |
             +----------------------+

UADK Installation
-----------------
Build UADK
^^^^^^^^^^

.. code-block:: shell

    git clone https://github.com/Linaro/uadk.git
    cd uadk
    mkdir build
    ./autogen.sh
    ./configure --prefix=$PWD/build
    make
    make install

Without --prefix, UADK will be installed to /usr/local/lib by default.
If get error:"cannot find -lnuma", please install the libnuma-dev

Run pkg-config libwd to ensure env is setup correctly
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

* export PKG_CONFIG_PATH=$PWD/build/lib/pkgconfig
* pkg-config libwd --cflags --libs
  -I/usr/local/include -L/usr/local/lib -lwd

* export PKG_CONFIG_PATH is required on demand.
  Not required if UADK is installed to /usr/local/lib

UADK Host Kernel Requirements
-----------------------------
User needs to make sure that ``UACCE`` is already supported in Linux kernel.
The kernel version should be at least v5.9 with SVA (Shared Virtual
Addressing) enabled.

Kernel Configuration
^^^^^^^^^^^^^^^^^^^^

``UACCE`` could be built as module or built-in.

Here's an example to enable UACCE with hardware accelerator in HiSilicon
Kunpeng platform.

*    CONFIG_IOMMU_SVA_LIB=y
*    CONFIG_ARM_SMMU=y
*    CONFIG_ARM_SMMU_V3=y
*    CONFIG_ARM_SMMU_V3_SVA=y
*    CONFIG_PCI_PASID=y
*    CONFIG_UACCE=y
*    CONFIG_CRYPTO_DEV_HISI_QM=y
*    CONFIG_CRYPTO_DEV_HISI_ZIP=y

Make sure all these above kernel configurations are selected.

Accelerator dev node permissions
--------------------------------
Hardware accelerators (eg: HiSilicon Kunpeng Zip accelerator) gets registered to
UADK and char devices are created in dev directory. In order to access resources
on hardware accelerator devices, write permission should be provided to user.

.. code-block:: shell

    $ sudo chmod 777 /dev/hisi_zip-*

How To Use UADK Compression In QEMU Migration
---------------------------------------------
* Make sure UADK is installed as above
* Build ``QEMU`` with ``--enable-uadk`` parameter

  E.g. configure --target-list=aarch64-softmmu --enable-kvm ``--enable-uadk``

* Enable ``UADK`` compression during migration

  Set ``migrate_set_parameter multifd-compression uadk``

Since UADK uses Shared Virtual Addressing(SVA) and device access virtual memory
directly it is possible that SMMUv3 may encounter page faults while walking the
IO page tables. This may impact the performance. In order to mitigate this,
please make sure to specify ``-mem-prealloc`` parameter to the destination VM
boot parameters.

Though both UADK and ZLIB are based on the deflate compression algorithm, UADK
is not fully compatible with ZLIB. Hence, please make sure to use ``uadk`` on
both source and destination during migration.
