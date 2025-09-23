# Set the C compiler and debugger
CC = clang
GDB = gdb 

LD = ld.lld
ASM = nasm

INC = -I/inc
SRC = $(shell find . -type f -name "*.c")
ASM_SRC = $(shell find . -type f -name "*.asm")
SHELL_OBJ = ../shell/shell.o  # Link ms_entry so we can run the shell as a process

# CRITICAL: assembly must be linked first
OBJ = ${ASM_SRC:.asm=.o} ${SRC:.c=.o} 

CCFLAGS = -Iinc -ggdb -c -ffreestanding -target x86_64-none-elf -mno-red-zone -w ${CFLAGS_EXTRA}
LDFLAGS = -Ttext 0x8200
LDFLAGS_BIN = ${LDFLAGS} --oformat binary
ASFLAGS = -f elf64

# Default target for kernel (produce kernel.bin instead of kernel)
all: kernel.bin

# Build kernel.bin (binary format)
kernel.bin: ${OBJ}
	${LD} -o $@ ${LDFLAGS_BIN} $^ ${SHELL_OBJ} 

# Optional: Keep kernel.elf for debugging or other purposes
kernel.elf: ${OBJ}
	${LD} -o $@ ${LDFLAGS} $^

%.o: %.c
	${CC} ${CCFLAGS} ${INC} -o $@ $^

%.o: %.asm
	${ASM} $< ${ASFLAGS} -o $@

clean:
	rm -f kernel.elf *.o *.bin **/*.o
