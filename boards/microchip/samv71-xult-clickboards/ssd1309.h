#ifndef SSD1309_H
#define SSD1309_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ======= PORTING POINTS (EDIT THESE 5 DEFINES) ===========================
 * Map them to your real pins in MCC:
 *   - CS   : Chip Select (GPIO, NOT hardware SS)
 *   - DC   : Data/Command select
 *   - RST  : OLED Reset
 *   - SPI  : We use SERCOM2 SPI PLIB calls
 * ========================================================================*/

/* 1) GPIO pins (change to your board's pins from MCC "Pin Names") */
#ifndef SSD1309_CS_PIN
#  define SSD1309_CS_PIN     GFX_DISP_INTF_PIN_CS
#endif
#ifndef SSD1309_DC_PIN
#  define SSD1309_DC_PIN     GFX_DISP_INTF_PIN_RSDC
#endif
#ifndef SSD1309_RST_PIN
#  define SSD1309_RST_PIN    GFX_DISP_INTF_PIN_RESET
#endif

/* 2) SPI PLIB macro wrappers (SERCOM2 SPI) */
#define SSD1309_SPI_Write(pData, size)     SERCOM2_SPI_Write(pData, size)
#define SSD1309_SPI_IsBusy()               SERCOM2_SPI_IsBusy()

/* 3) Delay (we use SYS_TIME if present). */
void ssd1309_delay_ms(uint32_t ms);

/* 4) Public constants */
#define SSD1309_WIDTH   128u
#define SSD1309_HEIGHT   64u
#define SSD1309_FB_SIZE (SSD1309_WIDTH * SSD1309_HEIGHT / 8u)  /* = 1024B */

/* ======= Public API ===================================================== */
void    ssd1309_init(void);
void    ssd1309_power_on(bool on);
void    ssd1309_set_contrast(uint8_t val);
void    ssd1309_invert(bool invert);
void    ssd1309_set_start_line(uint8_t line); /* 0..63 */

void    ssd1309_fill(uint8_t color);          /* 0=black, nonzero=white */
void    ssd1309_draw_pixel(uint8_t x, uint8_t y, uint8_t color);
void    ssd1309_update(void);
void    ssd1309_update_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h);

/* Layout screens */
void    oled_show_meter(void);
void    oled_show_dashboard(void);
void    oled_show_minimalist(void);
void    oled_show_grid(void);
void    oled_show_led_style(void);
void    oled_show_analog_meter(void);
void    oled_show_cyberpunk(void);
void    oled_show_graph(void);
void    oled_show_status(void);

/* Expose the framebuffer if you want to render directly */
extern uint8_t ssd1309_fb[SSD1309_FB_SIZE];

#endif /* SSD1309_H */
