# BLE UART Driver Changes for RNBD350 Migration

## File: ble_uart.c (SERCOM1)

This document contains all changes required in `ble_uart.c` to support the
RNBD350 module in Transparent UART mode. The RNBD350 sends ASCII status
strings (`%CONNECT%`, `%STREAM_OPEN%`, `%DISCONNECT%`) over UART that must
be filtered out before Kirana protocol data reaches `ble_data.rx_buff`.

---

## Complete Modified ble_uart.c

```c
/* ************************************************************************** */
/**
  @File Name
    ble_uart.c

  @Summary
    BLE UART driver for RNBD350 on SERCOM1 (P0 & P1).

  @Description
    Modified for RNBD350 migration:
    - Added status string filter (rnbd350_filter_rx_buffer)
    - Added connection state tracking (g_rnbd350_stream_open)
    - RX_COMPLETE case now filters before passing to protocol layer
 */
/* ************************************************************************** */

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "time.h"
#include "definitions.h"
#include "global.h"
#include "iso_uart.h"
#include "chg_mem_data.h"
#include "app_atm90e26.h"
#include "cp_pp.h"
#include "ble_uart.h"
#include "ble_app.h"

/* ************************************************************************** */
/* Global Data                                                                */
/* ************************************************************************** */

uart_params app_uart_ble_data;

/* ---- RNBD350 connection state ---- */
volatile bool g_rnbd350_stream_open = false;
static bool   g_rnbd350_connected   = false;

/* ************************************************************************** */
/* SERCOM1 Callbacks (UNCHANGED)                                              */
/* ************************************************************************** */

void usart_bleTX_callback(uintptr_t context)
{
    app_uart_ble_data.txdone = 1;
}

void usart_bleRX_callback(uintptr_t context)
{
    LED_AVAIL_Clear();
    switch (app_uart_ble_data.rxstatus) {
        case RX_IDEAL:
        {
        }
        break;
        case RX_BYTE1_REC:
        {
            SERCOM1_USART_Read(&app_uart_ble_data.rx_buff[1], 63);
            app_uart_ble_data.rxstatus = RX_WAIT;
            app_uart_ble_data.rx_overflow = MS_100_WAIT;
        }
        break;
        case RX_COMPLETE:
        {
        }
        break;
        case RX_WAIT:
        {
            app_uart_ble_data.rx_overflow = 0;
            app_uart_ble_data.rxbufflen = 64;
            app_uart_ble_data.rxstatus = RX_COMPLETE;
        }
        break;
        default: break;
    }
}

/* ************************************************************************** */
/* Init and TX Functions (UNCHANGED)                                          */
/* ************************************************************************** */

void app_uart_ble_init(void)
{
    SERCOM1_USART_WriteCallbackRegister(usart_bleTX_callback, 0);
    SERCOM1_USART_ReadCallbackRegister(usart_bleRX_callback, 0);
    app_uart_ble_data.rxstatus = RX_IDEAL;
    app_uart_ble_data.txdone = 1;
}

void app_uart_ble_send_data(void)
{
    app_uart_ble_data.txdone = 0;
    SERCOM1_USART_Write(app_uart_ble_data.tx_buff, app_uart_ble_data.txbufflen);
    while (app_uart_ble_data.txdone < 1);
}

void app_uart_ble_send_data_wait(void)
{
    uint8_t i;
#ifdef DEBUG_BLE
    switch (charger.arai_status) {
        case 0:  app_uart_iso_print("         IDLE\n");            break;
        case 1:  app_uart_iso_print("         INTIALIZATION\n");   break;
        case 2:  app_uart_iso_print("         AUTHORIZATION\n");   break;
        case 3:  app_uart_iso_print("     CHARGING...  \n");       break;
        case 4:  app_uart_iso_print("         CHARGING END \n");   break;
        case 5:  app_uart_iso_print("         ERROR        \n");   break;
    }
#endif
    SERCOM1_REGS->USART_INT.SERCOM_INTENCLR =
        (uint8_t) SERCOM_USART_INT_INTFLAG_DRE_Msk;

    for (i = 0; i < app_uart_ble_data.txbufflen; i++) {
        while (((SERCOM1_REGS->USART_INT.SERCOM_INTFLAG &
                 SERCOM_USART_INT_INTFLAG_DRE_Msk) !=
                 SERCOM_USART_INT_INTFLAG_DRE_Msk));
        SERCOM1_REGS->USART_INT.SERCOM_DATA = app_uart_ble_data.tx_buff[i];
    }
}

void app_uart_ble_print(char *msg)
{
    app_uart_ble_data.txbufflen =
        sprintf((char *) app_uart_ble_data.tx_buff, "%s", msg);
    app_uart_ble_send_data_wait();
}

void app_uart_ble_receive_data_start(void)
{
    SERCOM1_USART_Read(app_uart_ble_data.rx_buff, 1);
}

/* ************************************************************************** */
/* NEW: RNBD350 Status String Filter                                          */
/* ************************************************************************** */

/**
 * @brief Filter RNBD350 status strings from received UART data.
 *
 * The RNBD350 module sends ASCII status events over UART when BLE
 * connection state changes:
 *
 *   %CONNECT,<handle>%\r\n    — BLE central connected
 *   %STREAM_OPEN%\r\n         — Transparent UART data mode active
 *   %DISCONNECT%\r\n          — BLE central disconnected
 *   %REBOOT%\r\n              — Module rebooted
 *
 * These must NOT reach the Kirana protocol parser (k_buff_play) or
 * they corrupt the rx_buff and cause the mobile app to get stuck at
 * "Connecting to charger."
 *
 * This function scans app_uart_ble_data.rx_buff and handles three cases:
 *
 *   Case 1: Pure status string        → discard, return false
 *   Case 2: Status + Kirana frame     → extract frame, return true
 *   Case 3: Clean Kirana frame (0x18) → pass through, return true
 *
 * @return true  if buffer contains a valid Kirana frame to process
 * @return false if buffer was status-only (caller should discard and restart)
 */
static bool rnbd350_filter_rx_buffer(void)
{
    uint8_t *buf = app_uart_ble_data.rx_buff;
    uint8_t  len = app_uart_ble_data.rxbufflen;
    uint8_t  i;

    /* Empty buffer — nothing to do */
    if (len == 0) {
        return false;
    }

    /* ---- Case 1: Buffer starts with '%' — RNBD350 status string ---- */
    if (buf[0] == '%') {

        /* Null-terminate for string search (safe: rx_buff is 64 bytes) */
        buf[len] = '\0';

        /* Track connection state */
        if (strstr((const char *)buf, "%CONNECT") != NULL) {
            g_rnbd350_connected = true;
#ifdef DEBUG_BLE
            app_uart_iso_print("\r\nRNBD350: Connected\n");
#endif
        }

        if (strstr((const char *)buf, "%STREAM_OPEN") != NULL) {
            g_rnbd350_stream_open = true;
#ifdef DEBUG_BLE
            app_uart_iso_print("\r\nRNBD350: Stream open — data mode active\n");
#endif
        }

        if (strstr((const char *)buf, "%DISCONNECT") != NULL) {
            g_rnbd350_stream_open = false;
            g_rnbd350_connected = false;
#ifdef DEBUG_BLE
            app_uart_iso_print("\r\nRNBD350: Disconnected\n");
#endif
        }

        if (strstr((const char *)buf, "%REBOOT") != NULL) {
            g_rnbd350_stream_open = false;
            g_rnbd350_connected = false;
#ifdef DEBUG_BLE
            app_uart_iso_print("\r\nRNBD350: Rebooted\n");
#endif
        }

        /* Check if a Kirana frame is hiding after the status string.
         * This can happen if the app sends data very quickly after connecting
         * and the status string + data frame arrive in the same UART block.
         *
         * Example: %STREAM_OPEN%\r\n[0x18][0x01][0x02]...[0x17]
         *                            ^--- Kirana frame starts here
         */
        for (i = 1; i < len; i++) {
            if (buf[i] == K_FRAME_START_BYTE) {
                /* Found Kirana frame — shift it to the beginning */
                uint8_t frame_len = len - i;
                memmove(buf, &buf[i], frame_len);
                app_uart_ble_data.rxbufflen = frame_len;
                return true;  /* process the extracted frame */
            }
        }

        /* Pure status string, no Kirana data — discard entirely */
        return false;
    }

    /* ---- Case 2: Buffer starts with \r or \n (trailing from status) ---- */
    if (buf[0] == '\r' || buf[0] == '\n') {
        /* Skip all leading \r\n characters */
        for (i = 0; i < len; i++) {
            if (buf[i] != '\r' && buf[i] != '\n') {
                break;
            }
        }

        if (i >= len) {
            return false;  /* all whitespace — discard */
        }

        /* Shift real data to beginning */
        uint8_t data_len = len - i;
        memmove(buf, &buf[i], data_len);
        app_uart_ble_data.rxbufflen = data_len;

        /* If shifted data is another status string, filter again */
        if (buf[0] == '%') {
            return rnbd350_filter_rx_buffer();
        }

        /* Check if it's a Kirana frame */
        if (buf[0] == K_FRAME_START_BYTE) {
            return true;
        }

        /* Unknown data after whitespace — discard */
        return false;
    }

    /* ---- Case 3: Buffer starts with 0x18 — clean Kirana frame ---- */
    if (buf[0] == K_FRAME_START_BYTE) {
        return true;  /* pass through unchanged */
    }

    /* ---- Case 4: Unknown data — scan for Kirana frame start ---- */
    /* This handles rare cases like RNBD350 boot messages or partial
     * status strings that don't start with '%' */
    for (i = 0; i < len; i++) {
        if (buf[i] == K_FRAME_START_BYTE) {
            uint8_t frame_len = len - i;
            memmove(buf, &buf[i], frame_len);
            app_uart_ble_data.rxbufflen = frame_len;
            return true;
        }
    }

    /* No Kirana frame found anywhere — discard */
    return false;
}

/* ************************************************************************** */
/* MODIFIED: RX Task with RNBD350 Filter                                      */
/* ************************************************************************** */

void app_uart_ble_rx_task(void)
{
    switch (app_uart_ble_data.rxstatus) {
        case RX_IDEAL:
        {
            app_uart_ble_receive_data_start();
            app_uart_ble_data.rxstatus = RX_BYTE1_REC;
        }
        break;

        case RX_BYTE1_REC:
        {
            /* waiting for first byte — nothing to do */
        }
        break;

        case RX_COMPLETE:
        {
            /* ============================================ */
            /* NEW: RNBD350 status string filter            */
            /* ============================================ */
            /* Filter BEFORE copying to ble_data.rx_buff.   */
            /* If the buffer contains only %STATUS% strings */
            /* from the RNBD350, discard and restart.       */
            /* ============================================ */
            if (!rnbd350_filter_rx_buffer()) {
                /* Status string only — discard, restart reception */
#ifdef DEBUG_BLE
                app_uart_iso_print("BLE:Status filtered\n");
#endif
                app_uart_ble_receive_data_start();
                app_uart_ble_data.rxstatus = RX_BYTE1_REC;
                break;
            }

            /* ============================================ */
            /* Valid Kirana frame — pass to protocol layer  */
            /* (This part is the same as before)            */
            /* ============================================ */
            if (ble_data.process_ble_data == 0) {
                memcpy(ble_data.rx_buff,
                       app_uart_ble_data.rx_buff,
                       app_uart_ble_data.rxbufflen);
                ble_data.rxbufflen = app_uart_ble_data.rxbufflen;
                ble_data.process_ble_data = 1;

                app_uart_ble_receive_data_start();
                app_uart_ble_data.rxstatus = RX_BYTE1_REC;
            }

            app_uart_iso_print("BLE:Rec Data:\n");
            memcpy(app_uart_iso_data.tx_buff,
                   app_uart_ble_data.rx_buff,
                   app_uart_ble_data.rxbufflen);
            app_uart_iso_data.txbufflen = app_uart_ble_data.rxbufflen;
            app_uart_iso_send_data_wait();
        }
        break;

        case RX_WAIT:
        {
            /* waiting for remaining bytes or timeout — nothing to do */
        }
        break;

        default: break;
    }
}

/* ************************************************************************** */
/* Overflow Handler (UNCHANGED)                                               */
/* ************************************************************************** */

void app_uart_ble_rx_overflow_fun(void)
{
    if (app_uart_ble_data.rx_overflow > 0) {
        app_uart_ble_data.rx_overflow--;
        if ((app_uart_ble_data.rx_overflow == 0) &&
            (app_uart_ble_data.rxstatus == RX_WAIT)) {
            app_uart_ble_data.rxbufflen = SERCOM1_USART_ReadCountGet() + 1;
            app_uart_ble_data.rxstatus = RX_COMPLETE;
            SERCOM1_USART_ReadAbort();
        }
    }
}

/* *****************************************************************************
 End of File
 */
```

