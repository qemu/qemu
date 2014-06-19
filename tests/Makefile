export SRC_PATH

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
check-unit-y += tests/test-x86-cpuid$(EXESUF)
# all code tested by test-x86-cpuid is inside topology.h
gcov-files-test-x86-cpuid-y =
check-unit-y += tests/test-xbzrle$(EXESUF)
gcov-files-test-xbzrle-y = xbzrle.c
check-unit-y += tests/test-cutils$(EXESUF)
gcov-files-test-cutils-y += util/cutils.c
check-unit-y += tests/test-mul64$(EXESUF)
gcov-files-test-mul64-y = util/host-utils.c
check-unit-y += tests/test-int128$(EXESUF)
# all code tested by test-int128 is inside int128.h
gcov-files-test-int128-y =
check-unit-y += tests/test-bitops$(EXESUF)
check-unit-y += tests/test-qdev-global-props$(EXESUF)
check-unit-y += tests/check-qom-interface$(EXESUF)
gcov-files-check-qom-interface-y = qom/object.c
check-unit-$(CONFIG_POSIX) += tests/test-vmstate$(EXESUF)
check-unit-y += tests/test-qemu-opts$(EXESUF)
gcov-files-test-qemu-opts-y = qom/test-qemu-opts.c

check-block-$(CONFIG_POSIX) += tests/qemu-iotests-quick.sh

# All QTests for now are POSIX-only, but the dependencies are
# really in libqtest, not in the testcases themselves.

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
check-qtest-pci-y += tests/intel-hda-test$(EXESUF)
gcov-files-pci-y += hw/audio/intel-hda.c hw/audio/hda-codec.c

check-qtest-i386-y = tests/endianness-test$(EXESUF)
check-qtest-i386-y += tests/fdc-test$(EXESUF)
gcov-files-i386-y = hw/block/fdc.c
check-qtest-i386-y += tests/ide-test$(EXESUF)
check-qtest-i386-y += tests/hd-geo-test$(EXESUF)
gcov-files-i386-y += hw/block/hd-geometry.c
check-qtest-i386-y += tests/boot-order-test$(EXESUF)
check-qtest-i386-y += tests/bios-tables-test$(EXESUF)
check-qtest-i386-y += tests/rtc-test$(EXESUF)
check-qtest-i386-y += tests/i440fx-test$(EXESUF)
check-qtest-i386-y += tests/fw_cfg-test$(EXESUF)
check-qtest-i386-y += tests/blockdev-test$(EXESUF)
check-qtest-i386-y += tests/qdev-monitor-test$(EXESUF)
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
check-qtest-i386-y += tests/usb-hcd-ehci-test$(EXESUF)
gcov-files-i386-y += hw/usb/hcd-ehci.c
gcov-files-i386-y += hw/usb/hcd-uhci.c
gcov-files-i386-y += hw/usb/dev-hid.c
gcov-files-i386-y += hw/usb/dev-storage.c
check-qtest-i386-$(CONFIG_POSIX) += tests/vhost-user-test$(EXESUF)
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
gcov-files-arm-y += hw/misc/tmp105.c
check-qtest-ppc-y += tests/boot-order-test$(EXESUF)
check-qtest-ppc64-y += tests/boot-order-test$(EXESUF)
check-qtest-ppc64-y += tests/spapr-phb-test$(EXESUF)
gcov-files-ppc64-y += ppc64-softmmu/hw/ppc/spapr_pci.c
check-qtest-microblazeel-y = $(check-qtest-microblaze-y)
check-qtest-xtensaeb-y = $(check-qtest-xtensa-y)

# qom-test works for all sysemu architectures:
$(foreach target,$(SYSEMU_TARGET_LIST), \
    $(eval check-qtest-$(target)-y += tests/qom-test$(EXESUF)))

check-qapi-schema-y := $(addprefix tests/qapi-schema/, \
        comments.json empty.json funny-char.json indented-expr.json \
        missing-colon.json missing-comma-list.json \
        missing-comma-object.json non-objects.json \
        qapi-schema-test.json quoted-structural-chars.json \
        trailing-comma-list.json trailing-comma-object.json \
        unclosed-list.json unclosed-object.json unclosed-string.json \
        duplicate-key.json union-invalid-base.json flat-union-no-base.json \
        flat-union-invalid-discriminator.json \
        flat-union-invalid-branch-key.json flat-union-reverse-define.json \
        flat-union-string-discriminator.json \
        include-simple.json include-relpath.json include-format-err.json \
        include-non-file.json include-no-file.json include-before-err.json \
        include-nested-err.json include-self-cycle.json include-cycle.json \
        include-repetition.json event-nest-struct.json)

