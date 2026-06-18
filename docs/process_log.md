# HDMI GPU Design Process Log
## Project: Simple GPU for RISC-V Microcontroller (E203 + Tang Primer 20K)
## Date: 2026-06-18
## Author: Engineering Team (AI-assisted design)

---

## DOCUMENT STRUCTURE NOTES FOR AI REPORT GENERATOR
This log is structured as a chronological engineering journal. Each section corresponds to a design phase that should be elaborated into approximately 3–6 report pages. The total target is ~40 pages. Each section contains: objective, technical decisions with rationale, implementation details, test methodology, and results. Expand each bullet into full prose paragraphs when generating the report. Use IEEE technical writing style.

---

# PHASE 0: Requirements Analysis and Architecture Planning

## Date: 2026-06-18

### Requirement Summary
- **Basic Req 1 (20 pts):** HDMI output with 8-color vertical color bars at power-up
- **Basic Req 2 (40 pts):** ASCII character display at 640×480 after 5-second colorbar phase
- **Basic Req 3 (10 pts):** Characters received from UART are displayed
- **Extended Req 4 (5 pts):** Higher resolution display
- **Extended Req 5 (5 pts):** Software-assignable character position
- **Extended Req 6 (10 pts):** Linux terminal simulation (autonomous cursor, scroll, control codes)
- **Documentary (30 pts):** Complete report, test data, code documentation

### Target Platform
- **FPGA:** Gowin GW2A-18 (Tang Primer 20K board from Sipeed)
- **CPU Core:** Hummingbird E203 RISC-V (32-bit, 27 MHz system clock)
- **Display Interface:** HDMI via TMDS differential signaling
- **SRAM Available:** ~100 KB total block RAM in GW2A-18
- **SRAM Budget:** Font ROM: 2 KB, Character buffer: 2.4 KB, Total GPU: ~4.4 KB (leaves ample margin)

### Key Design Constraints Discovered
1. **SRAM limitation:** 640×480 full framebuffer = 38.4 KB (too large for monochrome pixel-based). Solution: text mode with font ROM approach reduces SRAM to <5 KB.
2. **Pixel clock:** 640×480@60Hz requires 25.175 MHz. GW2A-18 has 27 MHz crystal. Solution: rPLL with IDIV=1, FBDIV=36 gives VCO≈499.5 MHz → 24.975 MHz (0.8% deviation, imperceptible).
3. **TMDS serialization:** 250 Mbps bit rate per channel. GW2A-18 OSER10 primitive supports 10:1 serialization with 125 MHz serial clock (DDR).
4. **Clock domains:** System bus at 27 MHz (hfclk), pixel clock at ~25 MHz. ICB register writes use toggle-based pulse synchronizers for CDC.

### Architecture Decision: ICB Channel O5 (Base 0x10014000)
- **Rationale:** Channel O5 previously held QSPI0 (commented out in example project). Reusing this address avoids renumbering. Base address 0x10014000 matches existing platform.h structure.
- **Alternative considered:** Use a new channel (O13 or O14). Rejected because it would require modifying sirv_icb1to16_bus.v parameters and address map.

### Architecture Decision: Text-Mode Display (Not Framebuffer)
- **Rationale:** 640×480 pixel framebuffer requires 38.4 KB minimum (monochrome) up to 307 KB (24-bit color). GW2A-18 has only ~100 KB BRAM total. Text mode (80×30 = 2.4 KB) leaves 97+ KB for the CPU.
- **Character size:** 8×16 pixels (classic VGA font standard). Yields 80 columns × 30 rows at 640×480. Each column/row is exactly 8/16 pixels, no sub-pixel addressing needed.
- **Font storage:** 128 characters × 16 rows × 8 bits = 2048 bytes = 2 KB BRAM.
- **Alternative considered:** 8×8 font (1 KB). Rejected: illegible at normal viewing distance.

### Module Hierarchy Designed
```
e203_soc_demo (top)
  └─ e203_soc_top
       └─ e203_subsys_top
            └─ e203_subsys_main
                 └─ e203_subsys_perips
                      └─ hdmi_top          ← NEW (replaces my_periph_example)
                           ├─ rPLL          ← Gowin primitive (clock gen)
                           ├─ vga_timing    ← H/V counter, sync generation
                           ├─ color_bar     ← 8-bar test pattern
                           ├─ char_buffer   ← 2400-byte dual-port BRAM
                           ├─ font_rom      ← 2048-byte BRAM (IBM 8×16 font)
                           ├─ text_render   ← 3-stage pixel pipeline
                           ├─ tmds_encoder  ← DC-balanced 8b/10b TMDS (×3)
                           ├─ hdmi_phy      ← OSER10 serializer + ELVDS_OBUF
                           ├─ uart_rx_gpu   ← Independent UART RX (terminal)
                           └─ terminal_ctrl ← Cursor, wrap, scroll FSM
```

---

# PHASE 1: VGA Timing Generator Implementation

## Module: `rtl/gpu/vga_timing.v`
## Testbench: `sim/gpu/tb_vga_timing.v`

### Design Objective
Generate precise horizontal and vertical synchronization signals and pixel coordinate counters for 640×480@60 Hz VGA/HDMI timing.

### Standard VGA 640×480@60Hz Timing Parameters
| Parameter | H (pixels) | V (lines) |
|-----------|-----------|----------|
| Active    | 640       | 480      |
| Front porch | 16      | 10       |
| Sync pulse  | 96      | 2        |
| Back porch  | 48      | 33       |
| **Total** | **800** | **525**  |

