export SRC_PATH

qapi-py = $(SRC_PATH)/scripts/qapi.py $(SRC_PATH)/scripts/ordereddict.py

# Get the list of all supported sysemu targets
SYSEMU_TARGET_LIST := $(subst -softmmu.mak,,$(notdir \
   $(wildcard $(SRC_PATH)/default-configs/*-softmmu.mak)))

check-unit-y = tests/check-qdict$(EXESUF)
gcov-files-check-qdict-y = qobject/qdict.c
check-unit-y += tests/check-qfloat$(EXESUF)
gcov-files-check-qfloat-y = qobject/qfloat.c
check-unit-y += tests/check-qint$(EXESUF)
gcov-files-check-qint-y = qobject/qint.c
check-unit-y += tests/check-qstring$(EXESUF)
gcov-files-check-qstring-y = qobject/qstring.c
check-unit-y += tests/check-qlist$(EXESUF)
gcov-files-check-qlist-y = qobject/qlist.c
check-unit-y += tests/check-qnull$(EXESUF)
gcov-files-check-qnull-y = qobject/qnull.c
check-unit-y += tests/check-qjson$(EXESUF)
gcov-files-check-qjson-y = qobject/qjson.c
check-unit-y += tests/test-qmp-output-visitor$(EXESUF)
gcov-files-test-qmp-output-visitor-y = qapi/qmp-output-visitor.c
check-unit-y += tests/test-qmp-input-visitor$(EXESUF)
gcov-files-test-qmp-input-visitor-y = qapi/qmp-input-visitor.c
check-unit-y += tests/test-qmp-input-strict$(EXESUF)
check-unit-y += tests/test-qmp-commands$(EXESUF)
gcov-files-test-qmp-commands-y = qapi/qmp-dispatch.c
check-unit-y += tests/test-string-input-visitor$(EXESUF)
gcov-files-test-string-input-visitor-y = qapi/string-input-visitor.c
check-unit-y += tests/test-string-output-visitor$(EXESUF)
gcov-files-test-string-output-visitor-y = qapi/string-output-visitor.c
check-unit-y += tests/test-qmp-event$(EXESUF)
gcov-files-test-qmp-event-y += qapi/qmp-event.c
check-unit-y += tests/test-opts-visitor$(EXESUF)
gcov-files-test-opts-visitor-y = qapi/opts-visitor.c
check-unit-y += tests/test-coroutine$(EXESUF)
gcov-files-test-coroutine-y = coroutine-$(CONFIG_COROUTINE_BACKEND).c
check-unit-y += tests/test-visitor-serialization$(EXESUF)
check-unit-y += tests/test-iov$(EXESUF)
gcov-files-test-iov-y = util/iov.c
check-unit-y += tests/test-aio$(EXESUF)
check-unit-$(CONFIG_POSIX) += tests/test-rfifolock$(EXESUF)
check-unit-y += tests/test-throttle$(EXESUF)
gcov-files-test-aio-$(CONFIG_WIN32) = aio-win32.c
gcov-files-test-aio-$(CONFIG_POSIX) = aio-posix.c
check-unit-y += tests/test-thread-pool$(EXESUF)
gcov-files-test-thread-pool-y = thread-pool.c
gcov-files-test-hbitmap-y = util/hbitmap.c
check-unit-y += tests/test-hbitmap$(EXESUF)
gcov-files-test-hbitmap-y = blockjob.c
check-unit-y += tests/test-blockjob-txn$(EXESUF)
check-unit-y += tests/test-x86-cpuid$(EXESUF)
# all code tested by test-x86-cpuid is inside topology.h
gcov-files-test-x86-cpuid-y =
ifeq ($(CONFIG_SOFTMMU),y)
check-unit-y += tests/test-xbzrle$(EXESUF)
gcov-files-test-xbzrle-y = migration/xbzrle.c
check-unit-$(CONFIG_POSIX) += tests/test-vmstate$(EXESUF)
endif
check-unit-y += tests/test-cutils$(EXESUF)
gcov-files-test-cutils-y += util/cutils.c
check-unit-y += tests/test-mul64$(EXESUF)
gcov-files-test-mul64-y = util/host-utils.c
check-unit-y += tests/test-int128$(EXESUF)
# all code tested by test-int128 is inside int128.h
gcov-files-test-int128-y =
check-unit-y += tests/rcutorture$(EXESUF)
gcov-files-rcutorture-y = util/rcu.c
check-unit-y += tests/test-rcu-list$(EXESUF)
gcov-files-test-rcu-list-y = util/rcu.c
check-unit-y += tests/test-bitops$(EXESUF)
check-unit-$(CONFIG_HAS_GLIB_SUBPROCESS_TESTS) += tests/test-qdev-global-props$(EXESUF)
check-unit-y += tests/check-qom-interface$(EXESUF)
gcov-files-check-qom-interface-y = qom/object.c
check-unit-y += tests/check-qom-proplist$(EXESUF)
gcov-files-check-qom-proplist-y = qom/object.c
check-unit-y += tests/test-qemu-opts$(EXESUF)
gcov-files-test-qemu-opts-y = qom/test-qemu-opts.c
check-unit-y += tests/test-write-threshold$(EXESUF)
gcov-files-test-write-threshold-y = block/write-threshold.c
check-unit-$(CONFIG_GNUTLS_HASH) += tests/test-crypto-hash$(EXESUF)
check-unit-y += tests/test-crypto-cipher$(EXESUF)
check-unit-y += tests/test-crypto-secret$(EXESUF)
check-unit-$(CONFIG_GNUTLS) += tests/test-crypto-tlscredsx509$(EXESUF)
check-unit-$(CONFIG_GNUTLS) += tests/test-crypto-tlssession$(EXESUF)
ifneq (,$(findstring qemu-ga,$(TOOLS)))
check-unit-$(CONFIG_LINUX) += tests/test-qga$(EXESUF)
endif
check-unit-y += tests/test-timed-average$(EXESUF)
check-unit-y += tests/test-io-task$(EXESUF)
check-unit-y += tests/test-io-channel-socket$(EXESUF)
check-unit-y += tests/test-io-channel-file$(EXESUF)
check-unit-$(CONFIG_GNUTLS) += tests/test-io-channel-tls$(EXESUF)
check-unit-y += tests/test-io-channel-command$(EXESUF)
check-unit-y += tests/test-io-channel-buffer$(EXESUF)
check-unit-y += tests/test-base64$(EXESUF)
check-unit-$(if $(CONFIG_NETTLE_KDF),y,$(CONFIG_GCRYPT_KDF)) += tests/test-crypto-pbkdf$(EXESUF)
check-unit-y += tests/test-crypto-ivgen$(EXESUF)
check-unit-y += tests/test-crypto-afsplit$(EXESUF)
check-unit-y += tests/test-crypto-xts$(EXESUF)
check-unit-y += tests/test-crypto-block$(EXESUF)
gcov-files-test-logging-y = tests/test-logging.c
check-unit-y += tests/test-logging$(EXESUF)

check-block-$(CONFIG_POSIX) += tests/qemu-iotests-quick.sh

# All QTests for now are POSIX-only, but the dependencies are
# really in libqtest, not in the testcases themselves.

check-qtest-generic-y = tests/device-introspect-test$(EXESUF)
gcov-files-generic-y = qdev-monitor.c qmp.c

gcov-files-ipack-y += hw/ipack/ipack.c
check-qtest-ipack-y += tests/ipoctal232-test$(EXESUF)
gcov-files-ipack-y += hw/char/ipoctal232.c

check-qtest-virtioserial-y += tests/virtio-console-test$(EXESUF)
gcov-files-virtioserial-y += hw/char/virtio-console.c

gcov-files-virtio-y += i386-softmmu/hw/virtio/virtio.c
check-qtest-virtio-y += tests/virtio-net-test$(EXESUF)
gcov-files-virtio-y += i386-softmmu/hw/net/virtio-net.c
check-qtest-virtio-y += tests/virtio-balloon-test$(EXESUF)
gcov-files-virtio-y += i386-softmmu/hw/virtio/virtio-balloon.c
check-qtest-virtio-y += tests/virtio-blk-test$(EXESUF)
gcov-files-virtio-y += i386-softmmu/hw/block/virtio-blk.c
check-qtest-virtio-y += tests/virtio-rng-test$(EXESUF)
gcov-files-virtio-y += hw/virtio/virtio-rng.c
check-qtest-virtio-y += tests/virtio-scsi-test$(EXESUF)
gcov-files-virtio-y += i386-softmmu/hw/scsi/virtio-scsi.c
ifeq ($(CONFIG_VIRTIO)$(CONFIG_VIRTFS)$(CONFIG_PCI),yyy)
check-qtest-virtio-y += tests/virtio-9p-test$(EXESUF)
gcov-files-virtio-y += hw/9pfs/virtio-9p.c
gcov-files-virtio-y += i386-softmmu/hw/9pfs/virtio-9p-device.c
endif
check-qtest-virtio-y += tests/virtio-serial-test$(EXESUF)
gcov-files-virtio-y += i386-softmmu/hw/char/virtio-serial-bus.c
check-qtest-virtio-y += $(check-qtest-virtioserial-y)
gcov-files-virtio-y += $(gcov-files-virtioserial-y)

check-qtest-pci-y += tests/e1000-test$(EXESUF)
gcov-files-pci-y += hw/net/e1000.c
check-qtest-pci-y += tests/rtl8139-test$(EXESUF)
gcov-files-pci-y += hw/net/rtl8139.c
check-qtest-pci-y += tests/pcnet-test$(EXESUF)
gcov-files-pci-y += hw/net/pcnet.c
gcov-files-pci-y += hw/net/pcnet-pci.c
check-qtest-pci-y += tests/eepro100-test$(EXESUF)
gcov-files-pci-y += hw/net/eepro100.c
check-qtest-pci-y += tests/ne2000-test$(EXESUF)
gcov-files-pci-y += hw/net/ne2000.c
check-qtest-pci-y += tests/nvme-test$(EXESUF)
gcov-files-pci-y += hw/block/nvme.c
check-qtest-pci-y += tests/ac97-test$(EXESUF)
gcov-files-pci-y += hw/audio/ac97.c
check-qtest-pci-y += tests/es1370-test$(EXESUF)
gcov-files-pci-y += hw/audio/es1370.c
check-qtest-pci-y += $(check-qtest-virtio-y)
gcov-files-pci-y += $(gcov-files-virtio-y) hw/virtio/virtio-pci.c
check-qtest-pci-y += tests/tpci200-test$(EXESUF)
gcov-files-pci-y += hw/ipack/tpci200.c
check-qtest-pci-y += $(check-qtest-ipack-y)
gcov-files-pci-y += $(gcov-files-ipack-y)
check-qtest-pci-y += tests/display-vga-test$(EXESUF)
gcov-files-pci-y += hw/display/vga.c
gcov-files-pci-y += hw/display/cirrus_vga.c
gcov-files-pci-y += hw/display/vga-pci.c
gcov-files-pci-y += hw/display/virtio-gpu.c
gcov-files-pci-y += hw/display/virtio-gpu-pci.c
gcov-files-pci-$(CONFIG_VIRTIO_VGA) += hw/display/virtio-vga.c
check-qtest-pci-y += tests/intel-hda-test$(EXESUF)
gcov-files-pci-y += hw/audio/intel-hda.c hw/audio/hda-codec.c
check-qtest-pci-$(CONFIG_EVENTFD) += tests/ivshmem-test$(EXESUF)
gcov-files-pci-y += hw/misc/ivshmem.c

check-qtest-i386-y = tests/endianness-test$(EXESUF)
check-qtest-i386-y += tests/fdc-test$(EXESUF)
gcov-files-i386-y = hw/block/fdc.c
check-qtest-i386-y += tests/ide-test$(EXESUF)
check-qtest-i386-y += tests/ahci-test$(EXESUF)
check-qtest-i386-y += tests/hd-geo-test$(EXESUF)
gcov-files-i386-y += hw/block/hd-geometry.c
check-qtest-i386-y += tests/boot-order-test$(EXESUF)
check-qtest-i386-y += tests/bios-tables-test$(EXESUF)
check-qtest-i386-y += tests/pxe-test$(EXESUF)
check-qtest-i386-y += tests/rtc-test$(EXESUF)
check-qtest-i386-y += tests/ipmi-kcs-test$(EXESUF)
check-qtest-i386-y += tests/ipmi-bt-test$(EXESUF)
check-qtest-i386-y += tests/i440fx-test$(EXESUF)
check-qtest-i386-y += tests/fw_cfg-test$(EXESUF)
check-qtest-i386-y += tests/drive_del-test$(EXESUF)
check-qtest-i386-y += tests/wdt_ib700-test$(EXESUF)
check-qtest-i386-y += tests/tco-test$(EXESUF)
gcov-files-i386-y += hw/watchdog/watchdog.c hw/watchdog/wdt_ib700.c
check-qtest-i386-y += $(check-qtest-pci-y)
gcov-files-i386-y += $(gcov-files-pci-y)
check-qtest-i386-y += tests/vmxnet3-test$(EXESUF)
gcov-files-i386-y += hw/net/vmxnet3.c
gcov-files-i386-y += hw/net/vmxnet_rx_pkt.c
gcov-files-i386-y += hw/net/vmxnet_tx_pkt.c
check-qtest-i386-y += tests/pvpanic-test$(EXESUF)
gcov-files-i386-y += i386-softmmu/hw/misc/pvpanic.c
check-qtest-i386-y += tests/i82801b11-test$(EXESUF)
gcov-files-i386-y += hw/pci-bridge/i82801b11.c
check-qtest-i386-y += tests/ioh3420-test$(EXESUF)
gcov-files-i386-y += hw/pci-bridge/ioh3420.c
check-qtest-i386-y += tests/usb-hcd-ohci-test$(EXESUF)
gcov-files-i386-y += hw/usb/hcd-ohci.c
check-qtest-i386-y += tests/usb-hcd-uhci-test$(EXESUF)
gcov-files-i386-y += hw/usb/hcd-uhci.c
check-qtest-i386-y += tests/usb-hcd-ehci-test$(EXESUF)
gcov-files-i386-y += hw/usb/hcd-ehci.c
gcov-files-i386-y += hw/usb/dev-hid.c
gcov-files-i386-y += hw/usb/dev-storage.c
check-qtest-i386-y += tests/usb-hcd-xhci-test$(EXESUF)
gcov-files-i386-y += hw/usb/hcd-xhci.c
check-qtest-i386-y += tests/pc-cpu-test$(EXESUF)
check-qtest-i386-y += tests/q35-test$(EXESUF)
gcov-files-i386-y += hw/pci-host/q35.c
check-qtest-i386-$(CONFIG_VHOST_NET_TEST_i386) += tests/vhost-user-test$(EXESUF)
ifeq ($(CONFIG_VHOST_NET_TEST_i386),)
check-qtest-x86_64-$(CONFIG_VHOST_NET_TEST_x86_64) += tests/vhost-user-test$(EXESUF)
endif
check-qtest-i386-y += tests/test-netfilter$(EXESUF)
check-qtest-i386-y += tests/test-filter-mirror$(EXESUF)
check-qtest-i386-y += tests/test-filter-redirector$(EXESUF)
check-qtest-x86_64-y = $(check-qtest-i386-y)
gcov-files-i386-y += i386-softmmu/hw/timer/mc146818rtc.c
gcov-files-x86_64-y = $(subst i386-softmmu/,x86_64-softmmu/,$(gcov-files-i386-y))
check-qtest-mips-y = tests/endianness-test$(EXESUF)
check-qtest-mips64-y = tests/endianness-test$(EXESUF)
check-qtest-mips64el-y = tests/endianness-test$(EXESUF)
check-qtest-ppc-y = tests/endianness-test$(EXESUF)
check-qtest-ppc64-y = tests/endianness-test$(EXESUF)
check-qtest-sh4-y = tests/endianness-test$(EXESUF)
check-qtest-sh4eb-y = tests/endianness-test$(EXESUF)
check-qtest-sparc64-y = tests/endianness-test$(EXESUF)
#check-qtest-sparc-y = tests/m48t59-test$(EXESUF)
#check-qtest-sparc64-y += tests/m48t59-test$(EXESUF)
gcov-files-sparc-y += hw/timer/m48t59.c
gcov-files-sparc64-y += hw/timer/m48t59.c
check-qtest-arm-y = tests/tmp105-test$(EXESUF)
check-qtest-arm-y = tests/ds1338-test$(EXESUF)
gcov-files-arm-y += hw/misc/tmp105.c
check-qtest-arm-y += tests/virtio-blk-test$(EXESUF)
gcov-files-arm-y += arm-softmmu/hw/block/virtio-blk.c
check-qtest-ppc-y += tests/boot-order-test$(EXESUF)
check-qtest-ppc64-y += tests/boot-order-test$(EXESUF)
check-qtest-ppc64-y += tests/spapr-phb-test$(EXESUF)
gcov-files-ppc64-y += ppc64-softmmu/hw/ppc/spapr_pci.c
check-qtest-microblazeel-y = $(check-qtest-microblaze-y)
check-qtest-xtensaeb-y = $(check-qtest-xtensa-y)

check-qtest-generic-y += tests/qom-test$(EXESUF)

qapi-schema += alternate-any.json
qapi-schema += alternate-array.json
qapi-schema += alternate-base.json
qapi-schema += alternate-clash.json
qapi-schema += alternate-conflict-dict.json
qapi-schema += alternate-conflict-string.json
qapi-schema += alternate-empty.json
qapi-schema += alternate-nested.json
qapi-schema += alternate-unknown.json
qapi-schema += args-alternate.json
qapi-schema += args-any.json
qapi-schema += args-array-empty.json
qapi-schema += args-array-unknown.json
qapi-schema += args-int.json
qapi-schema += args-invalid.json
qapi-schema += args-member-array-bad.json
qapi-schema += args-member-case.json
qapi-schema += args-member-unknown.json
qapi-schema += args-name-clash.json
qapi-schema += args-union.json
qapi-schema += args-unknown.json
qapi-schema += bad-base.json
qapi-schema += bad-data.json
qapi-schema += bad-ident.json
qapi-schema += bad-type-bool.json
qapi-schema += bad-type-dict.json
qapi-schema += bad-type-int.json
qapi-schema += base-cycle-direct.json
qapi-schema += base-cycle-indirect.json
qapi-schema += command-int.json
qapi-schema += comments.json
qapi-schema += double-data.json
qapi-schema += double-type.json
qapi-schema += duplicate-key.json
qapi-schema += empty.json
qapi-schema += enum-bad-name.json
qapi-schema += enum-bad-prefix.json
qapi-schema += enum-clash-member.json
qapi-schema += enum-dict-member.json
qapi-schema += enum-int-member.json
qapi-schema += enum-member-case.json
qapi-schema += enum-missing-data.json
qapi-schema += enum-wrong-data.json
qapi-schema += escape-outside-string.json
qapi-schema += escape-too-big.json
qapi-schema += escape-too-short.json
qapi-schema += event-case.json
qapi-schema += event-nest-struct.json
qapi-schema += flat-union-array-branch.json
qapi-schema += flat-union-bad-base.json
qapi-schema += flat-union-bad-discriminator.json
qapi-schema += flat-union-base-any.json
qapi-schema += flat-union-base-union.json
qapi-schema += flat-union-clash-member.json
qapi-schema += flat-union-empty.json
qapi-schema += flat-union-inline.json
qapi-schema += flat-union-int-branch.json
qapi-schema += flat-union-invalid-branch-key.json
qapi-schema += flat-union-invalid-discriminator.json
qapi-schema += flat-union-no-base.json
qapi-schema += flat-union-optional-discriminator.json
qapi-schema += flat-union-string-discriminator.json
qapi-schema += funny-char.json
qapi-schema += ident-with-escape.json
qapi-schema += include-before-err.json
qapi-schema += include-cycle.json
qapi-schema += include-format-err.json
qapi-schema += include-nested-err.json
qapi-schema += include-no-file.json
qapi-schema += include-non-file.json
qapi-schema += include-relpath.json
qapi-schema += include-repetition.json
qapi-schema += include-self-cycle.json
qapi-schema += include-simple.json
qapi-schema += indented-expr.json
qapi-schema += leading-comma-list.json
qapi-schema += leading-comma-object.json
qapi-schema += missing-colon.json
qapi-schema += missing-comma-list.json
qapi-schema += missing-comma-object.json
qapi-schema += missing-type.json
qapi-schema += nested-struct-data.json
qapi-schema += non-objects.json
qapi-schema += qapi-schema-test.json
qapi-schema += quoted-structural-chars.json
qapi-schema += redefined-builtin.json
qapi-schema += redefined-command.json
qapi-schema += redefined-event.json
qapi-schema += redefined-type.json
qapi-schema += reserved-command-q.json
qapi-schema += reserved-enum-q.json
qapi-schema += reserved-member-has.json
qapi-schema += reserved-member-q.json
qapi-schema += reserved-member-u.json
qapi-schema += reserved-member-underscore.json
qapi-schema += reserved-type-kind.json
qapi-schema += reserved-type-list.json
qapi-schema += returns-alternate.json
qapi-schema += returns-array-bad.json
qapi-schema += returns-dict.json
qapi-schema += returns-unknown.json
qapi-schema += returns-whitelist.json
qapi-schema += struct-base-clash-deep.json
qapi-schema += struct-base-clash.json
qapi-schema += struct-data-invalid.json
qapi-schema += struct-member-invalid.json
qapi-schema += trailing-comma-list.json
qapi-schema += trailing-comma-object.json
qapi-schema += type-bypass-bad-gen.json
qapi-schema += unclosed-list.json
qapi-schema += unclosed-object.json
qapi-schema += unclosed-string.json
qapi-schema += unicode-str.json
qapi-schema += union-base-no-discriminator.json
qapi-schema += union-branch-case.json
qapi-schema += union-clash-branches.json
qapi-schema += union-empty.json
qapi-schema += union-invalid-base.json
qapi-schema += union-optional-branch.json
qapi-schema += union-unknown.json
qapi-schema += unknown-escape.json
qapi-schema += unknown-expr-key.json
check-qapi-schema-y := $(addprefix tests/qapi-schema/, $(qapi-schema))

GENERATED_HEADERS += tests/test-qapi-types.h tests/test-qapi-visit.h \
	tests/test-qmp-commands.h tests/test-qapi-event.h \
	tests/test-qmp-introspect.h

test-obj-y = tests/check-qint.o tests/check-qstring.o tests/check-qdict.o \
	tests/check-qlist.o tests/check-qfloat.o tests/check-qnull.o \
	tests/check-qjson.o \
	tests/test-coroutine.o tests/test-string-output-visitor.o \
	tests/test-string-input-visitor.o tests/test-qmp-output-visitor.o \
	tests/test-qmp-input-visitor.o tests/test-qmp-input-strict.o \
	tests/test-qmp-commands.o tests/test-visitor-serialization.o \
	tests/test-x86-cpuid.o tests/test-mul64.o tests/test-int128.o \
	tests/test-opts-visitor.o tests/test-qmp-event.o \
	tests/rcutorture.o tests/test-rcu-list.o

$(test-obj-y): QEMU_INCLUDES += -Itests
QEMU_CFLAGS += -I$(SRC_PATH)/tests


# Deps that are common to various different sets of tests below
test-util-obj-y = libqemuutil.a libqemustub.a
test-qom-obj-y = $(qom-obj-y) $(test-util-obj-y)
test-qapi-obj-y = tests/test-qapi-visit.o tests/test-qapi-types.o \
	tests/test-qapi-event.o tests/test-qmp-introspect.o \
	$(test-qom-obj-y)
test-crypto-obj-y = $(crypto-obj-y) $(test-qom-obj-y)
test-io-obj-y = $(io-obj-y) $(test-crypto-obj-y)
test-block-obj-y = $(block-obj-y) $(test-io-obj-y)

tests/check-qint$(EXESUF): tests/check-qint.o $(test-util-obj-y)
tests/check-qstring$(EXESUF): tests/check-qstring.o $(test-util-obj-y)
tests/check-qdict$(EXESUF): tests/check-qdict.o $(test-util-obj-y)
tests/check-qlist$(EXESUF): tests/check-qlist.o $(test-util-obj-y)
tests/check-qfloat$(EXESUF): tests/check-qfloat.o $(test-util-obj-y)
tests/check-qnull$(EXESUF): tests/check-qnull.o $(test-util-obj-y)
tests/check-qjson$(EXESUF): tests/check-qjson.o $(test-util-obj-y)
tests/check-qom-interface$(EXESUF): tests/check-qom-interface.o $(test-qom-obj-y)
tests/check-qom-proplist$(EXESUF): tests/check-qom-proplist.o $(test-qom-obj-y)
tests/test-coroutine$(EXESUF): tests/test-coroutine.o $(test-block-obj-y)
tests/test-aio$(EXESUF): tests/test-aio.o $(test-block-obj-y)
tests/test-rfifolock$(EXESUF): tests/test-rfifolock.o $(test-util-obj-y)
tests/test-throttle$(EXESUF): tests/test-throttle.o $(test-block-obj-y)
tests/test-blockjob-txn$(EXESUF): tests/test-blockjob-txn.o $(test-block-obj-y) $(test-util-obj-y)
tests/test-thread-pool$(EXESUF): tests/test-thread-pool.o $(test-block-obj-y)
tests/test-iov$(EXESUF): tests/test-iov.o $(test-util-obj-y)
tests/test-hbitmap$(EXESUF): tests/test-hbitmap.o $(test-util-obj-y)
tests/test-x86-cpuid$(EXESUF): tests/test-x86-cpuid.o
tests/test-xbzrle$(EXESUF): tests/test-xbzrle.o migration/xbzrle.o page_cache.o $(test-util-obj-y)
tests/test-cutils$(EXESUF): tests/test-cutils.o util/cutils.o
tests/test-int128$(EXESUF): tests/test-int128.o
tests/rcutorture$(EXESUF): tests/rcutorture.o $(test-util-obj-y)
tests/test-rcu-list$(EXESUF): tests/test-rcu-list.o $(test-util-obj-y)

tests/test-qdev-global-props$(EXESUF): tests/test-qdev-global-props.o \
	hw/core/qdev.o hw/core/qdev-properties.o hw/core/hotplug.o\
	hw/core/irq.o \
	hw/core/fw-path-provider.o \
	$(test-qapi-obj-y)
tests/test-vmstate$(EXESUF): tests/test-vmstate.o \
	migration/vmstate.o migration/qemu-file.o migration/qemu-file-buf.o \
        migration/qemu-file-unix.o migration/qjson.o \
	$(test-qom-obj-y)
tests/test-timed-average$(EXESUF): tests/test-timed-average.o qemu-timer.o \
	$(test-util-obj-y)
tests/test-base64$(EXESUF): tests/test-base64.o \
	libqemuutil.a libqemustub.a

tests/test-logging$(EXESUF): tests/test-logging.o $(test-util-obj-y)

tests/test-qapi-types.c tests/test-qapi-types.h :\
$(SRC_PATH)/tests/qapi-schema/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-types.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-types.py \
		$(gen-out-type) -o tests -p "test-" $<, \
		"  GEN   $@")
tests/test-qapi-visit.c tests/test-qapi-visit.h :\
$(SRC_PATH)/tests/qapi-schema/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-visit.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-visit.py \
		$(gen-out-type) -o tests -p "test-" $<, \
		"  GEN   $@")
tests/test-qmp-commands.h tests/test-qmp-marshal.c :\
$(SRC_PATH)/tests/qapi-schema/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-commands.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-commands.py \
		$(gen-out-type) -o tests -p "test-" $<, \
		"  GEN   $@")
tests/test-qapi-event.c tests/test-qapi-event.h :\
$(SRC_PATH)/tests/qapi-schema/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-event.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-event.py \
		$(gen-out-type) -o tests -p "test-" $<, \
		"  GEN   $@")
tests/test-qmp-introspect.c tests/test-qmp-introspect.h :\
$(SRC_PATH)/tests/qapi-schema/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-introspect.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-introspect.py \
		$(gen-out-type) -o tests -p "test-" $<, \
		"  GEN   $@")

tests/test-string-output-visitor$(EXESUF): tests/test-string-output-visitor.o $(test-qapi-obj-y)
tests/test-string-input-visitor$(EXESUF): tests/test-string-input-visitor.o $(test-qapi-obj-y)
tests/test-qmp-event$(EXESUF): tests/test-qmp-event.o $(test-qapi-obj-y)
tests/test-qmp-output-visitor$(EXESUF): tests/test-qmp-output-visitor.o $(test-qapi-obj-y)
tests/test-qmp-input-visitor$(EXESUF): tests/test-qmp-input-visitor.o $(test-qapi-obj-y)
tests/test-qmp-input-strict$(EXESUF): tests/test-qmp-input-strict.o $(test-qapi-obj-y)
tests/test-qmp-commands$(EXESUF): tests/test-qmp-commands.o tests/test-qmp-marshal.o $(test-qapi-obj-y)
tests/test-visitor-serialization$(EXESUF): tests/test-visitor-serialization.o $(test-qapi-obj-y)
tests/test-opts-visitor$(EXESUF): tests/test-opts-visitor.o $(test-qapi-obj-y)

tests/test-mul64$(EXESUF): tests/test-mul64.o $(test-util-obj-y)
tests/test-bitops$(EXESUF): tests/test-bitops.o $(test-util-obj-y)
tests/test-crypto-hash$(EXESUF): tests/test-crypto-hash.o $(test-crypto-obj-y)
tests/test-crypto-cipher$(EXESUF): tests/test-crypto-cipher.o $(test-crypto-obj-y)
tests/test-crypto-secret$(EXESUF): tests/test-crypto-secret.o $(test-crypto-obj-y)
tests/test-crypto-xts$(EXESUF): tests/test-crypto-xts.o $(test-crypto-obj-y)

tests/crypto-tls-x509-helpers.o-cflags := $(TASN1_CFLAGS)
tests/crypto-tls-x509-helpers.o-libs := $(TASN1_LIBS)
tests/pkix_asn1_tab.o-cflags := $(TASN1_CFLAGS)

tests/test-crypto-tlscredsx509.o-cflags := $(TASN1_CFLAGS)
tests/test-crypto-tlscredsx509$(EXESUF): tests/test-crypto-tlscredsx509.o \
	tests/crypto-tls-x509-helpers.o tests/pkix_asn1_tab.o $(test-crypto-obj-y)

tests/test-crypto-tlssession.o-cflags := $(TASN1_CFLAGS)
tests/test-crypto-tlssession$(EXESUF): tests/test-crypto-tlssession.o \
	tests/crypto-tls-x509-helpers.o tests/pkix_asn1_tab.o $(test-crypto-obj-y)
tests/test-io-task$(EXESUF): tests/test-io-task.o $(test-io-obj-y)
tests/test-io-channel-socket$(EXESUF): tests/test-io-channel-socket.o \
        tests/io-channel-helpers.o $(test-io-obj-y)
tests/test-io-channel-file$(EXESUF): tests/test-io-channel-file.o \
        tests/io-channel-helpers.o $(test-io-obj-y)
tests/test-io-channel-tls$(EXESUF): tests/test-io-channel-tls.o \
	tests/crypto-tls-x509-helpers.o tests/pkix_asn1_tab.o \
	tests/io-channel-helpers.o $(test-io-obj-y)
tests/test-io-channel-command$(EXESUF): tests/test-io-channel-command.o \
        tests/io-channel-helpers.o $(test-io-obj-y)
tests/test-io-channel-buffer$(EXESUF): tests/test-io-channel-buffer.o \
        tests/io-channel-helpers.o $(test-io-obj-y)
tests/test-crypto-pbkdf$(EXESUF): tests/test-crypto-pbkdf.o $(test-crypto-obj-y)
tests/test-crypto-ivgen$(EXESUF): tests/test-crypto-ivgen.o $(test-crypto-obj-y)
tests/test-crypto-afsplit$(EXESUF): tests/test-crypto-afsplit.o $(test-crypto-obj-y)
tests/test-crypto-block$(EXESUF): tests/test-crypto-block.o $(test-crypto-obj-y)

libqos-obj-y = tests/libqos/pci.o tests/libqos/fw_cfg.o tests/libqos/malloc.o
libqos-obj-y += tests/libqos/i2c.o tests/libqos/libqos.o
libqos-pc-obj-y = $(libqos-obj-y) tests/libqos/pci-pc.o
libqos-pc-obj-y += tests/libqos/malloc-pc.o tests/libqos/libqos-pc.o
libqos-pc-obj-y += tests/libqos/ahci.o
libqos-omap-obj-y = $(libqos-obj-y) tests/libqos/i2c-omap.o
libqos-imx-obj-y = $(libqos-obj-y) tests/libqos/i2c-imx.o
libqos-usb-obj-y = $(libqos-pc-obj-y) tests/libqos/usb.o
libqos-virtio-obj-y = $(libqos-pc-obj-y) tests/libqos/virtio.o tests/libqos/virtio-pci.o tests/libqos/virtio-mmio.o tests/libqos/malloc-generic.o

tests/device-introspect-test$(EXESUF): tests/device-introspect-test.o
tests/rtc-test$(EXESUF): tests/rtc-test.o
tests/m48t59-test$(EXESUF): tests/m48t59-test.o
tests/endianness-test$(EXESUF): tests/endianness-test.o
tests/spapr-phb-test$(EXESUF): tests/spapr-phb-test.o $(libqos-obj-y)
tests/fdc-test$(EXESUF): tests/fdc-test.o
tests/ide-test$(EXESUF): tests/ide-test.o $(libqos-pc-obj-y)
tests/ahci-test$(EXESUF): tests/ahci-test.o $(libqos-pc-obj-y)
tests/ipmi-kcs-test$(EXESUF): tests/ipmi-kcs-test.o
tests/ipmi-bt-test$(EXESUF): tests/ipmi-bt-test.o
tests/hd-geo-test$(EXESUF): tests/hd-geo-test.o
tests/boot-order-test$(EXESUF): tests/boot-order-test.o $(libqos-obj-y)
tests/bios-tables-test$(EXESUF): tests/bios-tables-test.o \
	tests/boot-sector.o $(libqos-obj-y)
tests/pxe-test$(EXESUF): tests/pxe-test.o tests/boot-sector.o $(libqos-obj-y)
tests/tmp105-test$(EXESUF): tests/tmp105-test.o $(libqos-omap-obj-y)
tests/ds1338-test$(EXESUF): tests/ds1338-test.o $(libqos-imx-obj-y)
tests/i440fx-test$(EXESUF): tests/i440fx-test.o $(libqos-pc-obj-y)
tests/q35-test$(EXESUF): tests/q35-test.o $(libqos-pc-obj-y)
tests/fw_cfg-test$(EXESUF): tests/fw_cfg-test.o $(libqos-pc-obj-y)
tests/e1000-test$(EXESUF): tests/e1000-test.o
tests/rtl8139-test$(EXESUF): tests/rtl8139-test.o $(libqos-pc-obj-y)
tests/pcnet-test$(EXESUF): tests/pcnet-test.o
tests/eepro100-test$(EXESUF): tests/eepro100-test.o
tests/vmxnet3-test$(EXESUF): tests/vmxnet3-test.o
tests/ne2000-test$(EXESUF): tests/ne2000-test.o
tests/wdt_ib700-test$(EXESUF): tests/wdt_ib700-test.o
tests/tco-test$(EXESUF): tests/tco-test.o $(libqos-pc-obj-y)
tests/virtio-balloon-test$(EXESUF): tests/virtio-balloon-test.o
tests/virtio-blk-test$(EXESUF): tests/virtio-blk-test.o $(libqos-virtio-obj-y)
tests/virtio-net-test$(EXESUF): tests/virtio-net-test.o $(libqos-pc-obj-y) $(libqos-virtio-obj-y)
tests/virtio-rng-test$(EXESUF): tests/virtio-rng-test.o $(libqos-pc-obj-y)
tests/virtio-scsi-test$(EXESUF): tests/virtio-scsi-test.o $(libqos-virtio-obj-y)
tests/virtio-9p-test$(EXESUF): tests/virtio-9p-test.o
tests/virtio-serial-test$(EXESUF): tests/virtio-serial-test.o
tests/virtio-console-test$(EXESUF): tests/virtio-console-test.o
tests/tpci200-test$(EXESUF): tests/tpci200-test.o
tests/display-vga-test$(EXESUF): tests/display-vga-test.o
tests/ipoctal232-test$(EXESUF): tests/ipoctal232-test.o
tests/qom-test$(EXESUF): tests/qom-test.o
tests/drive_del-test$(EXESUF): tests/drive_del-test.o $(libqos-pc-obj-y)
tests/qdev-monitor-test$(EXESUF): tests/qdev-monitor-test.o $(libqos-pc-obj-y)
tests/nvme-test$(EXESUF): tests/nvme-test.o
tests/pvpanic-test$(EXESUF): tests/pvpanic-test.o
tests/i82801b11-test$(EXESUF): tests/i82801b11-test.o
tests/ac97-test$(EXESUF): tests/ac97-test.o
tests/es1370-test$(EXESUF): tests/es1370-test.o
tests/intel-hda-test$(EXESUF): tests/intel-hda-test.o
tests/ioh3420-test$(EXESUF): tests/ioh3420-test.o
tests/usb-hcd-ohci-test$(EXESUF): tests/usb-hcd-ohci-test.o $(libqos-usb-obj-y)
tests/usb-hcd-uhci-test$(EXESUF): tests/usb-hcd-uhci-test.o $(libqos-usb-obj-y)
tests/usb-hcd-ehci-test$(EXESUF): tests/usb-hcd-ehci-test.o $(libqos-usb-obj-y)
tests/usb-hcd-xhci-test$(EXESUF): tests/usb-hcd-xhci-test.o $(libqos-usb-obj-y)
tests/pc-cpu-test$(EXESUF): tests/pc-cpu-test.o
tests/vhost-user-test$(EXESUF): tests/vhost-user-test.o qemu-char.o qemu-timer.o $(qtest-obj-y) $(test-io-obj-y)
tests/qemu-iotests/socket_scm_helper$(EXESUF): tests/qemu-iotests/socket_scm_helper.o
tests/test-qemu-opts$(EXESUF): tests/test-qemu-opts.o $(test-util-obj-y)
tests/test-write-threshold$(EXESUF): tests/test-write-threshold.o $(test-block-obj-y)
tests/test-netfilter$(EXESUF): tests/test-netfilter.o $(qtest-obj-y)
tests/test-filter-mirror$(EXESUF): tests/test-filter-mirror.o $(qtest-obj-y)
tests/test-filter-redirector$(EXESUF): tests/test-filter-redirector.o $(qtest-obj-y)
tests/ivshmem-test$(EXESUF): tests/ivshmem-test.o contrib/ivshmem-server/ivshmem-server.o $(libqos-pc-obj-y)
tests/vhost-user-bridge$(EXESUF): tests/vhost-user-bridge.o

ifeq ($(CONFIG_POSIX),y)
LIBS += -lutil
endif

# QTest rules

TARGETS=$(patsubst %-softmmu,%, $(filter %-softmmu,$(TARGET_DIRS)))
ifeq ($(CONFIG_POSIX),y)
QTEST_TARGETS = $(TARGETS)
check-qtest-y=$(foreach TARGET,$(TARGETS), $(check-qtest-$(TARGET)-y))
check-qtest-y += $(check-qtest-generic-y)
else
QTEST_TARGETS =
endif

qtest-obj-y = tests/libqtest.o $(test-util-obj-y)
$(check-qtest-y): $(qtest-obj-y)

tests/test-qga: tests/test-qga.o $(qtest-obj-y)

.PHONY: check-help
check-help:
	@echo "Regression testing targets:"
	@echo
	@echo " make check                Run all tests"
	@echo " make check-qtest-TARGET   Run qtest tests for given target"
	@echo " make check-qtest          Run qtest tests"
	@echo " make check-unit           Run qobject tests"
	@echo " make check-qapi-schema    Run QAPI schema tests"
	@echo " make check-block          Run block tests"
	@echo " make check-report.html    Generates an HTML test report"
	@echo " make check-clean          Clean the tests"
	@echo
	@echo "Please note that HTML reports do not regenerate if the unit tests"
	@echo "has not changed."
	@echo
	@echo "The variable SPEED can be set to control the gtester speed setting."
	@echo "Default options are -k and (for make V=1) --verbose; they can be"
	@echo "changed with variable GTESTER_OPTIONS."

SPEED = quick
GTESTER_OPTIONS = -k $(if $(V),--verbose,-q)
GCOV_OPTIONS = -n $(if $(V),-f,)

# gtester tests, possibly with verbose output

.PHONY: $(patsubst %, check-qtest-%, $(QTEST_TARGETS))
$(patsubst %, check-qtest-%, $(QTEST_TARGETS)): check-qtest-%: $(check-qtest-y)
	$(if $(CONFIG_GCOV),@rm -f *.gcda */*.gcda */*/*.gcda */*/*/*.gcda,)
	$(call quiet-command,QTEST_QEMU_BINARY=$*-softmmu/qemu-system-$* \
		QTEST_QEMU_IMG=qemu-img$(EXESUF) \
		MALLOC_PERTURB_=$${MALLOC_PERTURB_:-$$((RANDOM % 255 + 1))} \
		gtester $(GTESTER_OPTIONS) -m=$(SPEED) $(check-qtest-$*-y) $(check-qtest-generic-y),"GTESTER $@")
	$(if $(CONFIG_GCOV),@for f in $(gcov-files-$*-y) $(gcov-files-generic-y); do \
	  echo Gcov report for $$f:;\
	  $(GCOV) $(GCOV_OPTIONS) $$f -o `dirname $$f`; \
	done,)

.PHONY: $(patsubst %, check-%, $(check-unit-y))
$(patsubst %, check-%, $(check-unit-y)): check-%: %
	$(if $(CONFIG_GCOV),@rm -f *.gcda */*.gcda */*/*.gcda */*/*/*.gcda,)
	$(call quiet-command, \
		MALLOC_PERTURB_=$${MALLOC_PERTURB_:-$$((RANDOM % 255 + 1))} \
		gtester $(GTESTER_OPTIONS) -m=$(SPEED) $*,"GTESTER $*")
	$(if $(CONFIG_GCOV),@for f in $(gcov-files-$(subst tests/,,$*)-y) $(gcov-files-generic-y); do \
	  echo Gcov report for $$f:;\
	  $(GCOV) $(GCOV_OPTIONS) $$f -o `dirname $$f`; \
	done,)

# gtester tests with XML output

$(patsubst %, check-report-qtest-%.xml, $(QTEST_TARGETS)): check-report-qtest-%.xml: $(check-qtest-y)
	$(call quiet-command,QTEST_QEMU_BINARY=$*-softmmu/qemu-system-$* \
		QTEST_QEMU_IMG=qemu-img$(EXESUF) \
	  gtester -q $(GTESTER_OPTIONS) -o $@ -m=$(SPEED) $(check-qtest-$*-y) $(check-qtest-generic-y),"GTESTER $@")

check-report-unit.xml: $(check-unit-y)
	$(call quiet-command,gtester -q $(GTESTER_OPTIONS) -o $@ -m=$(SPEED) $^, "GTESTER $@")

# Reports and overall runs

check-report.xml: $(patsubst %,check-report-qtest-%.xml, $(QTEST_TARGETS)) check-report-unit.xml
	$(call quiet-command,$(SRC_PATH)/scripts/gtester-cat $^ > $@, "  GEN   $@")

check-report.html: check-report.xml
	$(call quiet-command,gtester-report $< > $@, "  GEN   $@")


# Other tests

QEMU_IOTESTS_HELPERS-$(CONFIG_LINUX) = tests/qemu-iotests/socket_scm_helper$(EXESUF)

.PHONY: check-tests/qemu-iotests-quick.sh
check-tests/qemu-iotests-quick.sh: tests/qemu-iotests-quick.sh qemu-img$(EXESUF) qemu-io$(EXESUF) $(QEMU_IOTESTS_HELPERS-y)
	$<

.PHONY: check-tests/test-qapi.py
check-tests/test-qapi.py: tests/test-qapi.py

.PHONY: $(patsubst %, check-%, $(check-qapi-schema-y))
$(patsubst %, check-%, $(check-qapi-schema-y)): check-%.json: $(SRC_PATH)/%.json
	$(call quiet-command, PYTHONPATH=$(SRC_PATH)/scripts \
		$(PYTHON) $(SRC_PATH)/tests/qapi-schema/test-qapi.py \
		$^ >$*.test.out 2>$*.test.err; \
		echo $$? >$*.test.exit, \
		"  TEST  $*.out")
	@diff -q $(SRC_PATH)/$*.out $*.test.out
	@# Sanitize error messages (make them independent of build directory)
	@perl -p -e 's|\Q$(SRC_PATH)\E/||g' $*.test.err | diff -q $(SRC_PATH)/$*.err -
	@diff -q $(SRC_PATH)/$*.exit $*.test.exit

# Consolidated targets

.PHONY: check-qapi-schema check-qtest check-unit check check-clean
check-qapi-schema: $(patsubst %,check-%, $(check-qapi-schema-y))
check-qtest: $(patsubst %,check-qtest-%, $(QTEST_TARGETS))
check-unit: $(patsubst %,check-%, $(check-unit-y))
check-block: $(patsubst %,check-%, $(check-block-y))
check: check-qapi-schema check-unit check-qtest
check-clean:
	$(MAKE) -C tests/tcg clean
	rm -rf $(check-unit-y) tests/*.o $(QEMU_IOTESTS_HELPERS-y)
	rm -rf $(sort $(foreach target,$(SYSEMU_TARGET_LIST), $(check-qtest-$(target)-y)) $(check-qtest-generic-y))

clean: check-clean

# Build the help program automatically

all: $(QEMU_IOTESTS_HELPERS-y)

-include $(wildcard tests/*.d)
-include $(wildcard tests/libqos/*.d)
