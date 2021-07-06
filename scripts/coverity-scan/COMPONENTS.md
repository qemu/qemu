This is the list of currently configured Coverity components:

alpha
  ~ (/qemu)?((/include)?/hw/alpha/.*|/target/alpha/.*)

arm
  ~ (/qemu)?((/include)?/hw/arm/.*|(/include)?/hw/.*/(arm|allwinner-a10|bcm28|digic|exynos|imx|omap|stellaris|pxa2xx|versatile|zynq|cadence).*|/hw/net/xgmac.c|/hw/ssi/xilinx_spips.c|/target/arm/.*)

avr
  ~ (/qemu)?((/include)?/hw/avr/.*|/target/avr/.*)

cris
  ~ (/qemu)?((/include)?/hw/cris/.*|/target/cris/.*)

hexagon
  ~ (/qemu)?(/target/hexagon/.*)

hppa
  ~ (/qemu)?((/include)?/hw/hppa/.*|/target/hppa/.*)

i386
  ~ (/qemu)?((/include)?/hw/i386/.*|/target/i386/.*|/hw/intc/[^/]*apic[^/]*\.c)

m68k
  ~ (/qemu)?((/include)?/hw/m68k/.*|/target/m68k/.*|(/include)?/hw(/.*)?/mcf.*)

microblaze
  ~ (/qemu)?((/include)?/hw/microblaze/.*|/target/microblaze/.*)

mips
  ~ (/qemu)?((/include)?/hw/mips/.*|/target/mips/.*)

nios2
  ~ (/qemu)?((/include)?/hw/nios2/.*|/target/nios2/.*)

ppc
  ~ (/qemu)?((/include)?/hw/ppc/.*|/target/ppc/.*|/hw/pci-host/(uninorth.*|dec.*|prep.*|ppc.*)|/hw/misc/macio/.*|(/include)?/hw/.*/(xics|openpic|spapr).*)

riscv
  ~ (/qemu)?((/include)?/hw/riscv/.*|/target/riscv/.*)

rx
  ~ (/qemu)?((/include)?/hw/rx/.*|/target/rx/.*)

s390
  ~ (/qemu)?((/include)?/hw/s390x/.*|/target/s390x/.*|/hw/.*/s390_.*)

sh4
  ~ (/qemu)?((/include)?/hw/sh4/.*|/target/sh4/.*)

sparc
  ~ (/qemu)?((/include)?/hw/sparc(64)?.*|/target/sparc/.*|/hw/.*/grlib.*|/hw/display/cg3.c)

tilegx
  ~ (/qemu)?(/target/tilegx/.*)

tricore
  ~ (/qemu)?((/include)?/hw/tricore/.*|/target/tricore/.*)

9pfs
  ~ (/qemu)?(/hw/9pfs/.*|/fsdev/.*)

audio
  ~ (/qemu)?((/include)?/(audio|hw/audio)/.*)

block
  ~ (/qemu)?(/block.*|(/include?)(/hw)?/(block|storage-daemon)/.*|(/include)?/hw/ide/.*|/qemu-(img|io).*|/util/(aio|async|thread-pool).*)

char
  ~ (/qemu)?(/qemu-char\.c|/include/sysemu/char\.h|(/include)?/hw/char/.*)

capstone
  ~ (/qemu)?(/capstone/.*)

crypto
  ~ (/qemu)?((/include)?/crypto/.*|/hw/.*/crypto.*)

disas
  ~ (/qemu)?((/include)?/disas.*)

fpu
  ~ (/qemu)?((/include)?(/fpu|/libdecnumber)/.*)

io
  ~ (/qemu)?((/include)?/io/.*)

ipmi
  ~ (/qemu)?((/include)?/hw/ipmi/.*)

libvixl
  ~ (/qemu)?(/disas/libvixl/.*)

migration
  ~ (/qemu)?((/include)?/migration/.*)

monitor
  ~ (/qemu)?(/qapi.*|/qobject/.*|/monitor\..*|/[hq]mp\..*)

nbd
  ~ (/qemu)?(/nbd/.*|/include/block/nbd.*|/qemu-nbd\.c)

net
  ~ (/qemu)?((/include)?(/hw)?/(net|rdma)/.*)

pci
  ~ (/qemu)?(/hw/pci.*|/include/hw/pci.*)

qemu-ga
  ~ (/qemu)?(/qga/.*)

scsi
  ~ (/qemu)?(/scsi/.*|/hw/scsi/.*|/include/hw/scsi/.*)

slirp
  ~ (/qemu)?(/.*slirp.*)

tcg
  ~ (/qemu)?(/accel/tcg/.*|/replay/.*|/(.*/)?softmmu.*)

trace
  ~ (/qemu)?(/.*trace.*\.[ch])

ui
  ~ (/qemu)?((/include)?(/ui|/hw/display|/hw/input)/.*)

usb
  ~ (/qemu)?(/hw/usb/.*|/include/hw/usb/.*)

user
  ~ (/qemu)?(/linux-user/.*|/bsd-user/.*|/user-exec\.c|/thunk\.c|/include/exec/user/.*)

util
  ~ (/qemu)?(/util/.*|/include/qemu/.*)

xen
  ~ (/qemu)?(.*/xen.*)

virtiofsd
  ~ (/qemu)?(/tools/virtiofsd/.*)

(headers)
  ~ (/qemu)?(/include/.*)

testlibs
  ~ (/qemu)?(/tests/qtest(/libqos/.*|/libqtest.*))

tests
  ~ (/qemu)?(/tests/.*)
