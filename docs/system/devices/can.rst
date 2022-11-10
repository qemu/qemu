CAN Bus Emulation Support
=========================
The CAN bus emulation provides mechanism to connect multiple
emulated CAN controller chips together by one or multiple CAN busses
(the controller device "canbus"  parameter). The individual busses
can be connected to host system CAN API (at this time only Linux
SocketCAN is supported).

The concept of busses is generic and different CAN controllers
can be implemented.

The initial submission implemented SJA1000 controller which
is common and well supported by by drivers for the most operating
systems.

The PCI addon card hardware has been selected as the first CAN
interface to implement because such device can be easily connected
to systems with different CPU architectures (x86, PowerPC, Arm, etc.).

In 2020, CTU CAN FD controller model has been added as part
of the bachelor thesis of Jan Charvat. This controller is complete
open-source/design/hardware solution. The core designer
of the project is Ondrej Ille, the financial support has been
provided by CTU, and more companies including Volkswagen subsidiaries.

The project has been initially started in frame of RTEMS GSoC 2013
slot by Jin Yang under our mentoring  The initial idea was to provide generic
CAN subsystem for RTEMS. But lack of common environment for code and RTEMS
testing lead to goal change to provide environment which provides complete
emulated environment for testing and RTEMS GSoC slot has been donated
to work on CAN hardware emulation on QEMU.

Examples how to use CAN emulation for SJA1000 based boards
----------------------------------------------------------
When QEMU with CAN PCI support is compiled then one of the next
CAN boards can be selected

(1) CAN bus Kvaser PCI CAN-S (single SJA1000 channel) board. QEMU startup options::

    -object can-bus,id=canbus0
    -device kvaser_pci,canbus=canbus0

Add "can-host-socketcan" object to connect device to host system CAN bus::

    -object can-host-socketcan,id=canhost0,if=can0,canbus=canbus0

(2) CAN bus PCM-3680I PCI (dual SJA1000 channel) emulation::

    -object can-bus,id=canbus0
    -device pcm3680_pci,canbus0=canbus0,canbus1=canbus0

Another example::

    -object can-bus,id=canbus0
    -object can-bus,id=canbus1
    -device pcm3680_pci,canbus0=canbus0,canbus1=canbus1

(3) CAN bus MIOe-3680 PCI (dual SJA1000 channel) emulation::

    -device mioe3680_pci,canbus0=canbus0

The ''kvaser_pci'' board/device model is compatible with and has been tested with
the ''kvaser_pci'' driver included in mainline Linux kernel.
The tested setup was Linux 4.9 kernel on the host and guest side.

Example for qemu-system-x86_64::

    qemu-system-x86_64 -accel kvm -kernel /boot/vmlinuz-4.9.0-4-amd64 \
      -initrd ramdisk.cpio \
      -virtfs local,path=shareddir,security_model=none,mount_tag=shareddir \
      -object can-bus,id=canbus0 \
      -object can-host-socketcan,id=canhost0,if=can0,canbus=canbus0 \
      -device kvaser_pci,canbus=canbus0 \
      -nographic -append "console=ttyS0"

Example for qemu-system-arm::

    qemu-system-arm -cpu arm1176 -m 256 -M versatilepb \
      -kernel kernel-qemu-arm1176-versatilepb \
      -hda rpi-wheezy-overlay \
      -append "console=ttyAMA0 root=/dev/sda2 ro init=/sbin/init-overlay" \
      -nographic \
      -virtfs local,path=shareddir,security_model=none,mount_tag=shareddir \
      -object can-bus,id=canbus0 \
      -object can-host-socketcan,id=canhost0,if=can0,canbus=canbus0 \
      -device kvaser_pci,canbus=canbus0,host=can0 \

The CAN interface of the host system has to be configured for proper
bitrate and set up. Configuration is not propagated from emulated
devices through bus to the physical host device. Example configuration
for 1 Mbit/s::

  ip link set can0 type can bitrate 1000000
  ip link set can0 up

Virtual (host local only) can interface can be used on the host
side instead of physical interface::

  ip link add dev can0 type vcan

The CAN interface on the host side can be used to analyze CAN
traffic with "candump" command which is included in "can-utils"::

  candump can0

