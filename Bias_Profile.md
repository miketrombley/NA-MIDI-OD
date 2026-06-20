# Bias_Profile.md — MCP41HV31 bias control reference

Digital-pot bias generator for the overdrive's gating/asymmetry control.
Parallel to `VCA_Profiles.md`. Records the **rails, the center reference, the
code↔voltage map, and firmware wiring** so gating ranges can be set digitally.

Firmware: driver `Core/Src/dpot_mcp41hv.c` / `Core/Inc/dpot_mcp41hv.h`,
wired in `Core/Src/main.c` (`USER CODE BEGIN 2`). See also `CLAUDE.md`.

---

## 1. Circuit

```
MCP41HV31 (7-bit, 128 taps)  wired as a voltage divider:
  P0A = +5V_A   (positive rail)
  P0B = -5V_A   (negative rail — via inverter, only reaches ~-4.35 V)
  P0W (wiper)  ->  OPA1677 buffer (U17)  ->  +V_BIAS

SPI3: CS=PC11, SCK=PC10, MOSI=PC12. Mode 0,0, 8-bit, MSB-first, 1-line TX-only, /16 (2.25 MHz).
SDO unconnected (transmit-only). VL = +3V3_D, V+ = +5V_A, V- = -5V_A.
```

> **⚠️ WLAT must be tied LOW.** On the first board rev WLAT (pin 6) was tied to
> +3V3_D (HIGH) alongside SHDN. WLAT high *inhibits the wiper-register → wiper
> transfer* (datasheet §4.3.2): SPI writes land in the register (CS frames them,
> data clocks in fine) but the analog output stays frozen at the 0x3F POR value
> — looks exactly like a dead bus even though SPI is working. Fix: **WLAT → DGND**
> (bodged on rev 1; fix the schematic for the next spin). SHDN stays HIGH (normal).
> Only tie WLAT to a GPIO if you later want zero-cross-synchronized updates.

The wiper voltage is linear in tap code; in a voltage-divider (potentiometer)
config the wiper resistance does **not** affect the output, so it's a clean
code→voltage map set only by the two rails.

---

## 2. Rails & center (the important part)

The rails are **asymmetric** — the inverter only gets the negative rail to
**~-4.35 V** while the positive rail is **~+5.0 V**. Consequences:

| Point | Code | Voltage | Note |
|---|---|---|---|
| Negative rail (P0B) | 0 | ~-4.35 V | max negative bias = most gated |
| 0 V crossing | ~59 | ~0 V | NOT the center — just where output = 0 |
| **Rail center (mid-scale)** | **63 (0x3F)** | **~+0.284 V** (measured) | **gating reference / POR default** |
| Positive rail (P0A) | 127 | ~+5.0 V | max positive bias |

- **Center of the swing = code 63** = the pot's mechanical mid-scale = the
  geometric midpoint of the rails, `(V_A + V_B)/2 ≈ +0.325 V` (measured +0.284 V
  at code 63; true mid is code 63.5). **This is our gating reference — NOT 0 V.**
  Because the rails are lopsided, 0 V output sits ~4 codes low (code 59).
- **Gating is referenced from code 63**, i.e. negative-going bias *relative to
  +0.28 V*, not relative to 0 V.

**Tap size:** `9.35 V / 127 = 73.6 mV / code`.

---

## 3. Code ↔ voltage math

```
V_bias(code) = V_B + (code/127)·(V_A − V_B)        V_B≈-4.35, V_A≈+5.0, span 9.35 V
code(V)      = 127·(V − V_B)/(V_A − V_B)

check: code 63 -> -4.35 + (63/127)·9.35 = +0.288 V   (measured 0.284 ✓)
```

---

## 4. Firmware mapping

`mcp41hv_set(c, control)` maps `control` 0..1 → code, **symmetric about the
center tap 63** (sacrifices the top LSB to keep ±swing symmetric):

```
code = round(control · 126)
  0.0 -> 0   (~-4.35 V, negative rail)
  0.5 -> 63  (~+0.28 V, rail center / gating reference)   <-- no change needed
  1.0 -> 126 (~+4.85 V, near + rail)
```

