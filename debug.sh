#!/bin/bash

echo "Starting debug session..."

if [ ! -f ./build/nvme.img ]; then
    dd if=/dev/zero of=./build/nvme.img bs=1M count=32 2>/dev/null
    dd if=./Bella.bmp of=./build/nvme.img bs=512 conv=notrunc 2>/dev/null
fi

/mnt/d/x/qemu/qemu-system-x86_64.exe -drive file=./build/mira.img,format=raw -serial tcp::6472,server,nowait -audiodev dsound,id=hda -device intel-hda -device hda-output,audiodev=hda -device e1000,netdev=net0 -netdev user,id=net0,hostfwd=udp::2026-:2026 -drive file=./build/nvme.img,format=raw,if=none,id=nvme0 -device nvme,serial=mira0001,drive=nvme0