
NUMA mechanics for sPAPR (pseries machines)
============================================

NUMA in sPAPR works different than the System Locality Distance
Information Table (SLIT) in ACPI. The logic is explained in the LOPAPR
1.1 chapter 15, "Non Uniform Memory Access (NUMA) Option". This
document aims to complement this specification, providing details
of the elements that impacts how QEMU views NUMA in pseries.

Associativity and ibm,associativity property
--------------------------------------------

Associativity is defined as a group of platform resources that has
similar mean performance (or in our context here, distance) relative to
everyone else outside of the group.

The format of the ibm,associativity property varies with the value of
bit 0 of byte 5 of the ibm,architecture-vec-5 property. The format with
bit 0 equal to zero is deprecated. The current format, with the bit 0
with the value of one, makes ibm,associativity property represent the
physical hierarchy of the platform, as one or more lists that starts
with the highest level grouping up to the smallest. Considering the
following topology:

::

    Mem M1 ---- Proc P1    |
    -----------------      | Socket S1  ---|
          chip C1          |               |
                                           | HW module 1 (MOD1)
    Mem M2 ---- Proc P2    |               |
    -----------------      | Socket S2  ---|
          chip C2          |

The ibm,associativity property for the processors would be:

* P1: {MOD1, S1, C1, P1}
* P2: {MOD1, S2, C2, P2}

Each allocable resource has an ibm,associativity property. The LOPAPR
specification allows multiple lists to be present in this property,
considering that the same resource can have multiple connections to the
platform.

Relative Performance Distance and ibm,associativity-reference-points
--------------------------------------------------------------------

The ibm,associativity-reference-points property is an array that is used
to define the relevant performance/distance  related boundaries, defining
the NUMA levels for the platform.

The definition of its elements also varies with the value of bit 0 of byte 5
of the ibm,architecture-vec-5 property. The format with bit 0 equal to zero
is also deprecated. With the current format, each integer of the
ibm,associativity-reference-points represents an 1 based ordinal index (i.e.
the first element is 1) of the ibm,associativity array. The first
boundary is the most significant to application performance, followed by
less significant boundaries. Allocated resources that belongs to the
same performance boundaries are expected to have relative NUMA distance
that matches the relevancy of the boundary itself. Resources that belongs
to the same first boundary will have the shortest distance from each
other. Subsequent boundaries represents greater distances and degraded
performance.

Using the previous example, the following setting reference points defines
three NUMA levels:

* ibm,associativity-reference-points = {0x3, 0x2, 0x1}

The first NUMA level (0x3) is interpreted as the third element of each
ibm,associativity array, the second level is the second element and
the third level is the first element. Let's also consider that elements
belonging to the first NUMA level have distance equal to 10 from each
other, and each NUMA level doubles the distance from the previous. This
means that the second would be 20 and the third level 40. For the P1 and
P2 processors, we would have the following NUMA levels:

::

  * ibm,associativity-reference-points = {0x3, 0x2, 0x1}

  * P1: associativity{MOD1, S1, C1, P1}

  First NUMA level (0x3) => associativity[2] = C1
  Second NUMA level (0x2) => associativity[1] = S1
  Third NUMA level (0x1) => associativity[0] = MOD1

  * P2: associativity{MOD1, S2, C2, P2}

  First NUMA level (0x3) => associativity[2] = C2
  Second NUMA level (0x2) => associativity[1] = S2
  Third NUMA level (0x1) => associativity[0] = MOD1

  P1 and P2 have the same third NUMA level, MOD1: Distance between them = 40

Changing the ibm,associativity-reference-points array changes the performance
distance attributes for the same associativity arrays, as the following
example illustrates:

::

  * ibm,associativity-reference-points = {0x2}

  * P1: associativity{MOD1, S1, C1, P1}

  First NUMA level (0x2) => associativity[1] = S1

  * P2: associativity{MOD1, S2, C2, P2}

  First NUMA level (0x2) => associativity[1] = S2

  P1 and P2 does not have a common performance boundary. Since this is a one level
  NUMA configuration, distance between them is one boundary above the first
  level, 20.


In a hypothetical platform where all resources inside the same hardware module
is considered to be on the same performance boundary:

::

  * ibm,associativity-reference-points = {0x1}

  * P1: associativity{MOD1, S1, C1, P1}

  First NUMA level (0x1) => associativity[0] = MOD0

  * P2: associativity{MOD1, S2, C2, P2}

  First NUMA level (0x1) => associativity[0] = MOD0

  P1 and P2 belongs to the same first order boundary. The distance between then
  is 10.


How the pseries Linux guest calculates NUMA distances
=====================================================

Another key difference between ACPI SLIT and the LOPAPR regarding NUMA is
how the distances are expressed. The SLIT table provides the NUMA distance
value between the relevant resources. LOPAPR does not provide a standard
way to calculate it. We have the ibm,associativity for each resource, which
provides a common-performance hierarchy,  and the ibm,associativity-reference-points
array that tells which level of associativity is considered to be relevant
or not.

The result is that each OS is free to implement and to interpret the distance
as it sees fit. For the pseries Linux guest, each level of NUMA duplicates
the distance of the previous level, and the maximum amount of levels is
limited to MAX_DISTANCE_REF_POINTS = 4 (from arch/powerpc/mm/numa.c in the
kernel tree). This results in the following distances:

* both resources in the first NUMA level: 10
* resources one NUMA level apart: 20
* resources two NUMA levels apart: 40
* resources three NUMA levels apart: 80
* resources four NUMA levels apart: 160


Consequences for QEMU NUMA tuning
---------------------------------

The way the pseries Linux guest calculates NUMA distances has a direct effect
on what QEMU users can expect when doing NUMA tuning. As of QEMU 5.1, this is
the default ibm,associativity-reference-points being used in the pseries
machine:

ibm,associativity-reference-points = {0x4, 0x4, 0x2}

The first and second level are equal, 0x4, and a third one was added in
commit a6030d7e0b35 exclusively for NVLink GPUs support. This means that
regardless of how the ibm,associativity properties are being created in
the device tree, the pseries Linux guest will only recognize three scenarios
as far as NUMA distance goes:

* if the resources belongs to the same first NUMA level = 10
* second level is skipped since it's equal to the first
* all resources that aren't a NVLink GPU, it is guaranteed that they will belong
  to the same third NUMA level, having distance = 40
* for NVLink GPUs, distance = 80 from everything else

In short, we can summarize the NUMA distances seem in pseries Linux guests, using
QEMU up to 5.1, as follows:

* local distance, i.e. the distance of the resource to its own NUMA node: 10
* if it's a NVLink GPU device, distance: 80
* every other resource, distance: 40

This also means that user input in QEMU command line does not change the
NUMA distancing inside the guest for the pseries machine.
