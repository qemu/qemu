cstart.o = $(TEST_DIR)/cstart64.o
bits = 64
ldarch = elf64-x86-64
CFLAGS += -mno-red-zone

tests = $(TEST_DIR)/access.flat $(TEST_DIR)/apic.flat \
	  $(TEST_DIR)/emulator.flat $(TEST_DIR)/idt_test.flat \
	  $(TEST_DIR)/xsave.flat $(TEST_DIR)/rmap_chain.flat \
	  $(TEST_DIR)/pcid.flat $(TEST_DIR)/debug.flat \
	  $(TEST_DIR)/ioapic.flat
tests += $(TEST_DIR)/svm.flat
tests += $(TEST_DIR)/vmx.flat
tests += $(TEST_DIR)/tscdeadline_latency.flat

include config/config-x86-common.mak
