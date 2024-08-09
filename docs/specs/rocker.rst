Rocker Network Switch Register Programming Guide
************************************************

..
   Copyright (c) Scott Feldman <sfeldma@gmail.com>
   Copyright (c) Neil Horman <nhorman@tuxdriver.com>
   Version 0.11, 12/29/2014

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

Introduction
============

Overview
--------

This document describes the hardware/software interface for the Rocker switch
device.  The intended audience is authors of OS drivers and device emulation
software.

Notations and Conventions
-------------------------

* In register descriptions, [n:m] indicates a range from bit n to bit m,
  inclusive.
* Use of leading 0x indicates a hexadecimal number.
* Use of leading 0b indicates a binary number.
* The use of RSVD or Reserved indicates that a bit or field is reserved for
  future use.
* Field width is in bytes, unless otherwise noted.
* Register are (R) read-only, (R/W) read/write, (W) write-only, or (COR) clear
  on read
* TLV values in network-byte-order are designated with (N).


PCI Configuration Registers
===========================

PCI Configuration Space
-----------------------

Each switch instance registers as a PCI device with PCI configuration space::

	offset	width	description		value
	---------------------------------------------
	0x0	2	Vendor ID		0x1b36
	0x2	2	Device ID		0x0006
	0x4	4	Command/Status
	0x8	1	Revision ID		0x01
	0x9	3	Class code		0x2800
	0xC	1	Cache line size
	0xD	1	Latency timer
	0xE	1	Header type
	0xF	1	Built-in self test
	0x10	4	Base address low
	0x14	4	Base address high
	0x18-28		Reserved
	0x2C	2	Subsystem vendor ID	*
	0x2E	2	Subsystem ID		*
	0x30-38		Reserved
	0x3C	1	Interrupt line
	0x3D	1	Interrupt pin		0x00
	0x3E	1	Min grant		0x00
	0x3D	1	Max latency		0x00
	0x40	1	TRDY timeout
	0x41	1	Retry count
	0x42	2	Reserved

        * Assigned by sub-system implementation

Memory-Mapped Register Space
============================

There are two memory-mapped BARs.  BAR0 maps device register space and is
0x2000 in size.  BAR1 maps MSI-X vector and PBA tables and is also 0x2000 in
size, allowing for 256 MSI-X vectors.

All registers are 4 or 8 bytes long.  It is assumed host software will access 4
byte registers with one 4-byte access, and 8 byte registers with either two
4-byte accesses or a single 8-byte access.  In the case of two 4-byte accesses,
access must be lower and then upper 4-bytes, in that order.

BAR0 device register space is organized as follows::

	offset		description
	------------------------------------------------------
	0x0000-0x000f	Bogus registers to catch misbehaving
			drivers.  Writes do nothing.  Reads
			back as 0xDEADBABE.
	0x0010-0x00ff	Test registers
	0x0300-0x03ff	General purpose registers
	0x1000-0x1fff	Descriptor control

Holes in register space are reserved.  Writes to reserved registers do nothing.
Reads to reserved registers read back as 0.

No fancy stuff like write-combining is enabled on any of the registers.

BAR1 MSI-X register space is organized as follows::

	offset		description
	------------------------------------------------------
	0x0000-0x0fff	MSI-X vector table (256 vectors total)
	0x1000-0x1fff	MSI-X PBA table


Interrupts, DMA, and Endianness
===============================

PCI Interrupts
--------------

The device supports only MSI-X interrupts.  BAR1 memory-mapped region contains
the MSI-X vector and PBA tables, with support for up to 256 MSI-X vectors.

The vector assignment is::

	vector		description
	-----------------------------------------------------
	0		Command descriptor ring completion
	1		Event descriptor ring completion
	2		Test operation completion
	3		RSVD
	4-255		Tx and Rx descriptor ring completion
			  Tx vector is even
			  Rx vector is odd

A MSI-X vector table entry is 16 bytes::

	field		offset	width	description
	-------------------------------------------------------------
	lower_addr	0x0	4	[31:2] message address[31:2]
					[1:0] Rsvd (4 byte alignment
						    required)
	upper_addr	0x4	4	[31:19] Rsvd
					[14:0] message address[46:32]
	data		0x8	4	message data[31:0]
	control		0xc	4	[31:1] Rsvd
					[0] mask (0 = enable,
						  1 = masked)

Software should install the Interrupt Service Routine (ISR) before any ports
are enabled or any commands are issued on the command ring.

DMA Operations
--------------

DMA operations are used for packet DMA to/from the CPU, command and event
processing.  Command processing includes statistical counters and table dumps,
table insertion/deletion, and more.  Event processing provides an async
notification method for device-originating events.  Each DMA operation has a
set of control registers to manage a descriptor ring.  The descriptor rings are
allocated from contiguous host DMA-able memory and registers specify the rings
base address, size and current head and tail indices.  Software always writes
the head, and hardware always writes the tail.