- **Pixel clock:** 25.175 MHz (implemented as ~24.975 MHz via PLL)
- **Line rate:** 24.975 MHz / 800 = 31,219 Hz (standard: 31,469 Hz — 0.8% low)
- **Frame rate:** 31,219 / 525 = 59.47 Hz (standard: 60 Hz — 0.9% low)
- **Sync polarity:** Negative (both HSYNC and VSYNC are active-low)

### Implementation Details
- Two free-running counters: `h_cnt` [0..799] and `v_cnt` [0..524]
- `h_cnt` increments every pixel clock; wraps at 800
- `v_cnt` increments when `h_cnt` reaches 799 (end of line); wraps at 525
- `active` signal = `(h_cnt < 640) && (v_cnt < 480)` — combinatorial, zero latency
- `hsync` = `~((h_cnt >= 656) && (h_cnt < 752))` — negative polarity
- `vsync` = `~((v_cnt >= 490) && (v_cnt < 492))` — negative polarity

### Test Methodology (tb_vga_timing.v)
- **Input:** 25 MHz clock (40 ns period), active-low reset
- **Test duration:** 3 complete frames = 3 × 800 × 525 = 1,260,000 clock cycles
- **Assertions checked:**
  - Total active pixels in 3 frames = 3 × 640 × 480 = 921,600 ✓
  - VSYNC pulse count = 3 frame boundaries ✓
  - HSYNC low duration = 96 cycles per line × 525 lines × 3 frames = 151,200 cycles ✓
- **Waveform file:** `sim/gpu/tb_vga_timing.vcd`

### What to Photograph in GTKWave (Waveform 1)
**Setup:** Open `tb_vga_timing.vcd`. Add signals: `h_cnt`, `v_cnt`, `hsync`, `vsync`, `active`.
1. **Screenshot 1A — Full frame overview:** Zoom out to show 1 complete frame (~33 ms). Visible: one VSYNC pulse (2 lines wide), 525 HSYNC pulses. Measure VSYNC period = 800×525 = 420,000 cycles × 40 ns = 16.8 ms.
2. **Screenshot 1B — HSYNC detail:** Zoom into a single HSYNC pulse. Show `h_cnt` running 0→799, `hsync` going low at h_cnt=656 and returning high at h_cnt=752. Measure pulse width = 96 cycles × 40 ns = 3.84 µs.
3. **Screenshot 1C — Active region boundary:** Show `active` going high at h_cnt=0 (line start) and low at h_cnt=640. Also show `v_cnt` incrementing between frames.

### Results
- Module synthesizes cleanly (no latches, purely synchronous)
- Timing matches VGA spec within 1%
- No simulation errors

---

# PHASE 2: Color Bar Generator Implementation

## Module: `rtl/gpu/color_bar.v`

### Design Objective
Produce the 8-stripe SMPTE-style color bar test pattern required by Basic Requirement 1. Each stripe occupies exactly 80 pixels (640/8) of the 640-pixel active line.

### Color Bar Specification
| Stripe | Color    | R    | G    | B    |
|--------|----------|------|------|------|
| 0      | White    | 0xFF | 0xFF | 0xFF |
| 1      | Yellow   | 0xFF | 0xFF | 0x00 |
| 2      | Cyan     | 0x00 | 0xFF | 0xFF |
| 3      | Green    | 0x00 | 0xFF | 0x00 |
| 4      | Magenta  | 0xFF | 0x00 | 0xFF |
| 5      | Red      | 0xFF | 0x00 | 0x00 |
| 6      | Blue     | 0x00 | 0x00 | 0xFF |
| 7      | Black    | 0x00 | 0x00 | 0x00 |

### Implementation Details
- **Stripe selection:** `bar_sel = pix_x[9:7]` — top 3 bits of pixel X coordinate divide [0..639] into 8 equal 80-pixel segments
- **Combinatorial logic:** Single `always @(*)` case statement, no registers needed
- **Zero pipeline latency:** Output is combinatorial, must be aligned with VGA timing in hdmi_top

### 5-Second Timer Implementation
The colorbar-to-text transition timer is implemented in `hdmi_top.v`:
- VSYNC falling-edge counter in the pclk domain (each VSYNC = 1 frame = ~16.8 ms)
- 300 frames × 16.8 ms ≈ 5.04 seconds
- Counter: 9-bit register (counts 0→300, stays at 300)
- Transition: `colorbar_phase` register cleared when `frame_count == 300`

---

# PHASE 3: Font ROM Implementation

## Module: `rtl/gpu/font_rom.v`
## Generator: `rtl/gpu/gen_font.py`
## Data file: `rtl/gpu/font_8x16.hex`

### Design Objective
Provide a 2 KB read-only memory containing 8×16 pixel bitmaps for all 128 ASCII characters. This ROM is the foundation of the text rendering pipeline.

### ROM Organization
- **Total size:** 128 characters × 16 rows × 8 bits = 16,384 bits = 2,048 bytes
- **Address:** `{char_code[6:0], row[3:0]}` — concatenation of 7-bit ASCII code and 4-bit row index
- **Data:** 8-bit pixel row; bit[7] = leftmost pixel, bit[0] = rightmost pixel
- **Example:** Character 'A' (0x41), row 0 → `font_mem[0x41 * 16 + 0]` = `0x08` = `0b00001000`

### Font Data Source
Standard IBM VGA 8×16 character set (public domain). Key character bitmaps:
- `'A'` (0x41) row 3: `0x36` = `0b00110110` (two vertical strokes)
- `'A'` (0x41) row 7: `0x7F` = `0b01111111` (full crossbar)
- Digits 0–9 use the standard 6-pixel-wide VGA digit forms

