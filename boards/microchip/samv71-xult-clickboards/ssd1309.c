/*© [2025] Microchip Technology Inc. and its subsidiaries */

#include "definitions.h"
#include "system/time/sys_time.h"
#include "ssd1309.h"
#include "stdio.h"
#include "ssd1309_font.h"
#include <math.h>
#include "string.h"

/* ====== Helpers: CS/DC/RST ============================================= */
static inline void CS_LOW(void)   { GFX_DISP_INTF_PIN_CS_Clear(); }
static inline void CS_HIGH(void)  { GFX_DISP_INTF_PIN_CS_Set(); }
static inline void DC_CMD(void)   { GFX_DISP_INTF_PIN_RSDC_Clear(); }
static inline void DC_DATA(void)  { GFX_DISP_INTF_PIN_RSDC_Set(); }
static inline void RST_LOW(void)  { GFX_DISP_INTF_PIN_RESET_Clear(); }
static inline void RST_HIGH(void) { GFX_DISP_INTF_PIN_RESET_Set(); }

/* ====== Tiny delay ====================================================== */
void ssd1309_delay_ms(uint32_t ms)
{
    SYS_TIME_HANDLE t = SYS_TIME_HANDLE_INVALID;
    if (SYS_TIME_DelayMS(ms, &t) != SYS_TIME_SUCCESS) return;
    while (!SYS_TIME_DelayIsComplete(t)) { }
}

/* ====== Framebuffer (1 KiB) ============================================ */
uint8_t ssd1309_fb[SSD1309_FB_SIZE];

/* ====== Low-level write ================================================= */
static inline void ssd1309_write_cmd(uint8_t c)
{
    DC_CMD(); CS_LOW();
    (void)SSD1309_SPI_Write(&c, 1);
    while (SSD1309_SPI_IsBusy()) { }
    CS_HIGH();
}

static inline void ssd1309_write_data(const uint8_t* d, size_t n)
{
    if (n == 0u) return;
    DC_DATA(); CS_LOW();
    (void)SSD1309_SPI_Write((void*)d, n);
    while (SSD1309_SPI_IsBusy()) { }
    CS_HIGH();
}

/* ====== Addressing window (page addressing) ============================ */
static inline void ssd1309_set_col(uint8_t x)
{
    ssd1309_write_cmd(0x00 | (x & 0x0F));
    ssd1309_write_cmd(0x10 | (x >> 4));
}

static inline void ssd1309_set_page(uint8_t page)
{
    ssd1309_write_cmd(0xB0 | (page & 0x07));
}

/* ====== Initialization seq ============================================= */
static void ssd1309_reset(void)
{
    RST_LOW();  ssd1309_delay_ms(10);
    RST_HIGH(); ssd1309_delay_ms(30);
}

static void ssd1309_init_regs(void)
{
    ssd1309_write_cmd(0xAE);
    ssd1309_write_cmd(0x20);
    ssd1309_write_cmd(0x02);
    ssd1309_write_cmd(0xA8); ssd1309_write_cmd(0x3F);
    ssd1309_write_cmd(0xD3); ssd1309_write_cmd(0x00);
    ssd1309_write_cmd(0x40);
    ssd1309_write_cmd(0xA1);
    ssd1309_write_cmd(0xC8);
    ssd1309_write_cmd(0xDA); ssd1309_write_cmd(0x12);
    ssd1309_write_cmd(0x81); ssd1309_write_cmd(0x7F);
    ssd1309_write_cmd(0xD9); ssd1309_write_cmd(0xF1);
    ssd1309_write_cmd(0xDB); ssd1309_write_cmd(0x34);
    ssd1309_write_cmd(0xA4);
    ssd1309_write_cmd(0xA6);
    ssd1309_write_cmd(0xD5); ssd1309_write_cmd(0xF0);
    ssd1309_write_cmd(0xAF);
}

/* ====== Public API ====================================================== */
void ssd1309_init(void)
{
    CS_HIGH(); DC_DATA(); RST_HIGH();
    ssd1309_reset();
    ssd1309_init_regs();
    ssd1309_fill(0);
    ssd1309_update();
}

void ssd1309_power_on(bool on)
{
    ssd1309_write_cmd(on ? 0xAF : 0xAE);
}

