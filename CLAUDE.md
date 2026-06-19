# MIDI-OD — project notes

STM32F105RBTx (Cortex-M3, no FPU) UI/control firmware for a USB-MIDI overdrive
pedal. Pure interface chip — **no audio processing** on this MCU. C / STM32 HAL,
CubeMX-generated `main.c`. Platform drivers (`led`, `led_rgb`, `pot`,
`footswitch`, `sp1513`) were ported from the "In The Water" Daisy/STM32H750
project (`~/Documents/Code - NativeAudio/Daisy/InTheWater/platform/`).

## ⚠️ PA15 / JTAG remap (don't remove)
`JFET_BYPASS` is on **PA15, which is JTDI** (a JTAG pin). Even with SYS Debug =
**Serial Wire** set in CubeMX, this CubeMX/F1 combo does **not** emit the SWJ
remap into `MX_GPIO_Init`, so PA15 won't toggle as GPIO on its own. We free it
in code, in `USER CODE BEGIN 2`:

```c
__HAL_RCC_AFIO_CLK_ENABLE();
__HAL_AFIO_REMAP_SWJ_NOJTAG();   // JTAG off, SWD kept (ST-Link still works)
```

Keep this. It survives regeneration (USER CODE block) and is required, not
redundant. Never use `SWJ_DISABLE` (kills SWD too).

## Clock
16 MHz crystal → PREDIV1 ÷2 → PLL ×9 → 72 MHz. USB 48 MHz, APB1 36 / APB2 72,
ADC 12 MHz. Already configured; don't touch unless USB timing changes.

## Pin / peripheral map
- **LEDs** (common-anode, active-low, 50 kHz PWM, ARR 1439):
  - LED2 (right): TIM3 CH1/2/3 = PA6/PA7/PB0 (R/G/B)
  - LED1 (left): **split timers** — R = TIM3_CH4 (PB1), G/B = TIM2 CH3/CH4 (PB10/PB11)
- **Pots** (ADC1, blocking poll @100 Hz, rail-corrected + hysteresis):
  POT1 PA0/IN0 · POT2 PC3/IN13 · POT3 PC0/IN10 · POT4 PA1/IN1 · POT5 PC2/IN12 · POT6 PC1/IN11
- **Footswitches** (active-low, internal pull-ups, debounced):
  SW_1 PA5 · SW_2 PA4
- **Bypass out**: JFET_BYPASS PA15 — `LOW (0) = bypass ON`, `HIGH (1) = bypass OFF`. Boots bypassed.
- Other: ADC_EXPRESSION PA2/IN2, EXP_MIDI_CNTRL PA8, SPI2_CS PB12, SPI3_CS PC11, USART1, USB_OTG_FS (Device).

## Subsystem profile docs
- **`VCA_Profiles.md`** — the 5 SSI2160 VCAs (PWM→RC→CV), measured ranges, and the
  duty↔effect calibration math.
- **`Bias_Profile.md`** — the MCP41HV31 bias DPOT: rails, center reference,
  code↔voltage map, POT6 mapping. Read it before touching bias code.
- **`MIDI.md`** — UART/TRS MIDI input: transport, parser/dispatch architecture,
  the CC map, and the knob↔MIDI hysteresis. Read it before touching MIDI code.
- **`Preset_Profile.md`** — the SW_2 preset system (`preset.[ch]`): the snapshot,
  the recall hold-vs-takeover gate, the knob-match cue, and LED2 states. Read it
  before touching preset, SW_2, or LED2 code.

## Bias control (SPI3 DPOT — `dpot_mcp41hv.[ch]`)
MCP41HV31 7-bit digital pot (U15) wired as a voltage divider (P0A=+5V_A,
P0B=−5V_A), wiper buffered (OPA1677) → `+V_BIAS`. SPI3 (CS=PC11/DPOT_CS,
SCK=PC10, MOSI=PC12), Mode 0,0, 1-line TX-only, /16. Write = `{0x00, code}`.
- **Rails are ASYMMETRIC** (~−4.35 / +5.0 V via inverter), so mid-scale **code 63
  ≈ +0.28 V is the gating center, NOT 0 V** (0 V output ≈ code 59).
- **WLAT must be tied LOW.** Rev-1 had it HIGH (with SHDN) → SPI writes land but
  the wiper output stays frozen (looks like a dead bus). Bodged to DGND; fix the
  schematic next spin.