The higher-order bit of DMA_DESC_COMP_ERR is used to mark hardware completion
of a descriptor.  Software will clear this bit when posting a descriptor to the
ring, and hardware will set this bit when the descriptor is complete.

Descriptor ring sizes must be a power of 2 and range from 2 to 64K entries.
Descriptor rings' base address must be 8-byte aligned.  Descriptors must be
packed within ring.  Each descriptor in each ring must also be aligned on an 8
byte boundary.  Each descriptor ring will have these registers::

	DMA_DESC_xxx_BASE_ADDR, offset 0x1000 + (x * 32), 64-bit, (R/W)
	DMA_DESC_xxx_SIZE, offset 0x1008 + (x * 32), 32-bit, (R/W)
	DMA_DESC_xxx_HEAD, offset 0x100c + (x * 32), 32-bit, (R/W)
	DMA_DESC_xxx_TAIL, offset 0x1010 + (x * 32), 32-bit, (R)
	DMA_DESC_xxx_CTRL, offset 0x1014 + (x * 32), 32-bit, (W)
	DMA_DESC_xxx_CREDITS, offset 0x1018 + (x * 32), 32-bit, (R/W)
	DMA_DESC_xxx_RSVD1, offset 0x101c + (x * 32), 32-bit, (R/W)

Where x is descriptor ring index::

	index		ring
	--------------------
	0		CMD
	1		EVENT
	2		TX (port 0)
	3		RX (port 0)
	4		TX (port 1)
	5		RX (port 1)
	.
	.
	.
	124		TX (port 61)
	125		RX (port 61)
	126		Resv
	127		Resv

Writing BASE_ADDR or SIZE will reset HEAD and TAIL to zero.  HEAD cannot be
written past TAIL.  To do so would wrap the ring.  An empty ring is when HEAD
== TAIL.  A full ring is when HEAD is one position behind TAIL.  Both HEAD and
TAIL increment and modulo wrap at the ring size.

CTRL register bits::

	bit	name		description
	------------------------------------------------------------------------
	[0]	CTRL_RESET	Reset the descriptor ring
	[1:31]	Reserved

All descriptor types share some common fields::

	field			width	description
	-------------------------------------------------------------------
	DMA_DESC_BUF_ADDR	8	Phys addr of desc payload, 8-byte
					aligned
	DMA_DESC_COOKIE		8	Desc cookie for completion matching,
					upper-most bit is reserved
	DMA_DESC_BUF_SIZE	2	Desc payload size in bytes
	DMA_DESC_TLV_SIZE	2	Desc payload total size in bytes
					used for TLVs.  Must be <=
					DMA_DESC_BUF_SIZE.
	DMA_DESC_COMP_ERR	2	Completion status of associated
					desc payload.  High order bit is
					clear on new descs, toggled by
					hw for completed items.

To support forward- and backward-compatibility, descriptor and completion
payloads are specified in TLV format.  Fields are packed with Type=field name,
Length=field length, and Value=field value.  Software will ignore unknown fields
filled in by the switch.  Likewise, the switch will ignore unknown fields
filled in by software.

Descriptor payload buffer is 8-byte aligned and TLVs are 8-byte aligned.  The
value within a TLV is also 8-byte aligned.  The (packed, 8 byte) TLV header is::

	field	width	description
	-----------------------------
	type	4	TLV type
	len	2	TLV value length
	pad	2	Reserved

The alignment requirements for descriptors and TLVs are to avoid unaligned
access exceptions in software.  Note that the payload for each TLV is also
8 byte aligned.

Figure 1 shows an example descriptor buffer with two TLVs::

                  <------- 8 bytes ------->

  8-byte  +––––+  +–––––––––––+–––––+–––––+                     +–+
  align           |   type    | len | pad |    TLV#1 hdr          |
                  +–––––––––––+–––––+–––––+    (len=22)           |
                  |                       |                       |
                  |  value                |    TVL#1 value        |
                  |                       |    (padded to 8-byte  |
                  |                 +–––––+     alignment)        |
                  |                 |/////|                       |
   8-byte +––––+  +–––––––––––+–––––––––––+                       |
   align          |   type    | len | pad |    TLV#2 hdr    DESC_BUF_SIZE
                  +–––––+–––––+–––––+–––––+    (len=2)            |
                  |value|/////////////////|    TLV#2 value        |
                  +–––––+/////////////////|                       |
                  |///////////////////////|                       |
                  |///////////////////////|                       |
                  |///////////////////////|                       |
                  |////////unused/////////|                       |
                  |////////space//////////|                       |
                  |///////////////////////|                       |
                  |///////////////////////|                       |
                  |///////////////////////|                       |
                  +–––––––––––––––––––––––+                     +–+

				fig. 1

TLVs can be nested within the NEST TLV type.

Interrupt credits
^^^^^^^^^^^^^^^^^

