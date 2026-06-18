# GTKWave Waveform Photography Guide
## HDMI GPU — Simulation Verification Screenshots

### HOW TO OPEN EACH WAVEFORM
From PowerShell in the project root:
```
cd sim\gpu
gtkwave tb_vga_timing.vcd view_vga_timing.gtkw
gtkwave tb_tmds_encoder.vcd view_tmds_encoder.gtkw
gtkwave tb_char_buffer.vcd view_char_buffer.gtkw
gtkwave tb_uart_rx_gpu.vcd view_uart_rx.gtkw
gtkwave tb_text_render.vcd view_text_render.gtkw
```
Each `.gtkw` file pre-loads the correct signals. Zoom with scroll wheel, pan with middle-click drag.

---

## WAVEFORM 1 — VGA Timing (tb_vga_timing.vcd)

### Screenshot 1A — Full Frame Overview
**Purpose:** Prove correct VSYNC period and line rate.

**Steps in GTKWave:**
1. Open with `view_vga_timing.gtkw`
2. Press `Ctrl+A` (or Edit → Zoom Full) to see the entire simulation
3. Zoom until you can see ~2–3 full VSYNC periods (each ~16.8 ms / 420,000 cycles)
4. Add a time marker at a VSYNC falling edge: right-click on vsync waveform at the falling edge, select "Insert Named Marker"
5. Add another marker at the next VSYNC falling edge
6. Read the time difference (should be ~16.8 ms)

**What must be visible:**
- `vsync` signal with clear periodic negative pulses (2 cycles wide, every ~420,000 cycles)
- `hsync` showing many fast negative pulses (96-cycle wide, one per line)
- `h_cnt` and `v_cnt` as staircase patterns
- Time cursor measurement showing VSYNC period ≈ 16.8 ms

**Photograph:** Full GTKWave window showing ~2 VSYNC periods. Ensure the time scale and signal labels are visible.

---

### Screenshot 1B — HSYNC Pulse Detail
**Purpose:** Verify HSYNC pulse width = 96 pixel clocks = 3.84 µs.

**Steps:**
1. Zoom in to one HSYNC pulse (the `hsync` signal dips low)
2. The pulse should be visible as a rectangular dip, with `h_cnt` shown as a hex or decimal number
3. Use GTKWave's two cursors (press `Shift+Click` to place cursor 2):
   - Cursor 1: on the HSYNC falling edge (h_cnt = 656)
   - Cursor 2: on the HSYNC rising edge (h_cnt = 752)
4. Read the time difference: 96 × 40 ns = 3.84 µs

**What must be visible:**
- `h_cnt` running from 640 through 799 (front porch: 640–655, sync: 656–751, back porch: 752–799)
- `hsync` going low at h_cnt=656 and returning high at h_cnt=752
- `active` going low at h_cnt=640 (start of blanking)
- Time measurement marker showing 3.84 µs pulse width

---

### Screenshot 1C — Active Region Boundary
**Purpose:** Show exactly when pixels are active vs blanked.

**Steps:**
1. Zoom to the start of a scanline (h_cnt=0)
2. Show ~800 pixel clocks (one complete line = 32 µs)
3. The `active` signal should be high for cycles 0–639, then low for 641–799

**What must be visible:**
- `active` HIGH for 640 cycles, then LOW for 160 cycles
- `h_blank` LOW for 640 cycles, HIGH for 160 cycles
- `pix_x` counting from 0 to 639 during active region

---

## WAVEFORM 2 — TMDS Encoder (tb_tmds_encoder.vcd)

### Screenshot 2A — Control Tokens During Blanking
**Purpose:** Verify the 4 TMDS control tokens match DVI spec exactly.

**Steps:**
1. Open with `view_tmds_encoder.gtkw`
2. Zoom to the first 20 clock cycles (the control token test runs immediately at startup)
3. Set signal `tmds_out` display format to Binary (right-click → Data Format → Binary)

**What must be visible:**
- `video_en = 0` (blanking mode)
- `ctrl_1, ctrl_0` cycling: 00 → 01 → 10 → 11
- `tmds_out` showing exactly:
  - {ctrl=00} → `1101010100`
  - {ctrl=01} → `0010101011`
  - {ctrl=10} → `0101010100`
  - {ctrl=11} → `1010101011`