- Driver change-detects (`mcp41hv_set_code`) so it can be called every 100 Hz
  loop with no zipper noise. **Don't** add an open `while(BSY)` wait — in 1-line
  BIDIMODE the BSY flag never clears and it hangs the MCU on boot.

## Control mapping (current)
- SW_1 → bypass (PA15). LED1 = engage indicator (on = effect in circuit).
- SW_2 → preset: tap = recall / un-recall, ~1 s hold = arm save, second hold =
  commit. LED2 = preset status (off / cyan / purple match-flash / breathing white).
  Single RAM-only snapshot — see `Preset_Profile.md`.
- POT1–POT5 → the 5 SSI2160 VCAs (HPF/LPF/LPF/VOLUME/GAIN) — see `VCA_Profiles.md`.
- POT6 → bias DPOT: center (code 63, no gating) → positive rail (code 127), C taper
  (`BIAS_TAPER_K` = 4.5). See `Bias_Profile.md`.
- **MIDI in** (CC 20–25 → POT1–POT6 targets) coexists with the physical pots via
  last-mover-wins hysteresis; CC29/FS1 → bypass, FS2 → preset recall toggle (no
  momentary-CC analogue for hold-to-save). See `MIDI.md`.
- The rainbow animation (`led_demo`) was a power-on pulse check; **removed** now
  that LED1 (gain meter) and LED2 (preset) have real jobs. `led_demo.[ch]` still
  compiles but is unused.
- **TODO**: preset persistence to flash (F105 has no EEPROM — needs external SPI
  flash); multi-slot presets if desired.

## Control smoothing / anti-zipper (TIM7 ISR)
A fast knob twist (or a big MIDI jump) used to step the VCA CVs / bias code at the
100 Hz loop rate — an audible "zzz" staircase, worst on **gain** and **bias**.
Fixed by splitting "decide the value" from "drive the output":
- The 100 Hz loop now only writes **targets** — `cv_target[5]` (HPF1/LPF1/LPF2/
  VOLUME/GAIN) and `bias_target` (DPOT code). Globals in `main.c` USER CODE PV.
- **`TIM7` update ISR @ `CTRL_SMOOTH_HZ` (2 kHz)** does the actual writes
  (`ctrl_smooth_tick` in `main.c`, called from `TIM7_IRQHandler` in
  `stm32f1xx_it.c`): one-pole slew per VCA (`CV_SMOOTH_COEF` 0.10 ≈ 4.8 ms τ) +
  bias **code-glide** (±1 code/tick). This is our stand-in for InTheWater's
  per-sample `fonepole` (it runs at 48 kHz in the audio callback; we have no
  audio loop, so a timer carries it).
- **TIM7 is hand-configured in code, NOT in CubeMX** (same pattern/rationale as
  USART1 below): `ctrl_smooth_start()` enables the clock + NVIC and the handler
  lives in a USER CODE block, so both survive regen with no generated-handler
  collision. Don't tick TIM7 in the `.ioc`. **NVIC priority 7 = below USART1 (6)**
  so MIDI RX always preempts the smoother (no dropped bytes). SPI bias writes run
  *inside* this ISR but only fire while gliding (change-detect) — fine at ~7 µs.
