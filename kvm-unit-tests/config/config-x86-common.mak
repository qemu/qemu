#This is a make file with common rules for both x86 & x86-64

all: test_cases

cflatobjs += lib/x86/io.o
cflatobjs += lib/x86/smp.o
cflatobjs += lib/x86/vm.o
cflatobjs += lib/x86/fwcfg.o
cflatobjs += lib/x86/apic.o
cflatobjs += lib/x86/atomic.o
cflatobjs += lib/x86/desc.o
cflatobjs += lib/x86/isr.o
cflatobjs += lib/x86/pci.o

$(libcflat): LDFLAGS += -nostdlib
$(libcflat): CFLAGS += -ffreestanding -I lib

CFLAGS += -m$(bits)
CFLAGS += -O1

libgcc := $(shell $(CC) -m$(bits) --print-libgcc-file-name)

FLATLIBS = lib/libcflat.a $(libgcc)
%.elf: %.o $(FLATLIBS) x86/flat.lds
	$(CC) $(CFLAGS) -nostdlib -o $@ -Wl,-T,x86/flat.lds \
		$(filter %.o, $^) $(FLATLIBS)

%.flat: %.elf
	$(OBJCOPY) -O elf32-i386 $^ $@

tests-common = $(TEST_DIR)/vmexit.flat $(TEST_DIR)/tsc.flat \
               $(TEST_DIR)/smptest.flat  $(TEST_DIR)/port80.flat \
               $(TEST_DIR)/realmode.flat $(TEST_DIR)/msr.flat \
               $(TEST_DIR)/hypercall.flat $(TEST_DIR)/sieve.flat \
               $(TEST_DIR)/kvmclock_test.flat  $(TEST_DIR)/eventinj.flat \
               $(TEST_DIR)/s3.flat $(TEST_DIR)/pmu.flat \
               $(TEST_DIR)/tsc_adjust.flat $(TEST_DIR)/asyncpf.flat \
               $(TEST_DIR)/init.flat $(TEST_DIR)/smap.flat

ifdef API
tests-common += api/api-sample
tests-common += api/dirty-log
tests-common += api/dirty-log-perf
endif

tests_and_config = $(TEST_DIR)/*.flat $(TEST_DIR)/unittests.cfg

test_cases: $(tests-common) $(tests)

$(TEST_DIR)/%.o: CFLAGS += -std=gnu99 -ffreestanding -I lib -I lib/x86

$(TEST_DIR)/access.elf: $(cstart.o) $(TEST_DIR)/access.o

$(TEST_DIR)/hypercall.elf: $(cstart.o) $(TEST_DIR)/hypercall.o

$(TEST_DIR)/sieve.elf: $(cstart.o) $(TEST_DIR)/sieve.o

$(TEST_DIR)/vmexit.elf: $(cstart.o) $(TEST_DIR)/vmexit.o

$(TEST_DIR)/smptest.elf: $(cstart.o) $(TEST_DIR)/smptest.o

$(TEST_DIR)/emulator.elf: $(cstart.o) $(TEST_DIR)/emulator.o

$(TEST_DIR)/port80.elf: $(cstart.o) $(TEST_DIR)/port80.o

$(TEST_DIR)/tsc.elf: $(cstart.o) $(TEST_DIR)/tsc.o

$(TEST_DIR)/tsc_adjust.elf: $(cstart.o) $(TEST_DIR)/tsc_adjust.o

$(TEST_DIR)/apic.elf: $(cstart.o) $(TEST_DIR)/apic.o

$(TEST_DIR)/ioapic.elf: $(cstart.o) $(TEST_DIR)/ioapic.o

$(TEST_DIR)/tscdeadline_latency.elf: $(cstart.o) $(TEST_DIR)/tscdeadline_latency.o

$(TEST_DIR)/init.elf: $(cstart.o) $(TEST_DIR)/init.o

$(TEST_DIR)/realmode.elf: $(TEST_DIR)/realmode.o
	$(CC) -m32 -nostdlib -o $@ -Wl,-T,$(TEST_DIR)/realmode.lds $^

$(TEST_DIR)/realmode.o: bits = 32

$(TEST_DIR)/msr.elf: $(cstart.o) $(TEST_DIR)/msr.o

$(TEST_DIR)/idt_test.elf: $(cstart.o) $(TEST_DIR)/idt_test.o

$(TEST_DIR)/xsave.elf: $(cstart.o) $(TEST_DIR)/xsave.o

$(TEST_DIR)/rmap_chain.elf: $(cstart.o) $(TEST_DIR)/rmap_chain.o

$(TEST_DIR)/svm.elf: $(cstart.o) $(TEST_DIR)/svm.o

$(TEST_DIR)/kvmclock_test.elf: $(cstart.o) $(TEST_DIR)/kvmclock.o \
                                $(TEST_DIR)/kvmclock_test.o

$(TEST_DIR)/eventinj.elf: $(cstart.o) $(TEST_DIR)/eventinj.o

$(TEST_DIR)/s3.elf: $(cstart.o) $(TEST_DIR)/s3.o

$(TEST_DIR)/pmu.elf: $(cstart.o) $(TEST_DIR)/pmu.o

$(TEST_DIR)/asyncpf.elf: $(cstart.o) $(TEST_DIR)/asyncpf.o

$(TEST_DIR)/pcid.elf: $(cstart.o) $(TEST_DIR)/pcid.o

$(TEST_DIR)/smap.elf: $(cstart.o) $(TEST_DIR)/smap.o

$(TEST_DIR)/vmx.elf: $(cstart.o) $(TEST_DIR)/vmx.o $(TEST_DIR)/vmx_tests.o

$(TEST_DIR)/debug.elf: $(cstart.o) $(TEST_DIR)/debug.o

arch_clean:
	$(RM) $(TEST_DIR)/*.o $(TEST_DIR)/*.flat $(TEST_DIR)/*.elf \
	$(TEST_DIR)/.*.d lib/x86/.*.d

api/%.o: CFLAGS += -m32

api/%: LDLIBS += -lstdc++ -lboost_thread -lpthread -lrt
api/%: LDFLAGS += -m32

api/libapi.a: api/kvmxx.o api/identity.o api/exception.o api/memmap.o
	$(AR) rcs $@ $^

api/api-sample: api/api-sample.o api/libapi.a

api/dirty-log: api/dirty-log.o api/libapi.a

api/dirty-log-perf: api/dirty-log-perf.o api/libapi.a
