#include "inc/idt.h"
#include "inc/keyboard.h"
#include "inc/mouse.h"
#include "inc/gdt.h"
#include "inc/assets.h"
#include "inc/vbe.h"
#include "inc/sentient.h"
#include "inc/dbg.h"

extern int ms_entry();

int mk_entry() {
    // Initialize the kernel components
    mk_gdt_init();
    mk_idt_init();
    mk_assets_init();
    mk_keyboard_init();
    mk_mouse_init();
    mk_vbe_init();
    mk_dbg_init();

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

    // Initialize the PIT
    // This is separated from the other initializations
    // because it enables interrupts and starts the scheduler
    mk_pit_init();

    /* mk_pit_init enables interrupts, so execution will not return here */
    /* Mira is now running :) */

    return 0;
}