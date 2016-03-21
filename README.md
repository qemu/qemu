     --^--
    /^ ^ ^\
       | O R B I T
       |
     | | http://www.orbitproject.eu/
      U

This branch is a modified version of the COLO fault-tolerance scheme.
It's very much a work-in-progress.
It contains:
  * The COLO Framework v2.4 set from https://github.com/coloft/qemu.git
    including the block code from branch colo-v2.4-periodic-mode
  * The userspace colo proxy from the December 2015 release of
    https://github.com/zhangckid/qemu.git 
      branch colo-v2.2-periodic-mode-with-colo-proxyV2
    including most of the integration code.
  * Two upstream quorum fixes:
     * fix seqfault when read fails in fifo mode
     * Change vote rules for 64 bits hash

The following patches added as part of the ORBIT work:
  * HMP equivalent commands for x-blockdev-change
  * RDMA transport modifications for COLO
  * A patch to use pthread_condwait to notify the main colo thread of miscompares
  * Hybrid mode that switches between COLO and simple checkpointing
  * Checkpoint statistics using QEMUs TimedAverage
  * Lock memory map updates during checkpoint reload
  * Parallel colo cache flushing

Other fixes added:
  * (Disabled) md5 comparison of RAM
  * checkpoint number tagging of packets on the sec-pri comparison socket
  * TCP sequence number fixup for incoming connections
  * various colo-proxy bugfixes
  * Migration events for postcopy (from upstream)
  * Turn nagling off on the proxy socket
  * Timeout when the secondary doesn't send anything that we've got a waiting
    primary packet for.
