# ADC Configuration & Test Guide
## SAMV71-XULT — AFEC0 Battery Monitoring

**Board:** `microchip/samv71-xult-clickboards`
**Peripheral:** SAMV71 AFEC0 (Analog Front End Controller)
**PX4 Branch:** `samv7-custom`

---

## 1. What is the AFEC0?

The SAMV71 does not have a simple ADC — it has an **AFEC (Analog Front End
Controller)**, which is Microchip's enhanced ADC with built-in signal
conditioning. PX4 uses it for **battery voltage and current monitoring**.

| Property | Detail |
|---|---|
| Peripheral | AFEC0 |
| Base Address | `0x4003C000` (`SAM_AFEC0_BASE`) |
| Resolution | 12-bit (0 – 4095 counts) |
| Reference Voltage | 3.3 V (VDDANA) |
| AFEC Clock | 150 MHz MCK ÷ 32 = **4.6875 MHz** |
| Trigger Mode | Software trigger (single conversion per request) |
| Channels Active | CH0 (voltage) + CH7 (current) |

---

## 2. Active ADC Channels

Defined in `boards/microchip/samv71-xult-clickboards/src/board_config.h`:

```c
#define ADC_BATTERY_VOLTAGE_CHANNEL  0    /* PD30 — AFEC0_AD0 */
#define ADC_BATTERY_CURRENT_CHANNEL  7    /* PA18 — AFEC0_AD7 */
#define BOARD_ADC_POS_REF_V          3.3f /* VDDANA reference */
#define ADC_CHANNELS  ((1 << 0) | (1 << 7))
```

### Physical Pin Mapping

| AFEC0 Channel | SAMV71 Pin | Signal | Measures |
|---|---|---|---|
| CH0 | PD30 | `AFEC0_AD0` | Battery **Voltage** |
| CH7 | PA18 | `AFEC0_AD7` | Battery **Current** |

> Both pins are configured as analog inputs (`GPIO_AFE0_AD0`, `GPIO_AFE0_AD7`)
> in `adc.cpp:99-100`. Do not configure these pins as GPIO.

---

## 3. How ADC Conversion Works

### Raw Count → Pin Voltage

The AFEC0 outputs a 12-bit value (0–4095):

```
V_pin = (raw_count / 4096) × 3.3 V

Example:
  raw_count = 2048  →  V_pin = (2048 / 4096) × 3.3 = 1.65 V
  raw_count = 4095  →  V_pin = (4095 / 4096) × 3.3 ≈ 3.3 V (full scale)
  raw_count = 0     →  V_pin = 0 V
```

### Pin Voltage → Battery Voltage

The raw battery voltage (e.g. 11.1 V for 3S LiPo) **must be divided down**
to 0–3.3 V before connecting to PD30. PX4 then scales it back up using
the `BAT1_V_DIV` parameter:

```
V_battery = V_pin × BAT1_V_DIV

Example (3S LiPo, divider ratio 5.7:1):
  V_pin = 1.95 V
  BAT1_V_DIV = 5.7
  V_battery = 1.95 × 5.7 = 11.115 V
```

### Pin Voltage → Battery Current

A current sensor (e.g. hall-effect or shunt amplifier) outputs a voltage
proportional to current. PX4 converts it using `BAT1_A_PER_V`:

```
I_battery = (V_pin - BAT1_V_OFFS) × BAT1_A_PER_V

Example (AttoPilot 45A or similar sensor):
  V_pin = 1.5 V
  BAT1_V_OFFS = 0.0 V  (zero-current offset)
  BAT1_A_PER_V = 36.36 A/V
  I_battery = 1.5 × 36.36 = 54.5 A
```

---

## 4. Voltage Divider Circuit (PD30)

The SAMV71-XULT dev board has **no built-in power module**. You must wire
an external voltage divider to PD30.

### Recommended Divider for 3S LiPo (max 12.6 V)

