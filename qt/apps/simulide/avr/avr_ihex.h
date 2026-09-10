/*
 * Intel HEX File Loader for AVR Firmware
 * 
 * Parses Intel HEX format files (.hex) and loads them into AVR flash memory.
 * Supports record types: 00 (data), 01 (EOF), 02 (extended segment), 
 * 03 (start segment), 04 (extended linear), 05 (start linear).
 */

#ifndef AVR_IHEX_H
#define AVR_IHEX_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Maximum firmware size (128KB should be enough for most AVR)
#define IHEX_MAX_SIZE (128 * 1024)

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t start_addr;
} IhexImage;

// Parse a single hex digit
static inline int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Parse a hex byte from two characters
static inline int hex_byte(const char *s) {
    int hi = hex_digit(s[0]);
    int lo = hex_digit(s[1]);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
}

// Load Intel HEX file into buffer
// Returns: number of bytes loaded, or -1 on error
int ihex_load_file(const char *filename, uint8_t *buffer, uint32_t buffer_size, uint32_t *start_addr);

// Load Intel HEX from string
int ihex_load_string(const char *hex_data, uint8_t *buffer, uint32_t buffer_size, uint32_t *start_addr);

// Parse a single Intel HEX record line
// Returns: 0 on success, -1 on error
int ihex_parse_line(const char *line, uint8_t *buffer, uint32_t buffer_size, 
                    uint32_t *base_addr, uint32_t *max_addr);

#endif // AVR_IHEX_H
