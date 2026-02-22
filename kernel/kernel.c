#include "inc/idt.h"
#include "inc/keyboard.h"
#include "inc/mouse.h"
#include "inc/gdt.h"
#include "inc/assets.h"
#include "inc/vbe.h"
#include "inc/sound.h"
#include "inc/ethernet.h"
#include "inc/nvme.h"
#include "inc/sentient.h"
#include "inc/adaptive.h"
#include "inc/dbg.h"
#include "inc/mem.h"

extern int ms_entry();

// * Test Intel HDA & Intel E1000 * //
// Sending and playing audio using UDP packets.
int mk_test_hda_entry() {
    mk_eth_socket_t *sock = mk_eth_socket();
    int allocated_size = 100 * 1024 * 1024;
    uint8_t *buf = mk_malloc(allocated_size);
    uint32_t off = 0;
    const void *rx_data;
    uint16_t rx_len;

    if (sock && buf) {
        sock->src_port = 2026;

        while (1) {
            if (mk_eth_recv(sock, &rx_data, &rx_len) == 0 && rx_len > 0) {
                const char *d = (const char *)rx_data;

                if (rx_len == 3 && d[0] == 'E' && d[1] == 'O' && d[2] == 'F') {
                    uint32_t p = 0;
                    while (p < off) {
                        uint32_t chunk = off - p;
                        if (chunk > 65536) {
                            chunk = 65536;
                        }

                        mk_snd_play(buf + p, chunk);
                        p += chunk;
                    }
              
                    off = 0;
                } else if (off + rx_len < allocated_size) {
                    mk_memcpy(buf + off, rx_data, rx_len);
                    off += rx_len;
                }
            }
        }
    }

    return 0;
}

// * Test NVMe * //
// Reads Bella.bmp from the NVMe disk and draws it centered on the VBE framebuffer.
int mk_test_nvme_entry() {
    mk_nvme_ns_t *ns = mk_nvme_open(1);

    if (!ns) {
        return -1;
    }

    // Bella.bmp
    uint32_t BMP_WIDTH = 323;
    uint32_t BMP_HEIGHT = 392;
    uint32_t BMP_PIXEL_OFFSET = 138;
    uint32_t BMP_FILE_SIZE = 506602;
    uint32_t BMP_SECTORS = ((BMP_FILE_SIZE + 511) / 512);

    uint8_t *file = (uint8_t *)mk_malloc(BMP_SECTORS * 512);

    if (!file) {
        return -1;
    }

    if (mk_nvme_read(ns, 0, BMP_SECTORS, file) != 0) {
        return -1;
    }

    // BMP pixel data is stored bottom-up. Flip it into a top-down buffer.
    uint32_t *pixels = (uint32_t *)(file + BMP_PIXEL_OFFSET);
    uint32_t *flipped = (uint32_t *)mk_malloc(BMP_WIDTH * BMP_HEIGHT * 4);

    if (!flipped) {
        return -1;
    }

    for (int row = 0; row < BMP_HEIGHT; row++) {
        uint32_t *src = pixels + (BMP_HEIGHT - 1 - row) * BMP_WIDTH;
        uint32_t *dst = flipped + row * BMP_WIDTH;

        for (int col = 0; col < BMP_WIDTH; col++) {
            dst[col] = src[col];
        }
    }

    // Draw.
    int x = (1280 - BMP_WIDTH) / 2;
    int y = (720 - BMP_HEIGHT) / 2;
    mk_vbe_draw_image(x, y, BMP_WIDTH, BMP_HEIGHT, flipped);

    while (1);

    return 0;
}

int mk_entry() {
    mk_slab_init();
    mk_gdt_init();
    mk_idt_init();
    mk_assets_init();
    mk_keyboard_init();
    mk_mouse_init();
    mk_snd_init();
    mk_eth_init();
    mk_nvme_init();
    mk_vbe_init();
#ifdef CONFIG_SENTIENT
    mk_sentient_init();
    mk_adaptive_init();
#endif
    mk_dbg_init();
    
    mk_task *hda_task = mk_create_task_from_function(mk_test_hda_entry, "HDA Test");
    hda_task->mode = MK_TASKS_KERNEL_MODE;
    mk_execute_task(hda_task);

    mk_task *nvme_task = mk_create_task_from_function(mk_test_nvme_entry, "NVMe Test");
    nvme_task->mode = MK_TASKS_KERNEL_MODE;
    mk_execute_task(nvme_task);

    // Create a task for the Mira Shell
    // We use a function pointer directly
    //mk_task *shell_task = mk_create_task_from_function(ms_entry, "Mira Shell");
    //mk_execute_task(shell_task);

    // * Sentient Profiler Testing: Disabled
    // * to keep the debug output clean.
    // // Create and execute the debug task
    // mk_task *dbg_task = mk_create_task_from_function(mk_dbg_entry, "Mira Debugger Driver");
    // dbg_task->mode = MK_TASKS_KERNEL_MODE;
    // mk_execute_task(dbg_task);

    // Create and execute the Sentient Profiler task
    mk_task *profiler_task = mk_create_task_from_function(mk_profiler_entry, "Mira Profiler");
    profiler_task->mode = MK_TASKS_KERNEL_MODE;
    mk_execute_task(profiler_task);

    // Create and execute the Apoptosis Worker task.
    // This is a kernel-mode task responsible for cleaning up terminated processes.
    mk_task *apoptosis_task = mk_create_task_from_function(mk_apoptosis_worker_entry, "Apoptosis Worker");
    apoptosis_task->mode = MK_TASKS_KERNEL_MODE;
    mk_execute_task(apoptosis_task);

    // Create and execute the Adaptive Profiler task
    // This profiler is an adaptive detection system for Sentient.
    mk_task *adaptive_task = mk_create_task_from_function(mk_adaptive_profiler_entry, "Adaptive Profiler");
    adaptive_task->mode = MK_TASKS_KERNEL_MODE;
    mk_execute_task(adaptive_task);

    // Initialize the PIT
    // This is separated from the other initializations
    // because it enables interrupts and starts the scheduler
    mk_pit_init();

    /* mk_pit_init enables interrupts, so execution will not return here */
    /* Mira is now running :) */

    return 0;
}