---

## Complete Modified ble_uart.h

```c
/* ************************************************************************** */
/**
  @File Name
    ble_uart.h

  @Summary
    BLE UART driver header for RNBD350 on SERCOM1.

  @Description
    Added RNBD350 connection state externals.
 */
/* ************************************************************************** */

#ifndef _BLE_UART_H
#define _BLE_UART_H

#include "ble_app.h"    /* for K_FRAME_START_BYTE */

#ifdef __cplusplus
extern "C" {
#endif

/* UART data buffer (shared with ble_app.c) */
extern uart_params app_uart_ble_data;

/* RNBD350 connection state flags
 *
 * g_rnbd350_stream_open:
 *   Set to true when %STREAM_OPEN% is received from RNBD350.
 *   This means Transparent UART data mode is active.
 *   Use this in ble_task() to gate protocol processing.
 *
 * g_rnbd350_connected:
 *   Set to true when %CONNECT% is received.
 *   Set to false when %DISCONNECT% is received.
 *   Can be used to show connection status on LEDs.
 */
extern volatile bool g_rnbd350_stream_open;

/* Function prototypes */
void app_uart_ble_init(void);
void app_uart_ble_send_data(void);
void app_uart_ble_send_data_wait(void);
void app_uart_ble_print(char *msg);
void app_uart_ble_receive_data_start(void);
void app_uart_ble_rx_task(void);
void app_uart_ble_rx_overflow_fun(void);

#ifdef __cplusplus
}
#endif

#endif /* _BLE_UART_H */

/* *****************************************************************************
 End of File
 */
```