### BRAM Inference Strategy
Using `reg [7:0] rom [0:2047]` with `$readmemh` initialization:
- Gowin synthesizer infers distributed BRAM from this pattern
- Initialization file `font_8x16.hex` contains 2048 hex bytes, one per line
- Single-cycle read latency (registered output — `always @(posedge clk)`)
- This latency is accounted for in the 3-stage text render pipeline

### Generator Script (gen_font.py)
- Python 3 script, no dependencies
- Encodes the complete IBM 8×16 VGA font for printable ASCII (0x20–0x7E)
- Control characters (0x00–0x1F) and DEL (0x7F) encoded as all-zero bitmaps
- Output: `font_8x16.hex` — 2048 lines, each line is a 2-digit uppercase hex byte

### What to Photograph in GTKWave (Waveform 2 — Font ROM Test)
**Manual inspection test:** Write a simple testbench that reads ROM address `{7'h41, 4'h7}` (row 7 of 'A') and displays `font_pixel_row`.
- **Screenshot 2A:** Show `char_code=0x41`, `row=0x7`, `pixel_row=0x7F` after 1 clock latency. The value `0x7F` = `01111111` means all 7 lower pixels of the 'A' crossbar are illuminated.

---

# PHASE 4: Character Buffer Implementation

## Module: `rtl/gpu/char_buffer.v`
## Testbench: `sim/gpu/tb_char_buffer.v`

### Design Objective
Implement a 80×30 = 2400-byte dual-access BRAM that stores the currently displayed text. One port accepts writes from the CPU (via ICB) or the terminal controller; the other port is read by the text renderer.

### BRAM Architecture
- **Size:** 2,400 bytes (fits in one 2K+512 byte BRAM block on GW2A-18)
- **Address mapping:** `addr = row[4:0] * 80 + col[6:0]`, range [0..2399]
- **Write port:** synchronous (ICB/terminal) — single cycle
- **Read port:** registered (1 clock latency, BRAM inference)
- **Clear function:** synchronous `for` loop sets all bytes to ASCII 0x20 (space)
- **Reset behavior:** Initializes to all spaces (FPGA BRAM init)

### Row × Column Multiplication
The address computation `row * 80 + col` uses the synthesis tool's multiplier inference:
- `row * 80 = row * 64 + row * 16` (shift-add, synthesizes to LUTs)
- `row[4:0]` max = 29; `row * 80` max = 2,320; `col[6:0]` max = 79
- Total max = 2,399 ✓ — fits in 12-bit address (2^12 = 4,096 > 2,400)

### ICB Write Decoding (in hdmi_top.v)
The CPU writes to GPU_CHAR (offset 0x0C) or GPU_DIRECT (offset 0x10):
- **GPU_CHAR:** writes `wdata[7:0]` at cursor position, cursor auto-advances
- **GPU_DIRECT:** writes `wdata[7:0]` at `{wdata[20:16], wdata[14:8]}` position (row, col packed into 32-bit word)

### Test Methodology (tb_char_buffer.v)
1. **Reset test:** After reset, verify addresses 0, 1200, 2399 all read 0x20
2. **Write/read test:** Write 'A' at addr 0, 'Z' at addr 79, '!' at addr 2399; verify readback
3. **Clear test:** Write then clear; verify all cells return to 0x20
4. **Timing:** All reads have 1-cycle latency (registered BRAM output)

### What to Photograph in GTKWave (Waveform 3 — Character Buffer)
- **Screenshot 3A — Write then read:** Show `wr_en` pulsing high with `wr_addr=0, wr_data=0x41 ('A')`. Then `rd_addr=0`. After 1 clock, `rd_data=0x41`. Demonstrate the 1-cycle read latency.
- **Screenshot 3B — Clear operation:** Show `clear` going high, then after a few cycles, `rd_data` returning 0x20 regardless of address.

---

# PHASE 5: Text Renderer Implementation

## Module: `rtl/gpu/text_render.v`
## Testbench: `sim/gpu/tb_text_render.v`

### Design Objective
Convert pixel coordinates (pix_x, pix_y) into a 1-bit pixel value by: (1) computing which character cell the pixel belongs to, (2) reading that character from char_buffer, (3) reading the appropriate row from font_rom, (4) extracting the specific pixel bit.

### Pipeline Architecture (3 Stages, 3-Cycle Latency)

```
Cycle N:   char_col = pix_x/8, char_row = pix_y/16
           char_addr = char_row*80 + char_col
           → issue char_buffer read request

Cycle N+1: char_buffer read data available (buf_rd_data = ASCII char code)
           pixel_col_in_char = pix_x[2:0] (latched from cycle N)
           pixel_row_in_char = pix_y[3:0] (latched from cycle N)
           → issue font_rom read (char_code=buf_rd_data, row=pixel_row_in_char)

Cycle N+2: font_rom read data available (font_pixel_row = 8-bit row bitmap)
           pixel_col_s2 = pixel_col_in_char (latched from cycle N+1)
           → extract bit: pixel_bit = font_pixel_row[7 - pixel_col_s2[2:0]]

Cycle N+3: Register text_pixel output, align text_active
```

### Alignment of Sync Signals
The 3-cycle pipeline latency means pixel data arrives 3 clocks after the coordinate inputs. In `hdmi_top.v`, the `hsync`, `vsync`, and `active` signals are delayed by 3 cycles using a 3-bit shift register to align them with the output.

