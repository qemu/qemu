This is the list of currently configured Coverity components:

alpha
  ~ .*/qemu((/include)?/hw/alpha/.*|/target/alpha/.*)

arm
  ~ .*/qemu((/include)?/hw/arm/.*|(/include)?/hw/.*/(arm|allwinner-a10|bcm28|digic|exynos|imx|omap|stellaris|pxa2xx|versatile|zynq|cadence).*|/hw/net/xgmac.c|/hw/ssi/xilinx_spips.c|/target/arm/.*)

avr
  ~ .*/qemu((/include)?/hw/avr/.*|/target/avr/.*)

hexagon-gen (component should be ignored in analysis)
  ~ .*/qemu(/target/hexagon/.*generated.*)

hexagon
  ~ .*/qemu(/target/hexagon/.*)

hppa
  ~ .*/qemu((/include)?/hw/hppa/.*|/target/hppa/.*)

i386
  ~ .*/qemu((/include)?/hw/i386/.*|/target/i386/.*|/hw/intc/[^/]*apic[^/]*\.c)

loongarch
  ~ .*/qemu((/include)?/hw/(loongarch/.*|.*/loongarch.*)|/target/loongarch/.*)

m68k
  ~ .*/qemu((/include)?/hw/m68k/.*|/target/m68k/.*|(/include)?/hw(/.*)?/mcf.*|(/include)?/hw/nubus/.*)

microblaze
  ~ .*/qemu((/include)?/hw/microblaze/.*|/target/microblaze/.*)

mips
  ~ .*/qemu((/include)?/hw/mips/.*|/target/mips/.*)

openrisc
  ~ .*/qemu((/include)?/hw/openrisc/.*|/target/openrisc/.*)

ppc
  ~ .*/qemu((/include)?/hw/ppc/.*|/target/ppc/.*|/hw/pci-host/(uninorth.*|dec.*|prep.*|ppc.*)|/hw/misc/macio/.*|(/include)?/hw/.*/(xics|openpic|spapr).*)

riscv
  ~ .*/qemu((/include)?/hw/riscv/.*|/target/riscv/.*|/hw/.*/(riscv_|ibex_|sifive_).*)

rx
  ~ .*/qemu((/include)?/hw/rx/.*|/target/rx/.*)

s390
  ~ .*/qemu((/include)?/hw/s390x/.*|/target/s390x/.*|/hw/.*/s390_.*)

sh4
  ~ .*/qemu((/include)?/hw/sh4/.*|/target/sh4/.*)

sparc
  ~ .*/qemu((/include)?/hw/sparc(64)?.*|/target/sparc/.*|/hw/.*/grlib.*|/hw/display/cg3.c)

tricore
  ~ .*/qemu((/include)?/hw/tricore/.*|/target/tricore/.*)

xtensa
  ~ .*/qemu((/include)?/hw/xtensa/.*|/target/xtensa/.*)

9pfs
  ~ .*/qemu(/hw/9pfs/.*|/fsdev/.*)

audio
  ~ .*/qemu((/include)?/(audio|hw/audio)/.*)

block
  ~ .*/qemu(/block.*|(/include?)/(block|storage-daemon)/.*|(/include)?/hw/(block|ide|nvme)/.*|/qemu-(img|io).*|/util/(aio|async|thread-pool).*)

char
  ~ .*/qemu((/include)?/hw/char/.*)

chardev
  ~ .*/qemu((/include)?/chardev/.*)

crypto
  ~ .*/qemu((/include)?/crypto/.*|/hw/.*/.*crypto.*|(/include/system|/backends)/cryptodev.*|/host/include/.*/host/crypto/.*)

disas
  ~ .*/qemu((/include)?/disas.*)

fpu
  ~ .*/qemu((/include)?(/fpu|/libdecnumber)/.*)

io
  ~ .*/qemu((/include)?/io/.*)

ipmi
  ~ .*/qemu((/include)?/hw/ipmi/.*)

migration
  ~ .*/qemu((/include)?/migration/.*)

monitor
  ~ .*/qemu((/include)?/(qapi|qobject|monitor)/.*|/job-qmp.c)

nbd
  ~ .*/qemu(/nbd/.*|/include/block/nbd.*|/qemu-nbd\.c)

net
  ~ .*/qemu((/include)?(/hw)?/(net|rdma)/.*)

pci
  ~ .*/qemu(/include)?/hw/(cxl/|pci).*

qemu-ga
  ~ .*/qemu(/qga/.*)

scsi
  ~ .*/qemu(/scsi/.*|/hw/scsi/.*|/include/hw/scsi/.*)

trace
  ~ .*/qemu(/.*trace.*\.[ch])

ui
  ~ .*/qemu((/include)?(/ui|/hw/display|/hw/input)/.*)

usb
  ~ .*/qemu(/hw/usb/.*|/include/hw/usb/.*)

user
  ~ .*/qemu(/linux-user/.*|/bsd-user/.*|/user-exec\.c|/thunk\.c|/include/user/.*)

util
  ~ .*/qemu(/util/.*|/include/qemu/.*)

vfio
  ~ .*/qemu(/include)?/hw/vfio/.*

virtio
  ~ .*/qemu(/include)?/hw/virtio/.*

xen
  ~ .*/qemu(.*/xen.*)

hvf
  ~ .*/qemu(.*/hvf.*)

kvm
  ~ .*/qemu(.*/kvm.*)

tcg
  ~ .*/qemu(/accel/tcg|/replay|/tcg)/.*

system
  ~ .*/qemu(/system/.*|/accel/.*)

(headers)
  ~ .*/qemu(/include/.*)

testlibs
  ~ .*/qemu(/tests/qtest(/libqos/.*|/libqtest.*|/libqmp.*))

tests
  ~ .*/qemu(/tests/.*)