So `setPot(0.5)` already lands on the rail center. `mcp41hv_set` only writes SPI
when the resulting code changes → no zipper noise at the 100 Hz pot rate.
`mcp41hv_write(c, code)` is the raw 0..127 path for bench profiling;
`mcp41hv_set_code(c, code)` is the **change-detected** raw-code path used by the
pot mapping below.

### POT6 → bias (current)

POT6 sweeps from **rail center (no gating) up to the positive rail**, with a
**C taper** (anti-log) so the bias moves fast off center then fine-tunes near
the top:

```
POT6 min (0.0) -> BIAS_CODE_MIN = code 63  -> rail center (~+0.28 V, no gating)
POT6 max (1.0) -> BIAS_CODE_MAX = code 127 -> V_A (full positive rail)

curve(x) = (1 − e^(−k·x)) / (1 − e^(−k))          k = BIAS_TAPER_K = 4.5
code     = round(63 + curve(pot6) · (127 − 63))
```

`curve()` is the C/anti-log taper: concave-down, `curve(0)=0`, `curve(1)=1`,
rising fast at the bottom and flattening at the top (bigger `k` = more
aggressive front end). Defined in `main.c` (`USER CODE BEGIN PD`) as
`BIAS_CODE_MIN` / `BIAS_CODE_MAX` / `BIAS_TAPER_K`, driven each 100 Hz pot poll
via `mcp41hv_set_code(&bias, ...)`. Boots at mid-scale (`MCP41HV_CODE_MID` = 63
= POT6 min = center). To change the throw, set `BIAS_CODE_MIN`/`MAX` to the
desired endpoint codes (`code = 127·(V − V_B)/(V_A − V_B)`); to change the
curve aggressiveness, adjust `BIAS_TAPER_K` (use `→0` for ~linear).

> **⚠️ Voltages need re-measuring.** Everything in §2/§3 (rails −4.35/+5.0, center
> +0.28 V, tap 73.6 mV, etc.) was measured with the chip **mounted upside-down and
> bodged 180° off** — those numbers are suspect. The orientation is fixed now;
> re-measure `+V_BIAS` at code 63 (center) and code 0 (V_B) and update §2/§3.

---

## 5. Output smoothing (anti-zipper) — RC + respin

The bias DPOT is the worst zipper source: 7-bit (≈74 mV/code) **and**, unlike the
VCAs, there's **no filter after the wiper** — the OPA1677 (U17) is a plain unity
buffer, so every code change is an instant DC step straight into the bias point.
Firmware now glides the code (TIM7 ISR, ±1 code/tick @ 2 kHz — see CLAUDE.md
"Control smoothing"), which makes steps small + evenly spaced, but a true fix
needs an **output RC**. Both items below are for the next spin.

### Add an RC on the wiper
Place it **before the buffer** (high-Z + input → no DC error, and R adds no output
impedance to `+V_BIAS`):

```
P0W (pin 12) ──[ R ]──┬── OPA1677 +in (pin 3)
                      │
                     [C]
                      │
                     AGND
```

- **Values:** R = 47 kΩ, C = 100 nF → τ ≈ 4.7 ms (fc ≈ 34 Hz). Smears the code
  steps but still tracks a hand-twist instantly. 1–5 ms is the useful range.
- **Why 47 k, not 10 k:** (a) keeps C a tiny 100 nF — 10 k would need ~470 nF for
  the same τ; (b) the wiper resistance varies 0–2.5 kΩ with code and is *in series*
  with R, so at 47 k it's only ~5 % of τ (stable across the sweep) vs ~25 % at 10 k;
  (c) OPA1677 is CMOS (pA bias current) so even 100 k adds no offset — only stay
  ≤~100 k to limit thermal noise / EMI pickup. If you must use 10 k, bump C to
  ~330–470 nF.
- **Ground reference:** cap to **analog ground** is correct — it filters the node
  relative to ground regardless of polarity. Since we only swing 0→+5 V the node
  just sits at a positive DC the whole time.