- Each token has the minimum possible transitions (designed for robust clock recovery)

**Key observation to annotate:** The control tokens are specifically chosen to have unique frequency characteristics detectable by the HDMI receiver for clock recovery.

---

### Screenshot 2B — DC Balance Counter
**Purpose:** Show the running disparity counter staying balanced during video encoding.

**Steps:**
1. Zoom to the DC balance test section (after the control token tests, ~200 ns into simulation)
2. `video_en` goes HIGH at this point
3. Show `data_in` incrementing 0x00→0xFF over 256 cycles
4. Show `dut.cnt` oscillating around 0

**What must be visible:**
- `video_en` transitioning from 0 to 1
- `data_in` incrementing (show as hex: 00, 01, 02... up to FF)
- `tmds_out` changing with each clock
- `dut.cnt` (the running disparity, internal signal) staying within ±4
- At end: 1285 ones out of 2560 total bits (DC balance within 0.4% of ideal 50%)

---

### Screenshot 2C — Video/Blanking Boundary
**Purpose:** Show clean transition between blanking (control token) and video (data) modes.

**Steps:**
1. Find the exact clock where `video_en` transitions from 0 to 1
2. Place cursor 1 just before the transition (last control token visible)
3. Place cursor 2 just after (first data symbol visible)
4. Zoom to show ~5 clock cycles before and after the transition

**What must be visible:**
- Last `ctrl=00` control token `1101010100` before transition
- First data-encoded symbol after transition (will be different encoding)
- `dut.cnt` resetting to 0 at the transition boundary

---

## WAVEFORM 3 — Character Buffer (tb_char_buffer.vcd)

### Screenshot 3A — Write Then Read (1-Cycle BRAM Latency)
**Purpose:** Demonstrate that BRAM has exactly 1-cycle read latency.

**Steps:**
1. Open with `view_char_buffer.gtkw`
2. Find the moment when `wr_en` goes HIGH (first write in Test 2)
3. Zoom to show 5 clock cycles around the write and subsequent read

**What must be visible:**
- Clock edge N: `wr_en=1`, `wr_addr=0`, `wr_data=0x41 ('A')`
- Clock edge N+1: `wr_en=0` (write complete)
- Clock edge N+1 or N+2: `rd_addr=0` set
- Clock edge N+3: `rd_data=0x41` appears (1-cycle read latency)

**Annotate the 1-cycle latency arrow** between `rd_addr` assertion and `rd_data` response. This is a key BRAM characteristic — synthesis tools need this pattern to correctly infer BRAM instead of distributed registers.

---

### Screenshot 3B — Clear Operation
**Purpose:** Show the synchronous clear function filling all cells with spaces.

**Steps:**
1. Find the clear test section (near end of simulation)
2. Show `rd_data=0x41` (before clear), then `clear=1` pulsing high, then `rd_data=0x20` (after clear)

**What must be visible:**
- `rd_data=0x41` ('A') — cell not yet cleared
- `clear=1` pulse
- Several clock cycles of wait (clear operates over 2400 cycles internally)
- `rd_data=0x20` (space character) — cell cleared

---

## WAVEFORM 4 — Text Render Pipeline (tb_text_render.vcd)

### Screenshot 4A — 3-Stage Pipeline Delay
**Purpose:** Prove the 3-cycle rendering pipeline is working correctly.

**Steps:**
1. Open with `view_text_render.gtkw`
2. Find the section where `pix_x` starts counting (pixels 0–15)
3. Zoom to show 20 clock cycles

**What must be visible (use arrows/annotations to mark):**
- **Cycle N:** `pix_x=0`, `pix_y=0`, `active=1`
  - `char_addr=0` sent to `char_buffer`
- **Cycle N+1 (Stage 1):** `cb_rd_data=0x41 ('A')` → this is the char code
  - `font_char=0x41`, `font_row=0` sent to `font_rom`
