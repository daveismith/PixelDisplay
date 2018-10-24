/****************************************************************
 * This is a library for the Chinese LED matrix displays
 *
 * Written by David Smith, based on work by Dominic Buchstaller
 * BSD License
 ***************************************************************/

#ifndef PXMATRIX_H__
#define PXMATRIX_H__
#include <inttypes.h>
#include "driver/spi_master.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

// Either the panel handles the multiplexing and we feed BINARY to A-E pins
// or we handle the multiplexing and activate one of A-D pins (STRAIGHT)
enum mux_patterns { BINARY, STRAIGHT };

// This is how the scanning is implemented. LINE just scans it left to right,
// ZIGZAG jumps 4 rows after every byte, ZAGGII alse revereses every second byte
enum scan_patterns {LINE, ZIGZAG, ZAGGIZ};

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
struct pxmatrix {};

class PxMatrix : public pxmatrix {

public:
   PxMatrix(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B);
   PxMatrix(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B, uint8_t C);
   PxMatrix(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B, uint8_t C, uint8_t D);
   PxMatrix(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B, uint8_t C, uint8_t D, uint8_t E);

   void begin(uint8_t steps);
   void begin();

   void clearDisplay(void);

   void display(uint16_t show_time);

   void drawPixelRGB565(int16_t x, int16_t y, uint16_t color);
   void drawPixelRGB565(int16_t x, int16_t y, uint16_t color, bool selected_buffer);

   void drawPixel(int16_t x, int16_t y, uint16_t color);
   void drawPixel(int16_t x, int16_t y, uint16_t color, bool selected_buffer);

   void drawPixelRGB888(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b);
   void drawPixelRGB888(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b, bool selected_buffer);

   // Does nothing for now
   uint8_t getPixel(int8_t x, int8_t y);

   // Converts RGB888 to RGB565
   uint16_t color565(uint8_t r, uint8_t g, uint8_t b);

   // Helpful for debugging (place in display update loop)
   void displayTestPattern(uint16_t show_time);

   // Helpful for debugging (place in display update loop)
   void displayTestPixel(uint16_t show_time);

   // Flush the buffer of the display
   void flushDisplay();

   // Rotate display
   void setRotate(bool rotate);

   // Help reduce display update latency on larger displays
   void setFastUpdate(bool fast_update);

   // Select active buffer to update display from
   void selectBuffer(bool selected_buffer);

   void swapBuffer();
   
   // Control the minimum colour values that result in an active pixel
   void setColorOffset(uint8_t r, uint8_t g, uint8_t b);

   // Set the multiplex pattern
   void setMuxPattern(mux_patterns mux_pattern);

   // Set the multiplex pattern
   void setScanPattern(scan_patterns scan_pattern);

private:
   // SPI Device
   spi_device_handle_t spi;
   spi_transaction_t _transactions[2];
   EventGroupHandle_t xDisplayEventGroup;

   uint8_t *buffer[2];
   uint8_t *flushBuffer;

   // GPIO Pins
   uint8_t _LATCH_PIN;
   uint8_t _OE_PIN;
   uint8_t _A_PIN;
   uint8_t _B_PIN;
   uint8_t _C_PIN;
   uint8_t _D_PIN;
   uint8_t _E_PIN;
   uint8_t _width;
   uint8_t _height;
   
   // Colour Offsets
   uint8_t _color_R_offset;
   uint8_t _color_G_offset;
   uint8_t _color_B_offset;

   // Colour pattern that is pushed to the display
   uint8_t _display_color;
   
   // Holds the pre-computed vaues for faster pixel drawing
   uint32_t _row_offset[64];

   // Holds the display row pattern type
   uint8_t _row_pattern;

   // Number of bytes in one colour
   uint8_t _pattern_color_bytes;

   // Total number of bytes that is pushed to the display at a time
   // 3 * _pattern_color_bytes
   uint16_t _send_buffer_size;

   uint16_t _buffer_size;

   // This is for double buffering
   bool _selected_buffer;
   bool _active_buffer;

   // Hols configuration
   bool _rotate;
   bool _fast_update;

   // Holds multiplex pattern
   mux_patterns _mux_pattern;

   // Holds the scan pattern
   scan_patterns _scan_pattern;

   // Used for test pattern
   uint16_t _test_pixel_counter;
   uint16_t _test_line_counter;
   int64_t _test_last_call;

   // Generic function that draws one pixel
   void fillMatrixBuffer(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b, bool selected_buffer);

   // Init code common to both constructors
   void init(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B);

   // Light up LEDs and hold for show_time microseconds
   void latch(uint16_t show_time);

   // Set row multiplexer
   void set_mux(uint8_t value);

};

#endif //__cplusplus

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pxmatrix pxmatrix;

extern pxmatrix* Create_PxMatrix(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B);
extern pxmatrix* Create_PxMatrix3(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B, uint8_t C);
extern pxmatrix* Create_PxMatrix4(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B, uint8_t C, uint8_t D);
extern pxmatrix* Create_PxMatrix5(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B, uint8_t C, uint8_t D, uint8_t E);

extern void pxmatrix_begin(pxmatrix *matrix, uint8_t steps);
extern void pxmatrix_clearDisplay(pxmatrix *matrix);
extern void pxmatrix_display(pxmatrix *matrix, uint16_t show_time);

extern void pxmatrix_drawPixelRGB565(pxmatrix *matrix, int16_t x, int16_t y, uint16_t color);
extern void pxmatrix_drawPixel(pxmatrix *matrix, int16_t x, int16_t y, uint16_t color);
extern void pxmatrix_drawPixelRGB888(pxmatrix *matrix, int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b);

extern uint16_t pxmatrix_color565(pxmatrix *matrix, uint8_t r, uint8_t g, uint8_t b);

extern void pxmatrix_displayTestPattern(pxmatrix *matrix, uint16_t show_time);

extern void pxmatrix_displayTestPixel(pxmatrix *matrix, uint16_t show_time);

extern void pxmatrix_setFastUpdate(pxmatrix *matrix, bool fast_update);

extern void pxmatrix_selectBuffer(pxmatrix *matrix, bool selected_buffer);

extern void pxmatrix_swapBuffer(pxmatrix *matrix);

#ifdef __cplusplus
}
#endif

#endif //PXMATRIX_H__
