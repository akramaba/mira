#!/bin/bash

echo "Starting debug session..."

/mnt/d/x/qemu/qemu-system-x86_64.exe -drive file=./build/mira.img,format=raw -serial tcp::6472,server,nowait -audiodev dsound,id=hda -device intel-hda -device hda-output,audiodev=hda