void ssd1309_set_contrast(uint8_t val)
{
    ssd1309_write_cmd(0x81);
    ssd1309_write_cmd(val);
}

void ssd1309_invert(bool invert)
{
    ssd1309_write_cmd(invert ? 0xA7 : 0xA6);
}

void ssd1309_set_start_line(uint8_t line)
{
    ssd1309_write_cmd(0x40 | (line & 0x3F));
}

void ssd1309_fill(uint8_t color)
{
    uint8_t fill = color ? 0xFF : 0x00;
    for (size_t i = 0; i < SSD1309_FB_SIZE; i++)
        ssd1309_fb[i] = fill;
}

void ssd1309_draw_pixel(uint8_t x, uint8_t y, uint8_t color)
{
    if (x >= SSD1309_WIDTH || y >= SSD1309_HEIGHT) return;
    size_t index = (size_t)(x + (y / 8u) * SSD1309_WIDTH);
    uint8_t mask = (uint8_t)(1u << (y & 7u));
    if (color) ssd1309_fb[index] |= mask;
    else       ssd1309_fb[index] &= (uint8_t)~mask;
}

void ssd1309_update(void)
{
    for (uint8_t page = 0; page < 8u; page++)
    {
        ssd1309_set_page(page);
        ssd1309_set_col(0);
        const uint8_t* row = &ssd1309_fb[(size_t)page * SSD1309_WIDTH];
        ssd1309_write_data(row, SSD1309_WIDTH);
    }
}

void ssd1309_update_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    if (x >= SSD1309_WIDTH || y >= SSD1309_HEIGHT) return;
    if (x + w > SSD1309_WIDTH)  w = SSD1309_WIDTH - x;
    if (y + h > SSD1309_HEIGHT) h = SSD1309_HEIGHT - y;

    uint8_t pageStart = (uint8_t)(y / 8u);
    uint8_t pageEnd   = (uint8_t)((y + h - 1u) / 8u);

    for (uint8_t page = pageStart; page <= pageEnd; page++)
    {
        ssd1309_set_page(page);
        ssd1309_set_col(x);
        const uint8_t* src = &ssd1309_fb[(size_t)page * SSD1309_WIDTH + x];
        ssd1309_write_data(src, w);
    }
}

/* =========================================================================
 * Live meter_data — layout must match afe_meas_results in app_atm90e26.h
 * ========================================================================= */
extern struct {
    union { float float_var; int32_t int1; } urms, irms, pmean, qmean,
                                              smean, freq, pf,
                                              at_energy, rt_energy;
    union { uint16_t word1; } enstatus;
} meter_data;

extern float meter_at_energy;

/* =========================================================================
 * Internal helper
 * ========================================================================= */
static void fmt(char* out, size_t n, float v, unsigned digits)
{
    char f[8];
    snprintf(f, sizeof f, "%%.%uf", digits);
    snprintf(out, n, f, v);
}

/* =========================================================================
 * LAYOUT 1 — oled_show_meter  (V / I / P / PF / Hz / Energy)
 * ========================================================================= */
void oled_show_meter(void)
{
    char t[24];

    float V   = meter_data.urms.float_var;
    float I   = meter_data.irms.float_var;
    float P   = meter_data.pmean.float_var;
    float PF  = meter_data.pf.float_var;
    float Hz  = meter_data.freq.float_var;
    float kWh = meter_at_energy;

    ssd1309_fill(0);

    ssd1309_draw_string(2,   0, "ATM90E26", 1, 2);

    ssd1309_draw_string(2,  12, "V:", 1, 1);
    fmt(t, sizeof t, V, 1);  ssd1309_draw_string(18, 12, t, 1, 1);

    ssd1309_draw_string(70, 12, "I:", 1, 1);
    fmt(t, sizeof t, I, 3);  ssd1309_draw_string(86, 12, t, 1, 1);

    ssd1309_draw_string(2,  24, "P:", 1, 1);
    fmt(t, sizeof t, P, 0);  ssd1309_draw_string(18, 24, t, 1, 1);
    ssd1309_draw_string(54, 24, "W",  1, 1);

    ssd1309_draw_string(70, 24, "PF:", 1, 1);
    fmt(t, sizeof t, PF, 2); ssd1309_draw_string(94, 24, t, 1, 1);

    ssd1309_draw_string(2,  36, "f:", 1, 1);
    fmt(t, sizeof t, Hz, 2); ssd1309_draw_string(18, 36, t, 1, 1);
    ssd1309_draw_string(54, 36, "Hz", 1, 1);

    ssd1309_draw_string(2,  48, "E:", 1, 1);
    fmt(t, sizeof t, kWh, 3); ssd1309_draw_string(18, 48, t, 1, 1);
    ssd1309_draw_string(66, 48, "kWh", 1, 1);

    ssd1309_update();
}

