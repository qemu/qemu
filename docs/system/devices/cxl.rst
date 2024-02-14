Compute Express Link (CXL)
==========================
From the view of a single host, CXL is an interconnect standard that
targets accelerators and memory devices attached to a CXL host.
This description will focus on those aspects visible either to
software running on a QEMU emulated host or to the internals of
functional emulation. As such, it will skip over many of the
electrical and protocol elements that would be more of interest
for real hardware and will dominate more general introductions to CXL.
It will also completely ignore the fabric management aspects of CXL
by considering only a single host and a static configuration.

CXL shares many concepts and much of the infrastructure of PCI Express,
with CXL Host Bridges, which have CXL Root Ports which may be directly
attached to CXL or PCI End Points. Alternatively there may be CXL Switches
with CXL and PCI Endpoints attached below them.  In many cases additional
control and capabilities are exposed via PCI Express interfaces.
This sharing of interfaces and hence emulation code is reflected
in how the devices are emulated in QEMU. In most cases the various
CXL elements are built upon an equivalent PCIe devices.

CXL devices support the following interfaces:

* Most conventional PCIe interfaces

  - Configuration space access
  - BAR mapped memory accesses used for registers and mailboxes.
  - MSI/MSI-X
  - AER
  - DOE mailboxes
  - IDE
  - Many other PCI express defined interfaces..

* Memory operations

  - Equivalent of accessing DRAM / NVDIMMs. Any access / feature
    supported by the host for normal memory should also work for
    CXL attached memory devices.

* Cache operations. The are mostly irrelevant to QEMU emulation as
  QEMU is not emulating a coherency protocol. Any emulation related
  to these will be device specific and is out of the scope of this
  document.

CXL 2.0 Device Types
--------------------
CXL 2.0 End Points are often categorized into three types.

**Type 1:** These support coherent caching of host memory.  Example might
be a crypto accelerators.  May also have device private memory accessible
via means such as PCI memory reads and writes to BARs.

**Type 2:** These support coherent caching of host memory and host
managed device memory (HDM) for which the coherency protocol is managed
by the host. This is a complex topic, so for more information on CXL
coherency see the CXL 2.0 specification.

**Type 3 Memory devices:**  These devices act as a means of attaching
additional memory (HDM) to a CXL host including both volatile and
persistent memory. The CXL topology may support interleaving across a
number of Type 3 memory devices using HDM Decoders in the host, host
bridge, switch upstream port and endpoints.

Scope of CXL emulation in QEMU
------------------------------
The focus of CXL emulation is CXL revision 2.0 and later. Earlier CXL
revisions defined a smaller set of features, leaving much of the control
interface as implementation defined or device specific, making generic
emulation challenging with host specific firmware being responsible
for setup and the Endpoints being presented to operating systems
as Root Complex Integrated End Points. CXL rev 2.0 looks a lot
more like PCI Express, with fully specified discoverability
of the CXL topology.

CXL System components
----------------------
A CXL system is made up a Host with a number of 'standard components'
the control and capabilities of which are discoverable by system software
using means described in the CXL 2.0 specification.

CXL Fixed Memory Windows (CFMW)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
A CFMW consists of a particular range of Host Physical Address space
which is routed to particular CXL Host Bridges.  At time of generic
software initialization it will have a particularly interleaving
configuration and associated Quality of Service Throttling Group (QTG).
This information is available to system software, when making
decisions about how to configure interleave across available CXL
memory devices.  It is provide as CFMW Structures (CFMWS) in
the CXL Early Discovery Table, an ACPI table.

Note: QTG 0 is the only one currently supported in QEMU.

CXL Host Bridge (CXL HB)
~~~~~~~~~~~~~~~~~~~~~~~~
A CXL host bridge is similar to the PCIe equivalent, but with a
specification defined register interface called CXL Host Bridge
Component Registers (CHBCR). The location of this CHBCR MMIO
space is described to system software via a CXL Host Bridge
Structure (CHBS) in the CEDT ACPI table.  The actual interfaces
are identical to those used for other parts of the CXL hierarchy
as CXL Component Registers in PCI BARs.

Interfaces provided include:

* Configuration of HDM Decoders to route CXL Memory accesses with
  a particularly Host Physical Address range to the target port
  below which the CXL device servicing that address lies.  This
  may be a mapping to a single Root Port (RP) or across a set of
  target RPs.

