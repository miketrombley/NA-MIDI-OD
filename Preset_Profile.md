# Preset_Profile — SW_2 preset system (`preset.[ch]`)

A single, RAM-only snapshot of the six resolved pot values, with recall, save,
real-time editing, and a "you matched the preset" cue. C port of the **Envelope
Reverb** `PresetManager` (Daisy/STM32H750 project); the state-machine behavior is
identical, only the language and the host wiring differ.

`preset.[ch]` is **effect-agnostic** — it knows only "six pots at values 0..1 and
a saved snapshot." `main.c` owns the meaning of each pot, the SW_2/LED2 wiring,
and the apply path (writing `cv_target[]` / `bias_target`).

## What a preset is
Six floats (0..1), one per pot, = the **resolved** value `eff[i]` (physical knob
*or* MIDI CC, last-mover-wins) at snapshot time. Index order matches POT1..POT6:
HPF1 / LPF1 / LPF2 / VOLUME / GAIN / bias. Lives in RAM (`Preset.preset[6]`),
**lost on power-cycle** — the F105 has no EEPROM; flash persistence (external SPI
flash) is a future add.

Snapshotting `eff[]` (not the driven value) means a save taken **while bypassed**
stores the real POT4/VOLUME setting, not the forced-mute 0 — expected/correct.

## Three modes (`PresetMode`)
- **`PRESET_LIVE`** — pots pass straight through; LED2 off. (Boot state.)
- **`PRESET_PRESET`** — recalled the snapshot. Untouched knobs hold the saved
  value; moving a knob hands that one pot back to live (see the gate below). LED2
  solid **red**. Crossing a knob back onto its saved value flashes **white**.
- **`PRESET_SAVE_ARMED`** — a save is pending; LED2 breathes **white** (1.5 s
  triangle). Controls stay live so you can dial in the sound before committing.

## SW_2 gestures (PA4, debounced, active-low)
Driven from the 100 Hz loop in `main.c` via the `Footswitch` wrapper
(`fsw_hold_ms` / `fsw_rising` / `fsw_falling`):
- **tap** (`< PRESET_HOLD_MS`) → `preset_recall_toggle()`:
  LIVE → recall (if a preset exists) → PRESET · PRESET → LIVE · SAVE_ARMED →
  cancel (restore the pre-arm mode).
- **~1 s hold** (`PRESET_HOLD_MS = 1000`) → `preset_hold_fired()`:
  LIVE/PRESET → arm (SAVE_ARMED, breathe) · SAVE_ARMED → **commit** the snapshot
  → PRESET. Each press fires the hold once (`sw2_hold_fired`); to do the second
  hold you release and press again.

**MIDI:** CC29 (`MIDI_CC_FS2`) press = `preset_recall_toggle()` (a tap). There is
no momentary-CC analogue for hold-to-save, so MIDI only recalls/un-recalls.

## Recall hold-vs-takeover gate (the "tweak back to your preset" feel)
This is the part that makes a recalled preset usable on a pedal with non-motorized
knobs. It lives in `main.c` (the host owns the apply path), not in `preset.c`:

- On **entry into PRESET mode** (recall or commit — detected via `preset_prev_mode`),
  every pot is re-armed: `pot_live[i] = false`, `pot_baseline[i] = eff[i]`.
- Each 100 Hz pass, while in PRESET: a pot stays on its **saved** value
  (`preset_value(i)`) until the physical knob travels past `PRESET_MOVE_EPS`
  (0.02); then `pot_live[i]` latches true and that pot follows the live knob
  forever (until the next recall). LIVE / SAVE_ARMED are fully live.
- The value actually driven is `ctl[i]`; `cv_target[]` / `bias_target` and the
  LED1 gain meter all read `ctl[]`, not raw `eff[]`.

This is **not** a locking soft-takeover — a moved knob takes over immediately (no
jump-wait). The "find your preset" affordance is purely the white cue:

## Knob-match cue (in `preset.c`) — purple edge flash (the "In The Water" look)
`preset_on_pot_move(i, eff[i])` runs for all six pots every poll. In PRESET mode,
when a knob crosses **within `CATCH_TOL` (0.03)** of its saved value, LED2 flashes
purple for `CATCH_FLASH_MS` (350 ms). Edge-triggered via `aligned[]` (fires on the
crossing, not continuously); `seed_alignment()` on recall stops a knob already on
its spot from spuriously flashing on its first move.

## LED2 colors (`preset_led_color`, `PRESET_LEVEL` = 0.60)
- LIVE: off.
- PRESET: cyan `{0, 0.60, 0.60}`.
- match-flash: purple `{0.55, 0, 0.85}` (350 ms edge flash).
- SAVE_ARMED: white breathe, peak 0.60 → trough 0.10, 1.5 s triangle.

## Tuning knobs
- `CATCH_TOL` (preset.c) — match-cue tolerance (~3% travel).
- `CATCH_FLASH_MS` (preset.c) — purple flash length (350 ms).
- `PRESET_MOVE_EPS` (main.c) — knob travel that reclaims a recalled pot.
- `PRESET_HOLD_MS` (main.c) — tap-vs-save threshold.
- breathe period / `PRESET_LEVEL` / `BREATHE_MIN` (preset.c) — LED brightness/timing.

## Build
`Core/Src/preset.c` is in `Debug/Core/Src/subdir.mk` + `Debug/objects.list` (same
manual-entry pattern as `midi.c`; STM32CubeIDE re-adds it on a project scan, the
manual entries keep CLI `make` working).

## Future / planned
- **Flash persistence** — external SPI flash (no EEPROM on F105). Save on commit,
  load at boot; add a magic/version word when serializing.
- **Multiple slots** — `preset[]`/`aligned[]` become 2-D + a slot index; needs a
  selection scheme (only SW_2 is free — likely MIDI Program Change or double-tap).