/* =========================================================================
 * LAYOUT 2 — oled_show_dashboard
 *
 *  128 x 64 px layout (size-1 font = 6 x 8 px per char)
 *
 *  y= 0..10  ▓ filled header bar  "ENERGY METER"
 *  y=11      ─ separator line
 *  y=13      Volt  230.5 V     Freq  50.0 Hz
 *  y=23      Curr   15.2 A     PF     0.97
 *  y=33      Pwr   3500  W
 *  y=43      Enrg   12.34 kWh
 *  y=54..63  ████░░░░░░  live power bar  (% of 7.3 kW max)
 *
 *  All values are read live from ATM90E26 via meter_data / meter_at_energy.
 * ========================================================================= */
void oled_show_dashboard(void)
{
    char t[20];

    /* ── Pull live values ────────────────────────────────────────────────── */
    float V   = meter_data.urms.float_var;    /* Vrms          */
    float I   = meter_data.irms.float_var;    /* Irms          */
    float P   = meter_data.pmean.float_var;   /* Active power W */
    float Hz  = meter_data.freq.float_var;    /* Frequency Hz  */
    float PF  = meter_data.pf.float_var;      /* Power factor  */
    float kWh = meter_at_energy;              /* Accumulated energy */

    /* Guard against NaN / garbage on startup */
    if (V   < 0.0f || V   > 300.0f) V   = 0.0f;
    if (I   < 0.0f || I   > 64.0f)  I   = 0.0f;
    if (P   < 0.0f || P   > 9999.0f)P   = 0.0f;
    if (Hz  < 40.0f|| Hz  > 70.0f)  Hz  = 0.0f;
    if (PF  < 0.0f || PF  > 1.0f)   PF  = 0.0f;
    if (kWh < 0.0f)                  kWh = 0.0f;

    ssd1309_fill(0);

    /* ── Header: filled bar ─────────────────────────────────────────────── */
    ssd1309_fill_rect(0, 0, 128, 11, 1);
    ssd1309_draw_string(20, 2, "ENERGY METER", 0, 1);   /* color 0 = black on white */

    /* ── Separator ──────────────────────────────────────────────────────── */
    ssd1309_draw_hline(0, 12, 128, 1);

    /* ── Column layout constants ─────────────────────────────────────────
     *  Left column:   label at x=0, value right-justified ending at x=76, unit at x=78
     *  Right column:  label at x=84, value right-justified ending at x=118, unit at x=120
     *  Each value field is max 5 chars × 6 px = 30 px wide → ends at given X
     * ─────────────────────────────────────────────────────────────────── */
#define LBL_L   0       /* left label x  */
#define VAL_L  54       /* left value right-edge x (right-justify into here) */
#define UNT_L  78       /* left unit x   */
#define LBL_R  88       /* right label x */
#define VAL_R 118       /* right value right-edge x */
#define UNT_R 122       /* right unit x  */

    /* Row Y positions */
#define ROW1  14
#define ROW2  24
#define ROW3  34
#define ROW4  44

    /* ── Helper: right-justify a string so it ends at edge_x ──────────── */
    /* We inline this since we can't nest functions in C89/C99 portably.   */

    /* ROW 1 — Voltage (left)  |  Frequency (right) */
    fmt(t, sizeof t, V, 1);
    ssd1309_draw_string(LBL_L, ROW1, "Volt", 1, 1);
    ssd1309_draw_string((uint8_t)(VAL_L - (int)strlen(t) * 6), ROW1, t, 1, 1);
    ssd1309_draw_string(UNT_L, ROW1, "V", 1, 1);

    fmt(t, sizeof t, Hz, 1);
    ssd1309_draw_string(LBL_R, ROW1, "Hz", 1, 1);
    ssd1309_draw_string((uint8_t)(VAL_R - (int)strlen(t) * 6), ROW1, t, 1, 1);

    /* ROW 2 — Current (left)  |  Power Factor (right) */
    fmt(t, sizeof t, I, 2);
    ssd1309_draw_string(LBL_L, ROW2, "Curr", 1, 1);
    ssd1309_draw_string((uint8_t)(VAL_L - (int)strlen(t) * 6), ROW2, t, 1, 1);
    ssd1309_draw_string(UNT_L, ROW2, "A", 1, 1);

    fmt(t, sizeof t, PF, 2);
    ssd1309_draw_string(LBL_R, ROW2, "PF", 1, 1);
    ssd1309_draw_string((uint8_t)(VAL_R - (int)strlen(t) * 6), ROW2, t, 1, 1);

    /* ROW 3 — Active Power (full width) */
    fmt(t, sizeof t, P, 0);
    ssd1309_draw_string(LBL_L, ROW3, "Pwr", 1, 1);
    ssd1309_draw_string((uint8_t)(VAL_L - (int)strlen(t) * 6), ROW3, t, 1, 1);
    ssd1309_draw_string(UNT_L, ROW3, "W", 1, 1);

    /* ROW 4 — Energy (full width) */
    fmt(t, sizeof t, kWh, 2);
    ssd1309_draw_string(LBL_L, ROW4, "Enrg", 1, 1);
    ssd1309_draw_string((uint8_t)(VAL_L - (int)strlen(t) * 6), ROW4, t, 1, 1);
    ssd1309_draw_string(UNT_L, ROW4, "kWh", 1, 1);

    /* ── Power bar (bottom 8 px) ─────────────────────────────────────────
     *  Shows P as % of 7300 W (7.3 kW = full EVSE capacity).
     *  Outline at y=55, fill at y=57..63.
     * ─────────────────────────────────────────────────────────────────── */
    ssd1309_draw_hline(0, 54, 128, 1);           /* top border   */
    ssd1309_draw_hline(0, 63, 128, 1);           /* bottom border */
    ssd1309_draw_vline(0,   54, 10, 1);          /* left border  */
    ssd1309_draw_vline(127, 54, 10, 1);          /* right border */

    int bar_fill = (int)((P / 7300.0f) * 124.0f);
    if (bar_fill > 124) bar_fill = 124;
    if (bar_fill > 0)
        ssd1309_fill_rect(2, 56, (uint8_t)bar_fill, 6, 1);

    /* Percentage label inside bar (shows P in kW) */
    float kW = P / 1000.0f;
    fmt(t, sizeof t, kW, 2);
    /* append "kW" */
    size_t tlen = strlen(t);
    if (tlen < sizeof(t) - 3) { t[tlen] = 'k'; t[tlen+1] = 'W'; t[tlen+2] = '\0'; }
    /* center text in bar area (128 px wide) */
    uint8_t tx = (uint8_t)((128u - (uint8_t)(strlen(t) * 6u)) / 2u);
    ssd1309_draw_string(tx, 56, t, (bar_fill > 64) ? 0 : 1, 1);

#undef LBL_L
#undef VAL_L
#undef UNT_L
#undef LBL_R
#undef VAL_R
#undef UNT_R
#undef ROW1
#undef ROW2
#undef ROW3
#undef ROW4

    ssd1309_update();
}

