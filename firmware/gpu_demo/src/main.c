// HDMI GPU demo firmware for Hummingbird E203 RISC-V
// Demonstrates: color bar → text display → UART terminal mode
//
// Build with the same toolchain as hello_world:
//   make -C firmware/gpu_demo

#include "gpu.h"

// UART0 base (for reading characters in non-terminal mode)
#define UART0_BASE  0x10013000UL
#define UART0_REG(off) (*(volatile uint32_t*)(UART0_BASE + (off)))
#define UART_RXDATA 0x04
#define UART_TXDATA 0x00

// Simple delay loop (~1 ms at 27 MHz)
static void delay_ms(uint32_t ms) {
    volatile uint32_t i;
    while (ms--) {
        for (i = 0; i < 2700; i++) {}
    }
}

// Banner printed in text mode
static const char *banner[] = {
    "========================================",
    "  HDMI GPU - Hummingbird E203 RISC-V   ",
    "  640 x 480 @ 60 Hz  |  80 x 30 chars  ",
    "  UART: 115200 8N1                      ",
    "========================================",
    "",
    "Color bars displayed for 5 seconds...",
    "Switching to text mode now.",
    "",
    "Type characters via UART to display them.",
    NULL
};

// Print UART-received characters via GPU (polling mode)
static void uart_echo_loop(void) {
    uint32_t rx;
    while (1) {
        rx = UART0_REG(UART_RXDATA);
        if (!(rx & (1u << 31))) {       // bit 31=empty flag for SiFive UART
            gpu_putchar((char)(rx & 0xFF));
        }
    }
}

int main(void) {
    uint32_t i;

    // --- Phase 1: color bars are shown automatically for 5 s by hardware ---
    // We just wait here; GPU hardware handles the colorbar timer.
    delay_ms(5100);  // slightly more than 5 s

    // --- Phase 2: switch to text mode ---
    gpu_set_text_mode();
    gpu_clear();

    // Print banner
    for (i = 0; banner[i] != NULL; i++) {
        gpu_set_cursor(0, (uint8_t)i);
        gpu_puts(banner[i]);
    }

    // Move cursor below banner
    gpu_set_cursor(0, (uint8_t)(i + 1));

    // --- Phase 3: UART echo ---
    // Option A: enable autonomous terminal mode (GPU handles UART directly)
    gpu_enable_terminal();

    // Option B (alternative): software polling echo
    // uart_echo_loop();   // uncomment to use software-driven echo

    // In autonomous terminal mode, we can do other work here
    while (1) {
        // Blink something or idle
        delay_ms(1000);
    }

    return 0;
}
