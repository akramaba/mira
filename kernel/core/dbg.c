#include "../inc/dbg.h"
#include "../inc/mem.h"
#include "../inc/scheduler.h"
#include "../inc/util.h"

// Circular buffer for sending debug messages
// to the user-space via syscall polling.
char mk_dbg_log_buffer[MK_DBG_LOG_BUFFER_SIZE];
volatile uint32_t mk_dbg_log_head = 0;
volatile uint32_t mk_dbg_log_tail = 0;

// Mira Kernel Debug Print Character
static void mk_dbg_putc(char c) {
    // ? Waits until the transmit buffer is empty before sending
    while ((mk_util_inb(MK_DBG_COM1_PORT + 5) & 0x20) == 0);
    mk_util_outb(MK_DBG_COM1_PORT, c);

    // Add the message to the log buffer
    uint32_t next_tail = (mk_dbg_log_tail + 1) % MK_DBG_LOG_BUFFER_SIZE;
    if (next_tail != mk_dbg_log_head) {
        mk_dbg_log_buffer[mk_dbg_log_tail] = c;
        mk_dbg_log_tail = next_tail;
    }
}

// Mira Kernel Debug Initialization
void mk_dbg_init(void) {
    // * Sets up different parameters for the serial port
    // * Source: https://wiki.osdev.org/Serial_Ports
    mk_util_outb(MK_DBG_COM1_PORT + 1, 0x00);
    mk_util_outb(MK_DBG_COM1_PORT + 3, 0x80);
    mk_util_outb(MK_DBG_COM1_PORT + 0, 0x01);
    mk_util_outb(MK_DBG_COM1_PORT + 1, 0x00);
    mk_util_outb(MK_DBG_COM1_PORT + 3, 0x03);
    mk_util_outb(MK_DBG_COM1_PORT + 2, 0xC7);
    mk_util_outb(MK_DBG_COM1_PORT + 4, 0x0B);
}

int mk_dbg_entry() {
    while (1) {
        uint64_t ticks = mk_pit_get_tick_count();

        mk_dbg_print("Tick: ");
        char tick_str[20];
        mk_dbg_itoa(ticks, tick_str);
        mk_dbg_print(tick_str);
        mk_dbg_print("\n");

        mk_task* current_task = mk_scheduler_get_current_task();
        if (current_task) {
            mk_dbg_print("Current Task ID: ");
            char task_id_str[10];
            mk_dbg_itoa(current_task->id, task_id_str);
            mk_dbg_print(task_id_str);
        }
    }

    return 0;
}

// Mira Kernel Debug Print String
void mk_dbg_print(const char* str) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        mk_dbg_putc(str[i]);
    }
}

// Mira Kernel Debug Integer to ASCII
void mk_dbg_itoa(int n, char* buf) {
    int i = 0;
    
    if (n == 0) {
        buf[i++] = '0';
        buf[i] = '\0';
        return;
    }

    int is_negative = 0;
    if (n < 0) {
        is_negative = 1;
        n = -n;
    }

    while (n != 0) {
        int rem = n % 10;
        buf[i++] = rem + '0';
        n = n / 10;
    }

    if (is_negative) {
        buf[i++] = '-';
    }
    
    buf[i] = '\0';

    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = buf[start];
        buf[start] = buf[end];
        buf[end] = temp;
        start++;
        end--;
    }
}