#include "inc/idt.h"
#include "inc/keyboard.h"
#include "inc/mouse.h"
#include "inc/gdt.h"
#include "inc/assets.h"
#include "inc/vbe.h"
#include "inc/sound.h"
#include "inc/ethernet.h"
#include "inc/sentient.h"
#include "inc/adaptive.h"
#include "inc/dbg.h"
#include "inc/mem.h"

extern int ms_entry();

int mk_entry() {
    mk_slab_init();
    // Initialize the kernel components
    mk_gdt_init();
    mk_idt_init();
    mk_assets_init();
    mk_keyboard_init();
    mk_mouse_init();
    mk_snd_init();
    mk_eth_init();
    mk_vbe_init();
#ifdef CONFIG_SENTIENT
    mk_sentient_init();
    mk_adaptive_init();
#endif
    mk_dbg_init();
    
    // * Test Intel HDA & Intel E1000 * //
    // Sending and playing audio using UDP packets.

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

    // Create a task for the Mira Shell
    // We use a function pointer directly
    mk_task *shell_task = mk_create_task_from_function(ms_entry, "Mira Shell");
    mk_execute_task(shell_task);

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