/*****************************************************************************
 * main.c — EVSE CP (ADC + PWM + Relay Board)
 * Merged: Button + OLED + Fault Check + Earth Monitor + Energy Meter + CP
 *
 * Key flags
 *   cp_task_enabled   false = CP state-machine disabled (charger runs by button only)
 *   show_values       true  = charger relay ON, OLED shows live meter data
 *****************************************************************************/

/*© [2025] Microchip Technology Inc. and its subsidiaries.
 * SOFTWARE IS "AS IS". NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY,
 * APPLY TO THIS SOFTWARE. */

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include "../src/config/default/definitions.h"
#include "../DB_V1.X/global.h"
#include "../DB_V1.X/cp_pp.h"
#include "../DB_V1.X/iso_uart.h"
#include "../DB_V1.X/myutils.h"
#include "../DB_V1.X/sanity_check.h"
#include "../DB_V1.X/i2c_eeprom.h"
#include "../DB_V1.X/app_atm90e26.h"
#include "../DB_V1.X/afe_calib_mem.h"
#include "../DB_V1.X/ble_app.h"
#include "../DB_V1.X/ble_uart.h"
#include "../DB_V1.X/chg_mem_data.h"
#include "../src/config/default/system/time/sys_time.h"
#include "../DB_V1.X/ssd1309.h"
#include "../DB_V1.X/ssd1309_font.h"

/* ── Externs ──────────────────────────────────────────────────────────────── */
extern volatile cp_adc_t    cp_adc;
extern afe_meas_results     meter_data;
extern volatile uint8_t     is_afe_ready;
extern volatile uint8_t     afe_read_register_ready;
extern float                meter_at_energy;
extern float                meter_rt_energy;
extern void AFE_init(void);
extern void AFE_Task(void);

/* ── System globals (required by modules) ─────────────────────────────────── */
uint8_t          time_100ms   = 0;
volatile uint16_t timercnt1sec;
volatile uint8_t  timer_flag;
volatile uint8_t  app_mode;
volatile uint8_t  frame_counter;
charger_status    EVSE_Status;

/* ── Task enable flags ────────────────────────────────────────────────────── */
/* Set cp_task_enabled = true when you want the IEC 61851 CP state-machine.
   Button → relay works regardless of this flag.                              */
static bool cp_task_enabled = false;

/* show_values: true = charger relay ON, OLED shows live meter                */
static bool show_values = false;

/* ── Fault flags ──────────────────────────────────────────────────────────── */
typedef enum {
    FAULT_NONE        = 0x00,
    FAULT_OVERVOLTAGE = 0x01,
    FAULT_UNDERVOLTAGE= 0x02,
    FAULT_OVERCURRENT = 0x04,
    FAULT_EARTH       = 0x08,
    FAULT_OVERPOWER   = 0x10,
    FAULT_EMG_SWITCH  = 0x20,
    FAULT_WELD        = 0x40,
} evse_fault_t;

static uint8_t active_faults = FAULT_NONE;

/* ── Thresholds ───────────────────────────────────────────────────────────── */
#define VRMS_MAX_V   253.0f   /* +10 % of 230 V */
#define VRMS_MIN_V   195.0f   /* -15 % of 230 V */
#define IRMS_MAX_A    32.0f   /* 32 A max        */
#define POWER_MAX_W 7360.0f   /* 7.36 kW         */

/* ═══════════════════════════════════════════════════════════════════════════
   10 ms SYSTEM TIMER CALLBACK  (ISR context – keep short)
   ═══════════════════════════════════════════════════════════════════════════ */
void user_timer10ms(uintptr_t context)
{
    /* 100 ms divider */
    if (time_100ms > 0)
    {
        if (--time_100ms == 0)
            time_100ms = 10;
    }

    /* 1-second flag */
    if (timercnt1sec > 0)
    {
        if (--timercnt1sec == 0)
        {
            timer_flag   = 1;
            timercnt1sec = MS_1000_WAIT;
        }
    }

    if (afe_wait_for_read > 0)        afe_wait_for_read--;
    if (afe_wait_for_read_energy > 0) afe_wait_for_read_energy--;

    if (cp_adc.cp_timekeeper > 0)
    {
        if (--cp_adc.cp_timekeeper == 0)
        {
            cp_adc.cp_ready      = 1;
            cp_adc.cp_timekeeper = 250;
        }
    }

    buzzer_task_10ms_isr();
    i2c_timeout();
    AFE_uart_rx_overflow_fun();
    app_uart_iso_rx_overflow_fun();
    app_uart_ble_rx_overflow_fun();
}