GENERATED_HEADERS += tests/test-qapi-types.h tests/test-qapi-visit.h \
                     tests/test-qmp-commands.h tests/test-qapi-event.h

test-obj-y = tests/check-qint.o tests/check-qstring.o tests/check-qdict.o \
	tests/check-qlist.o tests/check-qfloat.o tests/check-qjson.o \
	tests/test-coroutine.o tests/test-string-output-visitor.o \
	tests/test-string-input-visitor.o tests/test-qmp-output-visitor.o \
	tests/test-qmp-input-visitor.o tests/test-qmp-input-strict.o \
	tests/test-qmp-commands.o tests/test-visitor-serialization.o \
	tests/test-x86-cpuid.o tests/test-mul64.o tests/test-int128.o \
	tests/test-opts-visitor.o tests/test-qmp-event.o

test-qapi-obj-y = tests/test-qapi-visit.o tests/test-qapi-types.o \
                  tests/test-qapi-event.o

$(test-obj-y): QEMU_INCLUDES += -Itests
QEMU_CFLAGS += -I$(SRC_PATH)/tests
qom-core-obj = qom/object.o qom/qom-qobject.o qom/container.o

tests/test-x86-cpuid.o: QEMU_INCLUDES += -I$(SRC_PATH)/target-i386

tests/check-qint$(EXESUF): tests/check-qint.o libqemuutil.a
tests/check-qstring$(EXESUF): tests/check-qstring.o libqemuutil.a
tests/check-qdict$(EXESUF): tests/check-qdict.o libqemuutil.a
tests/check-qlist$(EXESUF): tests/check-qlist.o libqemuutil.a
tests/check-qfloat$(EXESUF): tests/check-qfloat.o libqemuutil.a
tests/check-qjson$(EXESUF): tests/check-qjson.o libqemuutil.a libqemustub.a
tests/check-qom-interface$(EXESUF): tests/check-qom-interface.o $(qom-core-obj) libqemuutil.a libqemustub.a
tests/test-coroutine$(EXESUF): tests/test-coroutine.o $(block-obj-y) libqemuutil.a libqemustub.a
tests/test-aio$(EXESUF): tests/test-aio.o $(block-obj-y) libqemuutil.a libqemustub.a
tests/test-rfifolock$(EXESUF): tests/test-rfifolock.o libqemuutil.a libqemustub.a
tests/test-throttle$(EXESUF): tests/test-throttle.o $(block-obj-y) libqemuutil.a libqemustub.a
tests/test-thread-pool$(EXESUF): tests/test-thread-pool.o $(block-obj-y) libqemuutil.a libqemustub.a
tests/test-iov$(EXESUF): tests/test-iov.o libqemuutil.a
tests/test-hbitmap$(EXESUF): tests/test-hbitmap.o libqemuutil.a libqemustub.a
tests/test-x86-cpuid$(EXESUF): tests/test-x86-cpuid.o
tests/test-xbzrle$(EXESUF): tests/test-xbzrle.o xbzrle.o page_cache.o libqemuutil.a
tests/test-cutils$(EXESUF): tests/test-cutils.o util/cutils.o
tests/test-int128$(EXESUF): tests/test-int128.o
tests/test-qdev-global-props$(EXESUF): tests/test-qdev-global-props.o \
	hw/core/qdev.o hw/core/qdev-properties.o hw/core/hotplug.o\
	hw/core/irq.o \
	hw/core/fw-path-provider.o \
	$(qom-core-obj) \
	$(test-qapi-obj-y) \
	libqemuutil.a libqemustub.a
tests/test-vmstate$(EXESUF): tests/test-vmstate.o \
	vmstate.o qemu-file.o \
	libqemuutil.a

tests/test-qapi-types.c tests/test-qapi-types.h :\
$(SRC_PATH)/tests/qapi-schema/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-types.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-types.py \
		$(gen-out-type) -o tests -p "test-" -i $<, \
		"  GEN   $@")
tests/test-qapi-visit.c tests/test-qapi-visit.h :\
$(SRC_PATH)/tests/qapi-schema/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-visit.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-visit.py \
		$(gen-out-type) -o tests -p "test-" -i $<, \
		"  GEN   $@")
tests/test-qmp-commands.h tests/test-qmp-marshal.c :\
$(SRC_PATH)/tests/qapi-schema/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-commands.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-commands.py \
		$(gen-out-type) -o tests -p "test-" -i $<, \
		"  GEN   $@")
tests/test-qapi-event.c tests/test-qapi-event.h :\
$(SRC_PATH)/tests/qapi-schema/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-event.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-event.py \
		$(gen-out-type) -o tests -p "test-" -i $<, \
		"  GEN   $@")

