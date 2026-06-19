# VCA_Profiles.md — SSI2160 VCA control reference

Five SSI2160 VCAs, each driven by an STM32 PWM → RC filter → control-voltage (VC)
pin. This doc records each VCA's **circuit, measured range, direction, and the
duty values currently in firmware**, plus the **calibration math** so we can set
a requested range digitally (compute a new `cvout_init` duty pair).

Firmware: `Core/Src/main.c` (`USER CODE BEGIN 2` for the `cvout_init` calls),
driver `Core/Src/cvout.c` / `Core/Inc/cvout.h`. Datasheets: SSI2160, SSI2164
AN701 ("Designing VCFs"). See also `CLAUDE.md`.

---

## 1. The CV pipeline (common to all five)

```
PWM (TIMx, 50 kHz, ARR 1439)  ->  10k series + 220nF to gnd  ->  ÷2 divider  ->  SSI2160 VC pin
        duty 0..1                  (RC ~72 Hz, ripple kill)      (see below)       -31 mV/dB
```

- **PWM:** 50 kHz (ARR 1439 @ 72 MHz). TIM4 is forced to this in code
  (`__HAL_TIM_SET_AUTORELOAD(&htim4, 1439)`); TIM2 set in CubeMX.
- **÷2 divider:** the 10k series resistor into the SSI2160's ~10k VC input
  impedance halves the CV. **Full PWM 3.3 V → ~1.65 V at the pin.**
- **Control law:** −31 mV/dB exponential (ground-referenced, 0 V = unity gain).
  - Filters: **0.1866 V/octave** at the pin (6.02 dB/oct × 31 mV/dB).
  - Volume: **32.3 dB/V** at the pin.
- **cvout driver:** `cvout_set(c, control)` maps `control` 0..1 →
  `duty = duty_min + control·(duty_max − duty_min)` → `CCR = duty·(ARR+1)`.
  The two `duty_*` args in `cvout_init` set range **and** direction (swap to invert).

### Duty → pin voltage → effect

```
CV_pin(duty)      = duty · 1.65 V                         (full PWM × ÷2)
filter octaves    = CV_pin / 0.1866 = duty · 8.84 octaves  (corner moves DOWN from ceiling)
filter corner     = ceiling · 2^(−8.84·duty)               ceiling = 1/(2π·R·C) at 0 V
volume attenuation = −CV_pin / 0.031 = −53.2·duty  dB
```

Reference points: `duty 0.555` → ~0.92 V → **~4.9 octaves / −29 dB**;
`duty 1.000` → ~1.65 V → **~8.84 octaves / −53 dB**.
Filters currently use a **0.555** span (~30:1); volumes use **1.0** (full ~−53 dB).

---

## 2. Current map & ranges (measured)

| Pot | VCA | Pin / Ch | Function | Down → Up | Range (measured) |
|---|---|---|---|---|---|
| POT1 | VC_HPF1   | PB3 / TIM2_CH2 | low-cut (HPF) | max cut → open | **~850 Hz → <20 Hz** |
| POT2 | VCA_LPF1  | PB6 / TIM4_CH1 | high-cut (LPF) | darkest → open | **~1.7 kHz → >26 kHz** |
| POT3 | VCA_LPF2  | PB9 / TIM4_CH4 | high-cut (LPF) | darkest → open | **~1.3 kHz → >26 kHz** |
| POT4 | VCA_VOLUME| PB8 / TIM4_CH3 | master out | lowest → highest | **~−50 dB → 0 dB** |
| POT5 | VCA_GAIN  | PB7 / TIM4_CH2 | drive level in | lowest → highest | **~−50 dB → 0 dB** |

POT6 unused. VOLUME is force-muted (control 0) when bypassed.

---

## 3. Per-VCA detail

### VC_HPF1 — low-cut (POT1)
- **Circuit:** AN701 Fig 7 single-pole HPF (LPF + summing-subtractor). R7 = 15k,
  **C12 = 10 nF** (changed from the stock 220 pF, which gave a 48 kHz ceiling).
  Ceiling = `1/(2π·15k·10n)` ≈ 1.06 kHz at 0 V.
- **Firmware:** `cvout_init(&hpf1, &htim2, TIM_CHANNEL_2, 0.000f, 0.555f)`
  (inverted: POT down = 0 V = max cut, POT up = max CV = open).
- **Measured (JFET unloaded, clean):** open corner **< 20 Hz** (+0.2 dB at 18.9 Hz);
  max-cut corner **~850 Hz** (−3 dB), ~23 dB low-end cut at 19 Hz. ~5 octaves.
- **Notes:** earlier readings (~575–625 Hz, +22 dB resonant peak) were corrupted by
  the JFET still loading the output — ignore those.

### VCA_LPF1 — high-cut (POT2)
- **Circuit:** SSI2160 1-pole LPF, stock **220 pF** integrator (≈50 kHz ceiling).
- **Firmware:** `cvout_init(&lpf1, &htim4, TIM_CHANNEL_1, 0.555f, 0.000f)`
  (inverted: POT down = darkest, POT up = open).
