CC = clang
QEMU = qemu-system-i386

CFLAGS = -m32 -march=i686 -ffreestanding -fno-pic -fno-pie \
         -fno-stack-protector -fno-builtin -nostdlib -Wall -Wextra \
         -ansi -pedantic -Werror \
         -Wno-unused-parameter -Wno-unused-function -O0 \
         -Isrc/include -Isrc/include/rtos -Isrc/include/usb -Isrc/include/fs -Isrc/include/shell -Itools
CC = ../ccia/ccia-i386
CFLAGS =  \
         -Isrc/include -Isrc/include/rtos -Isrc/include/usb -Isrc/include/fs -Isrc/include/shell -Itools \
	-I../ccia/include -I../ccia/include/riscv32 \

ASFLAGS = -m32

LDFLAGS = -m elf_i386 -T linker.ld --image-base=0x100000 -nostdlib

HOST_CC = clang
HOST_CFLAGS = -Wall -Wextra -ansi -pedantic -Werror -O2

BUILD_DIR = build
TOOLS_DIR = $(BUILD_DIR)/tools
MKFS_FAT = $(TOOLS_DIR)/mkfs.fat
MCOPY = $(TOOLS_DIR)/mcopy
LD = $(TOOLS_DIR)/ld
AS = $(TOOLS_DIR)/as
MAKE_TOOL = $(TOOLS_DIR)/make
DD = $(TOOLS_DIR)/dd

C_SRCS = $(shell find src -name '*.c')
ASM_CAP_SRCS = $(shell find src -name '*.S')
ASM_LOW_SRCS = $(shell find src -name '*.s')

OBJS = $(patsubst src/%.c, $(BUILD_DIR)/%.o, $(C_SRCS)) \
       $(patsubst src/%.S, $(BUILD_DIR)/%.o, $(ASM_CAP_SRCS)) \
       $(patsubst src/%.s, $(BUILD_DIR)/%.o, $(ASM_LOW_SRCS))

KERNEL_ELF = $(BUILD_DIR)/gemios.elf
DISK_IMG = $(BUILD_DIR)/test_disk.img
FAT32_IMG = $(BUILD_DIR)/test_fat32.img

.PHONY: all clean run run-nographic test test-ps2 test-fat32 test-hotplug  tools
.SECONDARY:

all: tools $(KERNEL_ELF) $(DISK_IMG) $(FAT32_IMG)

tools: $(MKFS_FAT) $(MCOPY) $(LD) $(AS) $(MAKE_TOOL) $(DD)

tools/make: tools/make.c
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@
	@echo "[HOST_CC] $< -> $@"

tools/as: tools/as.c
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@
	@echo "[HOST_CC] $< -> $@"

tools/ld: tools/ld.c
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@
	@echo "[HOST_CC] $< -> $@"

tools/dd: tools/dd.c
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@
	@echo "[HOST_CC] $< -> $@"

tools/mkfs.fat: tools/mkfs_fat.c
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@
	@echo "[HOST_CC] $< -> $@"

tools/mcopy: tools/mcopy.c
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@
	@echo "[HOST_CC] $< -> $@"

$(DD): tools/dd.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@
	@echo "[HOST_CC] $< -> $@"

$(MAKE_TOOL): tools/make.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@
	@echo "[HOST_CC] $< -> $@"

$(AS): tools/as.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@
	@echo "[HOST_CC] $< -> $@"

$(LD): tools/ld.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@
	@echo "[HOST_CC] $< -> $@"

$(MKFS_FAT): tools/mkfs_fat.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@
	@echo "[HOST_CC] $< -> $@"

$(MCOPY): tools/mcopy.c
	@mkdir -p $(@D)
	$(HOST_CC) $(HOST_CFLAGS) $< -o $@
	@echo "[HOST_CC] $< -> $@"

$(KERNEL_ELF): $(OBJS) $(LD) linker.ld
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) $(OBJS) -o $@
	@echo "[LD] Created $@"

$(BUILD_DIR)/%.s: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -S $< -o $@
	@echo "[CC -S] $< -> $@"

