/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "led_rgb.h"
#include "pot.h"
#include "footswitch.h"
#include "cvout.h"
#include "dpot_mcp41hv.h"   /* MCP41HV31 bias control (SPI3) */
#include "midi.h"           /* UART/TRS MIDI input (USART1 @ 31250) */
#include "midi_map.h"       /* CC number assignments */
#include "preset.h"         /* SW_2 preset snapshot/recall state machine       */
#include "w25q.h"           /* W25Q16JV external SPI NOR flash (SPI2, CS=PB12)  */
#include "preset_store.h"   /* preset persistence on the W25Q flash            */
#include <math.h>   /* powf — LED1 gain-curve meter; fabsf — MIDI hysteresis */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Bias DPOT range (POT6 min -> max), C taper over the useful 2.8 .. 3.7 V window
 * (see Bias_Profile.md):
 *   POT6 min -> BIAS_CODE_MIN = code 98  = ~+2.8 V
 *   POT6 max -> BIAS_CODE_MAX = code 111 = ~+3.7 V
 * The bias only does something musical in this band, so POT6 sweeps just it.
 * Codes from the measured endpoints (code 63 = 0.287 V, code 127 = 4.844 V,
 * linear in code):
 *   code(V) = 63 + (V - 0.287)*(127-63)/(4.844-0.287)
 *   code(2.8 V) ~= 98 (2.78 V) ; code(3.7 V) ~= 111 (3.70 V)                    */
#define BIAS_CODE_MIN   98     /* ~+2.8 V — bottom of the useful bias window      */
#define BIAS_CODE_MAX  111     /* ~+3.7 V — top of the useful bias window         */
/* C taper (anti-log): fast rise off the bottom, fine resolution near the top.
 * curve(x) = (1 - e^-k x) / (1 - e^-k); larger k = more aggressive front end.  */
#define BIAS_TAPER_K   2.5f

/* Volume taper (POT4): match a Tube Screamer Level pot's sweep. A TS Level is a
 * linear voltage divider, so its perceived taper is ~ -VOL_TS_DB_DECADE*log10(rot)
 * (measured fit ~24 dB/decade: 2.2 / 7.6 / 16.6 dB down at 3:00 / 12:00 / 9:00 -
 * most of the loudness lives in the top of the rotation). We turn that desired
 * attenuation into a cvout control by dividing through the volume VCA's full
 * commanded CV span: VC ~3.02 V after the 1k||10k series mod, at -31 mV/dB -> ~97 dB.
 * See VCA_Profiles.md. */
#define VOL_TS_DB_DECADE  24.0f   /* TS Level taper steepness (dB per decade of rot) */
#define VOL_CV_SPAN_DB    97.4f   /* full-scale commanded atten: 3.02 V / 31 mV-per-dB */

/* --- Control smoothing (anti-zipper, TIM7 ISR) ----------------------------
 * The 100 Hz loop only sets *targets*; a TIM7 update ISR at CTRL_SMOOTH_HZ does
 * the actual output writes — (a) one-pole-slews each VCA's CV toward its target
 * and (b) glides the bias DPOT one code per tick toward its target. A fast knob
 * twist (or a MIDI jump) then ramps instead of stepping, so the staircase no
 * longer lands in the audio band as "zzz". This is our equivalent of the
 * per-sample fonepole InTheWater runs in its 48 kHz audio callback — except we
 * have no audio loop, so a dedicated timer carries it.
 *   CV_SMOOTH_COEF: one-pole alpha in  y += a*(target - y).  0.10 @ 2 kHz ~=
 *   4.8 ms time constant — smooth yet still tracks a 100 Hz target with no
 *   audible lag. Lower = smoother/slower, higher = snappier/buzzier.
 * NOTE: the VCA path has a real RC after the PWM, so smoothing fully removes its
 * zipper. The bias DPOT has NO RC after the OPA1677 buffer, so each code is an
 * instant DC step — gliding makes the steps small + evenly spaced (much quieter)
 * but a true fix still needs an output RC on +V_BIAS (see Bias_Profile.md). */
#define CTRL_SMOOTH_HZ   2000u   /* TIM7 tick: PSC 71 -> 1 MHz, ARR 499 -> 2 kHz */
#define CV_SMOOTH_COEF   0.10f
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