```
Battery+ ──┬── R1 (10 kΩ) ──┬── PD30 (AFEC0_AD0)
           │                 │
           │                R2 (3.3 kΩ)
           │                 │
Battery- ──┴─────────────────┴── GND
```

**Divider ratio calculation:**

```
V_PD30 = V_bat × R2 / (R1 + R2)
       = 12.6  × 3300 / (10000 + 3300)
       = 12.6  × 0.248
       = 3.13 V  ← safely below 3.3 V max
```

**BAT1_V_DIV to set:**
```
BAT1_V_DIV = (R1 + R2) / R2 = 13300 / 3300 = 4.03
```

### For 4S LiPo (max 16.8 V) — use larger R1

```
R1 = 15 kΩ, R2 = 3.3 kΩ
Ratio = 18300 / 3300 = 5.55
BAT1_V_DIV = 5.55
```

> **WARNING:** Never connect battery voltage directly to PD30.
> Maximum input on any SAMV71 GPIO/analog pin = **3.6 V**.
> Exceeding this permanently damages the MCU.

---

## 5. Current Sensor Circuit (PA18)

Connect a current sensor output to PA18. Common options:

| Sensor | Output Range | BAT1_A_PER_V | BAT1_V_OFFS |
|---|---|---|---|
| AttoPilot 45A | 0–3.3 V = 0–45 A | 13.64 | 0.0 |
| AttoPilot 90A | 0–3.3 V = 0–90 A | 27.27 | 0.0 |
| ACS712 (30A) | 0–5 V (use divider!) | varies | 1.65 |
| Mauch PL-200 | 0–3.3 V | 67.02 | 0.0 |

> If your sensor outputs 0–5 V, add a voltage divider before PA18.
> Adjust `BAT1_A_PER_V` accordingly.

---

## 6. Firmware Configuration

### `default.px4board` — Already Configured

```
CONFIG_DRIVERS_ADC_BOARD_ADC=y    # AFEC0 driver enabled
CONFIG_MODULES_BATTERY_STATUS=y   # Battery monitoring module
```

### `board_config.h` — Key Definitions

```c
#define BOARD_ADC_BASE               SAM_AFEC0_BASE   /* 0x4003C000 */
#define BOARD_ADC_POS_REF_V          3.3f
#define ADC_BATTERY_VOLTAGE_CHANNEL  0                /* PD30 */
#define ADC_BATTERY_CURRENT_CHANNEL  7                /* PA18 */
#define ADC_CHANNELS                 ((1<<0) | (1<<7))
#define BOARD_NUMBER_BRICKS          1
#define BOARD_ADC_BRICK_VALID        1                /* always valid */
```

---

## 7. Runtime PX4 Parameters

Set these via NSH console or QGroundControl after connecting your power
circuit. Parameters saved to `/fs/mtd_params`.

### Battery Parameters

```sh
# Voltage divider ratio (match your resistor values)
param set BAT1_V_DIV    4.03      # for 3S LiPo with 10k/3.3k divider

# Current sensor scaling
param set BAT1_A_PER_V  27.27     # for 90A AttoPilot sensor

# Current sensor zero-offset (voltage output at 0A)
param set BAT1_V_OFFS   0.0       # 0V for most sensors

# Battery cell count
param set BAT1_N_CELLS  3         # 3 for 3S, 4 for 4S

# Voltage thresholds
param set BAT1_V_EMPTY  3.5       # empty cell voltage (V)
param set BAT1_V_CHARGED 4.2      # full cell voltage (V)

# Capacity (mAh) — set to your battery capacity
param set BAT1_CAPACITY 2200      # mAh

param save
reboot
```

### Full Parameter Reference