/* ── Timer / peripheral init ──────────────────────────────────────────────── */
static void init_system_timers(void)
{
    SYSTICK_TimerCallbackSet(user_timer10ms, 0);
    SYSTICK_TimerStart();

    TC1_CompareInitialize();   /* CP PWM        */
    TC2_TimerInitialize();     /* ADC delay     */
    TC4_CompareInitialize();   /* Relay PWM     */
    TC4_CompareStart();
}

/* ═══════════════════════════════════════════════════════════════════════════
   RELAY / PWM HELPERS
   ═══════════════════════════════════════════════════════════════════════════ */
static void relay_on(void)
{
    TC4_Compare8bitMatch0Set(169U);   /* ~90 % duty */
    TC4_CompareStart();
}

static void relay_off(void)
{
    TC4_Compare8bitMatch0Set(0U);
}

/* ═══════════════════════════════════════════════════════════════════════════
   OLED HELPERS
   ═══════════════════════════════════════════════════════════════════════════ */
static void oled_show_fault_screen(uint8_t faults)
{
    ssd1309_fill(0);
    ssd1309_draw_string(28, 0, "!! FAULT !!", 1, 1);
    ssd1309_draw_hline(0, 10, 128, 1);

    uint8_t row = 14;
    if (faults & FAULT_OVERVOLTAGE)  { ssd1309_draw_string(0, row, "OVERVOLTAGE",  1, 1); row += 10; }
    if (faults & FAULT_UNDERVOLTAGE) { ssd1309_draw_string(0, row, "UNDERVOLTAGE", 1, 1); row += 10; }
    if (faults & FAULT_OVERCURRENT)  { ssd1309_draw_string(0, row, "OVERCURRENT",  1, 1); row += 10; }
    if (faults & FAULT_EARTH)        { ssd1309_draw_string(0, row, "EARTH FAULT",  1, 1); row += 10; }
    if (faults & FAULT_OVERPOWER)    { ssd1309_draw_string(0, row, "OVER POWER",   1, 1); row += 10; }
    if (faults & FAULT_EMG_SWITCH)   { ssd1309_draw_string(0, row, "EMG SWITCH",   1, 1); row += 10; }
    if (faults & FAULT_WELD)         { ssd1309_draw_string(0, row, "RELAY WELD",   1, 1); }
    ssd1309_update();
}

static void oled_show_charger_off_screen(void)
{
    ssd1309_fill(0);

    /* Corner brackets */
    ssd1309_draw_hline(0,   0,  15, 1); ssd1309_draw_vline(0,   0,  15, 1);
    ssd1309_draw_hline(0,   1,  15, 1); ssd1309_draw_vline(1,   0,  15, 1);
    ssd1309_draw_hline(113, 0,  15, 1); ssd1309_draw_vline(127, 0,  15, 1);
    ssd1309_draw_hline(113, 1,  15, 1); ssd1309_draw_vline(126, 0,  15, 1);
    ssd1309_draw_hline(0,   63, 15, 1); ssd1309_draw_vline(0,   49, 15, 1);
    ssd1309_draw_hline(0,   62, 15, 1); ssd1309_draw_vline(1,   49, 15, 1);
    ssd1309_draw_hline(113, 63, 15, 1); ssd1309_draw_vline(127, 49, 15, 1);
    ssd1309_draw_hline(113, 62, 15, 1); ssd1309_draw_vline(126, 49, 15, 1);

    /* Battery body with NO-CHARGE slash */
    uint8_t bx = 46, by = 6;
    ssd1309_draw_rect(bx,     by,     34, 22, 1);
    ssd1309_draw_rect(bx + 1, by + 1, 32, 20, 1);
    ssd1309_fill_rect(bx + 34, by + 7, 4, 8, 1);
    for (uint8_t i = 0; i < 28; i++)
    {
        uint8_t yo = (i * 18u) / 28u;
        ssd1309_draw_pixel(bx + 3 + i, by + 2 + yo, 1);
        ssd1309_draw_pixel(bx + 3 + i, by + 3 + yo, 1);
        ssd1309_draw_pixel(bx + 4 + i, by + 2 + yo, 1);
        ssd1309_draw_pixel(bx + 4 + i, by + 3 + yo, 1);
        ssd1309_draw_pixel(bx + 5 + i, by + 2 + yo, 1);
    }
    ssd1309_draw_string(33, 38, "CHARGER OFF", 1, 1);
    ssd1309_update();
    ssd1309_delay_ms(2000);

    /* Return to idle splash */
    ssd1309_fill(0);
    ssd1309_draw_string(22, 15, "1 PHASE",    1, 2);
    ssd1309_draw_string(4,  35, "7.3kW EVSE", 1, 2);
    ssd1309_update();
}