- **Future option** (let CubeMX reserve + own TIM7, so a regen can't reassign it):
  enable TIM7 in the `.ioc` (timebase @ 2 kHz) + tick its global interrupt, regen,
  move the `ctrl_smooth_tick()` call into `HAL_TIM_PeriodElapsedCallback`, and drop
  the manual clock/NVIC in `ctrl_smooth_start()`. Full steps in the `TIM7_IRQHandler`
  comment in `stm32f1xx_it.c` (mirrors the USART1 note).
- **VCA zipper is fully gone** (the PWM has a real RC after it). **Bias is only
  reduced, not eliminated** — the DPOT has NO RC after the OPA1677 buffer, so each
  code is still an instant DC step; glide just makes them small + evenly spaced.
  **Proper bias fix = add an output RC on `+V_BIAS`** (hardware; see `Bias_Profile.md`).
- Tuning: smoother/slower = lower `CV_SMOOTH_COEF`; faster glide = raise
  `CTRL_SMOOTH_HZ` (re-derive PSC/ARR in `ctrl_smooth_start`).

## MIDI input (UART/TRS — `midi.[ch]`, `midi_map.h`)
C/HAL port of InTheWater's MidiHandler. USART1 RX (PA10) @ **31250** baud,
RX-only, interrupt-driven → 256-byte ring buffer → `midi_poll()` parses +
dispatches in the main loop. Full detail in `MIDI.md`; the load-bearing bits:
- **Baud is 31250** (was 115200), set in `MX_USART1_UART_Init` *and* the `.ioc`.
- **USART1 NVIC is enabled in code, NOT in CubeMX.** The `USART1_IRQHandler`
  lives in `stm32f1xx_it.c` (USER CODE BEGIN 1) and services RXNE directly. Do
  **not** tick "USART1 global interrupt" in the `.ioc` *unless* you also move the
  handler body — a generated handler would otherwise collide with the
  hand-written one. The NVIC enable + `UART_IT_RXNE` live in `main()` USER CODE 2.
- **Future option** (switch to CubeMX-managed vector): tick "USART1 global
  interrupt" in the NVIC tab, regenerate, and move the RXNE service into the
  generated `USART1_IRQHandler`'s `USER CODE BEGIN USART1_IRQn 0` block + `return;`.
  Steps are spelled out in the handler comment in `stm32f1xx_it.c`.
- **EXP_MIDI_CNTRL (PA8)** = MIDI/expression jack mux; boots LOW = MIDI selected
  (InTheWater D2 sense). ⚠️ Polarity unconfirmed on this board — verify on bench.
- **PA8 is push-pull on purpose (for now).** MIDI works driving it LOW, so
  push-pull is correct while we're MIDI-only. InTheWater drives the equivalent
  pin **open-drain** because its MMBT3904 level-shifter has a base pull-up (R31).
  Switch PA8 to open-drain ONLY if this board has that same pull-up / inverting
  shifter AND once we start driving it HIGH for expression — check the schematic
  then. Until then, leave push-pull (it's one regen-safe line in USER CODE).
- New `Core/Src/midi.c` must be in the build: added to `Debug/Core/Src/subdir.mk`
  and `Debug/objects.list` (STM32CubeIDE regenerates both from a project scan, so
  an IDE build re-adds it automatically; the manual entries keep CLI `make` working).

## Next / planned (not started)
- **TRS-jack mux layer (deferred — fix later).** The jack carries MIDI *or* an
  expression pedal, never both. InTheWater wraps this in `setMidiActive()` /
  `isMidiActive()` (drive the PA8 mux) + **dropping incoming MIDI while in
  expression mode** + PA9/EXP_ENABLE jack-detect. We have NONE of it — firmware
  is permanently in MIDI mode (PA8 boots LOW), which is correct while MIDI-only.
  Build this when adding expression-pedal input (ties PA8 mux + PA9 detect +
  PA2 ADC together). Not needed for MIDI testing.
- **Expression input** (ADC_EXPRESSION PA2/IN2; MIDI side is CC#28, also unwired).
- **MIDI channel (CC119) persistence** → external SPI flash (F105 has **no
  EEPROM**); load at boot via `midi_set_channel()`, save on change. RAM-only today.
- **MIDI Note / PitchBend** handlers (dispatch hooks exist in `midi.c`, unused —
  no synth on this MCU; the parser still skips them to stay byte-aligned).
- **DFU** firmware-update support.

## Gotchas learned
- Active-low full-OFF must not be a bright-then-off double write: with OC preload
  on, the bright value can latch for one PWM period (faint glow). Gate "off"
  through the brightness cap = 0 (same path as pot-at-zero), not a post-render
  override.
- `powf` gamma runs in software (no FPU). Fine at UI rates; if animations bog
  down, precompute a gamma LUT.
- **Active-low LED boot flash:** a TIM PWM pin goes AF *before* its channel is
  enabled; in that gap the pin sits LOW = LED ON, so the LEDs flash during boot
  (the USB init makes the window long/visible). Fix: preload each LED channel's
  compare to OFF (`CCR = ARR+1`, 100% high) and `HAL_TIM_PWM_Start` it inside the
  `USER CODE BEGIN TIM2/3_Init 2` blocks — i.e. *before* `HAL_TIM_MspPostInit`
  switches the pin to AF. Pin then goes high-Z → AF-high (off), never AF-low.
  Lives in USER CODE so it survives regeneration; don't move the LED PWM start
  back into the main `USER CODE 2`.