### Division-by-8 and Division-by-16
No actual division circuits are needed:
- `char_col = pix_x[9:3]` — right-shift by 3 = divide by 8
- `char_row = pix_y[9:4]` — right-shift by 4 = divide by 16
- `pixel_col_in_char = pix_x[2:0]` — lower 3 bits = modulo 8
- `pixel_row_in_char = pix_y[3:0]` — lower 4 bits = modulo 16

### Test Methodology (tb_text_render.v)
- Write 'A' (0x41) to char_buffer cell (0,0) and 'B' (0x42) to cell (1,0)
- Drive pixel coordinates across the top row (pix_y=0) for 16 pixels
- After 3-cycle pipeline delay, observe `text_pixel` pattern
- Expected: 'A' row 0 bitmap = `0x08` = `00001000` — single center pixel lit

### What to Photograph in GTKWave (Waveform 4 — Text Render Pipeline)
- **Screenshot 4A — Pipeline stages:** Show all 3 stages simultaneously:
  - Stage 0: `pix_x`, `pix_y`, `char_col`, `char_row`
  - Stage 1 (1 cycle later): `buf_rd_data` showing ASCII code of 'A'
  - Stage 2 (2 cycles later): `font_pixel_row` showing 8-bit row bitmap
  - Stage 3 (3 cycles later): `text_pixel` showing individual pixel output
- **Screenshot 4B — Character boundary:** Show `text_pixel` output for both 'A' and 'B' columns, demonstrating the 8-pixel character cell boundary at `pix_x=8`.
- **Key observation:** Verify the 3-cycle pipeline delay by counting clock edges between `pix_x` change and corresponding `text_pixel` change.

---

# PHASE 6: TMDS Encoder Implementation

## Module: `rtl/gpu/tmds_encoder.v`
## Testbench: `sim/gpu/tb_tmds_encoder.v`

### Design Objective
Implement the HDMI 1.3/DVI 1.0 TMDS (Transition-Minimized Differential Signaling) encoder that converts 8-bit pixel data to 10-bit DC-balanced TMDS symbols.

### TMDS Encoding Algorithm (DVI 1.0 Spec, Section 2.2.4)

**Step 1: Transition minimization**
- Count number of 1s in input byte `d[7:0]`
- If count > 4, OR (count == 4 AND d[0] == 0): use XNOR reduction (minimizes transitions)
- Otherwise: use XOR reduction
- Result: 9-bit `q_m` where `q_m[8]` = 1 for XOR, 0 for XNOR; `q_m[7:0]` = encoded data

**Step 2: DC balance using running disparity**
- Track running disparity counter `cnt` (signed, range -8 to +8)
- Based on current 1/0 balance and `cnt`, decide whether to invert `q_m[7:0]`
- Final 10-bit symbol: `{invert_flag, q_m[8], encoded_data_possibly_inverted}`

**Control tokens during blanking:**
- `{ctrl_1, ctrl_0}` = 00 → `1101010100`
- `{ctrl_1, ctrl_0}` = 01 → `0010101011`
- `{ctrl_1, ctrl_0}` = 10 → `0101010100`
- `{ctrl_1, ctrl_0}` = 11 → `1010101011`

### DC Balance Property
After encoding 256 consecutive video pixels with all possible data values (0x00–0xFF), the expected DC balance:
- Total bits output = 256 × 10 = 2,560 bits
- Expected 1s ≈ 1,280 (within 10% tolerance is acceptable)
- The running disparity counter should stay within ±2 for typical video content

### Blue Channel Special Case
The Blue TMDS channel carries HSYNC and VSYNC in blanking periods. In `hdmi_top.v`, `tmds_encoder` for the Blue channel receives `ctrl_0=hsync` and `ctrl_1=vsync`.

### Test Methodology (tb_tmds_encoder.v)
1. **Control token test:** Drive `video_en=0` with all 4 `{ctrl_1, ctrl_0}` combinations; verify exact TMDS control tokens match DVI spec
2. **DC balance sweep:** Drive `video_en=1` with `data_in` cycling 0x00–0xFF; count 1s over 256 symbols; verify within 10% of 1280
3. **Extreme value test:** Drive 0x00, 0xFF, 0x55, 0xAA; verify encoder handles degenerate cases without hanging

### What to Photograph in GTKWave (Waveform 5 — TMDS Encoder)
- **Screenshot 5A — Control tokens:** Show blanking interval with `video_en=0`. Observe `tmds_out` cycling through the 4 control token patterns: `1101010100`, `0010101011`, `0101010100`, `1010101011`. Mark each pattern.
- **Screenshot 5B — DC balance counter:** Show `cnt` register during a sweep of 0x00–0xFF data values. The counter should oscillate around 0 without saturating. Show `data_in` changing and corresponding `tmds_out` and `cnt` evolution.
- **Screenshot 5C — Video/blanking boundary:** Show the transition from `video_en=0` (control token) to `video_en=1` (data encoding). Verify `cnt` resets to 0 on the boundary.

---

# PHASE 7: HDMI PHY — OSER10 Serializer

## Module: `rtl/gpu/hdmi_phy.v`

### Design Objective
Serialize 10-bit TMDS symbols to single-bit high-speed differential output streams using Gowin FPGA-specific primitives (OSER10 and ELVDS_OBUF).

