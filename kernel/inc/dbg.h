#ifndef MK_DBG_H
#define MK_DBG_H

#include <stdint.h>
#include <stddef.h>

#define MK_DBG_COM1_PORT 0x3F8

// Mira Kernel Debug Initialization
// Setups the COM1 serial port for output.
void mk_dbg_init(void);

// Mira Kernel Debug Entry
// Entry point for the debug task.
int mk_dbg_entry(void);

// Mira Kernel Debug Print String
void mk_dbg_print(const char* str);

// Mira Kernel Debug Integer to ASCII
void mk_dbg_itoa(int n, char* buf);

#endif