MSI-X vectors used for descriptor ring completions use a credit mechanism for
efficient device, PCIe bus, OS and driver operations.  Each descriptor ring has
a credit count which represents the number of outstanding descriptors to be
processed by the driver.  As the device marks descriptors complete, the credit
count is incremented.  As the driver processes those outstanding descriptors,
it returns credits back to the device.  This way, the device knows the driver's
progress and can make decisions about when to fire the next interrupt or not.
When the credit count is zero, and the first descriptors are posted for the
driver, a single interrupt is fired.  Once the interrupt is fired, the
interrupt is disabled (auto-masked*).  In response to the interrupt, the driver
will process descriptors and PIO write a returned credit value for that
descriptor ring.  If the driver returns all credits (the driver caught up with
the device and there is no outstanding work), then the interrupt is unmasked,
but not fired.  If only partial credits are returned, the interrupt remains
masked but the device generates an interrupt, signaling the driver that more
outstanding work is available.

(* this masking is unrelated to the MSI-X interrupt mask register)

Endianness
----------

Device registers are hard-coded to little-endian (LE).  The driver should
convert to/from host endianness to LE for device register accesses.

Descriptors are LE.  Descriptor buffer TLVs will have LE type and length
fields, but the value field can either be LE or network-byte-order, depending
on context.  TLV values containing network packet data will be in network-byte
order.  A TLV value containing a field or mask used to compare against network
packet data is network-byte order.  For example, flow match fields (and masks)
are network-byte-order since they're matched directly, byte-by-byte, against
network packet data.  All non-network-packet TLV multi-byte values will be LE.

TLV values in network-byte-order are designated with (N).


Test Registers
==============

Rocker has several test registers to support troubleshooting register access,
interrupt generation, and DMA operations::

	TEST_REG, offset 0x0010, 32-bit (R/W)
	TEST_REG64, offset 0x0018, 64-bit (R/W)
	TEST_IRQ, offset 0x0020, 32-bit (R/W)
	TEST_DMA_ADDR, offset 0x0028, 64-bit (R/W)
	TEST_DMA_SIZE, offset 0x0030, 32-bit (R/W)
	TEST_DMA_CTRL, offset 0x0034, 32-bit (R/W)

Reads to TEST_REG and TEST_REG64 will read a value equal to twice the last
value written to the register.  The 32-bit and 64-bit versions are for testing
32-bit and 64-bit host accesses.

A vector can be written to TEST_IRQ and the device will generate an interrupt
for that vector.

To test basic DMA operations, allocate a DMA-able host buffer and put the
buffer address into TEST_DMA_ADDR and size into TEST_DMA_SIZE.  Then, write to
TEST_DMA_CTRL to manipulate the buffer contents.  TEST_DMA_CTRL operations are::

	operation		value	description
	-----------------------------------------------------------
	TEST_DMA_CTRL_CLEAR	1	clear buffer
	TEST_DMA_CTRL_FILL	2	fill buffer bytes with 0x96
	TEST_DMA_CTRL_INVERT	4	invert bytes in buffer

Various buffer address and sizes should be tested to verify no address boundary
issue exists.  In particular, buffers that start on odd-8-byte boundary and/or
span multiple PAGE sizes should be tested.


Ports
=====

Physical and Logical Ports
------------------------------------

The switch supports up to 62 physical (front-panel) ports.  Register
PORT_PHYS_COUNT returns the actual number of physical ports available::

	PORT_PHYS_COUNT, offset 0x0304, 32-bit, (R)

In addition to front-panel ports, the switch supports logical ports for
tunnels.

Front-panel ports and logical tunnel ports are mapped into a single 32-bit port
space.  A special CPU port is assigned port 0.  The front-panel ports are
mapped to ports 1-62.  A special loopback port is assigned port 63.  Logical
tunnel ports are assigned ports 0x0001000-0x0001ffff.
To summarize the port assignments::

	port			mapping
	-------------------------------------------------------
	0			CPU port (for packets to/from host CPU)
	1-62			front-panel physical ports
	63			loopback port
	64-0x0000ffff		RSVD
	0x00010000-0x0001ffff	logical tunnel ports
	0x00020000-0xffffffff	RSVD

Physical Port Mode
------------------

Switch front-panel ports operate in a mode.  Currently, the only mode is
OF-DPA.  OF-DPA[1] mode is based on OpenFlow Data Plane Abstraction (OF-DPA)
Abstract Switch Specification, Version 1.0, from Broadcom Corporation.  To
set/get the mode for front-panel ports, see port settings, below.

Port Settings
-------------

Link status for all front-panel ports is available via PORT_PHYS_LINK_STATUS::

	PORT_PHYS_LINK_STATUS, offset 0x0310, 64-bit, (R)

	Value is port bitmap.  Bits 0 and 63 always read 0.  Bits 1-62
	read 1 for link UP and 0 for link DOWN for respective front-panel ports.

