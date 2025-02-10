Facebook Yosemite v3.5 Platform and CraterLake Server (``fby35``)
==================================================================

Facebook has a series of multi-node compute server designs named
Yosemite. The most recent version released was
`Yosemite v3 <https://www.opencompute.org/documents/ocp-yosemite-v3-platform-design-specification-1v16-pdf>`__.

Yosemite v3.5 is an iteration on this design, and is very similar: there's a
baseboard with a BMC, and 4 server slots. The new server board design termed
"CraterLake" includes a Bridge IC (BIC), with room for expansion boards to
include various compute accelerators (video, inferencing, etc). At the moment,
only the first server slot's BIC is included.

Yosemite v3.5 is itself a sled which fits into a 40U chassis, and 3 sleds
can be fit into a chassis. See `here <https://www.opencompute.org/products-chiplets/237/wiwynn-yosemite-v3-server>`__
for an example.

In this generation, the BMC is an AST2600 and each BIC is an AST1030. The BMC
runs `OpenBMC <https://github.com/facebook/openbmc>`__, and the BIC runs
`OpenBIC <https://github.com/facebook/openbic>`__.

Firmware images can be retrieved from the Github releases or built from the
source code, see the README's for instructions on that. This image uses the
"fby35" machine recipe from OpenBMC, and the "yv35-cl" target from OpenBIC.
Some reference images can also be found here:

.. code-block:: bash

    $ wget https://github.com/facebook/openbmc/releases/download/openbmc-e2294ff5d31d/fby35.mtd
    $ wget https://github.com/peterdelevoryas/OpenBIC/releases/download/oby35-cl-2022.13.01/Y35BCL.elf

Since this machine has multiple SoC's, each with their own serial console, the
recommended way to run it is to allocate a pseudoterminal for each serial
console and let the monitor use stdio. Also, starting in a paused state is
useful because it allows you to attach to the pseudoterminals before the boot
process starts.

.. code-block:: bash

    $ qemu-system-arm -machine fby35 \
        -drive file=fby35.mtd,format=raw,if=mtd \
        -device loader,file=Y35BCL.elf,addr=0,cpu-num=2 \
        -serial pty -serial pty -serial mon:stdio \
        -display none -S
    $ screen /dev/tty0 # In a separate TMUX pane, terminal window, etc.
    $ screen /dev/tty1
    $ (qemu) c		   # Start the boot process once screen is setup.

This machine model supports emulation of the boot from the CE0 flash device by
setting option ``execute-in-place``. When using this option, the CPU fetches
instructions to execute by reading CE0 and not from a preloaded ROM
initialized at machine init time. As a result, execution will be slower.