- ⚠️ This refines §1's "wiper resistance doesn't affect the output": true for the
  **DC voltage**, but the wiper R *is* in series with the RC, so it affects the
  **time constant** — which is exactly why the external R should dominate it.

### Downstream bias-response lag — R12·C19 ≈ 1 s (different node)
**Symptom:** twist POT6 and `+V_BIAS` (buffer output) moves *immediately*, but the
audible bias change drags in over ~1 s. This is **not** the firmware glide (tens of
ms) and **not** the wiper node above — it's in the **drive section**.

- `+V_BIAS` feeds the U4B (OPA1678) **non-inverting input through R12 (1M)**, and
  that node also has **C19 (1µF)** in series to `DRIVE_IN`. The op-amp + input draws
  ~no current, so the only way the node's DC can follow a new `+V_BIAS` is to charge
  C19 through R12:

  ```
  τ = R12 · C19 = 1 MΩ · 1 µF = 1.0 s   (≈63% in 1 s, settled in ~3–5 s)
  ```

  That RC is exactly the lag you feel. The DPOT + OPA1677 buffer are fast (low-Z
  output); the delay is entirely downstream of `+V_BIAS`.
- **Speeding it up (next spin):** lower R12. C19/R12 is also the input high-pass
  corner (`fc = 1/(2π·R12·C19)`), so:
  - R12 = 1M  → τ = 1.0 s,  fc ≈ 0.16 Hz  (current — laggy but transparent)
  - R12 = 100k → τ = 100 ms, fc ≈ 1.6 Hz  (snappy, still fully transparent)
  - R12 = 10k  → τ = 10 ms,  fc ≈ 16 Hz   (feels instant; fc still below guitar's
    ~82 Hz fundamental, so fine. **10k is OK.**)
- Shrinking C19 raises fc faster per unit, so **reduce R12** as the lever, not C19.
- ⚠️ This 1 s lag currently *masks* some bias zipper. If you drop R12, lean on the
  TIM7 code-glide (§5) / an output RC to keep steps smooth — at 10 ms the RC still
  blends ~20 glide steps (0.5 ms apart @ 2 kHz), so anti-zipper survives.

### Rail / supply notes (the ±5V_A that feed the divider)
The bias wiper is a divider between **+5V_A** and **−5V_A**, so rail quality lands
directly on `+V_BIAS`. Assessed 2026-06-20:
- **+5V_A** = LP2985-5.0 LDO (U2) off `+9V_A` (R1 10R + 22u/100n pre-filter). Clean,
  regulated. **BYPASS pin (4) cap: added 10nF** — datasheet low-noise pin; measured
  only a small improvement on this board but harmless, so kept. (Was floating.)
- **−5V_A** = TPS60403 charge-pump inverter (U3, ~250 kHz, 1u flying cap), **unregulated**,
  then **R2 10R + 22u/100n post-filter** (fc ≈ 72 Hz → ~−50 dB at the switch freq).
  Ripple is well controlled — rails are *not* the dominant standby-noise source.
