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

## 5. Status / TODO

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

---

## 6. Measurements log

| Date | Code | control | Measured +V_BIAS | Notes |
|---|---|---|---|---|
| 2026-06-18 | 63 | 0.5 | +0.284 V | rail center; matches model (+0.288 pred) |
