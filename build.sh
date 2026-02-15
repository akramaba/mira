#!/bin/bash

# Check for 'sentient' build flag
# Example usage: ./build.sh sentient or ./build.sh control
BUILD_TYPE=$1
export CFLAGS_EXTRA=""
if [ "$BUILD_TYPE" = "sentient" ]; then
    export CFLAGS_EXTRA="-DCONFIG_SENTIENT=1"
fi

rm -f build/mira.img

(make -C boot)
boot_result=$?

# IMPORTANT: Build shell FIRST for the kernel to access its entry function
(make -C shell)
shell_result=$?

# IMPORTANT: Build assets BEFORE the kernel to ensure assets.bin is created
# for the kernel to use, as it must be able to access the assets
(python3 -u tools/assets_to_mfs.py)
mfs_result=$?

(make -C kernel)
kernel_result=$?

if (test -f boot/boot.bin) && (test -f kernel/kernel.bin) && (test -f shell/shell.bin) && (test -f build/assets.bin)
then
    echo "Build successful"
else
    echo "Build failed"
    sh clean.sh
    exit 1
fi

if [ "$boot_result" = "0" ] && [ "$kernel_result" = "0" ] && [ "$shell_result" = "0" ] && [ "$mfs_result" = "0" ]
then
    kernel_size=$(wc -c < kernel/kernel.bin)
    kernel_sectors=$(( ($kernel_size + 511) / 512 ))

    # Had a problem with the special characters in the older command
    # breaking and causing Mira to not boot because it was trying to read
    # too many sectors. This line fixes that, but it's not as "elegant."
    python3 -c "import struct; f=open('boot/boot.bin','r+b'); f.seek(2); f.write(struct.pack('<H', $kernel_sectors)); f.close()"

    cp boot/boot.bin ./build/mira.img
    cat kernel/kernel.bin >> build/mira.img

    echo "Build finished successfully"
    
    sh debug.sh

    sh clean.sh
else
    result=`expr $boot_result + $make_result`
    echo "Build failed with error code $result. See output for more info."

    sh clean.sh
fi