SPI_HandleTypeDef hspi2;
SPI_HandleTypeDef hspi3;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart1;

PCD_HandleTypeDef hpcd_USB_OTG_FS;

/* USER CODE BEGIN PV */
LedRgb led1;   /* LEFT  — R:TIM3_CH4(PB1)  G:TIM2_CH3(PB10)  B:TIM2_CH4(PB11) */
LedRgb led2;   /* RIGHT — R:TIM3_CH1(PA6)  G:TIM3_CH2(PA7)   B:TIM3_CH3(PB0)  */
Pot    pots[6];  /* POT1..POT6 — read pots[i].value (0..1) after polling */
Footswitch sw1, sw2;       /* SW_1 (PA5) = bypass footswitch, SW_2 (PA4) = preset */
bool   bypass_on = true;   /* JFET bypass network: true = bypass ON (PA15 LOW,
                              MCU out 0). Unit boots bypassed. */

/* --- Presets (SW_2 + LED2) -------------------------------------------------
 * One RAM-only snapshot of the six resolved pot values; SW_2 tap = recall,
 * ~1 s hold = arm save, second hold = commit (see preset.[ch] / Preset_Profile.md).
 * Recall holds each saved value until that pot physically moves: the loop reads
 * preset_value(i) for a not-yet-moved pot in PRESET mode, then hands control back
 * to the live knob once it travels past PRESET_MOVE_EPS. pot_live[] latches that
 * hand-off; it's re-armed for all pots whenever we (re)enter PRESET mode. */
Preset preset_state;
W25Q   flash;                /* W25Q16JV external NOR (SPI2/PB12); presets persist
                                here, future PK-Bootloader firmware images too.   */
#define PRESET_HOLD_MS    1000u    /* SW_2 hold to arm/commit a save             */
#define PRESET_MOVE_EPS   0.02f    /* knob travel that reclaims a recalled pot   */
bool       sw2_hold_fired = false; /* the current SW_2 hold already armed/committed */
PresetMode preset_prev_mode = PRESET_LIVE; /* detect entry into PRESET (re-seed gate) */
bool       pot_live[6]   = { true, true, true, true, true, true }; /* knob has taken over */
float      pot_baseline[6] = {0};  /* eff[] frozen at recall, to measure movement */
/* SSI2160 VCA control-voltage outputs (PWM -> RC -> VC pin). Same cvout driver. */
CvOut  hpf1;               /* VC_HPF1    PB3 / TIM2_CH2 — POT1 (low-cut)         */
CvOut  lpf1;               /* VCA_LPF1   PB6 / TIM4_CH1 — POT2 (high-cut)        */
CvOut  lpf2;               /* VCA_LPF2   PB9 / TIM4_CH4 — POT3 (high-cut, inv.)  */
CvOut  gain;               /* VCA_GAIN   PB7 / TIM4_CH2 — POT5 (drive level in)  */
CvOut  volume;             /* VCA_VOLUME PB8 / TIM4_CH3 — POT4 (master out)      */
Mcp41hv bias;              /* MCP41HV31 bias pot on SPI3 (CS = PC11 / DPOT_CS)   */

/* --- Control smoothing (anti-zipper) — see CTRL_SMOOTH_HZ. The 100 Hz loop
 * writes the targets below; the TIM7 ISR (ctrl_smooth_tick) does the output
 * writes. 32-bit aligned float/int loads & stores are atomic on Cortex-M3, so
 * the loop<->ISR handoff needs no locking. Index: 0=HPF1 1=LPF1 2=LPF2 3=VOLUME
 * 4=GAIN (matches the cvout_set order the loop used to call directly). */
TIM_HandleTypeDef htim7;                       /* smoothing tick — NOT in CubeMX */
static CvOut* const cv_chan[5] = { &hpf1, &lpf1, &lpf2, &volume, &gain };
volatile float cv_target[5] = {0};             /* loop -> ISR: per-VCA CV goal   */
volatile int   bias_target  = BIAS_CODE_MIN;   /* loop -> ISR: DPOT code goal    */

/* --- MIDI (UART/TRS on USART1 @ 31250) ------------------------------------
 * CC 20..25 drive POT1..POT6's targets; each pot arbitrates MIDI vs the
 * physical knob with last-mover-wins hysteresis (config_applyMidi port), so a
 * twist of the real knob always reclaims control. midi_rx[] latches true once a
 * CC has ever been seen for that pot. Indices line up with pots[0..5]. */
