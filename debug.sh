#!/bin/bash

echo "Starting debug session..."

# Debug command for running the Mira OS image file in QEMU (GDB runs on port 1234)
qemu-system-x86_64 -drive file=./build/mira.img,format=raw -serial tcp::6472,server,nowait -S -s