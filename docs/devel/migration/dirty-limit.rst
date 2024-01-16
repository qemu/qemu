Dirty limit
===========

The dirty limit, short for dirty page rate upper limit, is a new capability
introduced in the 8.1 QEMU release that uses a new algorithm based on the KVM
dirty ring to throttle down the guest during live migration.

The algorithm framework is as follows:

::

  ------------------------------------------------------------------------------
  main   --------------> throttle thread ------------> PREPARE(1) <--------
  thread  \                                                |              |
           \                                               |              |
            \                                              V              |
             -\                                        CALCULATE(2)       |
               \                                           |              |
                \                                          |              |
                 \                                         V              |
                  \                                    SET PENALTY(3) -----
                   -\                                      |
                     \                                     |
                      \                                    V
                       -> virtual CPU thread -------> ACCEPT PENALTY(4)
  ------------------------------------------------------------------------------

When the qmp command qmp_set_vcpu_dirty_limit is called for the first time,
the QEMU main thread starts the throttle thread. The throttle thread, once
launched, executes the loop, which consists of three steps:

  - PREPARE (1)

     The entire work of PREPARE (1) is preparation for the second stage,
     CALCULATE(2), as the name implies. It involves preparing the dirty
     page rate value and the corresponding upper limit of the VM:
     The dirty page rate is calculated via the KVM dirty ring mechanism,
     which tells QEMU how many dirty pages a virtual CPU has had since the
     last KVM_EXIT_DIRTY_RING_FULL exception; The dirty page rate upper
     limit is specified by caller, therefore fetch it directly.

  - CALCULATE (2)

     Calculate a suitable sleep period for each virtual CPU, which will be
     used to determine the penalty for the target virtual CPU. The
     computation must be done carefully in order to reduce the dirty page
     rate progressively down to the upper limit without oscillation. To
     achieve this, two strategies are provided: the first is to add or
     subtract sleep time based on the ratio of the current dirty page rate
     to the limit, which is used when the current dirty page rate is far
     from the limit; the second is to add or subtract a fixed time when
     the current dirty page rate is close to the limit.

  - SET PENALTY (3)

     Set the sleep time for each virtual CPU that should be penalized based
     on the results of the calculation supplied by step CALCULATE (2).

After completing the three above stages, the throttle thread loops back
to step PREPARE (1) until the dirty limit is reached.

On the other hand, each virtual CPU thread reads the sleep duration and
sleeps in the path of the KVM_EXIT_DIRTY_RING_FULL exception handler, that
is ACCEPT PENALTY (4). Virtual CPUs tied with writing processes will
obviously exit to the path and get penalized, whereas virtual CPUs involved
with read processes will not.

In summary, thanks to the KVM dirty ring technology, the dirty limit
algorithm will restrict virtual CPUs as needed to keep their dirty page
rate inside the limit. This leads to more steady reading performance during
live migration and can aid in improving large guest responsiveness.