/* =========================================================================
 * LAYOUT 3 — oled_show_minimalist  (big V, I and P side by side)
 * ========================================================================= */
static uint8_t anim_frame = 0;

void oled_show_minimalist(void)
{
    char t[24];
    float V = meter_data.urms.float_var;
    float I = meter_data.irms.float_var;
    float P = meter_data.pmean.float_var;

    ssd1309_fill(0);

    fmt(t, sizeof t, V, 1);
    ssd1309_draw_string(30,  8, t,   1, 2);
    ssd1309_draw_string(95, 12, "V", 1, 2);

    ssd1309_draw_hline(10, 30, 108, 1);

    fmt(t, sizeof t, I, 2);
    ssd1309_draw_string(5,  38, t,   1, 1);
    ssd1309_draw_string(38, 38, "A", 1, 1);

    ssd1309_draw_vline(64, 34, 26, 1);

    fmt(t, sizeof t, P, 0);
    ssd1309_draw_string(70,  38, t,   1, 1);
    ssd1309_draw_string(108, 38, "W", 1, 1);

    if (P > 10.0f)
    {
        uint8_t dot = (anim_frame / 8u) % 3u;
        for (uint8_t i = 0; i < 3u; i++)
            if (i == dot)
                ssd1309_fill_rect((uint8_t)(56u + i * 4u), 54u, 2u, 2u, 1);
        anim_frame++;
    }

    ssd1309_update();
}

