-include ../config-host.mak
-include $(SRC_PATH)/Makefile.objs
-include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH):$(SRC_PATH)/libcacard)

ifeq ($(CONFIG_WIN32),y)
QEMU_THREAD=qemu-thread-win32.o
else
QEMU_THREAD=qemu-thread-posix.o
endif


QEMU_OBJS=$(addprefix ../, $(QEMU_THREAD) $(oslib-obj-y) $(trace-obj-y) qemu-malloc.o qemu-timer-common.o)

QEMU_CFLAGS+=-I../

vscclient: $(libcacard-y) $(QEMU_OBJS) vscclient.o
	$(call quiet-command,$(CC) $(libcacard_libs) -lrt -o $@ $^,"  LINK  $(TARGET_DIR)$@")

all: vscclient

clean:
	rm -f *.o */*.o *.d */*.d *.a */*.a *~ */*~ vscclient