float midi_val[6]   = {0};   /* latest CC value, 0..1, per pot                */
bool  midi_rx[6]    = {0};   /* a CC has been received for this pot (latches) */
bool  using_midi[6] = {0};   /* hysteresis: MIDI currently owns this pot      */
float last_knob[6]  = {0};   /* knob position frozen when MIDI took over      */
float last_midi[6]  = {0};   /* last MIDI value seen                          */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_SPI2_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
static void MX_USB_OTG_FS_PCD_Init(void);
static void MX_SPI3_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Last-mover-wins arbitration between a physical knob and an incoming MIDI CC,
 * ported verbatim from "In The Water" (config_applyMidi). Returns the value to
 * actually use for this pot.
 *   - Before any CC for this pot: pass the knob through (usingMidi stays false).
 *   - Once a CC arrives, MIDI takes over when its move exceeds the knob's; a
 *     deliberate knob move (> HYST, and bigger than the MIDI move) reclaims it.
 *   - While MIDI owns the pot, *lastKnob is frozen so ADC jitter can't falsely
 *     steal control back. */
static float midi_apply(float knob, bool midiReceived, float midiValue,
                        float* lastKnob, float* lastMidi, bool* usingMidi)
{
  const float HYST = 0.02f;

  if (!midiReceived) {                 /* no CC yet -> pure knob */
    *usingMidi = false;
    *lastKnob  = knob;
    return knob;
  }

  float knobDelta = fabsf(knob - *lastKnob);
  float midiDelta = fabsf(midiValue - *lastMidi);

  if (*usingMidi) {
    if (knobDelta > HYST && knobDelta > midiDelta) {
      *usingMidi = false;
      *lastKnob  = knob;
    }
    /* *lastKnob intentionally frozen while MIDI is active */
  } else {
    if (midiDelta > HYST && midiDelta > knobDelta)
      *usingMidi = true;
    *lastKnob = knob;
  }

  *lastMidi = midiValue;
  return *usingMidi ? midiValue : knob;
}

/* Drive the JFET bypass network from code (mirrors the SW_1 handler):
 * bypass ON => PA15 LOW (MCU out 0), bypass OFF => PA15 HIGH. */
