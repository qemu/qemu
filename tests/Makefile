export SRC_PATH

CHECKS = check-qdict check-qfloat check-qint check-qstring check-qlist
CHECKS += check-qjson test-qmp-output-visitor test-qmp-input-visitor
CHECKS += test-string-input-visitor test-string-output-visitor test-coroutine
CHECKS += $(SRC_PATH)/tests/qemu-iotests-quick.sh

check-qint.o check-qstring.o check-qdict.o check-qlist.o check-qfloat.o check-qjson.o test-coroutine.o: $(GENERATED_HEADERS)

check-qint: check-qint.o qint.o $(tools-obj-y)
check-qstring: check-qstring.o qstring.o $(tools-obj-y)
check-qdict: check-qdict.o qdict.o qfloat.o qint.o qstring.o qbool.o qlist.o $(tools-obj-y)
check-qlist: check-qlist.o qlist.o qint.o $(tools-obj-y)
check-qfloat: check-qfloat.o qfloat.o $(tools-obj-y)
check-qjson: check-qjson.o $(qobject-obj-y) $(tools-obj-y)
test-coroutine: test-coroutine.o qemu-timer-common.o async.o $(coroutine-obj-y) $(tools-obj-y)

test-qmp-input-visitor.o test-qmp-output-visitor.o \
test-string-input-visitor.o test-string-output-visitor.o \
	test-qmp-commands.o qemu-ga$(EXESUF): QEMU_CFLAGS += -I $(qapi-dir)

$(qapi-dir)/test-qapi-types.c $(qapi-dir)/test-qapi-types.h :\
$(SRC_PATH)/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-types.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-types.py $(gen-out-type) -o "$(qapi-dir)" -p "test-" < $<, "  GEN   $@")
$(qapi-dir)/test-qapi-visit.c $(qapi-dir)/test-qapi-visit.h :\
$(SRC_PATH)/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-visit.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-visit.py $(gen-out-type) -o "$(qapi-dir)" -p "test-" < $<, "  GEN   $@")
$(qapi-dir)/test-qmp-commands.h $(qapi-dir)/test-qmp-marshal.c :\
$(SRC_PATH)/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-commands.py
	    $(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-commands.py $(gen-out-type) -o "$(qapi-dir)" -p "test-" < $<, "  GEN   $@")


test-string-output-visitor.o: $(addprefix $(qapi-dir)/, test-qapi-types.c test-qapi-types.h test-qapi-visit.c test-qapi-visit.h) $(qapi-obj-y)
test-string-output-visitor: test-string-output-visitor.o $(qobject-obj-y) $(qapi-obj-y) $(tools-obj-y) $(qapi-dir)/test-qapi-visit.o $(qapi-dir)/test-qapi-types.o

test-string-input-visitor.o: $(addprefix $(qapi-dir)/, test-qapi-types.c test-qapi-types.h test-qapi-visit.c test-qapi-visit.h) $(qapi-obj-y)
test-string-input-visitor: test-string-input-visitor.o $(qobject-obj-y) $(qapi-obj-y) $(tools-obj-y) $(qapi-dir)/test-qapi-visit.o $(qapi-dir)/test-qapi-types.o

test-qmp-output-visitor.o: $(addprefix $(qapi-dir)/, test-qapi-types.c test-qapi-types.h test-qapi-visit.c test-qapi-visit.h) $(qapi-obj-y)
test-qmp-output-visitor: test-qmp-output-visitor.o $(qobject-obj-y) $(qapi-obj-y) $(tools-obj-y) $(qapi-dir)/test-qapi-visit.o $(qapi-dir)/test-qapi-types.o

test-qmp-input-visitor.o: $(addprefix $(qapi-dir)/, test-qapi-types.c test-qapi-types.h test-qapi-visit.c test-qapi-visit.h) $(qapi-obj-y)
test-qmp-input-visitor: test-qmp-input-visitor.o $(qobject-obj-y) $(qapi-obj-y) $(tools-obj-y) $(qapi-dir)/test-qapi-visit.o $(qapi-dir)/test-qapi-types.o

test-qmp-commands.o: $(addprefix $(qapi-dir)/, test-qapi-types.c test-qapi-types.h test-qapi-visit.c test-qapi-visit.h test-qmp-marshal.c test-qmp-commands.h) $(qapi-obj-y)
test-qmp-commands: test-qmp-commands.o $(qobject-obj-y) $(qapi-obj-y) $(tools-obj-y) $(qapi-dir)/test-qapi-visit.o $(qapi-dir)/test-qapi-types.o $(qapi-dir)/test-qmp-marshal.o module.o

$(SRC_PATH)/tests/qemu-iotests-quick.sh: qemu-img qemu-io


.PHONY: check check-block

check: $(CHECKS)
	$(call quiet-command, gtester $(CHECKS), "  CHECK")

check-block:
	$(call quiet-command, $(SHELL) $(SRC_PATH)/tests/check-block.sh , "  CHECK")
