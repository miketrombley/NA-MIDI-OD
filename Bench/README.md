# Bench/ — measured reference data

Bode sweeps captured on a **Digilent Analog Discovery 3** (`Mikes_AD3`,
SN 210415BDD9C9) via WaveForms Network Analyzer. This folder is our standing
reference so we don't re-run sweeps every session: the **Tube Screamer** captures
are the external target we voice toward (irreplaceable — borrowed pedal), and the
**MIDI-OD** captures are our own pedal's characterized state.

Raw files are the WaveForms Bode export verbatim (tab-separated, `Frequency (Hz)`
+ `Channel 1 Magnitude (dB)`, header comments preserved). The tables below are the
**distilled** values — read these first.

## Analyzer setup (constant across captures unless noted)
20 Hz–20 kHz (some 30 kHz), 150–151 steps, Wavegen C1, 1× amplification, settle
10 ms, 16 min periods, Ch1 range 59.5 V, supplies off. Magnitude is absolute
(`Ch1 / Wavegen`, "Relative: no"), so **cross-capture comparison is valid** — all
share the same wavegen reference.

## Methodology / lessons (don't relearn these the hard way)
- **Reference to the device's own bypass.** Engaged-vs-bypass at a single frequency
  conflates the level with the freq shape; **position-to-position deltas cancel the
  shape** (that's how we read tapers cleanly).
- **Ground unused inputs.** A floating right input on the shared stereo IO board
  injected ~−14 dB of crosstalk that masqueraded as VCA feedthrough for several
  captures (the pre-`*_grounded` MIDI-OD mutes). Grounded → real floor is ~−60 dB.
- VCA control law (SSI2160): **−31 mV/dB**; ÷2 divider (10k series + ~10k VC input)
  → **53.2 dB** over full PWM duty (stock). Volume after the 1k‖10k mod: VC ~3.02 V
  full → **~97 dB** commanded. Filters: 0.1866 V/oct, ~8.84 oct full duty.

---

## Tube Screamer — reference target
Drive **max**, Tone **1 o'clock** for the Level-taper set; bypass and min-drive as
noted. Bypass ≈ **−2.8 dB**, flat.

### Level (volume) taper — the basis of our firmware volume curve
Normalized to Level-max = 0 dB:

| Level | ~Rotation | Gain re: max | re: bypass (1 kHz) |
|---|---|---|---|
| Max | 100% | 0 dB | −3.9 dB |
| 3:00 | ~80% | −2.2 dB | — |
| 12:00 | ~50% | −7.6 dB | — |
| 9:00 | ~20% | −16.6 dB | — |
| Min | 0% | ≤ −48 dB | −58.9 dB |

**Fit: gain(dB) ≈ 24·log10(rotation)** — a linear-divider taper, ~24 dB/decade.
This is `VOL_TS_DB_DECADE` in the firmware. Confirmed frequency-flat (Max→12:00 is
−7.6 dB at both 200 Hz and 1 kHz), so the Level pot is a clean broadband divider.

### Min-drive (gain reference) — Drive min, Tone open, Level max
Near unity: **peak −2.45 dB @ ~780 Hz** (≈ +0.35 dB over its own bypass), with a
**midrange hump at ~780 Hz**. This is the level we matched the MIDI-OD's min gain to.

Files: `tube-screamer/`

---

## MIDI-OD — our pedal (post crosstalk-fix / current firmware)
Bypass: **~−6 to −7 dB** mids, gentle treble rise to −3.4 dB @ 20 kHz.

### Volume VCA (POT4, gain max) — after the 1k‖10k VC mod + TS firmware taper
| | re: bypass (1 kHz) |
|---|---|
| Vol max | −3.3 dB (peak; ~matches TS Level-max −3.9) |
| Vol min (mute) | −54.9 dB (−59 dB @ 20 Hz) |

Range ~**51 dB** midband. Floor is the VCA noise floor (~−60 dB abs), not CV.
Firmware: `VOL_TS_DB_DECADE = 24.0`, `VOL_CV_SPAN_DB = 97.4`.

### Gain VCA (POT5) — min lifted to match TS min-drive
`cvout_init` gain `duty_min` tuned **1.000 → 0.810 → 0.680** (final). Min-gain peak:
−12.35 → −8.02 → **−6.36 dB**. Low end lands at **~unity** (re: bypass +0.6 dB @
80–100 Hz) = the TS's near-unity min-drive feel. **Saturates** as min gain rises
(slope halves each duty step — clipper compression onset), so ~−5 dB is the practical
clean ceiling; the residual mid scoop-vs-TS-hump is left to the HPF/LPF player knobs.

Files: `midi-od/`

---

## File inventory
```
tube-screamer/
  ts_bypass.csv
  ts_drive-max_tone-1oc_level-min.csv
  ts_drive-max_tone-1oc_level-9oc.csv
  ts_drive-max_tone-1oc_level-12oc.csv
  ts_drive-max_tone-1oc_level-3oc.csv
  ts_drive-max_tone-1oc_level-max.csv
  ts_drive-min_tone-open_level-max.csv      # gain-min reference
midi-od/
  od_bypass.csv
  od_gain-max_vol-min_mute.csv              # grounded; true ~-60 dB floor
  od_gain-max_vol-max.csv                   # grounded
  od_gain-min_vol-max_final.csv             # gain duty_min 0.68 (TS min-drive match)
```
Diagnostic/superseded captures (crosstalk-corrupted mutes, gain duty 1.0/0.81 steps)
were not archived — they're in the conversation history if ever needed.