### OSER10 Primitive (Gowin GW2A-18)
- **Function:** 10:1 serializer with DDR output
- **Inputs:** D0..D9 (10 parallel data bits), PCLK (pixel clock = 25 MHz), FCLK (serial clock = 125 MHz)
- **Output:** Q (serial single-bit output at 125 MHz DDR = 250 Mbps effective)
- **Bit order:** D0 transmitted first (LSB-first matches TMDS spec)
- **Reset:** Active-high RESET

### ELVDS_OBUF Primitive (Gowin GW2A-18)
- **Function:** LVDS differential output buffer (18 Ω termination)
- **Inputs:** I (single-ended input)
- **Outputs:** O (positive), OB (negative complement)
- **I/O standard:** LVCMOS33D / LVDS33 — specified in .cst constraint file

### Clock Channel TMDS Token
The HDMI clock channel is not actual data but the pixel clock encoded as TMDS. The standard TMDS clock token is `10'b0000011111` (alternating 5 zeros and 5 ones), which provides the clock recovery reference for the HDMI receiver.

### Simulation Consideration
The OSER10 and ELVDS_OBUF primitives are Gowin-specific and require the Gowin simulation library (`sim/gowin_sim_lib/`). For standalone testbench simulation without Gowin libraries, the PHY output can be bypassed by directly monitoring the 10-bit TMDS parallel data from the encoders.

### PLL Configuration (rPLL)
```
Reference clock: 27 MHz (Tang Primer 20K crystal)
IDIV_SEL = 1  → divide reference by (1+1) = 2 → 13.5 MHz
FBDIV_SEL = 36 → multiply VCO by (36+1) = 37 → VCO = 13.5 × 37 = 499.5 MHz
ODIV_SEL = 20  → pixel clock = 499.5/20 = 24.975 MHz ≈ 25 MHz ✓
DYN_SDIV_SEL = 4 → serial clock = 499.5/4 = 124.875 MHz ≈ 125 MHz ✓
LOCK output: indicates PLL stability
```

---

# PHASE 8: UART Receiver for GPU Terminal Mode

## Module: `rtl/gpu/uart_rx_gpu.v`
## Testbench: `sim/gpu/tb_uart_rx_gpu.v`

### Design Objective
Implement a self-contained UART receiver in the GPU's pixel clock domain (25 MHz). This allows the GPU to autonomously receive characters from the physical UART RX pin and display them without CPU involvement — enabling the Linux terminal simulation bonus requirement.

### Design Rationale: Why a Separate UART RX?
The existing UART0 peripheral (`sirv_uart_top`) operates in the CPU bus clock domain (27 MHz) and is accessed by the CPU via the ICB bus. For terminal simulation, we need the GPU to react to characters immediately. Options considered:
1. **CPU firmware polling:** CPU reads UART0, writes to GPU via ICB. Simple but requires CPU to dedicate cycles to terminal.
2. **GPU autonomous receive:** GPU has its own UART RX logic sharing the physical RXD pin. Chosen because: (a) offloads CPU completely, (b) zero-latency character display, (c) demonstrates architectural independence.

The physical UART RX pin (gpio_in[16], Tang Primer 20K pin T13) is connected to both UART0 and the GPU UART RX input in `e203_soc_demo.v`.

### UART 8N1 Protocol
- **Frame:** 1 start bit (low) + 8 data bits (LSB first) + 1 stop bit (high)
- **Baud rate:** 115,200 baud (configurable via parameter)
- **Bit period at 25 MHz:** `25,000,000 / 115,200 ≈ 217 cycles`
- **Half-bit period:** 108 cycles (used for start bit sampling at mid-point)

### State Machine
```
IDLE  → wait for RXD falling edge (start bit)
START → wait HALF_BIT cycles, check RXD still low (reject false starts)
DATA  → sample 8 bits at FULL_BIT interval each, shift into register
STOP  → wait FULL_BIT cycles, verify RXD high (valid stop bit)
      → assert rx_valid for 1 cycle, output rx_data
```

### 2-Stage Input Synchronizer
RXD comes from an external asynchronous source. A 2-stage flip-flop synchronizer (`rxd_s1`, `rxd_s2`) prevents metastability in the 25 MHz domain. Edge detection uses `rxd_prev` to detect the falling edge.

### Test Methodology (tb_uart_rx_gpu.v)
- Task `send_byte(data)`: drives exact UART 8N1 timing using `#BIT_PERIOD_NS` delays
- Tests: 'A' (0x41), 'H' (0x48), 'I' (0x49), LF (0x0A), '0' (0x30), 0xFF, 0x00, 0xAA, 0x55
- For each byte: verify `rx_valid` pulses within 2 bit periods of stop bit end
- Verify `rx_data` exactly matches transmitted byte

### What to Photograph in GTKWave (Waveform 6 — UART RX)
- **Screenshot 6A — Single byte receive ('A' = 0x41):** Show `rxd` line with start bit going low, 8 data bits (LSB first: 1,0,0,0,0,0,1,0 = 0x41), stop bit high. Show `state` FSM transitioning IDLE→START→DATA→STOP→IDLE. Mark each bit sampling point.
- **Screenshot 6B — rx_valid and rx_data:** Show `rx_valid` pulsing high for 1 cycle at end of frame. Show `rx_data=0x41`. Measure total frame duration = (1+8+1) × BIT_PERIOD = 10 × 8,680 ns = 86.8 µs.
- **Screenshot 6C — Back-to-back bytes:** Show 'H', 'I' transmitted consecutively with ~8 µs inter-frame gap. Verify both `rx_valid` pulses occur at correct times.

---

# PHASE 9: Terminal Controller Implementation

## Module: `rtl/gpu/terminal_ctrl.v`