/* =========================================================================
 * LAYOUT 4 — oled_show_grid  (2x2: V / I / P / Hz)
 * ========================================================================= */
void oled_show_grid(void)
{
    char t[24];
    float V  = meter_data.urms.float_var;
    float I  = meter_data.irms.float_var;
    float P  = meter_data.pmean.float_var;
    float Hz = meter_data.freq.float_var;

    ssd1309_fill(0);

    ssd1309_fill_rect(0, 0, 128, 10, 1);
    ssd1309_draw_string(35, 2, "METER", 0, 1);

    ssd1309_draw_vline(64, 12, 52, 1);
    ssd1309_draw_hline(0,  37, 128, 1);

    fmt(t, sizeof t, V, 1);
    ssd1309_draw_string(8, 16, "V", 1, 1);
    ssd1309_draw_string(8, 26, t,   1, 1);

    fmt(t, sizeof t, I, 2);
    ssd1309_draw_string(72, 16, "I", 1, 1);
    ssd1309_draw_string(72, 26, t,   1, 1);

    fmt(t, sizeof t, P, 0);
    ssd1309_draw_string(8, 42, "P", 1, 1);
    ssd1309_draw_string(8, 52, t,   1, 1);

    fmt(t, sizeof t, Hz, 1);
    ssd1309_draw_string(72, 42, "Hz", 1, 1);
    ssd1309_draw_string(72, 52, t,   1, 1);

    ssd1309_update();
}

/* =========================================================================
 * LAYOUT 5 — oled_show_led_style  (retro LED boxes)
 * ========================================================================= */
void oled_show_led_style(void)
{
    char t[24];
    float V = meter_data.urms.float_var;
    float I = meter_data.irms.float_var;
    float P = meter_data.pmean.float_var;

    ssd1309_fill(0);

    ssd1309_draw_rect(0, 0, 128, 64, 1);
    ssd1309_draw_rect(2, 2, 124, 60, 1);

    ssd1309_draw_rect(6,  8, 116, 14, 1);
    ssd1309_draw_string(10, 12, "V:", 1, 1);
    fmt(t, sizeof t, V, 1);
    ssd1309_draw_string(30, 12, t, 1, 1);

    ssd1309_draw_rect(6, 25, 116, 14, 1);
    ssd1309_draw_string(10, 29, "I:", 1, 1);
    fmt(t, sizeof t, I, 3);
    ssd1309_draw_string(30, 29, t, 1, 1);

    ssd1309_fill_rect(6, 42, 116, 16, 1);
    ssd1309_draw_string(10, 46, "P:", 0, 1);
    fmt(t, sizeof t, P, 0);
    ssd1309_draw_string(30, 46, t,       0, 1);
    ssd1309_draw_string(85, 46, "WATTS", 0, 1);

    ssd1309_update();
}

/* =========================================================================
 * LAYOUT 6 — oled_show_analog_meter  (needle gauge)
 * ========================================================================= */