tests/test-string-output-visitor$(EXESUF): tests/test-string-output-visitor.o $(test-qapi-obj-y) libqemuutil.a libqemustub.a
tests/test-string-input-visitor$(EXESUF): tests/test-string-input-visitor.o $(test-qapi-obj-y) libqemuutil.a libqemustub.a
tests/test-qmp-event$(EXESUF): tests/test-qmp-event.o $(test-qapi-obj-y) libqemuutil.a libqemustub.a
tests/test-qmp-output-visitor$(EXESUF): tests/test-qmp-output-visitor.o $(test-qapi-obj-y) libqemuutil.a libqemustub.a
tests/test-qmp-input-visitor$(EXESUF): tests/test-qmp-input-visitor.o $(test-qapi-obj-y) libqemuutil.a libqemustub.a
tests/test-qmp-input-strict$(EXESUF): tests/test-qmp-input-strict.o $(test-qapi-obj-y) libqemuutil.a libqemustub.a
tests/test-qmp-commands$(EXESUF): tests/test-qmp-commands.o tests/test-qmp-marshal.o $(test-qapi-obj-y) qapi-types.o qapi-visit.o libqemuutil.a libqemustub.a
tests/test-visitor-serialization$(EXESUF): tests/test-visitor-serialization.o $(test-qapi-obj-y) libqemuutil.a libqemustub.a
tests/test-opts-visitor$(EXESUF): tests/test-opts-visitor.o $(test-qapi-obj-y) libqemuutil.a libqemustub.a

tests/test-mul64$(EXESUF): tests/test-mul64.o libqemuutil.a
tests/test-bitops$(EXESUF): tests/test-bitops.o libqemuutil.a

libqos-obj-y = tests/libqos/pci.o tests/libqos/fw_cfg.o
libqos-obj-y += tests/libqos/i2c.o
libqos-pc-obj-y = $(libqos-obj-y) tests/libqos/pci-pc.o
libqos-pc-obj-y += tests/libqos/malloc-pc.o
libqos-omap-obj-y = $(libqos-obj-y) tests/libqos/i2c-omap.o

tests/rtc-test$(EXESUF): tests/rtc-test.o
tests/m48t59-test$(EXESUF): tests/m48t59-test.o
tests/endianness-test$(EXESUF): tests/endianness-test.o
tests/spapr-phb-test$(EXESUF): tests/spapr-phb-test.o $(libqos-obj-y)
tests/fdc-test$(EXESUF): tests/fdc-test.o
tests/ide-test$(EXESUF): tests/ide-test.o $(libqos-pc-obj-y)
tests/hd-geo-test$(EXESUF): tests/hd-geo-test.o
tests/boot-order-test$(EXESUF): tests/boot-order-test.o $(libqos-obj-y)
tests/bios-tables-test$(EXESUF): tests/bios-tables-test.o $(libqos-obj-y)
tests/tmp105-test$(EXESUF): tests/tmp105-test.o $(libqos-omap-obj-y)
tests/i440fx-test$(EXESUF): tests/i440fx-test.o $(libqos-pc-obj-y)
tests/fw_cfg-test$(EXESUF): tests/fw_cfg-test.o $(libqos-pc-obj-y)
tests/e1000-test$(EXESUF): tests/e1000-test.o
tests/rtl8139-test$(EXESUF): tests/rtl8139-test.o
tests/pcnet-test$(EXESUF): tests/pcnet-test.o
tests/eepro100-test$(EXESUF): tests/eepro100-test.o
tests/vmxnet3-test$(EXESUF): tests/vmxnet3-test.o
tests/ne2000-test$(EXESUF): tests/ne2000-test.o
tests/virtio-balloon-test$(EXESUF): tests/virtio-balloon-test.o
tests/virtio-blk-test$(EXESUF): tests/virtio-blk-test.o
tests/virtio-net-test$(EXESUF): tests/virtio-net-test.o
tests/virtio-rng-test$(EXESUF): tests/virtio-rng-test.o
tests/virtio-scsi-test$(EXESUF): tests/virtio-scsi-test.o
tests/virtio-9p-test$(EXESUF): tests/virtio-9p-test.o
tests/virtio-serial-test$(EXESUF): tests/virtio-serial-test.o
tests/virtio-console-test$(EXESUF): tests/virtio-console-test.o
tests/tpci200-test$(EXESUF): tests/tpci200-test.o
tests/display-vga-test$(EXESUF): tests/display-vga-test.o
tests/ipoctal232-test$(EXESUF): tests/ipoctal232-test.o
tests/qom-test$(EXESUF): tests/qom-test.o
tests/blockdev-test$(EXESUF): tests/blockdev-test.o $(libqos-pc-obj-y)
tests/qdev-monitor-test$(EXESUF): tests/qdev-monitor-test.o $(libqos-pc-obj-y)
tests/nvme-test$(EXESUF): tests/nvme-test.o
tests/pvpanic-test$(EXESUF): tests/pvpanic-test.o
tests/i82801b11-test$(EXESUF): tests/i82801b11-test.o
tests/ac97-test$(EXESUF): tests/ac97-test.o
tests/es1370-test$(EXESUF): tests/es1370-test.o
tests/intel-hda-test$(EXESUF): tests/intel-hda-test.o
tests/ioh3420-test$(EXESUF): tests/ioh3420-test.o
tests/usb-hcd-ehci-test$(EXESUF): tests/usb-hcd-ehci-test.o $(libqos-pc-obj-y)
tests/vhost-user-test$(EXESUF): tests/vhost-user-test.o qemu-char.o qemu-timer.o $(qtest-obj-y)
tests/qemu-iotests/socket_scm_helper$(EXESUF): tests/qemu-iotests/socket_scm_helper.o
tests/test-qemu-opts$(EXESUF): tests/test-qemu-opts.o libqemuutil.a libqemustub.a

