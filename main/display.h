#ifndef DISPLAY_H
#define DISPLAY_H

typedef enum {
   DISPLAY_MODE_ANIMATION,
   DISPLAY_MODE_COLOUR,
   DISPLAY_MODE_FILE
} display_mode_e;

#define BRIGHTNESS_MIN 0
#define BRIGHTNESS_MAX 2100

#define RATE_MIN 1
#define RATE_MAX 100
#define DEFAULT_RATE 30

void display_task(void *pvParameter);

void display_setBrightness(int16_t brightness, int16_t rate);
uint16_t display_getBrightness();

void display_setPower(bool power);
bool display_getPower();

void display_setMode(display_mode_e mode);
display_mode_e display_getMode();

void display_setColour(uint8_t r, uint8_t g, uint8_t b);

void display_setAnimation(size_t animation);
size_t display_getAnimation();

void display_setFile(const char *file);

#endif //DISPLAY_H