Other properties for front-panel ports are available via DMA CMD descriptors::

	Get PORT_SETTINGS descriptor:

		field		width	description
		----------------------------------------------
		PORT_SETTINGS	2	CMD_GET
		PPORT		4	Physical port #

	Get PORT_SETTINGS completion:

		field		width	description
		----------------------------------------------
		PPORT		4	Physical port #
		SPEED		4	Current port interface speed, in Mbps
		DUPLEX		1	1 = Full, 0 = Half
		AUTONEG		1	1 = enabled, 0 = disabled
		MACADDR		6	Port MAC address
		MODE		1	0 = OF-DPA
		LEARNING	1	MAC address learning on port
						1 = enabled
						0 = disabled
		PHYS_NAME	<var>	Physical port name (string)

	Set PORT_SETTINGS descriptor:

		field		width	description
		----------------------------------------------
		PORT_SETTINGS	2	CMD_SET
		PPORT		4	Physical port #
		SPEED		4	Port interface speed, in Mbps
		DUPLEX		1	1 = Full, 0 = Half
		AUTONEG		1	1 = enabled, 0 = disabled
		MACADDR		6	Port MAC address
		MODE		1	0 = OF-DPA

Port Enable
-----------

Front-panel ports are initially disabled, which means port ingress and egress
packets will be dropped.  To enable or disable a port, use PORT_PHYS_ENABLE::

	PORT_PHYS_ENABLE: offset 0x0318, 64-bit, (R/W)

	Value is bitmap of first 64 ports.  Bits 0 and 63 are ignored
	and always read as 0.  Write 1 to enable port; write 0 to disable it.
	Default is 0.


Switch Control
==============

This section covers switch-wide register settings.

Control
-------

This register is used for low level control of the switch::

	CONTROL: offset 0x0300, 32-bit, (W)

	bit	name		description
	------------------------------------------------------------------------
	[0]	CONTROL_RESET	If set, device will perform reset
	[1:31]	Reserved

Switch ID
---------

The switch has a SWITCH_ID to be used by software to uniquely identify the
switch::

	SWITCH_ID: offset 0x0320, 64-bit, (R)

	Value is opaque to switch software and no special encoding is implied.


Events
======

Non-I/O asynchronous events from the device are notified to the host using the
event ring.  The TLV structure for events is::

	field		width	description
	---------------------------------------------------
	TYPE		4	Event type, one of:
					1: LINK_CHANGED
					2: MAC_VLAN_SEEN
	INFO		<nest>	Event info (details below)

Link Changed Event
------------------

When link status changes on a physical port, this event is generated::

	field		width	description
	---------------------------------------------------
	INFO		<nest>
	  PPORT		4	Physical port
	  LINKUP	1	Link status:
					0: down
					1: up

MAC VLAN Seen Event
-------------------

When a packet ingresses on a port and the source MAC/VLAN isn't known to the
device, the device will generate this event.  In response to the event, the
driver should install to the device the MAC/VLAN on the port into the bridge
table.  Once installed, the MAC/VLAN is known on the port and this event will
no longer be generated.

::

	field		width	description
	---------------------------------------------------
	INFO		<nest>
	  PPORT		4	Physical port
	  MAC		6	MAC address
	  VLAN		2	VLAN ID


CPU Packet Processing
=====================

Ingress packets directed to the host CPU for further processing are delivered
in the DMA RX ring.  Likewise, host CPU originating packets destined to egress
on switch ports are scheduled by software using the DMA TX ring.

Tx Packet Processing
--------------------

Software schedules packets for egress on switch ports using the DMA TX ring.  A
TX descriptor buffer describes the packet location and size in host DMA-able
memory, the destination port, and any hardware-offload functions (such as L3
payload checksum offload).  Software then bumps the descriptor head to signal
hardware of new Tx work.  In response, hardware will DMA read Tx descriptors up
to head, DMA read descriptor buffer and packet data, perform offloading
functions, and finally frame packet on wire (network).  Once packet processing
is complete, hardware will writeback status to descriptor(s) to signal to
software that Tx is complete and software resources (e.g. skb) backing packet
can be released.

Figure 2 shows an example 3-fragment packet queued with one Tx descriptor.  A
TLV is used for each packet fragment::

	                                           pkt frag 1
	                                           +–––––––+  +–+
	                                       +–––+       |    |
	                         desc buf      |   |       |    |
	                        +––––––––+     |   |       |    |
	        Tx ring     +–––+        +–––––+   |       |    |
	      +–––––––––+   |   |  TLVs  |         +–––––––+    |
	      |         +–––+   +––––––––+         pkt frag 2   |
	      | desc 0  |       |        +–––––+   +–––––––+    |
	      +–––––––––+       |  TLVs  |     +–––+       |    |
	head+–+         |       +––––––––+         |       |    |
	      | desc 1  |       |        +–––––+   +–––––––+    |pkt
	      +–––––––––+       |  TLVs  |     |                |
	      |         |       +––––––––+     |   pkt frag 3   |
	      |         |                      |   +–––––––+    |
	      +–––––––––+                      +–––+       |    |
	      |         |                          |       |    |
	      |         |                          |       |    |
	      +–––––––––+                          |       |    |
	      |         |                          |       |    |
	      |         |                          |       |    |
	      +–––––––––+                          |       |    |
	      |         |                          +–––––––+  +–+
	      |         |
	      +–––––––––+

				fig 2.

