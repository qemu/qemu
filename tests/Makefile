export SRC_PATH

check-unit-y = tests/check-qdict$(EXESUF)
check-unit-y += tests/check-qfloat$(EXESUF)
check-unit-y += tests/check-qint$(EXESUF)
check-unit-y += tests/check-qstring$(EXESUF)
check-unit-y += tests/check-qlist$(EXESUF)
check-unit-y += tests/check-qjson$(EXESUF)
check-unit-y += tests/test-qmp-output-visitor$(EXESUF)
check-unit-y += tests/test-qmp-input-visitor$(EXESUF)
check-unit-y += tests/test-qmp-input-strict$(EXESUF)
check-unit-y += tests/test-qmp-commands$(EXESUF)
check-unit-y += tests/test-string-input-visitor$(EXESUF)
check-unit-y += tests/test-string-output-visitor$(EXESUF)
check-unit-y += tests/test-coroutine$(EXESUF)
check-unit-y += tests/test-visitor-serialization$(EXESUF)
check-unit-y += tests/test-iov$(EXESUF)

check-block-$(CONFIG_POSIX) += tests/qemu-iotests-quick.sh

# All QTests for now are POSIX-only, but the dependencies are
# really in libqtest, not in the testcases themselves.
check-qtest-i386-y = tests/fdc-test$(EXESUF)
check-qtest-i386-y += tests/hd-geo-test$(EXESUF)
check-qtest-i386-y += tests/rtc-test$(EXESUF)
check-qtest-x86_64-y = $(check-qtest-i386-y)
check-qtest-sparc-y = tests/m48t59-test$(EXESUF)
check-qtest-sparc64-y = tests/m48t59-test$(EXESUF)

GENERATED_HEADERS += tests/test-qapi-types.h tests/test-qapi-visit.h tests/test-qmp-commands.h

test-obj-y = tests/check-qint.o tests/check-qstring.o tests/check-qdict.o \
	tests/check-qlist.o tests/check-qfloat.o tests/check-qjson.o \
	tests/test-coroutine.o tests/test-string-output-visitor.o \
	tests/test-string-input-visitor.o tests/test-qmp-output-visitor.o \
	tests/test-qmp-input-visitor.o tests/test-qmp-input-strict.o \
	tests/test-qmp-commands.o tests/test-visitor-serialization.o

test-qapi-obj-y =  $(qobject-obj-y) $(qapi-obj-y) qemu-tool.o
test-qapi-obj-y += tests/test-qapi-visit.o tests/test-qapi-types.o
test-qapi-obj-y += module.o

$(test-obj-y): QEMU_INCLUDES += -Itests

tests/check-qint$(EXESUF): tests/check-qint.o qint.o
tests/check-qstring$(EXESUF): tests/check-qstring.o qstring.o
tests/check-qdict$(EXESUF): tests/check-qdict.o qdict.o qfloat.o qint.o qstring.o qbool.o qlist.o
tests/check-qlist$(EXESUF): tests/check-qlist.o qlist.o qint.o
tests/check-qfloat$(EXESUF): tests/check-qfloat.o qfloat.o
tests/check-qjson$(EXESUF): tests/check-qjson.o $(qobject-obj-y) qemu-tool.o
tests/test-coroutine$(EXESUF): tests/test-coroutine.o $(coroutine-obj-y) $(tools-obj-y) $(block-obj-y) iov.o libqemustub.a
tests/test-iov$(EXESUF): tests/test-iov.o iov.o

tests/test-qapi-types.c tests/test-qapi-types.h :\
$(SRC_PATH)/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-types.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-types.py $(gen-out-type) -o tests -p "test-" < $<, "  GEN   $@")
tests/test-qapi-visit.c tests/test-qapi-visit.h :\
$(SRC_PATH)/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-visit.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-visit.py $(gen-out-type) -o tests -p "test-" < $<, "  GEN   $@")
tests/test-qmp-commands.h tests/test-qmp-marshal.c :\
$(SRC_PATH)/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-commands.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-commands.py $(gen-out-type) -o tests -p "test-" < $<, "  GEN   $@")


