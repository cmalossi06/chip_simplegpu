// HDMI GPU peripheral driver header
// Base address: 0x10014000 (ICB channel O5)

#ifndef GPU_H
#define GPU_H

#include <stdint.h>

// Register base and accessor macro
#define GPU_BASE_ADDR   0x10014000UL
#define GPU_REG(off)    (*(volatile uint32_t*)(GPU_BASE_ADDR + (off)))

// Register offsets
#define GPU_CTRL        0x00  // Control register
#define GPU_STATUS      0x04  // Status register (read-only)
#define GPU_CURSOR      0x08  // Cursor position {row[12:8], col[6:0]}
#define GPU_CHAR        0x0C  // Write char at cursor (auto-advance)
#define GPU_DIRECT      0x10  // Direct write {row[20:16], col[14:8], char[7:0]}
#define GPU_CLEAR       0x14  // Write any value to clear screen

// GPU_CTRL bit fields
#define GPU_CTRL_MODE_TEXT   (1 << 0)  // 0=colorbar, 1=text mode
#define GPU_CTRL_TERMINAL_EN (1 << 1)  // enable autonomous UART terminal
#define GPU_CTRL_CURSOR_VIS  (1 << 2)  // cursor visible

// GPU_STATUS bit fields
#define GPU_STATUS_VSYNC     (1 << 0)
#define GPU_STATUS_TEXT_ACTIVE (1 << 1)

// Display geometry
#define GPU_COLS  80
#define GPU_ROWS  30

// --- API ---

// Switch to text mode (call after colorbar phase ends, or to force it)
static inline void gpu_set_text_mode(void) {
    GPU_REG(GPU_CTRL) |= GPU_CTRL_MODE_TEXT;
}

// Enable terminal mode (GPU autonomously receives UART and displays chars)
static inline void gpu_enable_terminal(void) {
    GPU_REG(GPU_CTRL) |= (GPU_CTRL_MODE_TEXT | GPU_CTRL_TERMINAL_EN);
}

// Clear the screen
static inline void gpu_clear(void) {
    GPU_REG(GPU_CLEAR) = 0;
}

// Set cursor position (col: 0-79, row: 0-29)
static inline void gpu_set_cursor(uint8_t col, uint8_t row) {
    GPU_REG(GPU_CURSOR) = ((uint32_t)row << 8) | col;
}

// Write a single character at the current cursor (cursor auto-advances)
static inline void gpu_putchar(char c) {
    GPU_REG(GPU_CHAR) = (uint8_t)c;
}

// Write a character at a specific position without moving cursor
static inline void gpu_putchar_at(char c, uint8_t col, uint8_t row) {
    GPU_REG(GPU_DIRECT) = ((uint32_t)row << 16) | ((uint32_t)col << 8) | (uint8_t)c;
}

// Print a null-terminated string at the current cursor
static inline void gpu_puts(const char *s) {
    while (*s) {
        gpu_putchar(*s++);
    }
}

// Print string at specific position
static inline void gpu_puts_at(const char *s, uint8_t col, uint8_t row) {
    gpu_set_cursor(col, row);
    gpu_puts(s);
}

// Wait for VSYNC (polls GPU_STATUS bit 0)
static inline void gpu_wait_vsync(void) {
    while (!(GPU_REG(GPU_STATUS) & GPU_STATUS_VSYNC));
    while (  GPU_REG(GPU_STATUS) & GPU_STATUS_VSYNC );
}

// Print decimal integer (simple, no stdlib required)
static inline void gpu_print_uint(uint32_t val) {
    char buf[11];
    int i = 10;
    buf[10] = '\0';
    if (val == 0) { gpu_putchar('0'); return; }
    while (val && i > 0) {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    gpu_puts(buf + i);
}

// Print hexadecimal integer (8 digits)
static inline void gpu_print_hex(uint32_t val) {
    static const char hex[] = "0123456789ABCDEF";
    int i;
    gpu_puts("0x");
    for (i = 28; i >= 0; i -= 4)
        gpu_putchar(hex[(val >> i) & 0xF]);
}

#endif // GPU_H