The TLVs for Tx descriptor buffer are::

	field			width	description
	---------------------------------------------------------------------
	PPORT			4	Destination physical port #
	TX_OFFLOAD		1	Hardware offload modes:
					  0: no offload
					  1: insert IP csum (ipv4 only)
					  2: insert TCP/UDP csum
					  3: L3 csum calc and insert
                        	             into csum offset (TX_L3_CSUM_OFF)
                 	                    16-bit 1's complement csum value.
                                	     IPv4 pseudo-header and IP
                        	             already calculated by OS
                  	                   and inserted.
					  4: TSO (TCP Segmentation Offload)
	TX_L3_CSUM_OFF		2	For L3 csum offload mode, the offset,
					from the beginning of the packet,
					of the csum field in the L3 header
	TX_TSO_MSS		2	For TSO offload mode, the
					Maximum Segment Size in bytes
        TX_TSO_HDR_LEN		2	For TSO offload mode, the
					length of ethernet, IP, and
					TCP/UDP headers, including IP
					and TCP options.
	TX_FRAGS		<array>	Packet fragments
	  TX_FRAG		<nest>	Packet fragment
	    TX_FRAG_ADDR	8	DMA address of packet fragment
	    TX_FRAG_LEN		2	Packet fragment length

Possible status return codes in descriptor on completion are::

	DESC_COMP_ERR	reason
	--------------------------------------------------------------------
	0		OK
	-ROCKER_ENXIO	address or data read err on desc buf or packet
			fragment
	-ROCKER_EINVAL	bad pport or TSO or csum offloading error
	-ROCKER_ENOMEM	no memory for internal staging tx fragment

Rx Packet Processing
--------------------

For packets ingressing on switch ports that are not forwarded by the switch but
rather directed to the host CPU for further processing are delivered in the DMA
RX ring.  Rx descriptor buffers are allocated by software and placed on the
ring.  Hardware will fill Rx descriptor buffers with packet data, write the
completion, and signal to software that a new packet is ready.  Since Rx packet
size is not known a-priori, the Rx descriptor buffer must be allocated for
worst-case packet size.  A single Rx descriptor will contain the entire Rx
packet data in one RX_FRAG.  Other Rx TLVs describe and hardware offloads
performed on the packet, such as checksum validation.

The TLVs for Rx descriptor buffer are::

	field		width	description
	---------------------------------------------------
	PPORT		4	Source physical port #
	RX_FLAGS	2	Packet parsing flags:
				  (1 << 0): IPv4 packet
				  (1 << 1): IPv6 packet
				  (1 << 2): csum calculated
				  (1 << 3): IPv4 csum good
				  (1 << 4): IP fragment
				  (1 << 5): TCP packet
				  (1 << 6): UDP packet
				  (1 << 7): TCP/UDP csum good
				  (1 << 8): Offload forward
	RX_CSUM		2	IP calculated checksum:
				  IPv4: IP payload csum
				  IPv6: header and payload csum
				(Only valid is RX_FLAGS:csum calc is set)
	RX_FRAG_ADDR	8	DMA address of packet fragment
	RX_FRAG_MAX_LEN	2	Packet maximum fragment length
	RX_FRAG_LEN	2	Actual packet fragment length after receive

Offload forward RX_FLAG indicates the device has already forwarded the packet
so the host CPU should not also forward the packet.

Possible status return codes in descriptor on completion are::

	DESC_COMP_ERR	reason
	--------------------------------------------------------------------
	0		OK
	-ROCKER_ENXIO	address or data read err on desc buf
	-ROCKER_ENOMEM	no memory for internal staging desc buf
	-ROCKER_EMSGSIZE Rx descriptor buffer wasn't big enough to contain
			packet data TLV and other TLVs.


OF-DPA Mode
===========

OF-DPA mode allows the switch to offload flow packet processing functions to
hardware.  An OpenFlow controller would communicate with an OpenFlow agent
installed on the switch.  The OpenFlow agent would (directly or indirectly)
communicate with the Rocker switch driver, which in turn would program switch
hardware with flow functionality, as defined in OF-DPA.  The block diagram is::

		+–––––––––––––––----–––+
		|        OF            |
		|  Remote Controller   |
		+––––––––+––----–––––––+
		         |
		         |
		+––––––––+–––––––––+
		|       OF         |
		|   Local Agent    |
		+––––––––––––––––––+
		|                  |
		|   Rocker Driver  |
		+––––––––––––––––––+
		    <this spec>
		+––––––––––––––––––+
		|                  |
		|   Rocker Switch  |
		+––––––––––––––––––+