- **Cycle N+2 (Stage 2):** `font_pixel_row=0x08 (= 0b00001000)` → row 0 of 'A'
  - `pixel_col_s2=0` (we're looking at col 0 of char 'A')
- **Cycle N+3 (Stage 3):** `text_pixel=0` (bit 7 of 0x08 is 0 — topmost pixel of 'A' center)

**Draw a 3-cycle arrow** spanning from `pix_x=0` assertion to corresponding `text_pixel` output.

---

### Screenshot 4B — Character Boundary ('A' to 'B')
**Purpose:** Show pixel column boundary between character cells.

**Steps:**
1. Show the transition from `pix_x=7` (last col of 'A') to `pix_x=8` (first col of 'B')
2. Zoom to show ~20 cycles around this boundary

**What must be visible:**
- `cb_rd_data` changing from `0x41 ('A')` to `0x42 ('B')` when `pix_x` crosses 8
- `font_pixel_row` changing accordingly (different row bitmap)
- `text_pixel` showing different pixel patterns for each character
- Character cell width = exactly 8 clock cycles

---

## WAVEFORM 5 — UART Receiver (tb_uart_rx_gpu.vcd)

### Screenshot 5A — Single Byte Reception ('A' = 0x41)
**Purpose:** Show complete UART 8N1 frame with state machine transitions.

**Steps:**
1. Open with `view_uart_rx.gtkw`
2. Find the first byte ('A' = 0x41) — simulation starts around t=20 µs
3. Show the full frame: start bit + 8 data bits + stop bit (~86.8 µs = ~2170 clocks)

**What must be visible:**
- `rxd=1` (idle) → `rxd=0` (start bit) → 8 data bits → `rxd=1` (stop bit)
- Data bits in LSB-first order for 0x41: `1, 0, 0, 0, 0, 0, 1, 0`
- `dut.state` FSM: `0 (IDLE) → 1 (START) → 2 (DATA) → 3 (STOP) → 0 (IDLE)`
- `dut.bit_idx` counting 0→7 during DATA state
- `dut.shift_reg` accumulating bits: ends at `0x41`

**Set `dut.state` display as:** Data Format → ASCII (if supported) or Decimal with values: 0=IDLE, 1=START, 2=DATA, 3=STOP.

**Annotate:** Draw vertical lines at each bit sample point (where `dut.clk_cnt` reaches CLKS_PER_BIT=217). Show these are aligned to bit centers.

---

### Screenshot 5B — rx_valid Pulse and Captured Data
**Purpose:** Show the 1-cycle rx_valid pulse and correct rx_data output.

**Steps:**
1. Find the end of the 'A' byte frame (stop bit ending)
2. Zoom to show ~10 clock cycles around when `rx_valid` pulses

**What must be visible:**
- `rx_valid` going HIGH for exactly 1 clock cycle
- `rx_data=0x41` valid at the same time as `rx_valid`
- `rx_caught=1` latching the pulse (testbench capture register)
- `rx_caught_data=0x41` stable for multiple cycles after the pulse

**Key point:** The 1-cycle `rx_valid` pulse is why the testbench uses a latch register — a polling loop would miss it.

---

### Screenshot 5C — Back-to-Back Bytes ('H' then 'I')
**Purpose:** Show two consecutive bytes received without data loss.

**Steps:**
1. Scroll forward to find bytes 2 and 3 ('H'=0x48, 'I'=0x49)
2. Show the complete frames for both bytes including the inter-frame gap
3. Zoom out enough to see both frames side by side (~180 µs wide view)

**What must be visible:**
- Two complete UART frames with correct idle gaps between them
- `rx_valid` pulsing once per byte
- `rx_caught_data` changing from `0x48` to `0x49` between frames
- FSM returning to IDLE between frames

---

## QUICK REFERENCE: What Each Signal Proves

| Signal | What it proves |
|--------|---------------|
| `hsync` pulse width = 96 cycles | H-sync timing correct per VGA spec |
| `vsync` period = 420,000 cycles | Frame rate ≈ 60 Hz correct |
| `active` = high for 640×480 | Visible window correct |
| `tmds_out` control tokens = exact spec values | TMDS encoder correct |
| `dut.cnt` stays near 0 | DC balance maintained |
| `rd_data` 1 cycle after `rd_addr` | BRAM latency = 1 cycle |
| `text_pixel` 3 cycles after `pix_x` | Pipeline depth = 3 |
| `rx_valid` 1 cycle at end of frame | UART correctly samples stop bit |
| `rx_data = transmitted byte` | UART correctly shifts all 8 bits |