void oled_show_analog_meter(void)
{
    char t[24];
    float V = meter_data.urms.float_var;
    float P = meter_data.pmean.float_var;

    ssd1309_fill(0);
    ssd1309_draw_string(30, 0, "VOLTAGE", 1, 1);

    uint8_t cx = 64u, cy = 35u, radius = 20u;

    for (int8_t angle = -60; angle <= 60; angle += 20)
    {
        float rad = (float)angle * 3.14159f / 180.0f;
        uint8_t x1 = (uint8_t)(cx + (int)(radius       * sinf(rad)));
        uint8_t y1 = (uint8_t)(cy - (int)(radius       * cosf(rad)));
        uint8_t x2 = (uint8_t)(cx + (int)((radius - 3) * sinf(rad)));
        uint8_t y2 = (uint8_t)(cy - (int)((radius - 3) * cosf(rad)));
        ssd1309_draw_pixel(x1, y1, 1);
        ssd1309_draw_pixel(x2, y2, 1);
    }

    float v_pct = V / 300.0f;
    if (v_pct > 1.0f) v_pct = 1.0f;
    float rad = (-60.0f + v_pct * 120.0f) * 3.14159f / 180.0f;
    uint8_t nx = (uint8_t)(cx + (int)(15 * sinf(rad)));
    uint8_t ny = (uint8_t)(cy - (int)(15 * cosf(rad)));
    ssd1309_draw_pixel(cx, cy,     1);
    ssd1309_draw_pixel(cx, cy - 1, 1);
    ssd1309_draw_pixel(nx, ny,     1);
    ssd1309_draw_pixel(nx + 1, ny, 1);

    fmt(t, sizeof t, V, 1);
    ssd1309_draw_string(45, 28, t,   1, 1);
    ssd1309_draw_string(80, 28, "V", 1, 1);

    ssd1309_draw_hline(0, 46, 128, 1);
    ssd1309_draw_string(2,  50, "Power:", 1, 1);
    fmt(t, sizeof t, P, 0);
    ssd1309_draw_string(42, 50, t,   1, 1);
    ssd1309_draw_string(85, 50, "W",  1, 1);

    ssd1309_update();
}

/* =========================================================================
 * LAYOUT 7 — oled_show_cyberpunk  (animated corner brackets)
 * ========================================================================= */
void oled_show_cyberpunk(void)
{
    char t[24];
    float V   = meter_data.urms.float_var;
    float I   = meter_data.irms.float_var;
    float P   = meter_data.pmean.float_var;
    float kWh = meter_at_energy;

    ssd1309_fill(0);

    uint8_t ba = (anim_frame / 4u) % 5u;
    ssd1309_draw_hline(0,                    0,  (uint8_t)(10u + ba), 1);
    ssd1309_draw_vline(0,                    0,  (uint8_t)(10u + ba), 1);
    ssd1309_draw_hline((uint8_t)(118u - ba), 0,  (uint8_t)(10u + ba), 1);
    ssd1309_draw_vline(127,                  0,  (uint8_t)(10u + ba), 1);
    ssd1309_draw_hline(0,                    63, (uint8_t)(10u + ba), 1);
    ssd1309_draw_vline(0,   (uint8_t)(54u - ba), 10u, 1);
    ssd1309_draw_hline((uint8_t)(118u - ba), 63, (uint8_t)(10u + ba), 1);
    ssd1309_draw_vline(127, (uint8_t)(54u - ba), 10u, 1);
    anim_frame++;

    fmt(t, sizeof t, V, 0);
    ssd1309_draw_string(25,  8, t,   1, 2);
    ssd1309_draw_string(85, 12, "V", 1, 1);

    ssd1309_draw_hline(15, 26, 98, 1);
    ssd1309_draw_hline(15, 27, 95, 1);

    fmt(t, sizeof t, I, 2);
    ssd1309_draw_string(15, 32, t,   1, 1);
    ssd1309_draw_string(50, 32, "A", 1, 1);

    fmt(t, sizeof t, P, 0);
    ssd1309_draw_string(70,  32, t,   1, 1);
    ssd1309_draw_string(110, 32, "W", 1, 1);

    ssd1309_draw_rect(15, 48, 98, 10, 1);
    fmt(t, sizeof t, kWh, 2);
    ssd1309_draw_string(35, 50, t,     1, 1);
    ssd1309_draw_string(75, 50, "kWh", 1, 1);

    ssd1309_update();
}

/* =========================================================================
 * LAYOUT 8 — oled_show_graph  (scrolling power history)
 * ========================================================================= */
#define GRAPH_WIDTH  100
#define GRAPH_HEIGHT  30