To participate in flow functions, ports must be configure for OF-DPA mode
during switch initialization.

OF-DPA Flow Table Interface
---------------------------

There are commands to add, modify, delete, and get stats of flow table entries.
The commands are issued using the DMA CMD descriptor ring.  The following
commands are defined::

	CMD_ADD:		add an entry to flow table
	CMD_MOD:		modify an entry in flow table
	CMD_DEL:		delete an entry from flow table
	CMD_GET_STATS:		get stats for flow entry

TLVs for add and modify commands are::

	field			width	description
	----------------------------------------------------
	OF_DPA_CMD		2	CMD_[ADD|MOD]
	OF_DPA_TBL		2	Flow table ID
					  0: ingress port
					  10: vlan
					  20: termination mac
					  30: unicast routing
					  40: multicast routing
					  50: bridging
					  60: ACL policy
	OF_DPA_PRIORITY		4	Flow priority
	OF_DPA_HARDTIME		4	Hard timeout for flow
	OF_DPA_IDLETIME		4	Idle timeout for flow
	OF_DPA_COOKIE		8	Cookie

Additional TLVs based on flow table ID:

Table ID 0: ingress port::

	field			width	description
	----------------------------------------------------
	OF_DPA_IN_PPORT		4	ingress physical port number
	OF_DPA_GOTO_TBL		2	goto table ID; zero to drop

Table ID 10: vlan::

	field			width	description
	----------------------------------------------------
	OF_DPA_IN_PPORT		4	ingress physical port number
	OF_DPA_VLAN_ID		2 (N)	vlan ID
	OF_DPA_VLAN_ID_MASK	2 (N)	vlan ID mask
	OF_DPA_GOTO_TBL		2	goto table ID; zero to drop
	OF_DPA_NEW_VLAN_ID	2 (N)	new vlan ID

Table ID 20: termination mac::

	field			width	description
	----------------------------------------------------
	OF_DPA_IN_PPORT		4	ingress physical port number
	OF_DPA_IN_PPORT_MASK	4	ingress physical port number mask
	OF_DPA_ETHERTYPE	2 (N)	must be either 0x0800 or 0x86dd
	OF_DPA_DST_MAC		6 (N)	destination MAC
	OF_DPA_DST_MAC_MASK	6 (N)	destination MAC mask
	OF_DPA_VLAN_ID		2 (N)	vlan ID
	OF_DPA_VLAN_ID_MASK	2 (N)	vlan ID mask
	OF_DPA_GOTO_TBL		2	only acceptable values are
					unicast or multicast routing
					table IDs
	OF_DPA_OUT_PPORT	2	if specified, must be
					controller, set zero otherwise

Table ID 30: unicast routing::

	field			width	description
	----------------------------------------------------
	OF_DPA_ETHERTYPE	2 (N)	must be either 0x0800 or 0x86dd
	OF_DPA_DST_IP		4 (N)	destination IPv4 address.
					Must be unicast address
	OF_DPA_DST_IP_MASK	4 (N)	IP mask.  Must be prefix mask
	OF_DPA_DST_IPV6		16 (N)	destination IPv6 address.
					Must be unicast address
	OF_DPA_DST_IPV6_MASK	16 (N)	IPv6 mask. Must be prefix mask
	OF_DPA_GOTO_TBL		2	goto table ID; zero to drop
	OF_DPA_GROUP_ID		4	data for GROUP action must
					be an L3 Unicast group entry

Table ID 40: multicast routing::

	field			width	description
	----------------------------------------------------
	OF_DPA_ETHERTYPE	2 (N)	must be either 0x0800 or 0x86dd
	OF_DPA_VLAN_ID		2 (N)	vlan ID
	OF_DPA_SRC_IP		4 (N)	source IPv4. Optional,
					can contain IPv4 address,
					must be completely masked
					if not used
	OF_DPA_SRC_IP_MASK	4 (N)	IP Mask
	OF_DPA_DST_IP		4 (N)	destination IPv4 address.
					Must be multicast address
	OF_DPA_SRC_IPV6		16 (N)	source IPv6 Address. Optional.
					Can contain IPv6 address,
					must be completely masked
					if not used
	OF_DPA_SRC_IPV6_MASK	16 (N)	IPv6 mask.
	OF_DPA_DST_IPV6		16 (N)	destination IPv6 Address. Must
					be multicast address
					Must be multicast address
	OF_DPA_GOTO_TBL		2	goto table ID; zero to drop
	OF_DPA_GROUP_ID		4	data for GROUP action must
					be an L3 multicast group entry

