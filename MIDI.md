# MIDI.md — UART/TRS MIDI input reference

MIDI control for the overdrive's UI/control chip (STM32F105). Parallel to
`VCA_Profiles.md` / `Bias_Profile.md`. A C/HAL port of the **"In The Water"**
Daisy MidiHandler (`~/Documents/Code - NativeAudio/Daisy/InTheWater/`), so the
two pedals share one CC map and stay MIDI-compatible.

Firmware: parser/dispatch `Core/Src/midi.c` + `Core/Inc/midi.h`, CC numbers
`Core/Inc/midi_map.h`, RX ISR in `Core/Src/stm32f1xx_it.c`, wiring in
`Core/Src/main.c`. See also `CLAUDE.md`.

---

## 1. Transport

```
TRS/DIN MIDI -> USART1 RX (PA10) @ 31250 baud, 8-N-1, RX-only
EXP_MIDI_CNTRL (PA8) = MIDI/expression jack mux select
```

- **UART only** (no USB-MIDI). USART1 was already RX-only; only the baud
  changed (115200 -> **31250**) — set in `MX_USART1_UART_Init` *and* the `.ioc`
  (`USART1.BaudRate=31250`) so a CubeMX regen stays consistent.
- **Interrupt-driven RX.** `USART1_IRQHandler` (in `stm32f1xx_it.c`, USER CODE
  BEGIN 1) services the RXNE flag directly and calls `midi_rx_push(byte)` into a
  256-byte ring buffer. `midi_poll()` drains/parses/dispatches in the main loop,
  so callbacks never run in interrupt context. The NVIC line + RXNE interrupt are
  enabled in `main()` USER CODE 2.
  - **USART1's NVIC is enabled in code, not CubeMX** — keeps the hand-written
    handler from colliding with a generated one on regen. Future option: switch
    to a CubeMX-managed vector (tick "USART1 global interrupt", regen, move the
    body into `USER CODE BEGIN USART1_IRQn 0` + `return`) — see the handler
    comment in `stm32f1xx_it.c`.
- **EXP_MIDI_CNTRL (PA8)** selects the TRS jack's MIDI-vs-expression mux. It
  boots LOW, which we treat as "MIDI selected" (matching InTheWater's D2 sense).
  ⚠️ **Polarity is unconfirmed on this board — verify on the bench.**
  - **PA8 is push-pull for now** — fine while we only ever drive it LOW (MIDI).
    InTheWater drives its equivalent pin **open-drain** (MMBT3904 base pull-up,
    R31). Switch to open-drain only if this board has that same pull-up/inverting
    shifter *and* once we drive it HIGH for expression. See CLAUDE.md.

---

## 2. Architecture (port of InTheWater)

```
USART1 RX ISR ──push──▶ ring buffer ──midi_poll()──▶ parser ──▶ channel filter ──▶ callbacks
 (per byte)            (256 B, SPSC)   (main loop)  (running    (omni / CC#119)   (on_midi_cc)
                                                     status)
```

- **Parser** is a running-status state machine: handles 2-byte (NoteOn/Off, CC,
  PitchBend) and 1-byte (ProgramChange, ChannelPressure) channel-voice messages,
  swallows SysEx (0xF0..0xF7), and ignores System Real-Time (clock) — there's no
  tempo subsystem on this pedal.
- **Channel filter:** boots **omni**. CC#119 locks it (`0` = omni, `1-16` =
  channel); intercepted before the filter so you can re-channel from any channel.
- **Dispatch callbacks** (set NULL to ignore): `MidiCCHandler`,
  `MidiNoteHandler`, `MidiPitchBendHandler`. Only the CC handler is wired today
  (`on_midi_cc` in `main.c`); Note/PitchBend hooks exist for future use.

---

## 3. CC map (`midi_map.h`)

Decade-organized, same numbers as InTheWater so both pedals share a controller
template. Values 0-127 scale to 0.0-1.0.

| CC | Name | Target | Action |
|---|---|---|---|
| 20 | POT1 | VC_HPF1 (low-cut)    | sets value, hysteresis vs knob |
| 21 | POT2 | VCA_LPF1 (high-cut)  | sets value, hysteresis vs knob |
| 22 | POT3 | VCA_LPF2 (high-cut)  | sets value, hysteresis vs knob |
| 23 | POT4 | VCA_VOLUME (master)  | sets value, hysteresis vs knob |
| 24 | POT5 | VCA_GAIN (drive in)  | sets value, hysteresis vs knob |
| 25 | POT6 | bias DPOT (gating)   | sets value, hysteresis vs knob |
| 26 | FS1  | bypass footswitch    | value>0 = press → toggles bypass |
| 27 | FS2  | LED2 (SW_2 sim)      | value>0 = press → toggles LED2 |
| 28 | EXPRESSION | (future)       | not yet wired |
| 29 | EFFECT_ONOFF | bypass       | 0 = bypassed, 127 = engaged (strict) |
| 119 | CHANNEL | accept channel   | 0 = omni, 1-16 = lock to channel |
| 120 | ALL_SOUND_OFF | panic      | force bypass (channel-agnostic) |
| 123 | ALL_NOTES_OFF | panic      | force bypass (channel-agnostic) |

CC 20-25 land on `pots[0..5]` directly (CC − 20 = pot index).

---

## 4. Knob ↔ MIDI arbitration (last-mover-wins hysteresis)

Verbatim port of InTheWater's `config_applyMidi` (`midi_apply` in `main.c`).
Each pot keeps `midi_val / midi_rx / using_midi / last_knob / last_midi`:

- Before any CC for a pot: the **physical knob** passes through untouched.
- After a CC arrives, whichever moved more (knob vs MIDI, threshold **0.02**)
  owns the pot. A deliberate knob twist always reclaims control; while MIDI owns
  the pot, `last_knob` is frozen so ADC jitter can't falsely steal it back.

The resolved value (`eff[i]`) feeds the same `cvout_set` / bias / LED-meter path
the raw knob used to — so MIDI and the physical pots are fully interchangeable.

`midi_rx[i]` **latches** (a pot stays MIDI-capable once it's seen one CC); it is
not cleared per loop.

---

## 5. Status / TODO

- [x] UART MIDI in (USART1 @ 31250, IRQ + ring buffer), parser, channel filter,
      CC#119 channel lock, panic.
- [x] CC 20-25 → 6 pot targets with last-mover-wins hysteresis; CC29/FS1 bypass;
      FS2 → LED2.
- [ ] **Confirm EXP_MIDI_CNTRL (PA8) polarity** for the MIDI/expression mux.
- [ ] **Bench-test** end to end: send CC 20-25, confirm each VCA/bias responds and
      that twisting the real knob reclaims it.
- [ ] Expression input (CC#28 + ADC_EXPRESSION PA2/IN2) — not started.
- [ ] MIDI Note / PitchBend handlers (hooks exist, unused — no synth on this MCU).
- [ ] Persist the MIDI channel (CC#119) across power cycles (RAM-only today; F105
      has no flash-settings store yet).
