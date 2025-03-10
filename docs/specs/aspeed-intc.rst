===========================
ASPEED Interrupt Controller
===========================

AST2700
-------
There are a total of 480 interrupt sources in AST2700. Due to the limitation of
interrupt numbers of processors, the interrupts are merged every 32 sources for
interrupt numbers greater than 127.

There are two levels of interrupt controllers, INTC (CPU Die) and INTCIO
(I/O Die).

Interrupt Mapping
-----------------
- INTC: Handles interrupt sources 0 - 127 and integrates signals from INTCIO.
- INTCIO: Handles interrupt sources 128 - 319 independently.

QEMU Support
------------
Currently, only GIC 192 to 201 are supported, and their source interrupts are
from INTCIO and connected to INTC at input pin 0 and output pins 0 to 9 for
GIC 192-201.

Design for GICINT 196
---------------------
The orgate has interrupt sources ranging from 0 to 31, with its output pin
connected to INTCIO "T0 GICINT_196". The output pin is then connected to INTC
"GIC_192_201" at bit 4, and its bit 4 output pin is connected to GIC 196.

INTC GIC_192_201 Output Pin Mapping
-----------------------------------
The design of INTC GIC_192_201 have 10 output pins, mapped as following:

====  ====
Bit   GIC
====  ====
0     192
1     193
2     194
3     195
4     196
5     197
6     198
7     199
8     200
9     201
====  ====

AST2700 A0
----------
It has only one INTC controller, and currently, only GIC 128-136 is supported.
To support both AST2700 A1 and AST2700 A0, there are 10 OR gates in the INTC,
with gates 1 to 9 supporting GIC 128-136.

Design for GICINT 132
---------------------
The orgate has interrupt sources ranging from 0 to 31, with its output pin
connected to INTC. The output pin is then connected to GIC 132.

Block Diagram of GICINT 196 for AST2700 A1 and GICINT 132 for AST2700 A0
------------------------------------------------------------------------

.. code-block::

   |-------------------------------------------------------------------------------------------------------|
   |                                                   AST2700 A1 Design                                   |
   |           To GICINT196                                                                                |
   |                                                                                                       |
   |   ETH1    |-----------|                    |--------------------------|        |--------------|       |
   |  -------->|0          |                    |         INTCIO           |        |  orgates[0]  |       |
   |   ETH2    |          4|   orgates[0]------>|inpin[0]-------->outpin[0]|------->| 0            |       |
   |  -------->|1         5|   orgates[1]------>|inpin[1]-------->outpin[1]|------->| 1            |       |
   |   ETH3    |          6|   orgates[2]------>|inpin[2]-------->outpin[2]|------->| 2            |       |
   |  -------->|2        19|   orgates[3]------>|inpin[3]-------->outpin[3]|------->| 3  OR[0:9]   |-----| |
   |   UART0   |         20|-->orgates[4]------>|inpin[4]-------->outpin[4]|------->| 4            |     | |
   |  -------->|7        21|   orgates[5]------>|inpin[5]-------->outpin[5]|------->| 5            |     | |
   |   UART1   |         22|   orgates[6]------>|inpin[6]-------->outpin[6]|------->| 6            |     | |
   |  -------->|8        23|   orgates[7]------>|inpin[7]-------->outpin[7]|------->| 7            |     | |
   |   UART2   |         24|   orgates[8]------>|inpin[8]-------->outpin[8]|------->| 8            |     | |
   |  -------->|9        25|   orgates[9]------>|inpin[9]-------->outpin[9]|------->| 9            |     | |
   |   UART3   |         26|                    |--------------------------|        |--------------|     | |
   |  ---------|10       27|                                                                             | |
   |   UART5   |         28|                                                                             | |
   |  -------->|11       29|                                                                             | |
   |   UART6   |           |                                                                             | |
   |  -------->|12       30|     |-----------------------------------------------------------------------| |
   |   UART7   |         31|     |                                                                         |
   |  -------->|13         |     |                                                                         |
   |   UART8   |  OR[0:31] |     |                |------------------------------|           |----------|  |
   |  -------->|14         |     |                |            INTC              |           |     GIC  |  |
   |   UART9   |           |     |                |inpin[0:0]--------->outpin[0] |---------->|192       |  |
   |  -------->|15         |     |                |inpin[0:1]--------->outpin[1] |---------->|193       |  |
   |   UART10  |           |     |                |inpin[0:2]--------->outpin[2] |---------->|194       |  |
   |  -------->|16         |     |                |inpin[0:3]--------->outpin[3] |---------->|195       |  |
   |   UART11  |           |     |--------------> |inpin[0:4]--------->outpin[4] |---------->|196       |  |
   |  -------->|17         |                      |inpin[0:5]--------->outpin[5] |---------->|197       |  |
   |   UART12  |           |                      |inpin[0:6]--------->outpin[6] |---------->|198       |  |
   |  -------->|18         |                      |inpin[0:7]--------->outpin[7] |---------->|199       |  |
   |           |-----------|                      |inpin[0:8]--------->outpin[8] |---------->|200       |  |
   |                                              |inpin[0:9]--------->outpin[9] |---------->|201       |  |
   |-------------------------------------------------------------------------------------------------------|
   |-------------------------------------------------------------------------------------------------------|
   |  ETH1    |-----------|     orgates[1]------->|inpin[1]----------->outpin[10]|---------->|128       |  |
   | -------->|0          |     orgates[2]------->|inpin[2]----------->outpin[11]|---------->|129       |  |
   |  ETH2    |          4|     orgates[3]------->|inpin[3]----------->outpin[12]|---------->|130       |  |
   | -------->|1         5|     orgates[4]------->|inpin[4]----------->outpin[13]|---------->|131       |  |
   |  ETH3    |          6|---->orgates[5]------->|inpin[5]----------->outpin[14]|---------->|132       |  |
   | -------->|2        19|     orgates[6]------->|inpin[6]----------->outpin[15]|---------->|133       |  |
   |  UART0   |         20|     orgates[7]------->|inpin[7]----------->outpin[16]|---------->|134       |  |
   | -------->|7        21|     orgates[8]------->|inpin[8]----------->outpin[17]|---------->|135       |  |
   |  UART1   |         22|     orgates[9]------->|inpin[9]----------->outpin[18]|---------->|136       |  |
   | -------->|8        23|                       |------------------------------|           |----------|  |
   |  UART2   |         24|                                                                                |
   | -------->|9        25|                       AST2700 A0 Design                                        |
   |  UART3   |         26|                                                                                |
   | -------->|10       27|                                                                                |
   |  UART5   |         28|                                                                                |
   | -------->|11       29| GICINT132                                                                      |
   |  UART6   |           |                                                                                |
   | -------->|12       30|                                                                                |
   |  UART7   |         31|                                                                                |
   | -------->|13         |                                                                                |
   |  UART8   |  OR[0:31] |                                                                                |
   | -------->|14         |                                                                                |
   |  UART9   |           |                                                                                |
   | -------->|15         |                                                                                |
   |  UART10  |           |                                                                                |
   | -------->|16         |                                                                                |
   |  UART11  |           |                                                                                |
   | -------->|17         |                                                                                |
   |  UART12  |           |                                                                                |
   | -------->|18         |                                                                                |
   |          |-----------|                                                                                |
   |                                                                                                       |
   |-------------------------------------------------------------------------------------------------------|