Table ID 50: bridging::

	field			width	description
	----------------------------------------------------
	OF_DPA_VLAN_ID		2 (N)	vlan ID
	OF_DPA_TUNNEL_ID	4	tunnel ID
	OF_DPA_DST_MAC		6 (N)	destination MAC
	OF_DPA_DST_MAC_MASK	6 (N)	destination MAC mask
	OF_DPA_GOTO_TBL		2	goto table ID; zero to drop
	OF_DPA_GROUP_ID		4	data for GROUP action must
					be a L2 Interface, L2
					Multicast, L2 Flood,
					or L2 Overlay group entry
					as appropriate
	OF_DPA_TUNNEL_LPORT	4	unicast Tenant Bridging
					flows specify a tunnel
					logical port ID
	OF_DPA_OUT_PPORT	2	data for OUTPUT action,
					restricted to CONTROLLER,
					set to 0 otherwise

Table ID 60: acl policy::

	field			width	description
	----------------------------------------------------
	OF_DPA_IN_PPORT		4	ingress physical port number
	OF_DPA_IN_PPORT_MASK	4	ingress physical port number mask
	OF_DPA_ETHERTYPE	2 (N)	ethertype
	OF_DPA_VLAN_ID		2 (N)	vlan ID
	OF_DPA_VLAN_ID_MASK	2 (N)	vlan ID mask
	OF_DPA_VLAN_PCP		2 (N)	vlan Priority Code Point
	OF_DPA_VLAN_PCP_MASK	2 (N)	vlan Priority Code Point mask
	OF_DPA_SRC_MAC		6 (N)	source MAC
	OF_DPA_SRC_MAC_MASK	6 (N)	source MAC mask
	OF_DPA_DST_MAC		6 (N)	destination MAC
	OF_DPA_DST_MAC_MASK	6 (N)	destination MAC mask
	OF_DPA_TUNNEL_ID	4	tunnel ID
	OF_DPA_SRC_IP		4 (N)	source IPv4. Optional,
					can contain IPv4 address,
					must be completely masked
					if not used
	OF_DPA_SRC_IP_MASK	4 (N)	IP Mask
	OF_DPA_DST_IP		4 (N)	destination IPv4 address.
					Must be multicast address
	OF_DPA_DST_IP_MASK	4 (N)	IP Mask
	OF_DPA_SRC_IPV6		16 (N)	source IPv6 Address. Optional.
					Can contain IPv6 address,
					must be completely masked
					if not used
	OF_DPA_SRC_IPV6_MASK	16 (N)	IPv6 mask
	OF_DPA_DST_IPV6		16 (N)	destination IPv6 Address. Must
					be multicast address.
	OF_DPA_DST_IPV6_MASK	16 (N)	IPv6 mask
	OF_DPA_SRC_ARP_IP	4 (N)	source IPv4 address in the ARP
					payload.  Only used if ethertype
					== 0x0806.
	OF_DPA_SRC_ARP_IP_MASK	4 (N)	IP Mask
	OF_DPA_IP_PROTO		1	IP protocol
	OF_DPA_IP_PROTO_MASK	1	IP protocol mask
	OF_DPA_IP_DSCP		1	DSCP
	OF_DPA_IP_DSCP_MASK	1	DSCP mask
	OF_DPA_IP_ECN		1	ECN
	OF_DPA_IP_ECN_MASK		1	ECN mask
	OF_DPA_L4_SRC_PORT	2 (N)	L4 source port, only for
					TCP, UDP, or SCTP
	OF_DPA_L4_SRC_PORT_MASK	2 (N)	L4 source port mask
	OF_DPA_L4_DST_PORT	2 (N)	L4 source port, only for
					TCP, UDP, or SCTP
	OF_DPA_L4_DST_PORT_MASK	2 (N)	L4 source port mask
	OF_DPA_ICMP_TYPE	1	ICMP type, only if IP
					protocol is 1
	OF_DPA_ICMP_TYPE_MASK	1	ICMP type mask
	OF_DPA_ICMP_CODE	1	ICMP code
	OF_DPA_ICMP_CODE_MASK	1	ICMP code mask
	OF_DPA_IPV6_LABEL	4 (N)	IPv6 flow label
	OF_DPA_IPV6_LABEL_MASK	4 (N)	IPv6 flow label mask
	OF_DPA_GROUP_ID		4	data for GROUP action
	OF_DPA_QUEUE_ID_ACTION	1	write the queue ID
	OF_DPA_NEW_QUEUE_ID	1	queue ID
	OF_DPA_VLAN_PCP_ACTION	1	write the VLAN priority
	OF_DPA_NEW_VLAN_PCP	1	VLAN priority
	OF_DPA_IP_DSCP_ACTION	1	write the DSCP
	OF_DPA_NEW_IP_DSCP	1	new DSCP
	OF_DPA_TUNNEL_LPORT	4	restrct to valid tunnel
					logical port, set to 0
					otherwise.
	OF_DPA_OUT_PPORT	2	data for OUTPUT action,
					restricted to CONTROLLER,
					set to 0 otherwise
	OF_DPA_CLEAR_ACTIONS	4	if 1 packets matching flow are
					dropped (all other instructions
					ignored)