| Parameter | Default | Description |
|---|---|---|
| `BAT1_V_DIV` | 5.7 | Voltage divider ratio (R1+R2)/R2 |
| `BAT1_A_PER_V` | 36.36 | Current sensor scaling (A/V) |
| `BAT1_V_OFFS` | 0.0 | Current sensor zero offset (V) |
| `BAT1_N_CELLS` | 3 | LiPo cell count |
| `BAT1_V_EMPTY` | 3.5 | Per-cell empty voltage (V) |
| `BAT1_V_CHARGED` | 4.2 | Per-cell full voltage (V) |
| `BAT1_CAPACITY` | -1 | Capacity in mAh (-1 = disable) |
| `BAT1_R_INTERNAL` | -1 | Internal resistance Ω (-1 = auto) |

---

## 8. How to Test the ADC

### Step 1 — Verify AFEC0 Driver Initialized

```sh
dmesg | grep -i adc
dmesg | grep -i afec
```

Expected output:
```
[boot] ADC driver initialized
```

No output here is normal — the ADC initializes silently. Errors only appear
if init fails.

### Step 2 — Check Raw ADC Report (uORB Topic)

```sh
listener adc_report
```

Expected output:
```
adc_report
  timestamp: 1234567890
  device_id: 0x4003C000       ← AFEC0 base address
  channel_id[0]: 0            ← CH0 = voltage channel (PD30)
  raw_data[0]: 1860           ← raw 12-bit count
  channel_id[1]: 7            ← CH7 = current channel (PA18)
  raw_data[1]: 512            ← raw 12-bit count
```

**Convert raw count to pin voltage manually:**
```
CH0 (voltage):  V_pin = (1860 / 4096) × 3.3 = 1.498 V
CH7 (current):  V_pin = (512  / 4096) × 3.3 = 0.413 V
```

### Step 3 — Check Processed Battery Status

```sh
listener battery_status
```

Expected output (with battery connected and correct params):
```
battery_status
  timestamp: 1234567890
  voltage_v: 11.1             ← V_bat = V_pin × BAT1_V_DIV
  current_a: 0.5              ← I_bat = V_pin × BAT1_A_PER_V
  remaining: 0.85             ← 85% charge estimate
  discharged_mah: 0.0
  cell_count: 3
  warning: 0                  ← 0=OK, 1=low, 2=critical, 3=emergency
  connected: 1                ← 1 = battery detected
```

### Step 4 — Direct ADC Test Command

```sh
adc test
```

This prints all active ADC channel readings:

```
ADC: reading CH0 = 1860  (1.498V at PD30)
ADC: reading CH7 = 512   (0.413V at PA18)
```

### Step 5 — Continuous Monitoring

```sh
# Watch battery status update every second
listener battery_status -n 100

# Watch raw ADC at full rate
listener adc_report -n 100
```

---

## 9. Voltage Calculation Verification

Use this to verify your `BAT1_V_DIV` is set correctly.

### Method

1. Measure battery voltage with a multimeter: e.g. **11.4 V**
2. Read raw ADC count from `listener adc_report` CH0: e.g. **1420**
3. Calculate pin voltage: `(1420 / 4096) × 3.3 = 1.144 V`
4. Calculate correct divider: `11.4 / 1.144 = 9.965`
5. Set: `param set BAT1_V_DIV 9.965`

### Quick Reference: Raw Count vs Pin Voltage

| Raw Count | Pin Voltage | Approx 3S Bat (÷4.03) | Approx 4S Bat (÷5.55) |
|---|---|---|---|
| 0 | 0.00 V | 0.0 V | 0.0 V |
| 512 | 0.41 V | 1.7 V | 2.3 V |
| 1024 | 0.82 V | 3.3 V | 4.6 V |
| 1638 | 1.32 V | 5.3 V | 7.3 V |
| 2048 | 1.65 V | 6.6 V | 9.2 V |
| 2731 | 2.20 V | 8.9 V | 12.2 V |
| 3277 | 2.64 V | 10.6 V | 14.6 V |
| 3800 | 3.06 V | 12.3 V | 17.0 V |
| 4095 | 3.30 V | 13.3 V | 18.3 V |

---

## 10. Troubleshooting

### `adc_report` shows no data / topic not publishing

