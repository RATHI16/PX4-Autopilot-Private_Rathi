# PX4IO SAMD21J18A Port — Porting Notes

This document records every file created or modified, every error encountered and
its fix, and the final build status for the PX4IO co-processor firmware port from
STM32F100 to the Microchip SAMD21J18A (Cortex-M0+, 256 KB flash, 32 KB SRAM,
48 MHz).

**Goal for this phase:** get the firmware to compile and link cleanly. Correct pin
assignments and full hardware validation come later.

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [File Inventory](#2-file-inventory)
3. [NuttX Board Configuration (`board.h`)](#3-nuttx-board-configuration-boardh)
4. [Board Source Files](#4-board-source-files)
5. [Platform HAL (`micro_hal.h`)](#5-platform-hal-micro_halh)
6. [IO Timer (`io_timer.h`, `io_timer_hw_description.h`, `io_timer.c`)](#6-io-timer)
7. [HRT — High-Resolution Timer (`hrt.c`)](#7-hrt--high-resolution-timer-hrtc)
8. [ADC Driver (`adc.cpp`)](#8-adc-driver-adccpp)
9. [PWM Servo (`pwm_servo.c`)](#9-pwm-servo-pwm_servoc)
10. [Board Identity / MCU Version](#10-board-identity--mcu-version)
11. [FMU Serial Link (`serial_samd.cpp`)](#11-fmu-serial-link-serial_samdcpp)
12. [NuttX Submodule Patches](#12-nuttx-submodule-patches)
13. [defconfig](#13-defconfig)
14. [px4iofirmware Integration](#14-px4iofirmware-integration)
15. [Linker Errors and Fixes](#15-linker-errors-and-fixes)
16. [Build Result](#16-build-result)
17. [Known Limitations / TODO](#17-known-limitations--todo)

---

## 1. Architecture Overview

PX4IO originally runs on an STM32F100 and communicates with the FMU over a
1.5 Mbps UART using DMA.  The SAMD21J18A port replaces:

| STM32 concept | SAMD21 equivalent |
|---|---|
| STM32 USART + DMA | SERCOM5 USART + SAMD DMAC |
| STM32 TIM2 32-bit free-running HRT | TC4+TC5 chained COUNT32 |
| STM32 ADC | SAMD21 ADC (AIN channels) |
| `stm32_*` NuttX drivers | `sam_*` NuttX `samd2l2` drivers |
| `serial.cpp` | `serial_samd.cpp` |

NuttX driver tree used: `arch/arm/src/samd2l2` (covers SAMD20, SAMD21, SAML21
families — controlled by `CONFIG_ARCH_FAMILY_SAMD21`).

---

## 2. File Inventory

### Created from scratch

| File | Purpose |
|---|---|
| `boards/microchip/samd21-io/nuttx-config/include/board.h` | Full NuttX clock + SERCOM + LED config |
| `platforms/nuttx/src/px4/microchip/samd2l2/include/px4_arch/micro_hal.h` | Platform HAL (GPIO, UUID, cache) |
| `platforms/nuttx/src/px4/microchip/samd2l2/include/px4_arch/io_timer.h` | IO timer type definitions (from rpi_common) |
| `platforms/nuttx/src/px4/microchip/samd2l2/include/px4_arch/io_timer_hw_description.h` | TCC hardware description helpers |
| `platforms/nuttx/src/px4/microchip/samd2l2/hrt/hrt.c` | Full HRT using TC4+TC5 COUNT32 |
| `platforms/nuttx/src/px4/microchip/samd2l2/adc/adc.cpp` | SAMD21 ADC driver (VSERVO + RSSI) |
| `platforms/nuttx/src/px4/microchip/samd2l2/io_pins/io_timer.c` | IO timer stubs |
| `platforms/nuttx/src/px4/microchip/samd2l2/io_pins/pwm_servo.c` | PWM servo stubs |
| `platforms/nuttx/src/px4/microchip/samd2l2/version/board_identity.c` | SAMD21 UUID at 4 Flash addresses |
| `platforms/nuttx/src/px4/microchip/samd2l2/version/board_mcu_version.c` | Minimal MCU version stub |
| `platforms/nuttx/NuttX/nuttx/arch/arm/src/samd2l2/hardware/sam_dmac.h` | Missing family-dispatch header |
| `src/modules/px4iofirmware/serial_samd.cpp` | SAMD21 FMU serial + DMA implementation |

### Modified

| File | What changed |
|---|---|
| `boards/microchip/samd21-io/src/board_config.h` | Added missing macros, fixed GPIO flags, removed redefinitions |
| `boards/microchip/samd21-io/nuttx-config/nsh/defconfig` | Added `CONFIG_EXPERIMENTAL=y`, `CONFIG_SAMD2L2_DMAC=y` |
| `platforms/nuttx/NuttX/nuttx/arch/arm/src/samd2l2/sam_dmac.c` | Fixed NuttX bug: `aligned(16)` → `aligned_data(16)` |
| `src/modules/px4iofirmware/px4io.cpp` | Added `CONFIG_USART5_TXBUFSIZE` fallback for msg buffer size |
| `src/modules/px4iofirmware/CMakeLists.txt` | Added SAMD21 serial selection, `arch_adc`, `arch_hrt` to link |

---

## 3. NuttX Board Configuration (`board.h`)

**File:** `boards/microchip/samd21-io/nuttx-config/include/board.h`

The NuttX `samd2l2` drivers require a specific set of macros.  These were
assembled by studying `boards/arm/samd2l2/samd21-xplained/include/board.h` in
the NuttX source tree.

### Clock configuration

```
XOSC:    disabled (no crystal on this board)
OSC8M:   always-on at /1 = 8 MHz  (NuttX requires it running)
DFLL48M: open-loop, COARSE=31 FINE=512, output 48 MHz
GCLK0:   source = DFLL48M → BOARD_CPU_FREQUENCY = 48 000 000 Hz
GCLK1:   source = OSC8M   → 8 MHz (available for slow peripherals)
GCLK2-7: disabled
PM bus dividers: CPU/APBA/APBB/APBC all /1
BOARD_FLASH_WAITSTATES = 2   (required for 48 MHz)
```

Key macros that NuttX clock initialisation (`sam_clockconfig.c`) reads:

```c
#define BOARD_XOSC_ENABLE         0
#define BOARD_OSC8M_PRESCALER     SYSCTRL_OSC8M_PRESC_1
#define BOARD_DFLL48M_OPENLOOP    1
#define BOARD_DFLL48M_COARSEVALUE 31
#define BOARD_DFLL48M_FINEVALUE   512
#define BOARD_GCLK0_SOURCE        GCLK_SOURCE_DFLL48M
#define BOARD_CPU_FREQUENCY       48000000
#define BOARD_FLASH_WAITSTATES    2
```

### SERCOM macros

NuttX `sam_usart.c` reads per-SERCOM macros at compile time.  Required pattern
(example for SERCOM5):

```c
#define BOARD_SERCOM5_GCLKGEN         0          // use GCLK0
#define BOARD_SERCOM05_SLOW_GCLKGEN   0          // slow clock (shared SERCOM0+5)
#define BOARD_SERCOM5_MUXCONFIG       (USART_CTRLA_TXPAD0_1 | USART_CTRLA_RXPAD1)
#define BOARD_SERCOM5_PINMAP_PAD0     PORT_SERCOM5_PAD0_1  // PB16 TX
#define BOARD_SERCOM5_PINMAP_PAD1     PORT_SERCOM5_PAD1_1  // PB17 RX
#define BOARD_SERCOM5_PINMAP_PAD2     0
#define BOARD_SERCOM5_PINMAP_PAD3     0
```

SERCOM3 (SBUS input) and SERCOM0 (DSM/Spektrum) are similarly configured.

**Note:** The macro `BOARD_SERCOM05_SLOW_GCLKGEN` (not `BOARD_SERCOM5_SLOW_GCLKGEN`)
is what NuttX actually tests — it is shared between SERCOM0 and SERCOM5.

---

## 4. Board Source Files

**File:** `boards/microchip/samd21-io/src/board_config.h`

Changes:

| Problem | Fix |
|---|---|
| `#error PX4_NUMBER_I2C_BUSES not supported` | Added `#define PX4_NUMBER_I2C_BUSES 1` and `#define PX4_NUMBER_SPI_BUSES 0` |
| `PORT_SERCOM0_PAD1_2` undeclared | Added `#include <sam_pinmap.h>` |
| `PORT_OPENDRAIN` undeclared | SAMD21 has no open-drain GPIO mode; changed `GPIO_SPEKTRUM_OUT` to use `PORT_OUTPUT` only |
| Redefinition of `ENABLE_SBUS_OUT`, `VDD_SERVO_FAULT`, `ADC_VSERVO`, `ADC_RSSI`, `PX4IO_ADC_CHANNEL_COUNT` | Removed from board_config.h (all defined in `px4io.h`) |

---

## 5. Platform HAL (`micro_hal.h`)

**File:** `platforms/nuttx/src/px4/microchip/samd2l2/include/px4_arch/micro_hal.h`

Maps PX4 generic GPIO API to SAMD21 NuttX port API:

```c
#pragma once
#include <px4_platform/micro_hal.h>
__BEGIN_DECLS
struct i2c_master_s;          /* forward decl required by px4_mtd.h */
#include <sam_port.h>

#define PX4_BUS_OFFSET                0
#define px4_arch_configgpio(p)        sam_configport(p)
#define px4_arch_unconfiggpio(p)      sam_configport(p)
#define px4_arch_gpioread(p)          sam_portread(p)
#define px4_arch_gpiowrite(p,v)       sam_portwrite(p,v)
#define px4_arch_gpiosetevent(...)    (-ENOSYS)
#define px4_savepanic(f,c,l)          (0)
#define PX4_CPU_UUID_BYTE_LENGTH      16
#define PX4_CPU_UUID_WORD32_LENGTH    (PX4_CPU_UUID_BYTE_LENGTH/sizeof(uint32_t))
#define TIMER_HRT_CYCLES_PER_US       (BOARD_CPU_FREQUENCY/1000000)
#define px4_cache_aligned_data()
#define px4_cache_aligned_alloc       malloc
__END_DECLS
```

The forward declaration of `struct i2c_master_s` was required to suppress a
`-Werror` about an anonymous struct inside a parameter list.

---

## 6. IO Timer

### `io_timer.h`

**File:** `platforms/nuttx/src/px4/microchip/samd2l2/include/px4_arch/io_timer.h`

Copied verbatim from `platforms/nuttx/src/px4/rpi/rpi_common/include/px4_arch/io_timer.h`.

The SAMD21 uses the same minimal `io_timers_t` struct (only a `base` address
field).  This is simpler than the full STM32 version which has clock registers,
IRQ vectors, and channel index fields.

```c
typedef struct io_timers_t {
    uint32_t base;
} io_timers_t;

typedef struct timer_io_channels_t {
    uint32_t gpio_out;
    uint32_t gpio_in;
    uint8_t  timer_index;
    uint8_t  timer_channel;
} timer_io_channels_t;
```

### `io_timer_hw_description.h`

**File:** `platforms/nuttx/src/px4/microchip/samd2l2/include/px4_arch/io_timer_hw_description.h`

Rewritten to match the rpi-derived `io_timers_t` struct.  Key changes from the
original STM32-style version:

- Removed fields: `clock_register`, `clock_bit`, `vectorno`,
  `first/last_channel_index`, `ccr_offset`
- Helper structs renamed from anonymous to named (`TimerChannel_s`, `GPIO_s`)
  to avoid "types may not be defined in parameter types" error
- Parameter `io_timers[]` renamed to `timers[]` to avoid shadowing the global
  declaration

### `io_timer.c`

**File:** `platforms/nuttx/src/px4/microchip/samd2l2/io_pins/io_timer.c`

All functions are stubs returning 0 / NULL for this build phase.  Required
extern data definitions (linker needs them):

```c
io_timer_channel_allocation_t allocations[IOTimerChanModeSize];
const io_timers_t io_timers[MAX_IO_TIMERS];
const io_timers_channel_mapping_t io_timers_channel_mapping;
const timer_io_channels_t timer_io_channels[MAX_TIMER_IO_CHANNELS];
const io_timers_t led_pwm_timers[MAX_LED_TIMERS];
const timer_io_channels_t led_pwm_channels[MAX_TIMER_LED_CHANNELS];
```

---

## 7. HRT — High-Resolution Timer (`hrt.c`)

**File:** `platforms/nuttx/src/px4/microchip/samd2l2/hrt/hrt.c`

Full call-queue HRT implementation using TC4+TC5 chained as a 32-bit counter.

### Hardware setup

- TC4 and TC5 are enabled in `PM_APBCMASK` (bits 12 and 13)
- GCLK0 (48 MHz DFLL) connected to TC4+TC5 via GCLK CLKCTRL ID `0x1C`
- TC4 configured as COUNT32 master, prescaler /1 → 48 MHz → 20.83 ns resolution
- CC0 used for compare-match interrupt (callout scheduling)
- `hrt_absolute_time()` returns µs: `rCOUNT / 48`

### Call queue

Uses NuttX `sq_queue_s` (singly-linked queue).  Calls are sorted by deadline.
`HRT_PEEK()` and `HRT_NEXT()` cast macros route through `(void*)` to suppress
`-Wcast-align` (the `sq_entry_t` is the first field of `hrt_call`, so alignment
is guaranteed correct).

### PPM globals

The RC library (`librc.a`) and `controls.cpp` reference these globals.  They
must be defined somewhere in the firmware; the HRT is the conventional location:

```c
#define PPM_MAX_CHANNELS  12
__EXPORT uint16_t ppm_buffer[PPM_MAX_CHANNELS];
__EXPORT uint16_t ppm_frame_length    = 0;
__EXPORT unsigned ppm_decoded_channels = 0;
__EXPORT uint64_t ppm_last_valid_decode = 0;
```

SAMD21 does not decode PPM in hardware — these are updated by the software PPM
decoder if a PPM RC receiver is wired to the board.

### Latency tracking

```c
const uint16_t latency_bucket_count = LATENCY_BUCKET_COUNT;
const uint16_t latency_buckets[LATENCY_BUCKET_COUNT] = { 1, 2, 5, 10, 20, 50, 100, 1000 };
__EXPORT uint32_t latency_counters[LATENCY_BUCKET_COUNT + 1];
```

---

## 8. ADC Driver (`adc.cpp`)

**File:** `platforms/nuttx/src/px4/microchip/samd2l2/adc/adc.cpp`

Single-ended 12-bit ADC using INTVCC1 (VDDANA) reference.  Two channels:
- AIN[0] (PA2) = VSERVO
- AIN[1] (PA3) = RSSI

### Notable details

- Required `#include <arm_internal.h>` for `getreg32`/`putreg32` (NuttX register
  accessors — not included transitively through other headers)
- Functions wrapped in `extern "C" { ... }` because `px4io.h` declares them with
  C linkage but the file compiles as C++ (`adc.cpp`)
- Contains a `__attribute__((weak))` implementation of `up_udelay()` — see
  [Linker Errors](#15-linker-errors-and-fixes) section

---

## 9. PWM Servo (`pwm_servo.c`)

**File:** `platforms/nuttx/src/px4/microchip/samd2l2/io_pins/pwm_servo.c`

Stub implementation for this build phase.  Two fixes were needed to compile:

| Error | Fix |
|---|---|
| `servo_position_t` unknown type | Changed to `uint16_t` (type is not defined in this codebase) |
| Conflicting types for `up_pwm_update` | Changed signature to `up_pwm_update(unsigned channel_mask)` to match `drv_pwm_output.h` |

---

## 10. Board Identity / MCU Version

### `board_identity.c`

**File:** `platforms/nuttx/src/px4/microchip/samd2l2/version/board_identity.c`

SAMD21 has a 128-bit serial number stored at four **non-contiguous** Flash
addresses:

```c
static const uint32_t uuid_addr[4] = {
    0x0080A00C,   /* word 0 */
    0x0080A040,   /* word 1 */
    0x0080A044,   /* word 2 */
    0x0080A048,   /* word 3 */
};
```

The original code included `hardware/sam_chipid.h` (SAMV7-only) and used a
CHIPID peripheral that does not exist on SAMD21.  Completely rewritten to read
the four Flash serial words.

### `board_mcu_version.c`

**File:** `platforms/nuttx/src/px4/microchip/samd2l2/version/board_mcu_version.c`

Minimal stub — SAMD21 has no CHIPID peripheral for silicon revision readout:

```c
int board_mcu_version(char *rev, const char **revstr, const char **errata) {
    *rev = 'A';
    *revstr = "SAMD21J18A";
    if (errata) { *errata = NULL; }
    return 0;
}
```

---

## 11. FMU Serial Link (`serial_samd.cpp`)

**File:** `src/modules/px4iofirmware/serial_samd.cpp`

Replaces the STM32-specific `serial.cpp` when building for SAMD21.  Uses
SERCOM5 USART + DMAC.

### BAUD calculation

```
BAUD = 65536 × (1 − 16 × f_baud / f_ref)
     = 65536 × (1 − 16 × 1 500 000 / 48 000 000)
     = 65536 × 0.5 = 32 768 = 0x8000
```

### DMAC

- RX channel: beat-triggered by `SERCOM5_RX`, callback `rx_dma_callback`
- TX channel: beat-triggered by `SERCOM5_TX`, no callback (fire-and-forget)
- `sam_dmachannel()` allocates channels using `DMAC_CHFLAGS_*` bitfields

### Fixes applied

| Error | Fix |
|---|---|
| `USART_CTRLA_MODE_USARTINT` undeclared | Renamed to `USART_CTRLA_MODE_INTUSART` (correct SAMD21 constant) |
| `USART_CTRLA_TXPO_PAD0` undeclared | Renamed to `USART_CTRLA_TXPAD0_1` |
| `USART_CTRLA_RXPO_PAD1` undeclared | Renamed to `USART_CTRLA_RXPAD1` |
| `USART_INT_ERROR` undeclared | SAMD20-only bit; replaced with STATUS register poll for `BUFOVF\|FERR\|PERR` |
| DMA callback signature mismatch | Changed from `(DMA_HANDLE, uint8_t, void*)` to `(DMA_HANDLE, void*, int)` to match `dma_callback_t` |
| `hrt_abstime` unknown type | Added `#include <drivers/drv_hrt.h>` |
| `last_rx_bytes`/`last_rx_time` unused warning | Marked `__attribute__((unused))` |

### `sam_serial_dma_poll()`

Currently a no-op stub.  SAMD21 has no IDLE-line interrupt (unlike STM32), so
proper short-packet detection requires reading the DMAC write-back descriptor's
`BTCNT` residual count.  This is marked as a TODO — for now, complete packets
(full `sizeof(IOPacket)` bytes) are handled by the DMA TC (transfer-complete)
callback, and short packets are detected when the next packet resets DMA.

---

## 12. NuttX Submodule Patches

Two bugs were found and fixed in the NuttX `samd2l2` driver tree.

### Missing `hardware/sam_dmac.h`

**File created:** `platforms/nuttx/NuttX/nuttx/arch/arm/src/samd2l2/hardware/sam_dmac.h`

The file did not exist, causing a fatal `#include` error from `sam_dmac.c`.
It should be a family dispatcher:

```c
#if defined(CONFIG_ARCH_FAMILY_SAMD20) || defined(CONFIG_ARCH_FAMILY_SAMD21)
#  include "samd_dmac.h"
#elif defined(CONFIG_ARCH_FAMILY_SAML21)
#  include "saml_dmac.h"
#endif
```

### `sam_dmac.c`: `aligned(16)` → `aligned_data(16)`

**File modified:** `platforms/nuttx/NuttX/nuttx/arch/arm/src/samd2l2/sam_dmac.c`

Three DMA descriptor arrays were declared using:
```c
locate_data(".lpram"), aligned(16)
```
which is invalid C syntax (`aligned` is not a GCC attribute in that position —
it requires `__attribute__((aligned(16)))`).  The correct NuttX idiom is the
`aligned_data()` macro:
```c
locate_data(".lpram") aligned_data(16)
```
Fixed in all three occurrences.

---

## 13. defconfig

**File:** `boards/microchip/samd21-io/nuttx-config/nsh/defconfig`

Added two options required to enable the DMAC driver:

```
CONFIG_EXPERIMENTAL=y
CONFIG_SAMD2L2_DMAC=y
```

In the NuttX Kconfig for `samd2l2`, `SAMD2L2_DMAC` has a `depends on EXPERIMENTAL`
guard.  Without `CONFIG_EXPERIMENTAL=y`, the DMAC Kconfig option is hidden and
`DMA_HANDLE` and related types are not compiled in, causing the build to fail
with `'DMA_HANDLE' does not name a type`.

Also added `CONFIG_USART5_TXBUFSIZE=128` for the SERCOM5 console port, which
`px4io.cpp` uses to size its debug message buffer.

---

## 14. px4iofirmware Integration

**File:** `src/modules/px4iofirmware/CMakeLists.txt`

### Serial implementation selection

```cmake
if(CONFIG_ARCH_CHIP_SAMD2X OR CONFIG_ARCH_CHIP_SAMD21J18A)
    set(SERIAL_IMPL serial_samd.cpp)
    set(ADC_IMPL "")          # ADC provided by arch_adc (samd2l2 platform layer)
else()
    set(SERIAL_IMPL serial.cpp)
    set(ADC_IMPL adc.cpp)     # STM32 inline ADC implementation
endif()
```

### Link libraries

Added `arch_hrt` (provides `hrt_*` API and PPM globals) and `arch_adc` (provides
`adc_init`/`adc_measure` for the SAMD21 case):

```cmake
target_link_libraries(px4iofirmware
    PUBLIC
        arch_hrt
        arch_io_pins
        arch_watchdog_iwdg
        nuttx_apps
        nuttx_arch
        nuttx_c
        nuttx_mm
        rc
)

if(CONFIG_ARCH_CHIP_SAMD2X OR CONFIG_ARCH_CHIP_SAMD21J18A)
    target_link_libraries(px4iofirmware PUBLIC arch_adc)
endif()
```

### `px4io.cpp` — debug message buffer

`px4io.cpp` used `CONFIG_USART1_TXBUFSIZE` (STM32 console UART) to size the
debug message buffer.  SAMD21 uses SERCOM5 as the console, so `CONFIG_USART5_TXBUFSIZE`
is the right symbol.  Added a fallback chain:

```c
#if defined(CONFIG_USART1_TXBUFSIZE)
static char msg[NUM_MSG][CONFIG_USART1_TXBUFSIZE];
#elif defined(CONFIG_USART5_TXBUFSIZE)
static char msg[NUM_MSG][CONFIG_USART5_TXBUFSIZE];
#else
static char msg[NUM_MSG][128];
#endif
```

---

## 15. Linker Errors and Fixes

After all compilation errors were resolved, three categories of linker error
remained.

### `adc_init`/`adc_measure` — C++ name mangling

**Symptom:**
```
undefined reference to `adc_init'
undefined reference to `adc_measure'
```

**Diagnosis:** `adc.cpp` compiles as C++, producing mangled symbols `_Z8adc_initv`
and `_Z11adc_measurej`.  `px4io.h` declares them with plain C linkage, so
the linker looks for unmangled names.

**Fix:** Wrap both function definitions in `extern "C" { ... }` in `adc.cpp`.

---

### PPM globals — missing definitions

**Symptom:**
```
undefined reference to `ppm_last_valid_decode'
undefined reference to `ppm_decoded_channels'
undefined reference to `ppm_buffer'
undefined reference to `ppm_frame_length'
```

**Diagnosis:** These `__EXPORT` globals are expected to be defined in the HRT
library (the rpi_common `hrt.c` defines them; our initial `hrt.c` did not).

**Fix:** Added to `hrt.c`:
```c
#define PPM_MAX_CHANNELS  12
__EXPORT uint16_t ppm_buffer[PPM_MAX_CHANNELS];
__EXPORT uint16_t ppm_frame_length    = 0;
__EXPORT unsigned ppm_decoded_channels = 0;
__EXPORT uint64_t ppm_last_valid_decode = 0;
```

---

### `up_udelay` — static archive link ordering

**Symptom:**
```
undefined reference to `up_udelay'   (from src/lib/rc/librc.a, dsm.cpp)
```

**Diagnosis:** `up_udelay` is defined in `NuttX/nuttx/arch/arm/src/libarch.a`.
However, the linker processes static archives left-to-right; `libarch.a` appears
in the link command BEFORE `librc.a`.  At the time `libarch.a` is processed,
there are no unresolved references to `up_udelay` yet (those appear only when
`librc.a` is processed later), so `up_udelay.o` is never extracted.  After
`librc.a` introduces the reference, there is no remaining instance of `libarch.a`
in the command to satisfy it.

**Fix:** Added a `__attribute__((weak))` fallback definition of `up_udelay` in
`adc.cpp`.  Since `libarch_adc.a` appears AFTER `librc.a` in the link command
(it is a SAMD21-only conditional dependency of `px4iofirmware`), the weak symbol
is extracted and resolves the reference.  If link ordering ever changes and the
strong `up_udelay` from `libarch.a` is extracted first, the weak definition is
simply ignored.

```c
void __attribute__((weak)) up_udelay(uint32_t usec)
{
    hrt_abstime t0 = hrt_absolute_time();
    while (hrt_elapsed_time(&t0) < (hrt_abstime)usec) {}
}
```

---

## 16. Build Result

```
make microchip_samd21-io_default

Memory region         Used Size  Region Size  %age Used
           flash:       40732 B       252 KB     15.78%
            sram:        3248 B        32 KB      9.91%
```

Output: `build/microchip_samd21-io_default/microchip_samd21-io_default.px4`

Zero compilation errors.  Zero linker errors.

---

## 17. Known Limitations / TODO

These are deferred to subsequent work phases:

1. **Pin assignments** — all GPIO macros in `board_config.h` and `board.h` are
   placeholders.  Physical pin mapping must be verified against the actual
   hardware schematic.

2. **`sam_serial_dma_poll()`** — currently a no-op.  Short-packet detection
   requires reading the DMAC write-back descriptor BTCNT field to determine
   the number of bytes received so far.  Implement residual read via
   `DMAC_CHANNEL_WRITEBACK` base address.

3. **TCC PWM driver** — `io_timer.c` and `pwm_servo.c` are stubs.  Real TCC
   (Timer/Counter for Control) configuration for 8-channel PWM output is needed.

4. **ADC channel mapping** — `adc_measure(0)` = AIN0 (PA2) and `adc_measure(1)` =
   AIN1 (PA3) are assumed.  Verify against schematic.

5. **`BOARD_SERCOM05_SLOW_GCLKGEN`** — set to GCLK0 (48 MHz).  The slow clock
   for I²C start/stop condition timing is normally a low-frequency clock; verify
   that SERCOM3/SERCOM0 work correctly with GCLK0 as the slow source, or assign
   GCLK1 (8 MHz) instead.

6. **Watchdog** — `wdt.c` is a stub.  The SAMD21 WDT is enabled in defconfig
   (`CONFIG_SAMD2L2_WDT=y`) but the `watchdog_init()`/`watchdog_pet()` shims
   need implementing.

7. **LED macros** — `LED_BLUE`, `LED_AMBER`, `LED_SAFETY`, `LED_GREEN` are
   defined in `board.h` but point to placeholder GPIO pins.

8. **`calculate_fw_crc()`** in `px4io.cpp** uses a hardcoded STM32 load address
   (`0x08001000`).  For SAMD21, the application start address depends on the
   bootloader size.  Update `APP_LOAD_ADDRESS` and `APP_SIZE_MAX` for the SAMD21
   flash layout.