- **Measured:** darkest corner **~1.7 kHz**; open **> 26 kHz** (off-scale = wide open).

### VCA_LPF2 — high-cut (POT3)
- **Circuit:** SSI2160 1-pole LPF, stock **220 pF**.
- **Firmware:** `cvout_init(&lpf2, &htim4, TIM_CHANNEL_4, 0.555f, 0.000f)`
  (inverted, matches LPF1).
- **Measured:** darkest corner **~1.3 kHz**; open **> 26 kHz**. Shows a resonant
  peak at the corner when cutting (the HPF1+LPF1+LPF2 bandpass Q — voicing, not a bug).
- History: was briefly parked at ~2 kHz with `duty 0.48` (measured ~2.3 kHz).

### VCA_GAIN — drive level in (POT5)
- **Circuit:** SSI2160 VCA feeding the diode-clipper overdrive stage (input gain).
- **Firmware:** `cvout_init(&gain, &htim4, TIM_CHANNEL_2, 1.000f, 0.000f)`
  (POT down = lowest, POT up = unity/0 dB).
- **Measured (at VCA output, before the clipper):** unity (0 dB) at POT up down to a
  **~−50 dB floor** at POT down. ~50 dB usable. The −50 dB floor is SSI2160 control
  feedthrough, not real signal.

### VCA_VOLUME — master out (POT4)
- **Circuit:** SSI2160 VCA on the post-drive master output.
- **Firmware:** `cvout_init(&volume, &htim4, TIM_CHANNEL_3, 1.000f, 0.000f)`.
- **Measured:** unity (0 dB) → **~−50 dB floor**, ~57 dB at the passband peak.
- **Bypass:** forced to control 0 (full attenuation) when bypassed, so only the dry
  JFET signal comes out (`cvout_set(&volume, bypass_on ? 0.0f : pots[3].value)`).

---

## 4. How to set a new range (compute duty endpoints)

**Filter corner f → duty:**
```
ceiling = 1 / (2π · R · C)          (0 V corner; HPF1 R=15k C=10n → ~1.06 kHz)
duty(f) = log2(ceiling / f) / 8.84
```
e.g. HPF1 to cut at 400 Hz: `duty = log2(1060/400)/8.84 = 1.40/8.84 = 0.159`.
(Clamp to ≤ 0.555 for the current ÷2-limited span; lower duty = higher corner.)

**Volume/gain attenuation A dB → duty:** `duty = A / 53.2`
e.g. −20 dB point: `duty = 20/53.2 = 0.376`.

Then set the `cvout_init(..., duty_min, duty_max)` pair (`min` = control-0/POT-down
end, `max` = control-1/POT-up end; order sets direction).

---

## 5. Future expansion (when we widen the ranges)

- **220 Ω / 10 µF PWM filter** (in place of 10k/220n): same RC time constant, but the
  220 Ω barely divides against the 10k VC input (~98% through vs 50%). **Removes the
  ÷2** → ~doubles every range: filters ~8.8 → ~17 octaves available, volume to ~−106 dB
  (full mute). Watch GPIO inrush (~15 mA peak); don't go below 220 Ω.
- **10 µF series input coupling cap** on a VCA: improves control feedthrough → drops
  the ~−50 dB volume floor toward true silence.
- **Integrator cap** sets a filter's ceiling: ceiling = `1/(2π·R·C)`. Bigger C = lower
  band (HPF1: 220 pF→10 nF moved 48 kHz → ~1 kHz). Swap per desired top corner.
- When the ÷2 is removed, recompute the "8.84" octave constant → **17.7** (and the
  "53.2" dB constant → **106**), since CV_pin then equals the full PWM voltage.

---

## 6. Appendix — reference sweep (HPF1, clean, JFET unloaded, 2026-06-18)

Channel 2 magnitude (dB), relative; 21 steps 18.9 Hz – 26.8 kHz.

| Freq (Hz) | Max (open) | Min (max cut) |
|---|---|---|
| 18.9  | +0.17  | −22.99 |
| 39.1  | +1.89  | −20.19 |
| 80.7  | +5.03  | −16.17 |
| 166.7 | +7.02  | −11.44 |
| 344.5 | +7.98  | −6.68  |
| 711.7 | +8.12  | −2.97  |
| 1023  | +7.84  | −1.37  |
| 1470  | +7.34  | −0.15  |
| 3038  | +6.59  | +1.06  |
| 6278  | +7.11  | +2.65  |
| 26800 | +13.77 | +10.43 |

(The broad +8 dB hump and HF rise are the drive chain's voicing, not the HPF.
Full raw Bode CSVs for all VCAs live in the chat history; detailed per-VCA profiles
to be added when we expand the ranges with the 220 Ω/10 µF mod.)