---

## Changes to ble_app.c (ble_task only)

```c
/* Add at top of file or include ble_uart.h */
extern volatile bool g_rnbd350_stream_open;

void ble_task(void)
{
    /* ============================================ */
    /* NEW: Don't process until Transparent UART    */
    /* data mode is active (STREAM_OPEN received)   */
    /* ============================================ */
    if (!g_rnbd350_stream_open) {
        ble_data.process_ble_data = 0;
        return;
    }

    if (ble_data.process_ble_data) {
#ifdef DEBUG_BLE
        app_uart_iso_print("\n\n\r APP DATA RX =>\n");
        app_uart_iso_data.txbufflen = to_ascii(ble_data.rx_buff,
                                               app_uart_iso_data.tx_buff,
                                               ble_data.rxbufflen);
        app_uart_iso_send_data_wait();
#endif
        k_buff_play();
        ble_data.process_ble_data = 0;
    }
}
```

---

## Summary of All Changes

| File | What Changed | Why |
|------|-------------|-----|
| **ble_uart.c** | Added `g_rnbd350_stream_open` and `g_rnbd350_connected` globals | Track RNBD350 connection state |
| **ble_uart.c** | Added `rnbd350_filter_rx_buffer()` function | Filter `%CONNECT%`, `%STREAM_OPEN%`, `%DISCONNECT%` status strings from rx_buff before they reach Kirana parser |
| **ble_uart.c** | Modified `RX_COMPLETE` case in `app_uart_ble_rx_task()` | Call filter before memcpy to `ble_data.rx_buff`. If status-only, discard and restart reception |
| **ble_uart.h** | Added `#include "ble_app.h"` | Needed for `K_FRAME_START_BYTE` in filter |
| **ble_uart.h** | Added `extern volatile bool g_rnbd350_stream_open` | Allow `ble_task()` to check connection state |
| **ble_app.c** | Added stream-open gate in `ble_task()` | Don't process protocol data until RNBD350 transparent UART is active |