TLVs for flow delete and get stats command are::

	field			width	description
	---------------------------------------------------
	OF_DPA_CMD		2	CMD_[DEL|GET_STATS]
	OF_DPA_COOKIE		8	Cookie

On completion of get stats command, the descriptor buffer is written back with
the following TLVs::

	field			width	description
	---------------------------------------------------
	OF_DPA_STAT_DURATION	4	Flow duration
	OF_DPA_STAT_RX_PKTS	8	Received packets
	OF_DPA_STAT_TX_PKTS	8	Transmit packets

Possible status return codes in descriptor on completion are::

	DESC_COMP_ERR	command			reason
	--------------------------------------------------------------------
	0		all			OK
	-ROCKER_EFAULT	all			head or tail index outside
						of ring
	-ROCKER_ENXIO	all			address or data read err on
						desc buf
	-ROCKER_EMSGSIZE GET_STATS		cmd descriptor buffer wasn't
						big enough to contain write-back
						TLVs
	-ROCKER_EINVAL	all			invalid parameters passed in
	-ROCKER_EEXIST	ADD			entry already exists
	-ROCKER_ENOSPC	ADD			no space left in flow table
	-ROCKER_ENOENT	MOD|DEL|GET_STATS	cookie invalid

Group Table Interface
---------------------

There are commands to add, modify, delete, and get stats of group table
entries.  The commands are issued using the DMA CMD descriptor ring.  The
following commands are defined::

	CMD_ADD:		add an entry to group table
	CMD_MOD:		modify an entry in group table
	CMD_DEL:		delete an entry from group table
	CMD_GET_STATS:		get stats for group entry

TLVs for add and modify commands are::

	field			width	description
	-----------------------------------------------------------
	FLOW_GROUP_CMD		2	CMD_[ADD|MOD]
	FLOW_GROUP_ID		2	Flow group ID
	FLOW_GROUP_TYPE		1	Group type:
					  0: L2 interface
					  1: L2 rewrite
					  2: L3 unicast
					  3: L2 multicast
					  4: L2 flood
					  5: L3 interface
					  6: L3 multicast
					  7: L3 ECMP
					  8: L2 overlay
	FLOW_VLAN_ID		2	Vlan ID (types 0, 3, 4, 6)
	FLOW_L2_PORT		2	Port (types 0)
	FLOW_INDEX		4	Index (all types but 0)
	FLOW_OVERLAY_TYPE	1	Overlay sub-type (type 8):
					  0: Flood unicast tunnel
					  1: Flood multicast tunnel
					  2: Multicast unicast tunnel
					  3: Multicast multicast tunnel
	FLOW_GROUP_ACTION		nest
	  FLOW_GROUP_ID		2	next group ID in chain (all
					types except 0)
	  FLOW_OUT_PORT		4	egress port (types 0, 8)
	  FLOW_POP_VLAN_TAG	1	strip outer VLAN tag (type 1
					only)
	  FLOW_VLAN_ID		2	(types 1, 5)
	  FLOW_SRC_MAC		6	(types 1, 2, 5)
	  FLOW_DST_MAC		6	(types 1, 2)

TLVs for flow delete and get stats command are::

	field			width	description
	-----------------------------------------------------------
	FLOW_GROUP_CMD		2	CMD_[DEL|GET_STATS]
	FLOW_GROUP_ID		2	Flow group ID

On completion of get stats command, the descriptor buffer is written back with
the following TLVs::

	field			width	description
	---------------------------------------------------
	FLOW_GROUP_ID		2	Flow group ID
	FLOW_STAT_DURATION	4	Flow duration
	FLOW_STAT_REF_COUNT	4	Flow reference count
	FLOW_STAT_BUCKET_COUNT	4	Flow bucket count

Possible status return codes in descriptor on completion are::

	DESC_COMP_ERR	command			reason
	--------------------------------------------------------------------
	0		all			OK
	-ROCKER_EFAULT	all			head or tail index outside
						of ring
	-ROCKER_ENXIO	all			address or data read err on
						desc buf
	-ROCKER_ENOSPC	GET_STATS		cmd descriptor buffer wasn't
						big enough to contain write-back
						TLVs
	-ROCKER_EINVAL	ADD|MOD			invalid parameters passed in
	-ROCKER_EEXIST	ADD			entry already exists
	-ROCKER_ENOSPC	ADD			no space left in flow table
	-ROCKER_ENOENT	MOD|DEL|GET_STATS	group ID invalid
	-ROCKER_EBUSY	DEL			group reference count non-zero
	-ROCKER_ENODEV	ADD			next group ID doesn't exist



References
==========

[1] OpenFlow Data Plane Abstraction (OF-DPA) Abstract Switch Specification,
Version 1.0, from Broadcom Corporation, February 21, 2014.