CXL Root Ports (CXL RP)
~~~~~~~~~~~~~~~~~~~~~~~
A CXL Root Port serves the same purpose as a PCIe Root Port.
There are a number of CXL specific Designated Vendor Specific
Extended Capabilities (DVSEC) in PCIe Configuration Space
and associated component register access via PCI bars.

CXL Switch
~~~~~~~~~~
Here we consider a simple CXL switch with only a single
virtual hierarchy. Whilst more complex devices exist, their
visibility to a particular host is generally the same as for
a simple switch design. Hosts often have no awareness
of complex rerouting and device pooling, they simply see
devices being hot added or hot removed.

A CXL switch has a similar architecture to those in PCIe,
with a single upstream port, internal PCI bus and multiple
downstream ports.

Both the CXL upstream and downstream ports have CXL specific
DVSECs in configuration space, and component registers in PCI
BARs.  The Upstream Port has the configuration interfaces for
the HDM decoders which route incoming memory accesses to the
appropriate downstream port.

A CXL switch is created in a similar fashion to PCI switches
by creating an upstream port (cxl-upstream) and a number of
downstream ports on the internal switch bus (cxl-downstream).

CXL Memory Devices - Type 3
~~~~~~~~~~~~~~~~~~~~~~~~~~~
CXL type 3 devices use a PCI class code and are intended to be supported
by a generic operating system driver. They have HDM decoders
though in these EP devices, the decoder is responsible not for
routing but for translation of the incoming host physical address (HPA)
into a Device Physical Address (DPA).

CXL Memory Interleave
---------------------
To understand the interaction of different CXL hardware components which
are emulated in QEMU, let us consider a memory read in a fully configured
CXL topology.  Note that system software is responsible for configuration
of all components with the exception of the CFMWs. System software is
responsible for allocating appropriate ranges from within the CFMWs
and exposing those via normal memory configurations as would be done
for system RAM.

Example system topology. x marks the match in each decoder level::

  |<------------------SYSTEM PHYSICAL ADDRESS MAP (1)----------------->|
  |    __________   __________________________________   __________    |
  |   |          | |                                  | |          |   |
  |   | CFMW 0   | |  CXL Fixed Memory Window 1       | | CFMW 2   |   |
  |   | HB0 only | |  Configured to interleave memory | | HB1 only |   |
  |   |          | |  memory accesses across HB0/HB1  | |          |   |
  |   |__________| |_____x____________________________| |__________|   |
           |             |                     |             |
           |             |                     |             |
           |             |                     |             |
           |       Interleave Decoder          |             |
           |       Matches this HB             |             |
           \_____________|                     |_____________/
               __________|__________      _____|_______________
              |                     |    |                     |
       (2)    | CXL HB 0            |    | CXL HB 1            |
              | HB IntLv Decoders   |    | HB IntLv Decoders   |
              | PCI/CXL Root Bus 0c |    | PCI/CXL Root Bus 0d |
              |                     |    |                     |
              |___x_________________|    |_____________________|
                  |                |       |               |
                  |                |       |               |
       A HB 0 HDM Decoder          |       |               |
       matches this Port           |       |               |
                  |                |       |               |
       ___________|___   __________|__   __|_________   ___|_________
   (3)|  Root Port 0  | | Root Port 1 | | Root Port 2| | Root Port 3 |
      |  Appears in   | | Appears in  | | Appears in | | Appear in   |
      |  PCI topology | | PCI topology| | PCI topo   | | PCI topo    |
      |  as 0c:00.0   | | as 0c:01.0  | | as de:00.0 | | as de:01.0  |
      |_______________| |_____________| |____________| |_____________|
            |                  |               |              |
            |                  |               |              |
       _____|_________   ______|______   ______|_____   ______|_______
   (4)|     x         | |             | |            | |              |
      | CXL Type3 0   | | CXL Type3 1 | | CXL type3 2| | CLX Type 3 3 |
      |               | |             | |            | |              |
      | PMEM0(Vol LSA)| | PMEM1 (...) | | PMEM2 (...)| | PMEM3 (...)  |
      | Decoder to go | |             | |            | |              |
      | from host PA  | | PCI 0e:00.0 | | PCI df:00.0| | PCI e0:00.0  |
      | to device PA  | |             | |            | |              |
      | PCI as 0d:00.0| |             | |            | |              |
      |_______________| |_____________| |____________| |______________|

Notes:

