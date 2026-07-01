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
HPF1 / LPF1 / LPF2 / VOLUME / GAIN / bias. Held in RAM (`Preset.preset[6]`) and
**persisted to external SPI flash** so it survives a power-cycle (the F105 has no
EEPROM). `preset.c` itself stays HAL-free; persistence lives in
`preset_store.[ch]` on the W25Q16JV — see `Flash_Profile.md`.

Snapshotting `eff[]` (not the driven value) means a save taken **while bypassed**
stores the real POT4/VOLUME setting, not the forced-mute 0 — expected/correct.

## Three modes (`PresetMode`)
- **`PRESET_LIVE`** — pots pass straight through; LED2 off. (Boot state.)
- **`PRESET_PRESET`** — recalled the snapshot. Untouched knobs hold the saved
  value; moving a knob hands that one pot back to live (see the gate below). LED2
  solid **red**. Crossing a knob back onto its saved value flashes **white** (up to
  750 ms, cancelled early if you leave the band).
- **`PRESET_SAVE_ARMED`** — a save is pending; LED2 breathes **white** (1.5 s
  triangle). Controls stay live so you can dial in the sound before committing.
  **Armed from a recalled preset, the recall hold-vs-takeover gate stays in
  effect** — untouched pots keep their saved value through the breathe, so what
  you hear is what will commit (see the commit note below).

## SW_2 gestures (PA4, debounced, active-low)
Driven from the 100 Hz loop in `main.c` via the `Footswitch` wrapper
(`fsw_hold_ms` / `fsw_rising` / `fsw_falling`):
- **tap** (`< PRESET_HOLD_MS`) → `preset_recall_toggle()`:
  LIVE → recall (if a preset exists) → PRESET · PRESET → LIVE · SAVE_ARMED →
  cancel (restore the pre-arm mode).
- **~1 s hold** (`PRESET_HOLD_MS = 1000`) → arm/commit:
  LIVE/PRESET → arm (SAVE_ARMED, breathe) via `preset_hold_fired()` · SAVE_ARMED →
  **commit** via `preset_commit()`. Each press fires the hold once
  (`sw2_hold_fired`); to do the second hold you release and press again. On the
  **commit** edge `main.c` also calls `preset_store_save()` to write the snapshot
  to flash (the erase blocks the loop briefly — see `Flash_Profile.md`). At boot,
  `preset_store_load()` reloads it.

  **Commit stores the GATED panel, not the raw knobs.** `main.c` builds the
  snapshot from the same gate that drives the audio: for a save armed from a
  recalled preset, each pot contributes its **saved** value unless the knob moved
  past `PRESET_MOVE_EPS` since recall, in which case it contributes the **live**
  value. So editing one knob and saving yields "preset + that edit" (copy-then-
  edit), never the whole physical panel. Armed from LIVE there's no gate, so the
  snapshot is the full live panel — a fresh sound saves whole. (The legacy
  `preset_hold_fired()` SAVE_ARMED branch — which snapshots raw `live[]` — is left
  in place for API parity but is no longer the host's commit path.)

**MIDI:** CC29 (`MIDI_CC_FS2`) press = `preset_recall_toggle()` (a tap). There is
no momentary-CC analogue for hold-to-save, so MIDI only recalls/un-recalls.

## Recall hold-vs-takeover gate (the "tweak back to your preset" feel)
This is the part that makes a recalled preset usable on a pedal with non-motorized
knobs. It lives in `main.c` (the host owns the apply path), not in `preset.c`:

- On **entry into PRESET mode** (recall or commit — detected via `preset_prev_mode`),
  every pot is re-armed: `pot_live[i] = false`, `pot_baseline[i] = eff[i]`.
- Each 100 Hz pass, while a preset is in play (PRESET, **or SAVE_ARMED when armed
  from a preset**): a pot stays on its **saved** value (`preset_value(i)`) until
  the physical knob travels past `PRESET_MOVE_EPS` (0.02); then `pot_live[i]`
  latches true and that pot follows the live knob forever (until the next recall
  or commit). LIVE and a SAVE_ARMED armed from LIVE are fully live.
- The value actually driven is `ctl[i]`; `cv_target[]` / `bias_target` and the
  LED1 gain meter all read `ctl[]`, not raw `eff[]`.

This is **not** a locking soft-takeover — a moved knob takes over immediately (no
jump-wait). The "find your preset" affordance is purely the white cue:

## Knob-match cue (in `preset.c`) — white latch-with-cancel flash
`preset_on_pot_move(i, eff[i])` runs for all six pots every poll. In PRESET mode,
when a knob crosses **within `CATCH_TOL` (0.03)** of its saved value, LED2 starts a
white flash that runs up to `CATCH_FLASH_MS` (750 ms) — **but it cancels the instant
the knob leaves the band** (edge-down kills the timer). Edge-triggered via `aligned[]`
(fires on the crossing, not continuously); `seed_alignment()` on recall stops a knob
already on its spot from spuriously flashing on its first move. The flash timer is
shared across pots, so it tracks the most recent knob to enter/leave its band.

## LED2 colors (`preset_led_color`) — matched to InTheWater's palette/brightness
Both boards share the same sp1513 die balance + gamma, and LED2's `max_brightness`
is 1.0 (set in `main.c`), so these render identically to InTheWater.
- LIVE: off.
- PRESET: red `{1.0, 0, 0}` (`RED_LEVEL`, full).
- match-flash: white `{0.70, 0.90, 0.90}` = ITW `kWhite` (up to 750 ms, cancels on leaving band).
- SAVE_ARMED: white breathe = `scale(kWhite, BREATHE_FLOOR + (1−FLOOR)·phase)`,
  trough 40% of kWhite → full peak, 1.5 s triangle (ITW's exact breathe).

## Tuning knobs
- `CATCH_TOL` (preset.c) — match-cue tolerance (~3% travel).
- `CATCH_FLASH_MS` (preset.c) — max white flash length (750 ms; cancels early on exit).
- `PRESET_MOVE_EPS` (main.c) — knob travel that reclaims a recalled pot.
- `PRESET_HOLD_MS` (main.c) — tap-vs-save threshold.
- `WHITE_R/G/B`, `RED_LEVEL`, `BREATHE_FLOOR`, breathe period (preset.c) — LED2
  color / brightness / breathe (all matched to InTheWater).

## Build
`Core/Src/preset.c` and `Core/Src/preset_store.c` are in `Debug/Core/Src/subdir.mk`
+ `Debug/objects.list` (same manual-entry pattern as `midi.c`; STM32CubeIDE re-adds
them on a project scan, the manual entries keep CLI `make` working).

## Future / planned
- **Flash persistence** — **done**: `preset_store.[ch]` on the W25Q16JV (SPI2),
  CRC-checked record with magic/version, save on commit + load at boot. See
  `Flash_Profile.md`.
- **Multiple slots** — `preset[]`/`aligned[]` become 2-D + a slot index; needs a
  selection scheme (only SW_2 is free — likely MIDI Program Change or double-tap).
  On flash, rotate one record per config-block sector (newest-valid-CRC wins).
- **Factory preset** — a built-in default snapshot shipped with the firmware, used
  when flash is blank/corrupt and as a "restore to factory" target (e.g. a reset
  gesture). Bake the six values into a `const` in firmware; on a failed
  `preset_store_load`, seed `preset_load_snapshot()` from it so the unit always
  has a sane recall, and optionally write it to flash on first boot.
