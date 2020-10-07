Supported CPU model configurations on MIPS hosts
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

QEMU supports variety of MIPS CPU models:

Supported CPU models for MIPS32 hosts
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following CPU models are supported for use on MIPS32 hosts.
Administrators / applications are recommended to use the CPU model that
matches the generation of the host CPUs in use. In a deployment with a
mixture of host CPU models between machines, if live migration
compatibility is required, use the newest CPU model that is compatible
across all desired hosts.

``mips32r6-generic``
    MIPS32 Processor (Release 6, 2015)

``P5600``
    MIPS32 Processor (P5600, 2014)

``M14K``, ``M14Kc``
    MIPS32 Processor (M14K, 2009)

``74Kf``
    MIPS32 Processor (74K, 2007)

``34Kf``
    MIPS32 Processor (34K, 2006)

``24Kc``, ``24KEc``, ``24Kf``
    MIPS32 Processor (24K, 2003)

``4Kc``, ``4Km``, ``4KEcR1``, ``4KEmR1``, ``4KEc``, ``4KEm``
    MIPS32 Processor (4K, 1999)


Supported CPU models for MIPS64 hosts
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following CPU models are supported for use on MIPS64 hosts.
Administrators / applications are recommended to use the CPU model that
matches the generation of the host CPUs in use. In a deployment with a
mixture of host CPU models between machines, if live migration
compatibility is required, use the newest CPU model that is compatible
across all desired hosts.

``I6400``
    MIPS64 Processor (Release 6, 2014)

``Loongson-2E``
    MIPS64 Processor (Loongson 2, 2006)

``Loongson-2F``
    MIPS64 Processor (Loongson 2, 2008)

``Loongson-3A1000``
    MIPS64 Processor (Loongson 3, 2010)

``Loongson-3A4000``
    MIPS64 Processor (Loongson 3, 2018)

``mips64dspr2``
    MIPS64 Processor (Release 2, 2006)

``MIPS64R2-generic``, ``5KEc``, ``5KEf``
    MIPS64 Processor (Release 2, 2002)

``20Kc``
    MIPS64 Processor (20K, 2000

``5Kc``, ``5Kf``
    MIPS64 Processor (5K, 1999)

``VR5432``
    MIPS64 Processor (VR, 1998)

``R4000``
    MIPS64 Processor (MIPS III, 1991)


Supported CPU models for nanoMIPS hosts
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following CPU models are supported for use on nanoMIPS hosts.
Administrators / applications are recommended to use the CPU model that
matches the generation of the host CPUs in use. In a deployment with a
mixture of host CPU models between machines, if live migration
compatibility is required, use the newest CPU model that is compatible
across all desired hosts.

``I7200``
    MIPS I7200 (nanoMIPS, 2018)

Preferred CPU models for MIPS hosts
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The following CPU models are preferred for use on different MIPS hosts:

``MIPS III``
    R4000

``MIPS32R2``
    34Kf

``MIPS64R6``
    I6400

``nanoMIPS``
    I7200