- **Why the rails are asymmetric** (§2's −4.35 / +5.0): the positive is a regulated
  5.00 V LDO; the negative is an *unregulated* charge pump (~12–16 Ω out) **minus the
  R2 10R drop** → ~−4.35 V. Expected, not a fault.
- **DESIGN DECISION — keep R2, the lost −5V headroom is intentional.** The R2 10R
  post-filter trades a bit of negative-rail voltage for ripple rejection, and that's
  a *wanted* trade: the OPA1677 (and OPA1678 drive stage) are **rail-to-rail in/out**,
  so they don't need the full −5 V to swing — the noise reduction is worth more than
  the headroom. Don't "recover" the voltage by shrinking/removing R2. If anything the
  same logic favors the single-supply respin below (no −5 V needed at all).
- **Watch:** the charge pump draws ~250 kHz *pulsed* current **from** the clean +5V_A,
  so it can re-pollute the positive rail. Keep a tight ceramic right at U3's IN pin.
- Standby noise is therefore mostly the **bias node itself** (no RC after the wiper
  buffer + R12 now small) → the wiper RC above is the real lever, not the rails.

### Drop the split rail — ✅ CHOSEN respin direction (2026-06-20)
**Decision:** next spin replaces the high-voltage MCP41HV31 + −5V_A with a cheap
**single-supply 0–5 V SPI pot** (A = +5 V, B = GND, wiper → buffer → 0–5 V `+V_BIAS`).
Confirmed we only ever use 0→+5 V on the bias, so the negative rail buys nothing here.
The wiper RC + small R12 + BP cap fixes above ride along on the same spin.

- **Free resolution from using the whole range.** Today only codes ~63–127 are used
  (positive half) — the bottom half is wasted. A 0–5 V part spreads the *same* musical
  bias range (~0→+5 V) across **all** codes → ~**2× the steps for the same throw =
  half the step size ≈ 6 dB less zipper, at the same bit count and chip cost.**
- **Then grab more bits on top.** 7-bit (128) is a big part of the zipper. An **8-bit
  (256)** part halves the step again; **10-bit (1024)** nearly kills it. Stacked with
  the range win above, 8-bit single-supply ≈ 4× finer than today. Best zipper win there is.
  - Cheap sweet spot: **MCP4151** (8-bit, SPI, single 5 V, ~$1) — drop-in-simple.
    **MCP4161** if you want the same in a NV-capable part. **AD5293** (10-bit) if you
    want to chase zipper to the floor (pricier). 10k end-to-end is fine (keeps divider
    current ~1 mA, low thermal noise).
- **Simpler map.** A true 0–5 V part gives code 0 = 0 V, mid = 2.5 V, full = 5 V —
  linear and predictable. The whole asymmetric-rail mess in §2–§3 (center = +0.28 V
  at code 63, 0 V at code ~59) **goes away** — rewrite §4's POT6 map without the
  `BIAS_CODE_MIN=63` offset (new min ≈ code 0). Re-find the no-gating reference voltage
  empirically (§6) and convert to the new linear code.
- **Buffer.** `+V_BIAS` never goes below 0 V, and the OPA1677 is RRIO, so it can run
  single-supply +5 V — confirm the downstream drive stage (U4B + input) is happy with
  a hair-above-0 minimum. Keep −5 V_A only if other circuitry still needs it.

---

## 6. Status / TODO

- [ ] **Confirm rails** with endpoint reads (`mcp41hv_write(&bias,0)` → V_B,
      `,127)` → V_A). Also proves the SPI link (0.5 alone can't — it's the POR
      default). If endpoints differ from -4.35/+5.0, update §2/§3 and the
      0-V-crossing code.
- [ ] **Find the functional no-gating point** empirically (sweep codes, find
      where gating/asymmetry vanishes by ear/scope) — may differ from both the
      rail center and 0 V; that's the real musical reference.
- [x] Map a physical pot (POT6) → bias in the 100 Hz loop. Done: POT6 min =
      code 63 (center), max = code 25 (~-2.5 V). See §4.
- [ ] Verify on the bench: POT6 max should read ~-2.5 V at +V_BIAS; adjust
      `BIAS_CODE_GATED` if the measured voltage is off (rails may differ).
- [ ] Decide if the gated end should go further (e.g. full -4.35 V at code 0)
      once the gating character is dialed in by ear.
- [ ] **Next-spin bias bundle** (all §5) — chosen 2026-06-20:
      1. swap MCP41HV31 → single-supply 0–5 V SPI pot (8-bit, e.g. MCP4151), drop −5V_A;
      2. add wiper RC (R series P0W→buffer +in, C to AGND, τ ≈ few ms);
      3. keep R12 small (10k now in, response-vs-noise per §5 lag note) — revisit with
         the new RC in place;
      4. LP2985 BYPASS 10nF (done on bench, populate on spin);
      5. rewrite §4 POT6 map for the new linear 0–5 V codes (no code-63 offset).

---

## 7. Measurements log

| Date | Code | control | Measured +V_BIAS | Notes |
|---|---|---|---|---|
| 2026-06-18 | 63 | 0.5 | +0.284 V | rail center; matches model (+0.288 pred) |