/* ═══════════════════════════════════════════════════════════════════════════
   FAULT CHECK TASK  — call every second via timer_flag
   Reads live AFE values; trips relay and shows OLED fault screen on fault.
   ═══════════════════════════════════════════════════════════════════════════ */
static void fault_check_task(void)
{
    /* Hardware faults from sanity_check module */
    EMG_SWITCH_Fault_Handler(0);
    relay_weld_detection();         /* sets fault.relay_weld_status if welded */

    uint8_t new_faults = FAULT_NONE;

    if (fault.EMG_SWITCH_fault_status) new_faults |= FAULT_EMG_SWITCH;
    if (fault.relay_weld_status)       new_faults |= FAULT_WELD;

    /* AFE metering faults (only when AFE is ready and charger is running) */
    if (is_afe_ready && show_values)
    {
        float vrms = meter_data.urms.float_var;
        float irms = meter_data.irms.float_var;
        float pwr  = (float)meter_data.pmean.word1;

        if (vrms > VRMS_MAX_V)                 new_faults |= FAULT_OVERVOLTAGE;
        if (vrms < VRMS_MIN_V && vrms > 10.0f) new_faults |= FAULT_UNDERVOLTAGE;
        if (irms > IRMS_MAX_A)                 new_faults |= FAULT_OVERCURRENT;
        if (pwr  > POWER_MAX_W)                new_faults |= FAULT_OVERPOWER;
    }

    /* React only when fault state changes */
    if (new_faults != active_faults)
    {
        active_faults = new_faults;

        if (active_faults != FAULT_NONE)
        {
            relay_off();
            show_values = false;
            LED_CHARGE_Clear();
            LED_FAULT_Set();
            buzzer_start_10ms(500);
            oled_show_fault_screen(active_faults);
        }
        else
        {
            LED_FAULT_Clear();
            buzzer_start_10ms(50);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   EARTH / PE CHECK TASK  — call every ~500 ms
   Uses check_PE_presense() from sanity_check.h.
   ═══════════════════════════════════════════════════════════════════════════ */
static void earth_check_task(void)
{
    if (check_PE_presense())
    {
        /* Earth OK — clear earth fault if it was set */
        if (active_faults & FAULT_EARTH)
        {
            active_faults &= ~FAULT_EARTH;
            earth_on();
        }
    }
    else
    {
        /* Earth lost during operation — trip immediately */
        if (!(active_faults & FAULT_EARTH))
        {
            active_faults |= FAULT_EARTH;
            relay_off();
            show_values = false;
            LED_CHARGE_Clear();
            LED_FAULT_Set();
            earth_off();
            buzzer_start_10ms(300);
            oled_show_fault_screen(active_faults);
            app_uart_iso_print("\r\n==EARTH LOST DURING OPERATION==\n");
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   ENERGY METER DISPLAY TASK
   Updates OLED with live VRMS / IRMS / PF / Energy from ATM90E26.
   Called every main loop; only writes to display when AFE data is fresh.
   ═══════════════════════════════════════════════════════════════════════════ */
static void energy_meter_task(void)
{
    if (!show_values)               return;   /* charger off  */
    if (!is_afe_ready)              return;   /* AFE not ready */
    if (!afe_read_register_ready)   return;   /* no fresh data */
    if (active_faults != FAULT_NONE) return;  /* fault screen showing */

    oled_show_dashboard();          /* renders VRMS, IRMS, PF, kWh */
    afe_read_register_ready = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
   CP TASK (IEC 61851 pilot signal)
   Disabled by default (cp_task_enabled = false).
   Charger button works independently — set cp_task_enabled = true to restore
   full CP state-machine control.
   ═══════════════════════════════════════════════════════════════════════════ */
static void cp_pilot_task(void)
{
    if (!cp_task_enabled) return;

    if (cp_adc.cp_ready)
    {
        cp_adc.cp_ready = 0;
        cp_task();          /* IEC 61851 A/B/C/D/E/F state machine */
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
   BUTTON TASK  — charger ON/OFF independent of CP task
   Active-LOW input with 20-count debounce.
   Refuses to start if any active fault is present.
   ═══════════════════════════════════════════════════════════════════════════ */
void buttons_task(void)
{
    static uint8_t  prev_state = 1;
    static uint32_t debounce   = 0;

    uint8_t curr_state = CHARGER_ON_OFF_Get();   /* Active-LOW */

    if (curr_state != prev_state)
    {
        debounce++;
        if (debounce >= 20)
        {
            prev_state = curr_state;
            debounce   = 0;

            if (curr_state == 0)   /* button pressed */
            {
                show_values = !show_values;

                if (show_values)
                {
                    /* ── CHARGER ON ───────────────────────────────────── */
                    if (active_faults != FAULT_NONE)
                    {
                        /* Refuse start when faulted */
                        show_values = false;
                        oled_show_fault_screen(active_faults);
                        buzzer_start_10ms(500);
                        return;
                    }

                    relay_on();
                    LED_CHARGE_Toggle();
                    LED_FAULT_Clear();

                    ssd1309_power_on(true);
                    ssd1309_delay_ms(10);
                    oled_show_dashboard();

                    buzzer_start_10ms(100);
                }
                else
                {
                    /* ── CHARGER OFF ──────────────────────────────────── */
                    relay_off();
                    LED_CHARGE_Clear();
                    oled_show_charger_off_screen();
                    buzzer_start_10ms(200);
                }
            }
        }
    }
    else
    {
        debounce = 0;
    }
}

bool is_showing_values(void) { return show_values; }

/* ═══════════════════════════════════════════════════════════════════════════
   MAIN
   ═══════════════════════════════════════════════════════════════════════════ */
int main(void)
{
    SYS_Initialize(NULL);
    LED_AVAIL_Set();

    /* UART init */
    app_uart_iso_init();
    app_uart_ble_init();

    /* Timers + peripherals */
    init_system_timers();
    init_fault_det();
    timer_flag   = 0;
    timercnt1sec = MS_1000_WAIT;
    frame_counter = 0;
    relay_off();   /* relay OFF at startup */

    /* BLE */
    ble_init();
    SYSTICK_DelayMs(500);
    app_uart_ble_receive_data_start();
    SYSTICK_DelayMs(3000);

    /* ── Emergency switch check at power-on ── */
    EMG_SWITCH_Fault_Handler(0);
    if (fault.EMG_SWITCH_fault_status)
    {
        app_uart_iso_print("\r\n==EMERGENCY SWITCH PRESSED AT POR==\n");
        while (1) { LED_FAULT_Set(); buzzer_fault_start(); }
    }

    /* ── Earth / PE detection ── */
    if (check_PE_presense())
    {
        app_uart_iso_print("\r\n==EARTH DETECTED==\n");
        earth_on();
    }
    else
    {
        app_uart_iso_print("\r\n==EARTH NOT PRESENT — CONNECT EARTH==\n");
        buzzer_fault_start();
        earth_off();
        LED_FAULT_Set();
        while (1);   /* halt until earth is connected */
    }

    /* ── AFE / EEPROM / calibration ── */
    AFE_Task();
    eeprom_init();
    app_uart_iso_print("\r\n==SYSTEM READY==\n");
    app_mode = APP_MODE_APP;

    SYSTICK_DelayMs(10);
    read_AFEcalibration_data();
    cp_chg_init();
    AFE_init();

    /* ── OLED startup splash ── */
    ssd1309_init();
    ssd1309_fill(0);
    ssd1309_draw_string(22, 15, "1 PHASE",    1, 2);
    ssd1309_draw_string(4,  35, "7.3kW EVSE", 1, 2);
    ssd1309_update();
    ssd1309_delay_ms(2000);
    /* idle screen */
    ssd1309_fill(0);
    ssd1309_draw_string(22, 15, "1 PHASE",    1, 2);
    ssd1309_draw_string(4,  35, "7.3kW EVSE", 1, 2);
    ssd1309_update();

    /* ── Main loop ─────────────────────────────────────────────────────── */
    static uint8_t earth_tick = 0;

    while (true)
    {
        SYS_Tasks();

        /* ISO UART + calibration handler */
        app_uart_iso_rx_task();
        if (calib_buff.process_buff == 1)
        {
            process_calib_cmd();
            calib_buff.process_buff = 0;
        }

        if (app_mode == APP_MODE_CALIBRATION)
        {
            AFE_process_calibration();
            continue;
        }

        /* ── Normal APP mode ────────────────────────────────────────── */
        app_uart_ble_rx_task();
        ble_task();

        /* Energy meter sampling */
        AFE_Task();

        /* CP pilot state-machine (disabled until cp_task_enabled = true) */
        cp_pilot_task();

        /* Button: toggle charger relay + OLED */
        buttons_task();

        /* Energy meter → live OLED update */
        energy_meter_task();

        /* 1-second periodic: fault check */
        if (timer_flag)
        {
            timer_flag = 0;
            fault_check_task();
        }

        /* Earth check every ~500 ms (50 × 10 ms loop delay) */
        if (++earth_tick >= 50)
        {
            earth_tick = 0;
            earth_check_task();
        }

        ssd1309_delay_ms(10);
    }

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
   print_afe_data — debug helper (call manually or via ISO UART command)
   ═══════════════════════════════════════════════════════════════════════════ */
void print_afe_data(void)
{
    if (afe_read_register_ready != 1) return;
    afe_read_register_ready = 0;

    app_uart_iso_data.txbufflen = sprintf((char *)app_uart_iso_data.tx_buff, "$===========\n");
    app_uart_iso_send_data_wait();

    afe_print_read_registers_buff();

    app_uart_iso_data.txbufflen = sprintf((char *)app_uart_iso_data.tx_buff,
        "$VRMS=%3.1f#\n",    meter_data.urms.float_var);
    app_uart_iso_send_data_wait();

    app_uart_iso_data.txbufflen = sprintf((char *)app_uart_iso_data.tx_buff,
        "$IRMS=%2.2f#\n",    meter_data.irms.float_var);
    app_uart_iso_send_data_wait();

    uint16_t te = meter_data.pmean.word1;
    app_uart_iso_data.txbufflen = sprintf((char *)app_uart_iso_data.tx_buff,
        "$ACTIVE PWR=%05dW#\n", te);
    app_uart_iso_send_data_wait();

    te = meter_data.smean.word1;
    app_uart_iso_data.txbufflen = sprintf((char *)app_uart_iso_data.tx_buff,
        "$APPARENT PWR=%05dVA#\n", te);
    app_uart_iso_send_data_wait();

    app_uart_iso_data.txbufflen = sprintf((char *)app_uart_iso_data.tx_buff,
        "$FREQ=%2.2f#\n",    meter_data.freq.float_var);
    app_uart_iso_send_data_wait();

    app_uart_iso_data.txbufflen = sprintf((char *)app_uart_iso_data.tx_buff,
        "$PF=%1.3f#\n",      meter_data.pf.float_var);
    app_uart_iso_send_data_wait();

    app_uart_iso_data.txbufflen = sprintf((char *)app_uart_iso_data.tx_buff,
        "$AT ENERGY=%f#\n",  meter_at_energy);
    app_uart_iso_send_data_wait();

    app_uart_iso_data.txbufflen = sprintf((char *)app_uart_iso_data.tx_buff,
        "$RT ENERGY=%f#\n",  meter_rt_energy);
    app_uart_iso_send_data_wait();

    app_uart_iso_data.txbufflen = sprintf((char *)app_uart_iso_data.tx_buff, "$===========\n");
    app_uart_iso_send_data_wait();
}

/*******************************************************************************
 End of File
*******************************************************************************/