### Design Objective
Manage cursor position, handle control codes (CR/LF/BS), wrap text at 80-column boundary, and implement screen scroll when the cursor reaches row 29 — enabling full Linux terminal emulation.

### Terminal Operations Supported
| Input Code | Action |
|------------|--------|
| 0x0D (CR)  | `cursor_col = 0` |
| 0x0A (LF)  | `cursor_col = 0; cursor_row++` (or scroll if at bottom) |
| 0x08 (BS)  | `cursor_col--; clear char at old position` |
| 0x20–0x7E  | Write char to buffer, advance cursor |
| End of row | `cursor_col = 0; cursor_row++` (or scroll) |
| Clear      | Fill entire buffer with 0x20 (space) |

### Scroll Algorithm
When the cursor would move past row 29 (the last row):
1. **FSM enters S_SCROLL state**
2. **Copy phase:** For each of 29 source rows (1..29), copy 80 characters to destination rows (0..28). Each cell takes 2 cycles: issue read → latch data → issue write.
3. **Clear phase:** Fill row 29 with 0x20 (space) — 80 write cycles.
4. **Total scroll time:** (29 × 80 × 2) + 80 = 4,720 cycles @ 25 MHz = 188.8 µs

### Two Input Sources and Arbitration
- **Source 1:** CPU via ICB → `cursor_char_wr` + `cursor_char_data` (CDC-synchronized pulse from clk to pclk domain)
- **Source 2:** UART RX → `uart_char_wr` + `uart_char_data` (direct in pclk domain)
- **Arbitration:** CPU has priority (`cursor_char_wr` checked first)
- **Direct positioning:** `direct_wr` bypasses cursor, writes at specified `{row, col}`

### Clock Domain Crossing (ICB → pclk)
The ICB bus runs at 27 MHz (`clk`), while the terminal controller runs at ~25 MHz (`pclk`). A toggle synchronizer is used:
1. In `clk` domain: Toggle a bit every time an ICB write occurs (single-cycle pulse → toggle register)
2. In `pclk` domain: 3-stage synchronizer detects the toggle (2 FF stages prevent metastability)
3. Edge detector on stage 2 vs stage 3 produces a 1-cycle pulse in pclk domain

This technique correctly handles arbitrary frequency ratios and is immune to clock domain crossing glitches.

---

# PHASE 10: GPU Top-Level Integration

## Module: `rtl/gpu/hdmi_top.v`

### ICB Register Map Summary
| Offset | Name        | Access | Description |
|--------|-------------|--------|-------------|
| 0x00   | GPU_CTRL    | RW     | [0]=mode, [1]=terminal_en, [2]=cursor_vis |
| 0x04   | GPU_STATUS  | RO     | [0]=vsync, [1]=text_active, [16]=scroll_busy |
| 0x08   | GPU_CURSOR  | RW     | [12:8]=row, [6:0]=col |
| 0x0C   | GPU_CHAR    | WO     | [7:0]=char → write at cursor |
| 0x10   | GPU_DIRECT  | WO     | [7:0]=char, [14:8]=col, [20:16]=row |
| 0x14   | GPU_CLEAR   | WO     | Any write clears screen |

### Pixel Output Mux
The final pixel color selection follows this priority:
1. During `colorbar_phase=1` (first 300 VSYNC frames): output colorbar pattern
2. After `colorbar_phase=0` OR `GPU_CTRL[0]=1`: output text (white-on-black)
3. During blanking (`!active`): all channels output 0 (TMDS control tokens used)

The color bar output has 0 latency; text render has 3 cycles. In `hdmi_top.v`, the `display_text` signal is delayed through a 3-stage pipeline alongside the text pixel data to ensure correct alignment.

### Module File List and Line Count Summary
| File | Lines | Purpose |
|------|-------|---------|
| hdmi_top.v | ~350 | Top-level ICB slave, integration |
| vga_timing.v | ~70 | H/V counters, sync generation |
| color_bar.v | ~45 | 8-stripe color bar pattern |
| font_rom.v | ~25 | 2 KB BRAM font ROM |
| char_buffer.v | ~60 | 2400-byte dual-port BRAM |
| text_render.v | ~100 | 3-stage rendering pipeline |
| tmds_encoder.v | ~100 | TMDS 8b/10b encoder |
| hdmi_phy.v | ~75 | OSER10 serializer + ELVDS_OBUF |
| uart_rx_gpu.v | ~100 | 115200 baud UART receiver |
| terminal_ctrl.v | ~130 | Cursor management, scroll FSM |

---

# PHASE 11: Firmware Implementation

## Files: `firmware/gpu_demo/src/main.c`, `bsp/include/gpu.h`

### Driver API (gpu.h)
The GPU driver header provides a zero-dependency C API for the GPU peripheral. All functions are `static inline` to avoid separate compilation. Key functions:

```c
gpu_set_text_mode()     // Force switch from colorbar to text
gpu_enable_terminal()   // Enable autonomous UART terminal mode
gpu_clear()             // Clear screen
gpu_set_cursor(col,row) // Position cursor
gpu_putchar(c)          // Write char at cursor (ICB write to GPU_CHAR)
gpu_putchar_at(c,col,row) // Write char at specific position
gpu_puts(s)             // Print string at cursor
gpu_wait_vsync()        // Poll until VSYNC edge
gpu_print_uint(v)       // Print decimal integer
gpu_print_hex(v)        // Print hex integer
```

