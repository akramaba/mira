#!/bin/bash

echo "Starting debug session..."

/mnt/d/x/qemu/qemu-system-x86_64.exe -drive file=./build/mira.img,format=raw -serial tcp::6472,server,nowait -audiodev dsound,id=hda -device intel-hda -device hda-output,audiodev=hda -device e1000,netdev=net0 -netdev user,id=net0,hostfwd=udp::2026-:2026