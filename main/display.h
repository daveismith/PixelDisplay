#ifndef DISPLAY_H
#define DISPLAY_H

#include "gfxfont.h"

typedef enum {
   DISPLAY_MODE_ANIMATION,
   DISPLAY_MODE_COLOUR,
   DISPLAY_MODE_FILE,
   DISPLAY_MODE_MANUAL,
   DISPLAY_MODE_END	/* Needs To Be The Last One */
} display_mode_e;

#define BRIGHTNESS_MIN 0
#define BRIGHTNESS_MAX 2100

#define DIM_RATE_MIN 1
#define DIM_RATE_MAX 100
#define DEFAULT_DIM_RATE 30

#define RATE_MIN 33
#define DEFAULT_RATE 66

void display_task(void *pvParameter);

size_t display_width();
size_t display_height();

void display_setBrightness(int16_t brightness, int16_t dimRate);
uint16_t display_getBrightness();

void display_setPower(bool power);
bool display_getPower();

void display_setMode(display_mode_e mode);
display_mode_e display_getMode();

void display_setRate(uint32_t rate);

void display_setColour(uint8_t r, uint8_t g, uint8_t b);

void display_setAnimation(size_t animation);
size_t display_getAnimation();

void display_setFile(const char *file);

// Manual Mode Commands
void display_update();

void display_drawLine(size_t x0, size_t y0, size_t x1, size_t y1, uint8_t r, uint8_t g, uint8_t b);
void display_fillScreen(uint8_t r, uint8_t g, uint8_t b);

void display_drawRect(size_t x, size_t y, size_t w, size_t h, uint8_t r, uint8_t g, uint8_t b);
void display_fillRect(size_t x, size_t y, size_t w, size_t h, uint8_t r, uint8_t g, uint8_t b);

void display_drawCircle(size_t x0, size_t y0, size_t radius, uint8_t r, uint8_t g, uint8_t b);
void display_fillCircle(size_t x0, size_t y0, size_t radius, uint8_t r, uint8_t g, uint8_t b);

void display_setPixel(size_t x, size_t y, uint8_t r, uint8_t g, uint8_t b);

void display_setFont(GFXfont *font);

void display_print(char *text);
void display_getTextBounds(char *text, size_t x, size_t y, size_t *x1, size_t *y1, size_t *w, size_t *h);

#endif //DISPLAY_H