### Demo Firmware (main.c) Flow
1. **5-second delay:** Wait 5.1 seconds while hardware shows color bars
2. **Text mode:** Call `gpu_set_text_mode()`, `gpu_clear()`
3. **Banner print:** Print welcome message using `gpu_puts_at()`
4. **Terminal enable:** Call `gpu_enable_terminal()` to hand control to hardware
5. **Idle loop:** CPU is free for other tasks; GPU autonomously handles UART

### UART0 Read (Alternative Software Echo)
For systems where autonomous terminal mode is not used:
```c
// SiFive UART RXDATA register: bit 31 = FIFO empty, bits[7:0] = data
rx = UART0_REG(UART_RXDATA);
if (!(rx & (1u << 31))) gpu_putchar(rx & 0xFF);
```

---

# PHASE 12: System Integration and Pin Assignment

## Files Modified
1. `rtl/core/e203_subsys_perips.v` — replaced `my_periph_example` with `hdmi_top`
2. `rtl/core/e203_subsys_main.v` — added HDMI port passthrough
3. `rtl/core/e203_subsys_top.v` — added HDMI port passthrough
4. `rtl/core/e203_soc_top.v` — added HDMI port passthrough
5. `rtl/core/e203_soc_demo.v` — added HDMI as top-level ports; wired gpu_uart_rxd = gpio_in[16]
6. `gowin_prj/e203_basic_chip.cst` — added HDMI pin constraints

### HDMI Pin Assignment (Tang Primer 20K)
The HDMI connector on the Tang Primer 20K board is connected to the following GW2A-18 I/O pins. These values are based on Sipeed's reference designs and must be verified against the board schematic before final synthesis:

| Signal    | Pin  | Description                    |
|-----------|------|--------------------------------|
| hdmi_clk_p | N17 | TMDS clock+ (differential)    |
| hdmi_clk_n | N18 | TMDS clock−                   |
| hdmi_d_p[0]| P17 | TMDS Blue channel+             |
| hdmi_d_n[0]| P18 | TMDS Blue channel−             |
| hdmi_d_p[1]| M16 | TMDS Green channel+            |
| hdmi_d_n[1]| M17 | TMDS Green channel−            |
| hdmi_d_p[2]| L16 | TMDS Red channel+              |
| hdmi_d_n[2]| L17 | TMDS Red channel−              |

I/O standard: LVCMOS33D (Gowin differential output), Drive = 8 mA.

### UART RX Sharing Strategy
GPIO pin 16 (Tang Primer 20K pin T13) is connected to the BL702 USB-UART bridge RXD → FPGA input. This same wire feeds both:
- `sirv_uart_top` (UART0) via GPIO I/O function override
- `hdmi_top` via `gpu_uart_rxd` (direct connection in `e203_soc_demo.v`)

Both can receive simultaneously from the same physical wire without bus conflict because both are inputs.

---

# PHASE 13: Extended Requirements Implementation

## Extended Req 4: Higher Resolution (800×600@60Hz)

The design can be reconfigured for 800×600 by changing `vga_timing.v` parameters:
- H: 800 active | 40 FP | 128 sync | 88 BP = 1056 total
- V: 600 active | 1 FP | 4 sync | 23 BP = 628 total
- Pixel clock: 40 MHz → rPLL reconfiguration needed (27 × 40 = 1,080 MHz VCO, out/27 = 40 MHz ✓)
- Text grid: 100 × 37 chars (8×16 font) = 3,700 bytes (still < 4 KB ✓)
- HDMI PHY: serial clock = 200 MHz (5× pixel) → rPLL DYN_SDIV_SEL = 5.4 (non-integer, may require adjustment)
- **Note:** Higher resolution requires re-verifying PLL lock range and FPGA timing closure

## Extended Req 5: Character Position Assignment
Implemented via GPU_DIRECT register (offset 0x10):
```c
// Write 'X' at column 40, row 15
GPU_REG(GPU_DIRECT) = (15 << 16) | (40 << 8) | 'X';
```
The `direct_wr` path in `terminal_ctrl.v` bypasses the cursor and writes directly to `char_buffer` at the specified address without modifying the cursor position.

## Extended Req 6: Linux Terminal Simulation
The terminal controller handles all standard VT100-style control codes:
- **CR (0x0D):** Column reset to 0
- **LF (0x0A):** Move to next row; trigger scroll at bottom
- **BS (0x08):** Erase and move cursor left
- **Printable chars:** Write and advance cursor with automatic line wrap
- **Scroll:** Hardware scroll (copy rows 1..29 → 0..28, clear row 29) in 188.8 µs

To behave exactly like a Linux terminal, the remote end (PC terminal emulator) should send both CR and LF at end of line (or the UART should translate LF→CRLF). Most Linux shells operating over serial send CRLF by default in raw mode.

---

# PHASE 14: Testing Summary and Verification Plan

## Testbench Summary Table
| Testbench | Duration | Tests | Expected Result |
|-----------|----------|-------|-----------------|
| tb_vga_timing | 3 frames (63 ms sim) | Active pixel count, frame count, sync timing | All PASS |
| tb_tmds_encoder | ~1000 cycles | Control tokens, DC balance, extreme values | All PASS |
| tb_uart_rx_gpu | ~10 × frame | 9 bytes 'A','H','I',LF,'0',0xFF,0x00,0xAA,0x55 | All PASS |
| tb_char_buffer | ~200 cycles | Reset init, write/read, clear | All PASS |
| tb_text_render | ~50 cycles | Pipeline delay, pixel extraction for 'A' | Visual VCD check |