```sh
uorb status | grep adc
```

If `adc_report` is not listed:
- Verify `CONFIG_DRIVERS_ADC_BOARD_ADC=y` in `default.px4board`
- Check boot log: `dmesg` for ADC init errors

### Raw count stuck at 0 or 4095

| Symptom | Cause | Fix |
|---|---|---|
| CH0 raw = 0 | PD30 floating or shorted to GND | Check divider wiring |
| CH0 raw = 4095 | PD30 > 3.3 V (overvoltage) | Increase R1 in divider |
| CH7 raw = 0 | PA18 floating | Check current sensor wiring |
| CH7 raw = 4095 | PA18 > 3.3 V | Add divider before PA18 |

### `battery_status` shows wrong voltage

1. Measure real battery voltage with multimeter
2. Read `adc_report` CH0 raw count
3. Recalculate `BAT1_V_DIV` using method in Section 9
4. `param set BAT1_V_DIV <new_value>` then `param save`

### `battery_status.connected = 0` (battery not detected)

This board uses `BOARD_ADC_BRICK_VALID = 1` (always valid) in `board_config.h`.
Battery is always reported as connected. If `connected = 0`, the battery_status
module may not have started:

```sh
battery_status status
battery_status start   # if not running
```

### `adc test` command not found

The `adc` command is part of `CONFIG_SYSTEMCMDS_TESTS`. Verify it is enabled:

```
CONFIG_SYSTEMCMDS_TESTS=y    ← in default.px4board (already set)
```

---

## 11. ADC Initialization Flow (Summary)

```
boot
 └─ board_app_initialize()
     └─ px4_platform_init()
         └─ px4_arch_adc_init(SAM_AFEC0_BASE)
             ├─ sam_afec0_enableclk()       ← enable peripheral clock
             ├─ sam_configgpio(GPIO_AFE0_AD0)  ← PD30 analog
             ├─ sam_configgpio(GPIO_AFE0_AD7)  ← PA18 analog
             ├─ Configure AFEC MR: PRESCAL=31, STARTUP_64
             ├─ Configure AFEC EMR: STM, 12-bit, TAG
             ├─ Configure AFEC ACR: IBCTL=3
             └─ Test conversion on CH0 (pass/fail)
                 └─ g_adc_initialized = true

Runtime:
  battery_status module calls px4_arch_adc_sample() periodically
    ├─ Enable channel
    ├─ Start conversion (software trigger)
    ├─ Poll ISR for EOC (End Of Conversion) — 50 µs timeout
    ├─ Read CDR register
    └─ Disable channel → return 12-bit result
```

---

## 12. Test Checklist

### Hardware

- [ ] Voltage divider wired: Battery+ → R1 → PD30 → R2 → GND
- [ ] Current sensor wired: sensor output → PA18
- [ ] Sensor supply connected (5V or 3.3V per sensor datasheet)
- [ ] Divider output < 3.3 V at max battery voltage (verify with multimeter)

### Firmware

- [ ] `CONFIG_DRIVERS_ADC_BOARD_ADC=y` in `default.px4board`
- [ ] `CONFIG_MODULES_BATTERY_STATUS=y` in `default.px4board`

### Parameters

- [ ] `BAT1_V_DIV` set to match divider resistor ratio
- [ ] `BAT1_A_PER_V` set to match current sensor spec
- [ ] `BAT1_N_CELLS` set to battery cell count
- [ ] `param save` + `reboot` after parameter changes

### Verification

- [ ] `listener adc_report` — CH0 and CH7 show non-zero counts
- [ ] `listener battery_status` — `voltage_v` matches multimeter reading
- [ ] `listener battery_status` — `connected = 1`
- [ ] `listener battery_status` — `remaining` between 0.0 and 1.0
- [ ] `listener battery_status` — `warning = 0` (no low-battery alert)

---

*Document generated for PX4 branch `samv7-custom`, board `microchip/samv71-xult-clickboards`.*
