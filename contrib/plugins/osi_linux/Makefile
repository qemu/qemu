### Additional plugin object files.
PLUGIN_OBJ_FILES=$(PLUGIN_OBJ_DIR)/kernelinfo_read.o

PLUGIN_OBJ_FILES+=$(PLUGIN_OBJ_DIR)/default_profile.o
PLUGIN_OBJ_FILES+=$(PLUGIN_OBJ_DIR)/kernel_2_4_x_profile.o
PLUGIN_OBJ_FILES+=$(PLUGIN_OBJ_DIR)/kernelinfo_downloader.o

LIBS += -lcurl

### Preprocessor flags.
# Process enumeration starts from approximately the current task by
# default. With this flag, it is set to start from the init task.
# This is less robust than the default and you may need to re-extract
# the kernel profile each time you change your kernel.
# QEMU_CFLAGS += -DOSI_LINUX_LIST_FROM_INIT

# Include threads when listing all processes.
# QEMU_CFLAGS += -DOSI_LINUX_LIST_THREADS

# Enable test mode. See osi_linux.cpp:asid_changed().
# QEMU_CFLAGS += -DOSI_LINUX_TEST

# Enable extra debugging for process listing.
# QEMU_CFLAGS += -DOSI_LINUX_PSDEBUG

# Enable infinite loop detection.
# QEMU_CFLAGS += -DOSI_MAX_PROC=256

### Recipes.
$(PLUGIN_OBJ_DIR)/kernelinfo_read.o: $(PLUGIN_SRC_DIR)/utils/kernelinfo/kernelinfo_read.c $(PLUGIN_SRC_DIR)/utils/kernelinfo/kernelinfo.h
	@[ -d  $(dir $@) ] || mkdir -p $(dir $@)
	$(call quiet-command,$(CC) $(QEMU_INCLUDES) $(QEMU_CFLAGS) -c -o $@ $<,"UTIL CC $(TARGET_DIR)$@")

$(PLUGIN_OBJ_DIR)/kernelinfo.conf: $(PLUGIN_SRC_DIR)/kernelinfo.conf
	$(call quiet-command,cp $< $@,"CP      $(TARGET_DIR)$@")

$(PLUGIN_TARGET_DIR)/panda_$(PLUGIN_NAME).so: \
	$(PLUGIN_OBJ_DIR)/$(PLUGIN_NAME).o \
	$(PLUGIN_OBJ_FILES)

$(PLUGIN_OBJ_DIR)/kernelinfo.zip: $(addprefix $(PLUGIN_SRC_DIR)/utils/kernelinfo/,Makefile kernel.conf kernelinfo.c kernelinfo.h kernelinfo_read.c kernelinfo_size.c kernelinfo_cmp.py kernelinfo_parse.py)
	$(call quiet-command,cd $(PLUGIN_SRC_DIR)/utils && zip -qr $(CURDIR)/$@ kernelinfo,"ZIP      $(TARGET_DIR)$@")

$(PLUGIN_OBJ_DIR)/kernelinfo24.zip: $(addprefix $(PLUGIN_SRC_DIR)/utils/kernelinfo24/,Makefile kernelinfo24.c kernelinfo24.sh)
	$(call quiet-command,cd $(PLUGIN_SRC_DIR)/utils && zip -qr $(CURDIR)/$@ kernelinfo24,"ZIP      $(TARGET_DIR)$@")

all: $(PLUGIN_OBJ_DIR)/kernelinfo.conf $(PLUGIN_OBJ_DIR)/kernelinfo.zip $(PLUGIN_OBJ_DIR)/kernelinfo24.zip