## What Was NOT Changed

| File | Section | Status |
|------|---------|--------|
| **ble_uart.c** | `usart_bleTX_callback()` | UNCHANGED |
| **ble_uart.c** | `usart_bleRX_callback()` | UNCHANGED |
| **ble_uart.c** | `app_uart_ble_init()` | UNCHANGED |
| **ble_uart.c** | `app_uart_ble_send_data()` | UNCHANGED |
| **ble_uart.c** | `app_uart_ble_send_data_wait()` | UNCHANGED |
| **ble_uart.c** | `app_uart_ble_print()` | UNCHANGED |
| **ble_uart.c** | `app_uart_ble_receive_data_start()` | UNCHANGED |
| **ble_uart.c** | `app_uart_ble_rx_overflow_fun()` | UNCHANGED |
| **ble_app.c** | `k_buff_play()` | UNCHANGED |
| **ble_app.c** | All `kirana_*` handlers | UNCHANGED |
| **ble_app.h** | All definitions and prototypes | UNCHANGED |
| **main.c** | Main loop structure | UNCHANGED |

## Filter Logic Flowchart

```
SERCOM1 RX complete (1-64 bytes in app_uart_ble_data.rx_buff)
    │
    ▼
app_uart_ble_rx_task() → RX_COMPLETE
    │
    ▼
rnbd350_filter_rx_buffer()
    │
    ├─ buf[0] == '%' ?
    │   ├─ YES → parse status string
    │   │         ├─ %CONNECT%      → g_rnbd350_connected = true
    │   │         ├─ %STREAM_OPEN%  → g_rnbd350_stream_open = true
    │   │         ├─ %DISCONNECT%   → both flags = false
    │   │         └─ scan for 0x18 in remaining bytes
    │   │              ├─ found  → memmove frame to start, return TRUE
    │   │              └─ none   → return FALSE (discard)
    │   │
    │   └─ NO → continue
    │
    ├─ buf[0] == '\r' or '\n' ?
    │   ├─ YES → skip leading whitespace
    │   │         ├─ all whitespace → return FALSE
    │   │         ├─ found '%'      → recurse (filter again)
    │   │         └─ found 0x18     → return TRUE
    │   └─ NO → continue
    │
    ├─ buf[0] == 0x18 ?
    │   └─ YES → return TRUE (clean Kirana frame)
    │
    └─ else → scan for 0x18 anywhere
              ├─ found  → memmove, return TRUE
              └─ none   → return FALSE (discard)
    │
    ▼
TRUE → memcpy to ble_data.rx_buff → ble_task() → k_buff_play()
FALSE → discard, restart SERCOM1 reception
```

## Testing Procedure

1. Flash the updated firmware
2. Open debug UART terminal (Docklight / PuTTY on ISO UART)
3. Power on charger — you should see normal boot messages
4. Open mobile app — connect to "EV-CHARGER"
5. On debug terminal you should see:

```
RNBD350: Connected
RNBD350: Stream open — data mode active
BLE:Rec Data:
18 01 02 45 30 ...          ← clean INIT frame (no % garbage)
         INTIALIZATION
BLE:Rec Data:
18 02 0B 0A 0B ...          ← AUTH frame
         AUTHORIZATION
```

6. If you see `BLE:Status filtered` — the filter is working correctly
7. If app proceeds past "Connecting to charger" — migration complete