tests/test-string-output-visitor$(EXESUF): tests/test-string-output-visitor.o $(test-qapi-obj-y)
tests/test-string-input-visitor$(EXESUF): tests/test-string-input-visitor.o $(test-qapi-obj-y)
tests/test-qmp-output-visitor$(EXESUF): tests/test-qmp-output-visitor.o $(test-qapi-obj-y)
tests/test-qmp-input-visitor$(EXESUF): tests/test-qmp-input-visitor.o $(test-qapi-obj-y)
tests/test-qmp-input-strict$(EXESUF): tests/test-qmp-input-strict.o $(test-qapi-obj-y)
tests/test-qmp-commands$(EXESUF): tests/test-qmp-commands.o tests/test-qmp-marshal.o $(test-qapi-obj-y)
tests/test-visitor-serialization$(EXESUF): tests/test-visitor-serialization.o $(test-qapi-obj-y)

tests/rtc-test$(EXESUF): tests/rtc-test.o $(trace-obj-y)
tests/m48t59-test$(EXESUF): tests/m48t59-test.o $(trace-obj-y)
tests/fdc-test$(EXESUF): tests/fdc-test.o tests/libqtest.o $(trace-obj-y)
tests/hd-geo-test$(EXESUF): tests/hd-geo-test.o tests/libqtest.o $(trace-obj-y)

# QTest rules

TARGETS=$(patsubst %-softmmu,%, $(filter %-softmmu,$(TARGET_DIRS)))
QTEST_TARGETS=$(foreach TARGET,$(TARGETS), $(if $(check-qtest-$(TARGET)-y), $(TARGET),))
check-qtest-$(CONFIG_POSIX)=$(foreach TARGET,$(TARGETS), $(check-qtest-$(TARGET)-y))

qtest-obj-y = tests/libqtest.o $(oslib-obj-y) libqemustub.a
$(check-qtest-y): $(qtest-obj-y)

.PHONY: check-help
check-help:
	@echo "Regression testing targets:"
	@echo
	@echo " make check                Run all tests"
	@echo " make check-qtest-TARGET   Run qtest tests for given target"
	@echo " make check-qtest          Run qtest tests"
	@echo " make check-unit           Run qobject tests"
	@echo " make check-block          Run block tests"
	@echo " make check-report.html    Generates an HTML test report"
	@echo
	@echo "Please note that HTML reports do not regenerate if the unit tests"
	@echo "has not changed."
	@echo
	@echo "The variable SPEED can be set to control the gtester speed setting."
	@echo "Default options are -k and (for make V=1) --verbose; they can be"
	@echo "changed with variable GTESTER_OPTIONS."

SPEED = quick
GTESTER_OPTIONS = -k $(if $(V),--verbose,-q)

# gtester tests, possibly with verbose output

.PHONY: $(patsubst %, check-qtest-%, $(QTEST_TARGETS))
$(patsubst %, check-qtest-%, $(QTEST_TARGETS)): check-qtest-%: $(check-qtest-y)
	$(call quiet-command,QTEST_QEMU_BINARY=$*-softmmu/qemu-system-$* \
		gtester $(GTESTER_OPTIONS) -m=$(SPEED) $(check-qtest-$*-y),"GTESTER $@")

.PHONY: $(patsubst %, check-%, $(check-unit-y))
$(patsubst %, check-%, $(check-unit-y)): check-%: %
	$(call quiet-command,gtester $(GTESTER_OPTIONS) -m=$(SPEED) $*,"GTESTER $*")

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

.PHONY: check-tests/qemu-iotests-quick.sh
check-tests/qemu-iotests-quick.sh: tests/qemu-iotests-quick.sh qemu-img$(EXESUF) qemu-io$(EXESUF)
	$<

# Consolidated targets

.PHONY: check-qtest check-unit check
check-qtest: $(patsubst %,check-qtest-%, $(QTEST_TARGETS))
check-unit: $(patsubst %,check-%, $(check-unit-y))
check-block: $(patsubst %,check-%, $(check-block-y))
check: check-unit check-qtest

-include $(wildcard tests/*.d)