$(BUILD_DIR)/%.o: $(BUILD_DIR)/%.s $(AS)
	@mkdir -p $(@D)
	$(AS) $< -o $@
	@echo "[AS] $< -> $@"

$(BUILD_DIR)/%.o: src/%.S $(AS)
	@mkdir -p $(@D)
	$(AS) $< -o $@
	@echo "[AS] $< -> $@"

$(BUILD_DIR)/%.o: src/%.s $(AS)
	@mkdir -p $(@D)
	$(AS) $< -o $@
	@echo "[AS] $< -> $@"

$(DISK_IMG): $(MKFS_FAT) $(MCOPY) $(DD)
	@mkdir -p $(BUILD_DIR)
	@echo "[DISK] Creating 32MB FAT16 test disk image..."
	$(DD) if=/dev/zero of=$(DISK_IMG) bs=1M count=32 status=none
	$(MKFS_FAT) -F 16 -n "GEMIOS16" $(DISK_IMG) > /dev/null
	@echo "Welcome to GEMIOS RTOS on USB Mass Storage (FAT16)!" > $(BUILD_DIR)/README.TXT
	@echo "Testing USB BOT & SCSI read/write functionality." >> $(BUILD_DIR)/README.TXT
	@echo "All systems operational!" > $(BUILD_DIR)/STATUS.TXT
	$(MCOPY) -i $(DISK_IMG) $(BUILD_DIR)/README.TXT ::README.TXT
	$(MCOPY) -i $(DISK_IMG) $(BUILD_DIR)/STATUS.TXT ::STATUS.TXT
	@echo "[DISK] Created $(DISK_IMG) (FAT16) with test files"

$(FAT32_IMG): $(MKFS_FAT) $(MCOPY) $(DD)
	@mkdir -p $(BUILD_DIR)
	@echo "[DISK] Creating 64MB FAT32 test disk image..."
	$(DD) if=/dev/zero of=$(FAT32_IMG) bs=1M count=64 status=none
	$(MKFS_FAT) -F 32 -n "GEMIOS32" $(FAT32_IMG) > /dev/null
	@echo "Welcome to GEMIOS RTOS on USB Mass Storage (FAT32)!" > $(BUILD_DIR)/README.TXT
	@echo "Testing FAT32 cluster chain reading and root directory traversal." >> $(BUILD_DIR)/README.TXT
	@echo "FAT32 verification passed!" > $(BUILD_DIR)/STATUS.TXT
	@echo "GEMIOS RTOS FAT32 Document" > $(BUILD_DIR)/FAT32DOC.TXT
	@echo "Supports cluster traversal and 32-bit FAT table lookups." >> $(BUILD_DIR)/FAT32DOC.TXT
	$(MCOPY) -i $(FAT32_IMG) $(BUILD_DIR)/README.TXT ::README.TXT
	$(MCOPY) -i $(FAT32_IMG) $(BUILD_DIR)/STATUS.TXT ::STATUS.TXT
	$(MCOPY) -i $(FAT32_IMG) $(BUILD_DIR)/FAT32DOC.TXT ::FAT32DOC.TXT
	@echo "[DISK] Created $(FAT32_IMG) (FAT32) with test files"

run: $(KERNEL_ELF) $(DISK_IMG)
	./run-qemu.sh

run-nographic: $(KERNEL_ELF) $(DISK_IMG)
	./run-qemu.sh --nographic

run-fat32: $(KERNEL_ELF) $(FAT32_IMG)
	./run-qemu.sh --fat32

test: $(KERNEL_ELF) $(DISK_IMG)
	./test-qemu.sh

test-ps2: $(KERNEL_ELF) $(DISK_IMG)
	./test-ps2.sh

test-fat32: $(KERNEL_ELF) $(FAT32_IMG)
	./test-fat32.sh

test-hotplug: $(KERNEL_ELF) $(DISK_IMG) $(FAT32_IMG)
	./test-hotplug.sh

test-usb-device: $(KERNEL_ELF) $(DISK_IMG)
	./test-usb-device.sh

clean:
	rm -rf $(BUILD_DIR) tools/make tools/as tools/ld tools/dd tools/mkfs.fat tools/mcopy
