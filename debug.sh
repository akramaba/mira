#!/bin/bash

# Debug command for running the Mira OS image file in QEMU
/mnt/d/x/qemu/qemu-system-x86_64.exe -drive file=./build/mira.img,format=raw -audiodev dsound,id=speaker -machine pcspk-audiodev=speaker -serial tcp::6472,server,nowait

# Keyboard Parameters
# -device usb-kbd

# For testing the sound card 
# D:\X\qemu_old\qemu\qemu-system-x86_64.exe -drive file=./build/mira.img,format=raw -audiodev dsound,id=speaker -machine pcspk-audiodev=speaker

# For testing the sound card with SB16
#/mnt/d/x/qemu_old2/qemu/qemu-system-x86_64.exe -soundhw sb16 -drive file=./build/mira.img,format=raw