CTU CAN FD support examples
---------------------------
This open-source core provides CAN FD support. CAN FD drames are
delivered even to the host systems when SocketCAN interface is found
CAN FD capable.

The PCIe board emulation is provided for now (the device identifier is
ctucan_pci). The default build defines two CTU CAN FD cores
on the board.

Example how to connect the canbus0-bus (virtual wire) to the host
Linux system (SocketCAN used) and to both CTU CAN FD cores emulated
on the corresponding PCI card expects that host system CAN bus
is setup according to the previous SJA1000 section::

  qemu-system-x86_64 -enable-kvm -kernel /boot/vmlinuz-4.19.52+ \
      -initrd ramdisk.cpio \
      -virtfs local,path=shareddir,security_model=none,mount_tag=shareddir \
      -vga cirrus \
      -append "console=ttyS0" \
      -object can-bus,id=canbus0-bus \
      -object can-host-socketcan,if=can0,canbus=canbus0-bus,id=canbus0-socketcan \
      -device ctucan_pci,canbus0=canbus0-bus,canbus1=canbus0-bus \
      -nographic

Setup of CTU CAN FD controller in a guest Linux system::

  insmod ctucanfd.ko || modprobe ctucanfd
  insmod ctucanfd_pci.ko || modprobe ctucanfd_pci

  for ifc in /sys/class/net/can* ; do
    if [ -e  $ifc/device/vendor ] ; then
      if ! grep -q 0x1760 $ifc/device/vendor ; then
        continue;
      fi
    else
      continue;
    fi
    if [ -e  $ifc/device/device ] ; then
       if ! grep -q 0xff00 $ifc/device/device ; then
         continue;
       fi
    else
      continue;
    fi
    ifc=$(basename $ifc)
    /bin/ip link set $ifc type can bitrate 1000000 dbitrate 10000000 fd on
    /bin/ip link set $ifc up
  done

The test can run for example::

  candump can1

in the guest system and next commands in the host system for basic CAN::

  cangen can0

for CAN FD without bitrate switch::

  cangen can0 -f

and with bitrate switch::

  cangen can0 -b

The test can also be run the other way around, generating messages in the
guest system and capturing them in the host system. Other combinations are
also possible.

Links to other resources
------------------------

 (1) `CAN related projects at Czech Technical University, Faculty of Electrical Engineering <http://canbus.pages.fel.cvut.cz>`_
 (2) `Repository with development can-pci branch at Czech Technical University <https://gitlab.fel.cvut.cz/canbus/qemu-canbus>`_
 (3) `RTEMS page describing project <https://devel.rtems.org/wiki/Developer/Simulators/QEMU/CANEmulation>`_
 (4) `RTLWS 2015 article about the project and its use with CANopen emulation <http://cmp.felk.cvut.cz/~pisa/can/doc/rtlws-17-pisa-qemu-can.pdf>`_
 (5) `GNU/Linux, CAN and CANopen in Real-time Control Applications Slides from LinuxDays 2017 (include updated RTLWS 2015 content) <https://www.linuxdays.cz/2017/video/Pavel_Pisa-CAN_canopen.pdf>`_
 (6) `Linux SocketCAN utilities <https://github.com/linux-can/can-utils>`_
 (7) `CTU CAN FD project including core VHDL design, Linux driver, test utilities etc. <https://gitlab.fel.cvut.cz/canbus/ctucanfd_ip_core>`_
 (8) `CTU CAN FD Core Datasheet Documentation <http://canbus.pages.fel.cvut.cz/ctucanfd_ip_core/doc/Datasheet.pdf>`_
 (9) `CTU CAN FD Core System Architecture Documentation <http://canbus.pages.fel.cvut.cz/ctucanfd_ip_core/doc/System_Architecture.pdf>`_
 (10) `CTU CAN FD Driver Documentation <https://canbus.pages.fel.cvut.cz/ctucanfd_ip_core/doc/linux_driver/build/ctucanfd-driver.html>`_
 (11) `Integration with PCIe interfacing for Intel/Altera Cyclone IV based board <https://gitlab.fel.cvut.cz/canbus/pcie-ctu_can_fd>`_
