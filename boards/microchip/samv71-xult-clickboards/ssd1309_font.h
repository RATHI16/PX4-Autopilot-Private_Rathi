#ifndef SSD1309_FONT_H
#define SSD1309_FONT_H

#include <stdint.h>
#include "ssd1309.h"

/* ======= Text & Shape Drawing API ======================================= */

void ssd1309_draw_char(uint8_t x, uint8_t y, char c, uint8_t color, uint8_t size);
void ssd1309_draw_string(uint8_t x, uint8_t y, const char *str, uint8_t color, uint8_t size);

void ssd1309_draw_hline(uint8_t x, uint8_t y, uint8_t w, uint8_t color);
void ssd1309_draw_vline(uint8_t x, uint8_t y, uint8_t h, uint8_t color);
void ssd1309_draw_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color);
void ssd1309_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t color);

#endif /* SSD1309_FONT_H */
