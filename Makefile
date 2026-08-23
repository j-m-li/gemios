CC = clang
LD = ld.lld
QEMU = qemu-system-i386

CFLAGS = -m32 -march=i686 -ffreestanding -fno-pic -fno-pie \
         -fno-stack-protector -fno-builtin -nostdlib -Wall -Wextra \
         -Wno-unused-parameter -Wno-unused-function -O2 \
         -Isrc/include -Isrc/include/rtos -Isrc/include/usb -Isrc/include/fs -Isrc/include/shell

ASFLAGS = -m32

LDFLAGS = -m elf_i386 -T linker.ld --image-base=0x100000 -nostdlib

BUILD_DIR = build

C_SRCS = $(shell find src -name '*.c')
ASM_SRCS = $(shell find src -name '*.S')

OBJS = $(patsubst src/%.c, $(BUILD_DIR)/%.o, $(C_SRCS)) \
       $(patsubst src/%.S, $(BUILD_DIR)/%.o, $(ASM_SRCS))

KERNEL_ELF = $(BUILD_DIR)/gemos.elf
DISK_IMG = $(BUILD_DIR)/test_disk.img
FAT32_IMG = $(BUILD_DIR)/test_fat32.img

.PHONY: all clean run run-nographic test test-fat32

all: $(KERNEL_ELF) $(DISK_IMG) $(FAT32_IMG)

$(KERNEL_ELF): $(OBJS) linker.ld
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) $(OBJS) -o $@
	@echo "[LD] Created $@"

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "[CC] $<"

$(BUILD_DIR)/%.o: src/%.S
	@mkdir -p $(@D)
	$(CC) $(ASFLAGS) -c $< -o $@
	@echo "[AS] $<"

$(DISK_IMG):
	@mkdir -p $(BUILD_DIR)
	@echo "[DISK] Creating 32MB FAT16 test disk image..."
	dd if=/dev/zero of=$(DISK_IMG) bs=1M count=32 status=none
	mkfs.fat -F 16 -n "GEMOS_FAT16" $(DISK_IMG) > /dev/null
	@echo "Welcome to GEMOS RTOS on USB Mass Storage (FAT16)!" > $(BUILD_DIR)/README.TXT
	@echo "Testing USB BOT & SCSI read/write functionality." >> $(BUILD_DIR)/README.TXT
	@echo "All systems operational!" > $(BUILD_DIR)/STATUS.TXT
	mcopy -i $(DISK_IMG) $(BUILD_DIR)/README.TXT ::README.TXT
	mcopy -i $(DISK_IMG) $(BUILD_DIR)/STATUS.TXT ::STATUS.TXT
	@echo "[DISK] Created $(DISK_IMG) (FAT16) with test files"

$(FAT32_IMG):
	@mkdir -p $(BUILD_DIR)
	@echo "[DISK] Creating 64MB FAT32 test disk image..."
	dd if=/dev/zero of=$(FAT32_IMG) bs=1M count=64 status=none
	mkfs.fat -F 32 -n "GEMOS_FAT32" $(FAT32_IMG) > /dev/null
	@echo "Welcome to GEMOS RTOS on USB Mass Storage (FAT32)!" > $(BUILD_DIR)/README.TXT
	@echo "Testing FAT32 cluster chain reading and root directory traversal." >> $(BUILD_DIR)/README.TXT
	@echo "FAT32 verification passed!" > $(BUILD_DIR)/STATUS.TXT
	@echo "GEMOS RTOS FAT32 Document" > $(BUILD_DIR)/FAT32DOC.TXT
	@echo "Supports cluster traversal and 32-bit FAT table lookups." >> $(BUILD_DIR)/FAT32DOC.TXT
	mcopy -i $(FAT32_IMG) $(BUILD_DIR)/README.TXT ::README.TXT
	mcopy -i $(FAT32_IMG) $(BUILD_DIR)/STATUS.TXT ::STATUS.TXT
	mcopy -i $(FAT32_IMG) $(BUILD_DIR)/FAT32DOC.TXT ::FAT32DOC.TXT
	@echo "[DISK] Created $(FAT32_IMG) (FAT32) with test files"

run: $(KERNEL_ELF) $(DISK_IMG)
	./run-qemu.sh

run-fat32: $(KERNEL_ELF) $(FAT32_IMG)
	./run-qemu.sh --fat32

test: $(KERNEL_ELF) $(DISK_IMG)
	./test-qemu.sh

test-fat32: $(KERNEL_ELF) $(FAT32_IMG)
	./test-fat32.sh

clean:
	rm -rf $(BUILD_DIR)
