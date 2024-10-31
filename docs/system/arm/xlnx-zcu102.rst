Xilinx ZynqMP ZCU102 (``xlnx-zcu102``)
======================================

The ``xlnx-zcu102`` board models the Xilinx ZynqMP ZCU102 board.
This board has 4 Cortex-A53 CPUs and 2 Cortex-R5F CPUs.

Machine-specific options
""""""""""""""""""""""""

The following machine-specific options are supported:

secure
  Set ``on``/``off`` to enable/disable emulating a guest CPU which implements the
  Arm Security Extensions (TrustZone). The default is ``off``.

virtualization
  Set ``on``/``off`` to enable/disable emulating a guest CPU which implements the
  Arm Virtualization Extensions. The default is ``off``.