static float   power_history[GRAPH_WIDTH];
static uint8_t graph_index = 0;

void oled_show_graph(void)
{
    char t[24];
    float V = meter_data.urms.float_var;
    float I = meter_data.irms.float_var;
    float P = meter_data.pmean.float_var;

    ssd1309_fill(0);

    power_history[graph_index] = P;
    graph_index = (uint8_t)((graph_index + 1u) % GRAPH_WIDTH);

    ssd1309_draw_string(2,  2, "V:", 1, 1);
    fmt(t, sizeof t, V, 0); ssd1309_draw_string(15, 2, t, 1, 1);
    ssd1309_draw_string(50, 2, "I:", 1, 1);
    fmt(t, sizeof t, I, 2); ssd1309_draw_string(63, 2, t, 1, 1);

    ssd1309_draw_rect(2, 14, (uint8_t)(GRAPH_WIDTH + 2), (uint8_t)(GRAPH_HEIGHT + 2), 1);

    float max_p = 100.0f;
    for (uint8_t i = 0; i < GRAPH_WIDTH; i++)
        if (power_history[i] > max_p) max_p = power_history[i];

    for (uint8_t i = 0; i < GRAPH_WIDTH - 1u; i++)
    {
        uint8_t idx = (uint8_t)((graph_index + i) % GRAPH_WIDTH);
        uint8_t h1  = (uint8_t)((power_history[idx] / max_p) * GRAPH_HEIGHT);
        if (h1 > GRAPH_HEIGHT) h1 = (uint8_t)GRAPH_HEIGHT;
        for (uint8_t y = 0; y < h1; y++)
            ssd1309_draw_pixel((uint8_t)(4u + i),
                               (uint8_t)(15u + GRAPH_HEIGHT - y), 1);
    }

    ssd1309_draw_string(2,  50, "P:", 1, 1);
    fmt(t, sizeof t, P, 0); ssd1309_draw_string(15, 50, t, 1, 1);
    ssd1309_draw_string(60, 50, "W", 1, 1);

    ssd1309_update();
}

/* =========================================================================
 * LAYOUT 9 — oled_show_status  (LED indicators + readings)
 * ========================================================================= */
void oled_show_status(void)
{
    char t[24];
    float V  = meter_data.urms.float_var;
    float I  = meter_data.irms.float_var;
    float P  = meter_data.pmean.float_var;
    float PF = meter_data.pf.float_var;

    ssd1309_fill(0);

    if (P > 5.0f) ssd1309_fill_rect(2, 2, 4, 4, 1);
    else          ssd1309_draw_rect(2, 2, 4, 4, 1);
    ssd1309_draw_string(10, 2, "PWR", 1, 1);

    if (V > 200.0f && V < 250.0f) ssd1309_fill_rect(50, 2, 4, 4, 1);
    else                           ssd1309_draw_rect(50, 2, 4, 4, 1);
    ssd1309_draw_string(58, 2, "GRD", 1, 1);

    if (PF > 0.9f) ssd1309_fill_rect(98, 2, 4, 4, 1);
    else           ssd1309_draw_rect(98, 2, 4, 4, 1);
    ssd1309_draw_string(106, 2, "PF", 1, 1);

    ssd1309_draw_hline(0, 12, 128, 1);

    ssd1309_draw_string(4, 18, "Voltage", 1, 1);
    fmt(t, sizeof t, V, 1);
    ssd1309_draw_string(55, 18, t,    1, 1);
    ssd1309_draw_string(105, 18, "V", 1, 1);

    ssd1309_draw_string(4, 30, "Current", 1, 1);
    fmt(t, sizeof t, I, 3);
    ssd1309_draw_string(55, 30, t,    1, 1);
    ssd1309_draw_string(105, 30, "A", 1, 1);

    ssd1309_draw_string(4, 42, "Power", 1, 1);
    fmt(t, sizeof t, P, 0);
    ssd1309_draw_string(55, 42, t,    1, 1);
    ssd1309_draw_string(105, 42, "W", 1, 1);

    ssd1309_draw_string(4, 54, "PF", 1, 1);
    fmt(t, sizeof t, PF, 2);
    ssd1309_draw_string(55, 54, t, 1, 1);

    ssd1309_update();
}
