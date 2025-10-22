# Mira OS - Technical Features & Architecture

### About This Document

This document provides a deep dive into the core architecture and technical implementation of Mira OS. While the main [README.md](README.md) provides a high-level overview, this file is for those who want to understand *how* the system was built and the engineering challenges that were overcome.

I engineered every component from first principles in C and x86-64 Assembly to create a pristine, scientifically rigorous testbed for my research. This required building a complete modern operating system from the ground up.

---

### Key Architectural Achievements at a Glance

This table summarizes the most significant engineering accomplishments within the project.

| Component | Key Achievement | Engineering Challenge & Demonstrated Skills |
| :--- | :--- | :--- |
| **Bootloader** | 16-bit to 64-bit Long Mode Transition | Orchestrating the complex sequence of CPU mode switches (Real -> Protected -> Long), configuring the GDT, and initializing core hardware without any OS support. (Skills: **Assembly, Low-Level CPU Control, System Initialization**) |
| **Memory Mgmt.** | 4-Level Paging & Virtual Memory from Scratch | Manually building the entire x86-64 page table hierarchy (PML4, PDPT, etc.) to establish a virtual address space, enforce memory protection, and map hardware. (Skills: **OS Theory, Memory Systems, x86-64 Architecture**) |
| **Kernel & Scheduler** | Preemptive Multitasking | Implementing assembly-level context switching routines and a PIT-interrupt-driven scheduler to manage concurrent user-mode processes, ensuring system responsiveness. (Skills: **Concurrency, Process Management, System Call Design**) |
| **Graphics**| User-Mode Graphical Subsystem via Syscalls | Architecting a secure kernel interface to allow unprivileged Ring 3 applications to access a VBE linear framebuffer, including a custom 2D library with **high-quality anti-aliased rounded rectangles and text rendering.** (Skills: **API Design, Graphics Programming, Kernel/User Separation**) |

---

### Core Architectural Features

#### Boot Process & CPU Initialization

I engineered a multi-stage boot process to transition the machine from a primitive 16-bit state into a fully-featured 64-bit graphical environment.

*   **Multi-Stage Bootstrapping:** A 512-byte bootsector initiates a chain-loading process that elevates the CPU from 16-bit Real Mode, through 32-bit Protected Mode, and finally into 64-bit Long Mode, where the kernel takes control.
*   **BIOS-based Kernel Loading:** The bootloader uses extended `INT 13h` BIOS interrupts to load the kernel from disk. The build system dynamically calculates the number of sectors required and patches this value into the bootloader.
*   **Segment and Mode Configuration (GDT):** I configured the Global Descriptor Table (GDT) twice: first for the initial 32-bit flat memory model, and then a new GDT for the 64-bit long mode environment. Code and data segments for both kernel and user space were defined.
*   **VBE Graphics Initialization:** Before the C kernel is even entered, the bootloader uses VBE 2.0 BIOS calls to switch to a 1280x720 (32-bit color) graphical mode, which establishes the linear framebuffer that the rest of the OS will use.

#### Memory Management

Mira OS implements a complete memory hierarchy from scratch. This demonstrates the foundations for memory protection and dynamic allocation.

*   **4-Level Paging (x86-64):** I manually architected and populated a full 4-level page table structure (PML4, PDPT, PD, PT) to identity-map the first 4GiB of physical memory. This virtual memory system is enabled just before entering long mode and is crucial for memory protection.
*   **Kernel Heap:** A fast and simple bump allocator manages a 4MB kernel heap, providing dynamic memory allocation (`mk_malloc`) for kernel structures without relying on any external libraries.
*   **Post-Boot GDT & TSS:** Once in the kernel, a new GDT is established in C. This includes a Task State Segment (TSS) descriptor, which is critical for the CPU to handle privilege transitions from user mode (Ring 3) to kernel mode (Ring 0) during interrupts and system calls.

#### Kernel, Multitasking, and System Calls

The monolithic kernel is the core of the OS, designed to provide a secure, preemptive multitasking environment.

*   **Preemptive Multitasking:** A round-robin scheduler, driven by a 1000 Hz Programmable Interval Timer (PIT) interrupt, performs context switches between tasks to ensure system responsiveness.
*   **Secure User Mode:** The kernel creates and manages processes that run in unprivileged Ring 3. Each has its own stack and memory protections enforced by the paging system.
*   **System Call API (`int 0x80`):** I designed a system call interface for user-mode applications to securely request kernel services. The syscall dispatcher handles requests for console output, window creation, graphical drawing, and reading the high-precision CPU Time-Stamp Counter (RDTSC).

#### Graphics & Windowing System

Mira features a 2D graphics subsystem and a basic compositor, built from the ground up.

*   **VBE Linear Framebuffer Driver:** A C-level driver provides optimized primitives for drawing pixels and raw image buffers to the 1280x720 graphical display initialized by the bootloader.
*   **`mira2d` Graphics Library:** I wrote a custom user-space library that provides a 2D drawing context for applications, exposing functions for clearing buffers, drawing lines, and rendering complex primitives.
*   **High-Quality Anti-Aliased Rendering:** To achieve a polished, modern UI, the `mira2d` library implements **anti-aliasing for text and rounded rectangles** by using a 4x supersampling algorithm. This method renders a higher-resolution version in an off-screen buffer before downscaling for display.
*   **Kernel-Side Window Management:** The kernel includes a basic window manager and compositor. Via system calls, user processes can request a window, and the kernel composites their off-screen framebuffers directly to the screen. Updates and redraws are also managed by the kernel.

#### Device Drivers & I/O

The kernel contains custom-written drivers to interface with essential hardware.

*   **Unified PS/2 Driver:** A PS/2 driver polls for both keyboard and mouse data in a single routine to prevent race conditions. The keyboard driver supports the standard US layout and extended keys, while the mouse driver decodes 3-byte packets to process movement and button states.
*   **Programmable Interval Timer (PIT):** The PIT is configured to fire at 1000 Hz in order to deliver the hardware interrupt that serves as the "heartbeat" for the entire task scheduling system.
*   **Serial Port (COM1):** A simple serial driver provides `printk`-style debug output to the COM1 port, which was important for kernel-level debugging in QEMU.

#### Build System & Tooling

An automated build system is included in the project.

*   **Makefile Orchestration:** The kernel, bootloader, and user-space applications are each built using their own Makefiles. A master build script manages them.
*   **Custom Asset Pipeline:** A Python script (`assets_to_mfs.py`) packs all graphical and data assets into a single binary blob, which is embedded in the kernel and loaded at runtime.
*   **Automated Image Creation:** A master `build.sh` script compiles all components, runs the asset pipeline, calculates the final kernel size in sectors, patches the bootloader with this value, and assembles the final, bootable disk image.