static void set_bypass(bool on)
{
  bypass_on = on;
  HAL_GPIO_WritePin(JFET_BYPASS_GPIO_Port, JFET_BYPASS_Pin,
                    on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* MIDI Control Change dispatch. CC 20..25 stash a value+flag for POT1..POT6
 * (applied with hysteresis in the loop); the rest act immediately. */
static void on_midi_cc(uint8_t cc, uint8_t value)
{
  if (cc >= MIDI_CC_POT1 && cc <= MIDI_CC_POT6) {
    uint8_t i = (uint8_t)(cc - MIDI_CC_POT1);   /* CC20..25 -> pots[0..5] */
    midi_val[i] = value * (1.0f / 127.0f);
    midi_rx[i]  = true;
    return;
  }

  switch (cc) {
    case MIDI_CC_EFFECT_ONOFF:           /* strict 0/127 direct bypass */
      if (value == 0)        set_bypass(true);
      else if (value == 127) set_bypass(false);
      break;

    case MIDI_CC_FS1:                    /* footswitch sim: a press toggles bypass */
      if (value > 0) set_bypass(!bypass_on);
      break;

    case MIDI_CC_FS2:                    /* footswitch sim: a press = preset recall toggle
                                          * (SW_2 tap). Hold-to-save has no momentary-CC
                                          * analogue, so MIDI only recalls/un-recalls. */
      if (value > 0) preset_recall_toggle(&preset_state);
      break;

    case MIDI_CC_ALL_SOUND_OFF:          /* panic -> force bypass */
    case MIDI_CC_ALL_NOTES_OFF:
      set_bypass(true);
      break;

    default:
      break;
  }
}

/* Smoothing tick — called from TIM7_IRQHandler (stm32f1xx_it.c) at
 * CTRL_SMOOTH_HZ. (a) One-pole-slews each VCA's CV toward cv_target[] and writes
 * it (the PWM's RC then smooths the small per-tick steps to a clean ramp).
 * (b) Glides the bias DPOT one code per tick toward bias_target; mcp41hv_set_code
 * change-detects, so SPI only fires while it's actually moving. Kept short: no
 * expf/powf here (those stay in the 100 Hz loop). */
void ctrl_smooth_tick(void)
{
  static float cv_smooth[5];           /* ISR-private slewed CV state */

  for (int i = 0; i < 5; i++) {
    cv_smooth[i] += CV_SMOOTH_COEF * (cv_target[i] - cv_smooth[i]);
    cvout_set(cv_chan[i], cv_smooth[i]);
  }

  int cur = bias.last_code;            /* -1 only before the boot write; here >=0 */
  if      (cur < bias_target) mcp41hv_set_code(&bias, (uint8_t)(cur + 1));
  else if (cur > bias_target) mcp41hv_set_code(&bias, (uint8_t)(cur - 1));
}

/* Bring up TIM7 as the smoothing tick: a basic timer (no GPIO) not configured in
 * CubeMX, so we enable its clock + NVIC by hand (same pattern as USART1's RX
 * vector) and it survives regeneration. APB1 timer clock = 72 MHz; PSC 71 ->
 * 1 MHz, ARR 499 -> 2 kHz update IRQ. Priority 7 = BELOW USART1 (6) so MIDI RX
 * always preempts the smoother and can never drop a byte. */
static void ctrl_smooth_start(void)
{
  __HAL_RCC_TIM7_CLK_ENABLE();
  htim7.Instance               = TIM7;
  htim7.Init.Prescaler         = (72000000u / 1000000u) - 1u;   /* -> 1 MHz */
  htim7.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim7.Init.Period            = (1000000u / CTRL_SMOOTH_HZ) - 1u;
  htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_Base_Init(&htim7);

  HAL_NVIC_SetPriority(TIM7_IRQn, 7, 0);
  HAL_NVIC_EnableIRQ(TIM7_IRQn);
  HAL_TIM_Base_Start_IT(&htim7);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_SPI2_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_USB_OTG_FS_PCD_Init();
  MX_SPI3_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  /* PA15 (JFET_BYPASS) is JTDI by default. Disable JTAG — keeping SW-debug so
   * the ST-Link still connects — to free PA15 (and PB3/PB4) for GPIO. */
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_AFIO_REMAP_SWJ_NOJTAG();

  /* LED2 (right): all three colors on TIM3. */
  ledrgb_init(&led2, &htim3,
              TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3,
              LED_POLARITY_ACTIVE_LOW, 1.0f);
  /* LED1 (left): R on TIM3_CH4, G/B on TIM2 — split across two timers. */
  ledrgb_init_split(&led1,
                    &htim3, TIM_CHANNEL_4,   /* R */
                    &htim2, TIM_CHANNEL_3,   /* G */
                    &htim2, TIM_CHANNEL_4,   /* B */
                    LED_POLARITY_ACTIVE_LOW, 1.0f);

  /* (LED PWM channels were already started OFF in MX_TIM2/3_Init's USER CODE 2
   * blocks to avoid the active-low boot flash; ledrgb_init above just re-asserts
   * the off duty and binds the structs.) */

  /* TIM4 carries three CV outs (CH1/CH2/CH4); CubeMX left it at the default 65535
   * (~1.1 kHz). Force 50 kHz like TIM2/TIM3 so the 10k/220n RC rejects the PWM
   * ripple (survives regen; mirror Period=1439 in CubeMX if you want). */
  __HAL_TIM_SET_AUTORELOAD(&htim4, 1439);

  /* SSI2160 VCA CV outs — one pot each. cvout maps 0..1 -> duty -> CV (÷2 from the
   * 10k series + ~10k VC input). Octave-linear, -31 mV/dB. Filters use a 0.555
   * duty span (~5 octaves); volumes use 1.0 (full ~-53 dB).
   *
   * Directions (flip the two duty args to reverse any knob):
   *   POT1 HPF1:   down = max cut (~600 Hz)  up = open (20 Hz)
   *   POT2 LPF1:   down = darkest            up = open (bright)
   *   POT3 LPF2:   down = darkest            up = open (bright)
   *   POT4 VOLUME: down = lowest out         up = highest (unity)
   *   POT5 GAIN:   down = lowest in          up = highest (unity) */
  cvout_init(&hpf1,   &htim2, TIM_CHANNEL_2, 0.000f, 0.555f); /* POT1 (inv.) */
  cvout_init(&lpf1,   &htim4, TIM_CHANNEL_1, 0.555f, 0.000f); /* POT2 (inv.) */
  cvout_init(&lpf2,   &htim4, TIM_CHANNEL_4, 0.555f, 0.000f); /* POT3 */
  cvout_init(&volume, &htim4, TIM_CHANNEL_3, 1.000f, 0.000f); /* POT4 */
  cvout_init(&gain,   &htim4, TIM_CHANNEL_2, 0.680f, 0.000f); /* POT5 — min gain
                            lifted to match the TS min-drive output level. duty 0.81
                            only gave +4.3 dB (gain VCA has a ~-13 dB feedthrough
                            floor that swallows the first slice of CV); 0.68 clears
                            it to land the peak ~-2.5 dB. Max gain unchanged.
                            See VCA_Profiles.md. */
  HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);   /* VC_HPF1    (POT1) */
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);   /* VCA_LPF1   (POT2) */
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);   /* VCA_LPF2   (POT3) */
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);   /* VCA_VOLUME (POT4) */
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);   /* VCA_GAIN   (POT5) */

  /* Pots: calibrate ADC1 once, then bind POT1..POT6 to their channels. */
  HAL_ADCEx_Calibration_Start(&hadc1);
  static const uint32_t pot_channels[6] = {
    ADC_CHANNEL_0,   /* POT1  PA0 */
    ADC_CHANNEL_13,  /* POT2  PC3 */
    ADC_CHANNEL_10,  /* POT3  PC0 */
    ADC_CHANNEL_1,   /* POT4  PA1 */
    ADC_CHANNEL_12,  /* POT5  PC2 */
    ADC_CHANNEL_11,  /* POT6  PC1 */
  };
  for (int i = 0; i < 6; i++)
    pot_init(&pots[i], &hadc1, pot_channels[i]);

  /* Footswitches. active_low = switch-to-ground + external pull-up. SW_1 is the
   * bypass footswitch; SW_2 still toggles LED2. */
  fsw_init(&sw1, SW_1_GPIO_Port, SW_1_Pin, true);
  fsw_init(&sw2, SW_2_GPIO_Port, SW_2_Pin, true);

  preset_init(&preset_state);   /* SW_2 preset state machine (boots LIVE, LED2 off) */

  /* --- External flash (W25Q16JV, SPI2, CS = PB12 / SPI2_CS) ------------------
   * 2 MB SPI NOR. Today: persists the preset snapshot (preset_store, last 4 KB
   * sector) so a saved preset survives a power-cycle — the F105 has no EEPROM.
   * Later: staging area for PK-Bootloader firmware images (bottom of the array;
   * see Flash_Profile.md). w25q_init wakes the part and reads the JEDEC ID; if
   * it doesn't answer (flash.present == false) save/load no-op and the unit just
   * runs RAM-only, so a missing/dead chip never bricks boot.
   * SPI2 is already up (MX_SPI2_Init, full-duplex Mode 0 @2.25 MHz). */
  w25q_init(&flash, &hspi2, SPI2_CS_GPIO_Port, SPI2_CS_Pin);
  preset_store_load(&flash, &preset_state);   /* loads snapshot if a valid one exists */

  /* Boot bypassed: drive PA15 low (matches CubeMX reset level + bypass_on). */
  HAL_GPIO_WritePin(JFET_BYPASS_GPIO_Port, JFET_BYPASS_Pin, GPIO_PIN_RESET);

  /* --- Bias DPOT (MCP41HV31, SPI3, CS = PC11 / DPOT_CS) ----------------------
   * Rails are ASYMMETRIC (~-4.35 / +5.0 V); mid-scale (code 63) is the rail
   * center ~+0.28 V, NOT 0 V — gating is referenced from code 63. POT6 drives
   * the bias in the loop from center (code 63, no gating) up to the positive
   * rail (code 127), C-tapered. Re-measure voltages post orientation-fix.
   * See Bias_Profile.md. */
  mcp41hv_init(&bias, &hspi3, DPOT_CS_GPIO_Port, DPOT_CS_Pin);
  mcp41hv_set_code(&bias, MCP41HV_CODE_MID);   /* boot center; POT6 takes over in the loop */

  /* --- MIDI input (UART/TRS on USART1 RX = PA10 @ 31250) --------------------
   * USART1 RX is interrupt-driven: USART1_IRQHandler (stm32f1xx_it.c) pushes
   * each byte to the MIDI ring buffer; midi_poll() drains/parses/dispatches in
   * the main loop. CC 20..25 -> POT1..POT6 targets (hysteresis), CC29 = bypass.
   * Init + register the handler BEFORE enabling the RX interrupt.
   *
   * EXP_MIDI_CNTRL (PA8) selects the TRS jack's MIDI/expression mux. It boots
   * LOW (= MIDI, matching InTheWater's D2 sense) so MIDI is live out of reset;
   * confirm the polarity on the bench.
   *
   * USART1's NVIC line is enabled here in code (NOT in CubeMX), so the
   * USART1_IRQHandler in stm32f1xx_it.c lives in a USER CODE block and survives
   * regen with no collision. We enable the NVIC line + the RXNE interrupt bit.
   * (Future option: let CubeMX manage the vector — see the note on the handler
   * in stm32f1xx_it.c.) */
  midi_init();
  midi_set_cc_handler(on_midi_cc);
  HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(USART1_IRQn);
  __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);

  /* Start the TIM7 control-smoothing ISR. From here on the loop sets cv_target[]
   * / bias_target and the ISR drives the actual VCA/bias outputs. Targets boot
   * at 0 / code 63 (matches the cvout/bias boot state above) so nothing jumps;
   * the first 100 Hz pass fills in the real knob positions and the ISR ramps to
   * them over a few ms. */
  ctrl_smooth_start();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    static uint32_t last_pot_ms = 0;
    uint32_t now = HAL_GetTick();

    /* Drain + dispatch any MIDI bytes the USART1 RX ISR has queued. Runs every
     * iteration (cheap when idle) so CCs are applied promptly. */
    midi_poll();

    /* Poll all 6 pots at 100 Hz. Read pots[i].value (0..1) anywhere. */
    if (now - last_pot_ms >= 10) {
      float dt_ms = (float)(now - last_pot_ms);   /* ~10 ms; drives preset timing */
      last_pot_ms = now;
      for (int i = 0; i < 6; i++)
        pot_poll(&pots[i]);

      fsw_poll(&sw1, now);
      fsw_poll(&sw2, now);

      /* Resolve each pot's effective value: physical knob unless a MIDI CC has
       * taken it over (last-mover-wins). eff[0..5] line up with POT1..POT6. */
      float eff[6];
      for (int i = 0; i < 6; i++)
        eff[i] = midi_apply(pots[i].value, midi_rx[i], midi_val[i],
                            &last_knob[i], &last_midi[i], &using_midi[i]);

      /* Feed the resolved values to the preset layer (updates the match cue and,
       * on commit, is what gets snapshotted). Always non-blocking. */
      for (int i = 0; i < 6; i++)
        preset_on_pot_move(&preset_state, i, eff[i]);

      /* SW_1 = bypass footswitch. Toggle the JFET network: bypass ON => PA15
       * LOW (MCU out 0), bypass OFF => PA15 HIGH (MCU out 1). */
      if (fsw_rising(&sw1)) {
        bypass_on = !bypass_on;
        HAL_GPIO_WritePin(JFET_BYPASS_GPIO_Port, JFET_BYPASS_Pin,
                          bypass_on ? GPIO_PIN_RESET : GPIO_PIN_SET);
      }

      /* SW_2 = preset: tap toggles recall/live, ~1 s hold arms a save, a second
       * hold commits. Each press fires the hold once; a release that hasn't held
       * long enough is the tap. */
      if (fsw_rising(&sw2)) sw2_hold_fired = false;
      if (!sw2_hold_fired && fsw_pressed(&sw2)
          && fsw_hold_ms(&sw2, now) >= PRESET_HOLD_MS) {
        /* A second hold (SAVE_ARMED -> PRESET) is the COMMIT; persist the fresh
         * snapshot to flash so it survives a power-cycle. The erase+program
         * blocks here for up to a few hundred ms — fine for this deliberate
         * gesture (MIDI RX is buffered in its ISR, the TIM7 smoother keeps the
         * outputs gliding; only the LEDs/loop pause briefly). */
        bool committing = (preset_mode(&preset_state) == PRESET_SAVE_ARMED);
        preset_hold_fired(&preset_state);
        sw2_hold_fired = true;
        if (committing) preset_store_save(&flash, &preset_state);
      }
      if (fsw_falling(&sw2) && !sw2_hold_fired)
        preset_recall_toggle(&preset_state);

      /* Recall hold-vs-takeover gate. On entry into PRESET mode (recall or commit)
       * re-arm every pot: it holds the saved value until the physical knob travels
       * past PRESET_MOVE_EPS, after which the live knob owns it again (and crossing
       * back onto the saved value flashes LED2 white). LIVE/SAVE_ARMED are fully
       * live. ctl[] is the value actually driven; eff[] is the raw knob/MIDI. */
      PresetMode pm = preset_mode(&preset_state);
      if (pm == PRESET_PRESET && preset_prev_mode != PRESET_PRESET) {
        for (int i = 0; i < 6; i++) { pot_live[i] = false; pot_baseline[i] = eff[i]; }
      }
      preset_prev_mode = pm;

      float ctl[6];
      for (int i = 0; i < 6; i++) {
        if (pm == PRESET_PRESET) {
          if (!pot_live[i] && fabsf(eff[i] - pot_baseline[i]) > PRESET_MOVE_EPS)
            pot_live[i] = true;                 /* knob reclaimed this pot */
          ctl[i] = pot_live[i] ? eff[i] : preset_value(&preset_state, i);
        } else {
          ctl[i] = eff[i];                      /* LIVE / SAVE_ARMED: straight through */
        }
      }

      /* Publish each pot's driven value as a *target*; the TIM7 ISR slews the
       * actual VCA CV / bias code toward it (anti-zipper — see CTRL_SMOOTH_HZ).
       * Index order matches cv_chan[]: HPF1 LPF1 LPF2 VOLUME GAIN. */
      cv_target[0] = ctl[0];   /* POT1 -> HPF1   */
      cv_target[1] = ctl[1];   /* POT2 -> LPF1   */
      cv_target[2] = ctl[2];   /* POT3 -> LPF2   */
      /* VOLUME (POT4) with a Tube-Screamer Level taper. The raw knob is linear,
       * but a TS Level pot crams most of the loudness into the top of the sweep
       * (only ~7.6 dB down at noon). Reshape POT4 to that curve: desired
       * attenuation = -VOL_TS_DB_DECADE*log10(knob), mapped through the VCA's full
       * commanded CV span (VOL_CV_SPAN_DB). Knob max = loudest (control 1); knob
       * min = deepest mute (control 0 = max CV, ~-55 dB floor). Forced to full
       * attenuation when bypassed so only the dry JFET path passes; the slew also
       * gives a soft (few-ms) bypass transition for free. */
      float vol_curve;
      {
        float p = ctl[3];                       /* POT4 / MIDI volume 0..1 */
        if (p <= 0.0f) {
          vol_curve = 0.0f;                     /* hard bottom = deepest mute */
        } else {
          float atten_db = -VOL_TS_DB_DECADE * log10f(p);   /* >=0 dB below max */
          vol_curve = 1.0f - atten_db / VOL_CV_SPAN_DB;
          if (vol_curve < 0.0f) vol_curve = 0.0f;
          else if (vol_curve > 1.0f) vol_curve = 1.0f;
        }
      }
      cv_target[3] = bypass_on ? 0.0f : vol_curve;   /* POT4 -> VOLUME */
      cv_target[4] = ctl[4];   /* POT5 -> GAIN   */

      /* POT6 -> bias: code 98 (~+2.8 V) -> 111 (~+3.7 V), with a C taper so the
       * bias moves fast off the bottom then fine-tunes near the top. Set the
       * target code here; the ISR glides toward it one code at a time
       * (change-detected SPI), so a fast twist ramps instead of lurching.
       * For a linear sweep instead, replace the two lines below with:
       *   float bias_curve = ctl[5]; */
      float bias_curve = (1.0f - expf(-BIAS_TAPER_K * ctl[5]))
                       / (1.0f - expf(-BIAS_TAPER_K));
      bias_target = (int)lroundf(
          (float)BIAS_CODE_MIN
          + bias_curve * ((float)BIAS_CODE_MAX - (float)BIAS_CODE_MIN));

      /* Advance preset timing (breathe phase + match-cue countdown). */
      preset_tick(&preset_state, dt_ms);

      /* LED1 = engage indicator: solid red while the effect is in circuit
       * (bypass OFF), off when bypassed. ledrgb_set drives the per-channel color
       * AND re-renders the PWM (ledrgb_setBrightness only moves the cap, so on its
       * own it never lit); LED1's max_brightness stays 1.0 from init. */
      if (bypass_on) ledrgb_off(&led1);
      else           ledrgb_set(&led1, 1.0f, 0.0f, 0.0f);   /* solid red */

      /* LED2 = preset status: off (live), solid red (recalled), white flash when a
       * knob crosses onto its saved value, breathing white (save armed). */
      if (preset_led_on(&preset_state)) {
        PresetColor c = preset_led_color(&preset_state);
        ledrgb_set(&led2, c.r, c.g, c.b);
      } else {
        ledrgb_off(&led2);
      }
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV2;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.Prediv1Source = RCC_PREDIV1_SOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  RCC_OscInitStruct.PLL2.PLL2State = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_USB;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV3;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the Systick interrupt time
  */
  __HAL_RCC_PLLI2S_ENABLE();
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_10;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief SPI3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI3_Init(void)
{

  /* USER CODE BEGIN SPI3_Init 0 */

  /* USER CODE END SPI3_Init 0 */

  /* USER CODE BEGIN SPI3_Init 1 */

  /* USER CODE END SPI3_Init 1 */
  /* SPI3 parameter configuration*/
  hspi3.Instance = SPI3;
  hspi3.Init.Mode = SPI_MODE_MASTER;
  hspi3.Init.Direction = SPI_DIRECTION_1LINE;
  hspi3.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi3.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi3.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi3.Init.NSS = SPI_NSS_SOFT;
  hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi3.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI3_Init 2 */

  /* USER CODE END SPI3_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 1439;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */
  /* Anti-flash for LED1 G/B (CH3/CH4, active-low) -- see TIM3_Init 2. CH2 is the
   * HPF1 VCA (not an LED), so it's left for USER CODE 2. */
  {
    uint32_t off = __HAL_TIM_GET_AUTORELOAD(&htim2) + 1;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, off);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, off);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);
  }
  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 1439;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */
  /* Anti-flash: LEDs are common-anode/active-low. Preload every channel's compare
   * to OFF (full-high) and start the PWM *before* MspPostInit switches the pins to
   * AF -- so they come up high (off) instead of briefly sitting AF-disabled-low
   * (which would light them during boot). CH1/2/3 = LED2 RGB, CH4 = LED1 R. */
  {
    uint32_t off = __HAL_TIM_GET_AUTORELOAD(&htim3) + 1;   /* CCR = ARR+1 = 100% high */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, off);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, off);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, off);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, off);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
  }
  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 65535;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 31250;   /* MIDI standard (was 115200); mirrored in .ioc */
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USB_OTG_FS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_FS_PCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_FS_Init 0 */

  /* USER CODE END USB_OTG_FS_Init 0 */

  /* USER CODE BEGIN USB_OTG_FS_Init 1 */

  /* USER CODE END USB_OTG_FS_Init 1 */
  hpcd_USB_OTG_FS.Instance = USB_OTG_FS;
  hpcd_USB_OTG_FS.Init.dev_endpoints = 4;
  hpcd_USB_OTG_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_OTG_FS.Init.Sof_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_FS.Init.vbus_sensing_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_FS_Init 2 */

  /* USER CODE END USB_OTG_FS_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI2_CS_GPIO_Port, SPI2_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, EXP_MIDI_CNTRL_Pin|JFET_BYPASS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(DPOT_CS_GPIO_Port, DPOT_CS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : PC13 DPOT_CS_Pin */
  GPIO_InitStruct.Pin = GPIO_PIN_13|DPOT_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : SW_2_Pin SW_1_Pin */
  GPIO_InitStruct.Pin = SW_2_Pin|SW_1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI2_CS_Pin */
  GPIO_InitStruct.Pin = SPI2_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SPI2_CS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : EXP_MIDI_CNTRL_Pin JFET_BYPASS_Pin */
  GPIO_InitStruct.Pin = EXP_MIDI_CNTRL_Pin|JFET_BYPASS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