(1) **3 CXL Fixed Memory Windows (CFMW)** corresponding to different
    ranges of the system physical address map.  Each CFMW has
    particular interleave setup across the CXL Host Bridges (HB)
    CFMW0 provides uninterleaved access to HB0, CFMW2 provides
    uninterleaved access to HB1. CFMW1 provides interleaved memory access
    across HB0 and HB1.

(2) **Two CXL Host Bridges**. Each of these has 2 CXL Root Ports and
    programmable HDM decoders to route memory accesses either to
    a single port or interleave them across multiple ports.
    A complex configuration here, might be to use the following HDM
    decoders in HB0. HDM0 routes CFMW0 requests to RP0 and hence
    part of CXL Type3 0. HDM1 routes CFMW0 requests from a
    different region of the CFMW0 PA range to RP2 and hence part
    of CXL Type 3 1.  HDM2 routes yet another PA range from within
    CFMW0 to be interleaved across RP0 and RP1, providing 2 way
    interleave of part of the memory provided by CXL Type3 0 and
    CXL Type 3 1. HDM3 routes those interleaved accesses from
    CFMW1 that target HB0 to RP 0 and another part of the memory of
    CXL Type 3 0 (as part of a 2 way interleave at the system level
    across for example CXL Type3 0 and CXL Type3 2.
    HDM4 is used to enable system wide 4 way interleave across all
    the present CXL type3 devices, by interleaving those (interleaved)
    requests that HB0 receives from from CFMW1 across RP 0 and
    RP 1 and hence to yet more regions of the memory of the
    attached Type3 devices.  Note this is a representative subset
    of the full range of possible HDM decoder configurations in this
    topology.

(3) **Four CXL Root Ports.** In this case the CXL Type 3 devices are
    directly attached to these ports.

(4) **Four CXL Type3 memory expansion devices.**  These will each have
    HDM decoders, but in this case rather than performing interleave
    they will take the Host Physical Addresses of accesses and map
    them to their own local Device Physical Address Space (DPA).

Example topology involving a switch::

  |<------------------SYSTEM PHYSICAL ADDRESS MAP (1)----------------->|
  |    __________   __________________________________   __________    |
  |   |          | |                                  | |          |   |
  |   | CFMW 0   | |  CXL Fixed Memory Window 1       | | CFMW 2   |   |
  |   | HB0 only | |  Configured to interleave memory | | HB1 only |   |
  |   |          | |  memory accesses across HB0/HB1  | |          |   |
  |   |____x_____| |__________________________________| |__________|   |
           |             |                     |             |
           |             |                     |             |
           |             |                     |
  Interleave Decoder     |                     |             |
   Matches this HB       |                     |             |
           \_____________|                     |_____________/
               __________|__________      _____|_______________
              |                     |    |                     |
              | CXL HB 0            |    | CXL HB 1            |
              | HB IntLv Decoders   |    | HB IntLv Decoders   |
              | PCI/CXL Root Bus 0c |    | PCI/CXL Root Bus 0d |
              |                     |    |                     |
              |___x_________________|    |_____________________|
                  |              |          |               |
                  |
       A HB 0 HDM Decoder
       matches this Port
       ___________|___
      |  Root Port 0  |
      |  Appears in   |
      |  PCI topology |
      |  as 0c:00.0   |
      |___________x___|
                  |
                  |
                  \_____________________
                                        |
                                        |
            ---------------------------------------------------
           |    Switch 0  USP as PCI 0d:00.0                   |
           |    USP has HDM decoder which direct traffic to    |
           |    appropriate downstream port                    |
           |    Switch BUS appears as 0e                       |
           |x__________________________________________________|
            |                  |               |              |
            |                  |               |              |
       _____|_________   ______|______   ______|_____   ______|_______
   (4)|     x         | |             | |            | |              |
      | CXL Type3 0   | | CXL Type3 1 | | CXL type3 2| | CLX Type 3 3 |
      |               | |             | |            | |              |
      | PMEM0(Vol LSA)| | PMEM1 (...) | | PMEM2 (...)| | PMEM3 (...)  |
      | Decoder to go | |             | |            | |              |
      | from host PA  | | PCI 10:00.0 | | PCI 11:00.0| | PCI 12:00.0  |
      | to device PA  | |             | |            | |              |
      | PCI as 0f:00.0| |             | |            | |              |
      |_______________| |_____________| |____________| |______________|

Example command lines
---------------------
A very simple setup with just one directly attached CXL Type 3 Persistent Memory device::

  qemu-system-x86_64 -M q35,cxl=on -m 4G,maxmem=8G,slots=8 -smp 4 \
  ...
  -object memory-backend-file,id=cxl-mem1,share=on,mem-path=/tmp/cxltest.raw,size=256M \
  -object memory-backend-file,id=cxl-lsa1,share=on,mem-path=/tmp/lsa.raw,size=256M \
  -device pxb-cxl,bus_nr=12,bus=pcie.0,id=cxl.1 \
  -device cxl-rp,port=0,bus=cxl.1,id=root_port13,chassis=0,slot=2 \
  -device cxl-type3,bus=root_port13,persistent-memdev=cxl-mem1,lsa=cxl-lsa1,id=cxl-pmem0 \
  -M cxl-fmw.0.targets.0=cxl.1,cxl-fmw.0.size=4G

A very simple setup with just one directly attached CXL Type 3 Volatile Memory device::

  qemu-system-x86_64 -M q35,cxl=on -m 4G,maxmem=8G,slots=8 -smp 4 \
  ...
  -object memory-backend-ram,id=vmem0,share=on,size=256M \
  -device pxb-cxl,bus_nr=12,bus=pcie.0,id=cxl.1 \
  -device cxl-rp,port=0,bus=cxl.1,id=root_port13,chassis=0,slot=2 \
  -device cxl-type3,bus=root_port13,volatile-memdev=vmem0,id=cxl-vmem0 \
  -M cxl-fmw.0.targets.0=cxl.1,cxl-fmw.0.size=4G

The same volatile setup may optionally include an LSA region::

  qemu-system-x86_64 -M q35,cxl=on -m 4G,maxmem=8G,slots=8 -smp 4 \
  ...
  -object memory-backend-ram,id=vmem0,share=on,size=256M \
  -object memory-backend-file,id=cxl-lsa0,share=on,mem-path=/tmp/lsa.raw,size=256M \
  -device pxb-cxl,bus_nr=12,bus=pcie.0,id=cxl.1 \
  -device cxl-rp,port=0,bus=cxl.1,id=root_port13,chassis=0,slot=2 \
  -device cxl-type3,bus=root_port13,volatile-memdev=vmem0,lsa=cxl-lsa0,id=cxl-vmem0 \
  -M cxl-fmw.0.targets.0=cxl.1,cxl-fmw.0.size=4G

A setup suitable for 4 way interleave. Only one fixed window provided, to enable 2 way
interleave across 2 CXL host bridges.  Each host bridge has 2 CXL Root Ports, with
the CXL Type3 device directly attached (no switches).::

  qemu-system-x86_64 -M q35,cxl=on -m 4G,maxmem=8G,slots=8 -smp 4 \
  ...
  -object memory-backend-file,id=cxl-mem1,share=on,mem-path=/tmp/cxltest.raw,size=256M \
  -object memory-backend-file,id=cxl-mem2,share=on,mem-path=/tmp/cxltest2.raw,size=256M \
  -object memory-backend-file,id=cxl-mem3,share=on,mem-path=/tmp/cxltest3.raw,size=256M \
  -object memory-backend-file,id=cxl-mem4,share=on,mem-path=/tmp/cxltest4.raw,size=256M \
  -object memory-backend-file,id=cxl-lsa1,share=on,mem-path=/tmp/lsa.raw,size=256M \
  -object memory-backend-file,id=cxl-lsa2,share=on,mem-path=/tmp/lsa2.raw,size=256M \
  -object memory-backend-file,id=cxl-lsa3,share=on,mem-path=/tmp/lsa3.raw,size=256M \
  -object memory-backend-file,id=cxl-lsa4,share=on,mem-path=/tmp/lsa4.raw,size=256M \
  -device pxb-cxl,bus_nr=12,bus=pcie.0,id=cxl.1 \
  -device pxb-cxl,bus_nr=222,bus=pcie.0,id=cxl.2 \
  -device cxl-rp,port=0,bus=cxl.1,id=root_port13,chassis=0,slot=2 \
  -device cxl-type3,bus=root_port13,persistent-memdev=cxl-mem1,lsa=cxl-lsa1,id=cxl-pmem0 \
  -device cxl-rp,port=1,bus=cxl.1,id=root_port14,chassis=0,slot=3 \
  -device cxl-type3,bus=root_port14,persistent-memdev=cxl-mem2,lsa=cxl-lsa2,id=cxl-pmem1 \
  -device cxl-rp,port=0,bus=cxl.2,id=root_port15,chassis=0,slot=5 \
  -device cxl-type3,bus=root_port15,persistent-memdev=cxl-mem3,lsa=cxl-lsa3,id=cxl-pmem2 \
  -device cxl-rp,port=1,bus=cxl.2,id=root_port16,chassis=0,slot=6 \
  -device cxl-type3,bus=root_port16,persistent-memdev=cxl-mem4,lsa=cxl-lsa4,id=cxl-pmem3 \
  -M cxl-fmw.0.targets.0=cxl.1,cxl-fmw.0.targets.1=cxl.2,cxl-fmw.0.size=4G,cxl-fmw.0.interleave-granularity=8k

An example of 4 devices below a switch suitable for 1, 2 or 4 way interleave::

  qemu-system-x86_64 -M q35,cxl=on -m 4G,maxmem=8G,slots=8 -smp 4 \
  ...
  -object memory-backend-file,id=cxl-mem0,share=on,mem-path=/tmp/cxltest.raw,size=256M \
  -object memory-backend-file,id=cxl-mem1,share=on,mem-path=/tmp/cxltest1.raw,size=256M \
  -object memory-backend-file,id=cxl-mem2,share=on,mem-path=/tmp/cxltest2.raw,size=256M \
  -object memory-backend-file,id=cxl-mem3,share=on,mem-path=/tmp/cxltest3.raw,size=256M \
  -object memory-backend-file,id=cxl-lsa0,share=on,mem-path=/tmp/lsa0.raw,size=256M \
  -object memory-backend-file,id=cxl-lsa1,share=on,mem-path=/tmp/lsa1.raw,size=256M \
  -object memory-backend-file,id=cxl-lsa2,share=on,mem-path=/tmp/lsa2.raw,size=256M \
  -object memory-backend-file,id=cxl-lsa3,share=on,mem-path=/tmp/lsa3.raw,size=256M \
  -device pxb-cxl,bus_nr=12,bus=pcie.0,id=cxl.1 \
  -device cxl-rp,port=0,bus=cxl.1,id=root_port0,chassis=0,slot=0 \
  -device cxl-rp,port=1,bus=cxl.1,id=root_port1,chassis=0,slot=1 \
  -device cxl-upstream,bus=root_port0,id=us0 \
  -device cxl-downstream,port=0,bus=us0,id=swport0,chassis=0,slot=4 \
  -device cxl-type3,bus=swport0,persistent-memdev=cxl-mem0,lsa=cxl-lsa0,id=cxl-pmem0 \
  -device cxl-downstream,port=1,bus=us0,id=swport1,chassis=0,slot=5 \
  -device cxl-type3,bus=swport1,persistent-memdev=cxl-mem1,lsa=cxl-lsa1,id=cxl-pmem1 \
  -device cxl-downstream,port=2,bus=us0,id=swport2,chassis=0,slot=6 \
  -device cxl-type3,bus=swport2,persistent-memdev=cxl-mem2,lsa=cxl-lsa2,id=cxl-pmem2 \
  -device cxl-downstream,port=3,bus=us0,id=swport3,chassis=0,slot=7 \
  -device cxl-type3,bus=swport3,persistent-memdev=cxl-mem3,lsa=cxl-lsa3,id=cxl-pmem3 \
  -M cxl-fmw.0.targets.0=cxl.1,cxl-fmw.0.size=4G,cxl-fmw.0.interleave-granularity=4k

Deprecations
------------

The Type 3 device [memdev] attribute has been deprecated in favor of the
[persistent-memdev] attributes. [memdev] will default to a persistent memory
device for backward compatibility and is incapable of being used in combination
with [persistent-memdev].

Kernel Configuration Options
----------------------------

In Linux 5.18 the following options are necessary to make use of
OS management of CXL memory devices as described here.

* CONFIG_CXL_BUS
* CONFIG_CXL_PCI
* CONFIG_CXL_ACPI
* CONFIG_CXL_PMEM
* CONFIG_CXL_MEM
* CONFIG_CXL_PORT
* CONFIG_CXL_REGION

References
----------

 - Consortium website for specifications etc:
   http://www.computeexpresslink.org
 - Compute Express Link (CXL) Specification, Revision 3.1, August 2023
