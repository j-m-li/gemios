import subprocess, time, os, sys

OUTPUT_LOG = 'build/test_lfn_output.log'
SERIAL_LOG = 'build/serial_lfn_output.log'

try:
    os.remove(OUTPUT_LOG)
except OSError:
    pass
try:
    os.remove(SERIAL_LOG)
except OSError:
    pass

log_file = open(OUTPUT_LOG, 'w')
cmd = [
    'qemu-system-i386',
    '-kernel', 'build/gemios.elf',
    '-m', '256M',
    '-display', 'none',
    '-serial', f'file:{SERIAL_LOG}',
    '-monitor', 'stdio',
    '-device', 'qemu-xhci,id=xhci,p2=8,p3=8',
    '-device', 'usb-kbd,bus=xhci.0,port=1',
    '-device', 'usb-mouse,bus=xhci.0,port=2',
    '-drive', 'if=none,id=usbstick,format=raw,file=build/test_lfn.img',
    '-device', 'usb-storage,bus=xhci.0,port=3,drive=usbstick',
]

proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=log_file, stderr=subprocess.STDOUT, text=True)

time.sleep(1.2) # Wait for boot

commands = [
    'storage',
    'ls usb0',
    'cat usb0 "My Long Document 2026.txt"',
    'cat usb0 "Second Very Long Name.txt"',
    'mkdir usb0 "Created Inside GEMIOS"',
    'ls usb0',
    'rm usb0 "Second Very Long Name.txt"',
    'ls usb0'
]

for cmd_text in commands:
    for ch in cmd_text:
        if ch == ' ':
            proc.stdin.write('sendkey spc\n')
        elif ch == '.':
            proc.stdin.write('sendkey dot\n')
        elif ch == '/':
            proc.stdin.write('sendkey slash\n')
        elif ch == '-':
            proc.stdin.write('sendkey minus\n')
        elif ch == '_':
            proc.stdin.write('sendkey shift-minus\n')
        elif ch == '"':
            proc.stdin.write('sendkey shift-apostrophe\n')
        elif ch >= 'A' and ch <= 'Z':
            proc.stdin.write(f'sendkey shift-{ch.lower()}\n')
        else:
            proc.stdin.write(f'sendkey {ch}\n')
        proc.stdin.flush()
        time.sleep(0.04)
    proc.stdin.write('sendkey ret\n')
    proc.stdin.flush()
    time.sleep(0.5)

time.sleep(1.5)
proc.terminate()
proc.wait()
log_file.close()
print("QEMU LFN interaction test finished.")