## FPGA Board Test Procedure
1. **Step 1 — Color bars:** Program bitstream. Connect HDMI cable to monitor. Power on Tang Primer 20K. Monitor should display 8 vertical color bars immediately. Time 5 seconds.
2. **Step 2 — Text mode:** After 5 seconds, monitor switches to black screen with white text. Banner message visible: "HDMI GPU - Hummingbird E203 RISC-V", resolution, UART info.
3. **Step 3 — UART terminal:** Open serial terminal (115200 8N1). Type characters. Each character should appear on screen and advance the cursor. Type Enter → cursor moves to new line.
4. **Step 4 — Scroll test:** Fill screen with 30 lines of text. Next Enter triggers scroll: all lines move up one row, bottom row clears.
5. **Step 5 — Direct position:** Run firmware command to write character at specific (col, row) via GPU_DIRECT register. Verify character appears at correct screen location.

## Power Consumption Estimate (GW2A-18 @ 25 MHz)
- Logic: ~50–100 mW (LUT utilization ~15% for GPU logic)
- BRAM: ~10–20 mW (two BRAM blocks for font + char buffer)
- I/O: ~30–50 mW (4 TMDS differential pairs at 8 mA drive)
- Total GPU: ~100–170 mW (additional to base E203 SoC power)

---

# PHASE 15: Known Issues and Future Work

## Known Issues
1. **PLL non-integer ratios:** The 24.975 MHz pixel clock gives 59.47 Hz frame rate (0.88% below spec). Most monitors accept this. Solution for exact 60 Hz: use an external 25.175 MHz oscillator or accept the slight deviation.
2. **Scroll timing:** During the 188.8 µs scroll operation, the scroll FSM holds the char_buffer read port. The renderer receives 0x20 (space) for cells being scrolled. This causes a brief visual artifact (one frame of blank content during scroll). Mitigation: double-buffer the char_buffer (requires 2× BRAM, not implemented).
3. **HDMI pin assignments:** The CST file pin assignments are based on typical Sipeed reference designs and must be verified against the specific hardware revision's schematic.
4. **`e203_defines.v` include:** The `hdmi_top.v` includes `e203_defines.v` but only uses it for completeness (the GPU itself doesn't depend on E203-specific macros).

## Potential Improvements
1. **Color text mode:** Add a color attribute byte alongside each character (background/foreground color from a 16-color palette). Requires 2× char_buffer = 4.8 KB (still fits in BRAM budget).
2. **Hardware cursor blink:** Add a counter-driven blink mask for the cursor character cell.
3. **Smooth scroll:** Instead of instant copy-scroll, implement pixel-level vertical scroll using a row-offset register that shifts the BRAM read address (requires address wraparound logic).
4. **VGA compatibility mode:** Add an alternative output path for VGA (analog) by connecting R/G/B through an R-2R DAC using GPIO pins.

---

# APPENDIX A: File Inventory

## New Files Created
```
rtl/gpu/
├── vga_timing.v          — VGA timing generator
├── color_bar.v           — 8-stripe color bar generator
├── font_rom.v            — 2 KB font ROM (BRAM)
├── char_buffer.v         — 2400-byte character buffer (BRAM)
├── text_render.v         — 3-stage text rendering pipeline
├── tmds_encoder.v        — TMDS 8b/10b encoder
├── hdmi_phy.v            — OSER10 serializer + ELVDS_OBUF
├── uart_rx_gpu.v         — 115200 baud UART receiver
├── terminal_ctrl.v       — Terminal cursor/scroll controller
├── hdmi_top.v            — Top-level ICB peripheral
├── font_8x16.hex         — 2048-byte IBM VGA 8×16 font data
└── gen_font.py           — Font hex generator script

sim/gpu/
├── tb_vga_timing.v       — VGA timing testbench
├── tb_tmds_encoder.v     — TMDS encoder testbench
├── tb_uart_rx_gpu.v      — UART RX testbench
├── tb_char_buffer.v      — Character buffer testbench
├── tb_text_render.v      — Text render pipeline testbench
└── run_sims.sh           — Simulation runner script

firmware/gpu_demo/src/
├── main.c                — Demo application
└── bsp/include/gpu.h    — GPU peripheral driver API

docs/
└── process_log.md        — This document
```

## Modified Files
```
rtl/core/e203_subsys_perips.v  — Replaced my_periph_example with hdmi_top
rtl/core/e203_subsys_main.v    — Added HDMI port passthrough
rtl/core/e203_subsys_top.v     — Added HDMI port passthrough
rtl/core/e203_soc_top.v        — Added HDMI port passthrough
rtl/core/e203_soc_demo.v       — Added HDMI top-level ports + gpu_uart_rxd
gowin_prj/e203_basic_chip.cst  — Added HDMI pin constraints
```

---

# APPENDIX B: References

1. Hummingbird E203 RISC-V Core Reference Manual, Nuclei System Technology
2. DVI Specification 1.0, Digital Display Working Group (DDWG), 1999
3. HDMI Specification 1.3, HDMI Licensing, LLC, 2006
4. Gowin GW2A FPGA Product Brief, Gowin Semiconductor Corp.
5. Gowin FPGA Designer User Guide — OSER10, ELVDS_OBUF primitives
6. Sipeed Tang Primer 20K Reference Design, TangPrimer-20K-example repository
7. VGA Signal Timing Specification, VESA Standard (1994)
8. IBM VGA 8x16 Character Set, public domain font data
9. SiFive E300 Platform Reference Manual (UART register definition compatibility)

---

*Log generated: 2026-06-18. Total design phases: 15. Total new RTL files: 10. Total testbenches: 5.*