ifeq ($(CONFIG_POSIX),y)
LIBS += -lutil
endif

# QTest rules

TARGETS=$(patsubst %-softmmu,%, $(filter %-softmmu,$(TARGET_DIRS)))
ifeq ($(CONFIG_POSIX),y)
QTEST_TARGETS=$(foreach TARGET,$(TARGETS), $(if $(check-qtest-$(TARGET)-y), $(TARGET),))
check-qtest-y=$(foreach TARGET,$(TARGETS), $(check-qtest-$(TARGET)-y))
endif

qtest-obj-y = tests/libqtest.o libqemuutil.a libqemustub.a
$(check-qtest-y): $(qtest-obj-y)

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
		MALLOC_PERTURB_=$${MALLOC_PERTURB_:-$$((RANDOM % 255 + 1))} \
		gtester $(GTESTER_OPTIONS) -m=$(SPEED) $(check-qtest-$*-y),"GTESTER $@")
	$(if $(CONFIG_GCOV),@for f in $(gcov-files-$*-y); do \
	  echo Gcov report for $$f:;\
	  $(GCOV) $(GCOV_OPTIONS) $$f -o `dirname $$f`; \
	done,)

.PHONY: $(patsubst %, check-%, $(check-unit-y))
$(patsubst %, check-%, $(check-unit-y)): check-%: %
	$(if $(CONFIG_GCOV),@rm -f *.gcda */*.gcda */*/*.gcda */*/*/*.gcda,)
	$(call quiet-command, \
		MALLOC_PERTURB_=$${MALLOC_PERTURB_:-$$((RANDOM % 255 + 1))} \
		gtester $(GTESTER_OPTIONS) -m=$(SPEED) $*,"GTESTER $*")
	$(if $(CONFIG_GCOV),@for f in $(gcov-files-$(subst tests/,,$*)-y); do \
	  echo Gcov report for $$f:;\
	  $(GCOV) $(GCOV_OPTIONS) $$f -o `dirname $$f`; \
	done,)

# gtester tests with XML output

$(patsubst %, check-report-qtest-%.xml, $(QTEST_TARGETS)): check-report-qtest-%.xml: $(check-qtest-y)
	$(call quiet-command,QTEST_QEMU_BINARY=$*-softmmu/qemu-system-$* \
	  gtester -q $(GTESTER_OPTIONS) -o $@ -m=$(SPEED) $(check-qtest-$*-y),"GTESTER $@")

check-report-unit.xml: $(check-unit-y)
	$(call quiet-command,gtester -q $(GTESTER_OPTIONS) -o $@ -m=$(SPEED) $^, "GTESTER $@")

# Reports and overall runs

check-report.xml: $(patsubst %,check-report-qtest-%.xml, $(QTEST_TARGETS)) check-report-unit.xml
	$(call quiet-command,$(SRC_PATH)/scripts/gtester-cat $^ > $@, "  GEN    $@")

check-report.html: check-report.xml
	$(call quiet-command,gtester-report $< > $@, "  GEN    $@")


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
	rm -rf $(sort $(foreach target,$(SYSEMU_TARGET_LIST), $(check-qtest-$(target)-y)))

clean: check-clean

# Build the help program automatically

all: $(QEMU_IOTESTS_HELPERS-y)

-include $(wildcard tests/*.d)
-include $(wildcard tests/libqos/*.d)
