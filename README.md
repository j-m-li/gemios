# GEMIOS: 32-bit x86 Preemptive RTOS with USB xHCI Support

**GEMIOS** is a bare-metal, preemptive Real-Time Operating System (RTOS) for x86 32-bit (i686) systems compiled with LLVM/Clang. It features a USB 3.0/2.0 xHCI host controller stack, USB HID Keyboard & Mouse drivers, USB Mass Storage (BOT/SCSI) driver, USB Hub support with cascaded device routing, FAT filesystem support, and an interactive real-time shell with command history.

All code is dedicated to the **Public Domain** (UNLICENSE).

---

## 🌟 Key Features

1. **x86 32-Bit Real-Time Kernel**:
   - Multiboot 1 compliant bootloader entry point.
   - Flat segmentation GDT (Global Descriptor Table).
   - Interrupt handling with IDT and PIC 8259 remap.
   - High-resolution 1000 Hz (1ms) PIT 8254 timer tick.
   - Bitmap Page Frame Allocator (PMM) managing full system RAM.
   - Kernel dynamic heap memory manager (`kmalloc`, `kfree`, `kmalloc_aligned`).
   - PCI Bus Scanner & Configuration manager with Bus Mastering DMA support.

2. **Preemptive Priority-Based RTOS Scheduler**:
   - Fixed-priority preemptive scheduling with round-robin time slicing for equal priorities.
   - True interrupt-driven preemption via PIT IRQ0 timer tick and `int 0x80` syscall yield.
   - Real-time synchronization primitives:
     - Counting Semaphores (`rtos_sem_t`)
     - Mutexes with Priority Inheritance (`rtos_mutex_t`)
     - Thread-safe Message Queues (`rtos_queue_t`)
     - Event Flags / Groups (`rtos_event_t`)
   - Real-time sleep and delay timers (`rtos_sleep_ms`, `rtos_delay_ticks`).

3. **USB xHCI (eXtensible Host Controller Interface 1.0+)**:
   - BIOS-to-OS ownership handoff (`USBLEGSUP`).
   - Command Ring, Event Ring, and Interrupter 0 management.
   - Transfer Rings for Control, Bulk, and Interrupt transfers.
   - DCBAA and Device/Input Contexts (64-byte aligned DMA structures).
   - Automatic Root Hub port status monitoring and reset sequencing.

4. **Dual Keyboard & HID Drivers**:
   - **USB Keyboard Driver**: Non-blocking asynchronous event handling, boot protocol, modifier keys (Shift, CapsLock), scancode translation.
   - **PS/2 Keyboard Driver**: 8042 controller configuration on IRQ 1 with scan code set 1 decoding.
   - **USB Mouse Driver**: Boot protocol, relative movement tracking, button states (Left, Right, Middle), screen bounds clamping, and live coordinate display.

5. **USB Mass Storage Class (MSC)**:
   - Bulk-Only Transport (BOT) with Command Block Wrapper (CBW) and Command Status Wrapper (CSW).
   - SCSI command support: `INQUIRY`, `TEST UNIT READY`, `READ CAPACITY (10)`, `READ (10)`, `WRITE (10)`.
   - Registered as block device `usb0`.
   - FAT12/16/32 Filesystem driver supporting hierarchical directory navigation (`cd`, `pwd`), directory listings (`ls`), file reading (`cat`), directory creation (`mkdir`), and text editing (`edit`).

6. **USB Hub Support**:
   - Hub class driver (0x09) with descriptor parsing.
   - Individual downstream port power enable and reset.
   - Downstream device detection and xHCI Route String hierarchical enumeration (`Slot Context`).

7. **Interactive Shell with Command History**:
   - **Arrow Key Navigation**: `↑` and `↓` arrow keys to browse through previous commands.
   - **History List**: `history` command lists recent executed commands.
   - **History Expansion**: `!n` executes command number `n`, and `!!` executes the last command.

---

## 🛠️ Toolchain & Requirements

- **Compiler**: `clang` (with 32-bit x86 target support)
- **Assembler**: Built-in self-hosted 32-bit x86 assembler (`tools/as.c`, replaces clang/gas for `.s`/`.S`)
- **Linker**: Built-in self-hosted ELF32 static linker (`tools/ld.c`, replaces `ld.lld`)
- **Disk Utilities**: Built-in self-hosted C tools (`tools/mkfs_fat.c` and `tools/mcopy.c`, no external `mtools`/`dosfstools` required)
- **Emulator**: `qemu-system-i386` or `qemu-system-x86_64`

---

## 🚀 Building and Running

### 1. Compile the Kernel
```bash
make
```
This builds `build/gemios.elf` using Clang and creates a formatted 32MB FAT test disk image (`build/test_disk.img`).

### 2. Launch in QEMU VM
```bash
./run-qemu.sh
```
Or using make:
```bash
make run
```

To run in headless or terminal-only mode:
```bash
./run-qemu.sh --nographic
```

### 3. Run Automated Test Suite
```bash
./test-qemu.sh
```

---

## 💻 Interactive RTOS Shell Commands

| Command | Description |
|---|---|
| `help` | Display list of available commands |
| `history` | List command history with line numbers |
| `!n` / `!!` | Re-execute command by history number or repeat last command |
| `↑` / `↓` | Navigate command history interactively |
| `ps` | List all active RTOS tasks, state, priority, runtime, stack |
| `mem` | Display physical memory pages and heap usage |
| `pci` | List PCI bus devices and BAR allocations |
| `lsusb` | Display complete USB device tree (xHCI root ports and hubs) |
| `storage` | List detected USB Mass Storage block devices |
| `readsec <dev> <lba>` | Read and hex-dump a 512-byte sector from disk |
| `writesec <dev> <lba> <text>` | Write text data to a sector and verify |
| `cd [dev] [dir]` | Change current directory on FAT filesystem |
| `pwd` | Print current working directory |
| `ls [dev] [dir]` | List directory contents on the FAT filesystem |
| `cat [dev] <path>` | Display contents of a file on the FAT drive |
| `mkdir [dev] <dir>` | Create a directory on the FAT filesystem |
| `rm [-r] [dev] <path>` | Remove files, directories, or wildcards (`rm -r *`) |
| `edit [dev] <path>` | Fullscreen MS-DOS style UTF-8 text editor |
| `mouse` | Display live USB mouse coordinates and button state |
| `bench` | Run RTOS preemptive context-switching benchmark |
| `uptime` | Display uptime in seconds, milliseconds, and timer ticks |
| `clear` | Clear the console screen |
| `reboot` | Reboot the virtual machine |

---

## 📜 License

This software is dedicated to the **Public Domain** under the terms of the [UNLICENSE](LICENSE.txt).
