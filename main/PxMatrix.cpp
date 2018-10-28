/****************************************************************
 * This is a library for the Chinese LED matrix displays
 *
 * Written by David Smith, based on work by Dominic Buchstaller
 * BSD License
 ***************************************************************/

// Handle The Defined Stuff For ESP
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "PxMatrix.h"

//#define USE_HSPI

#ifdef USE_HSPI
  #define SPI_HOST_TYPE HSPI_HOST
  #define SPI_BUS_CLK 14
  #define SPI_BUS_MOSI 13
  #define SPI_BUS_MISO 12
  #define SPI_BUS_SS 4
#else
  #define SPI_HOST_TYPE VSPI_HOST
  #define SPI_BUS_CLK 18
  #define SPI_BUS_MOSI 23
  #define SPI_BUS_MISO 19
  #define SPI_BUS_SS 21
#endif //


#ifndef _BV
#define _BV(x) (1 << (x))
#endif

#define BIT_BUFFER_SWAP_OK   ( 1 << 0 )

#define color_depth 8
#define color_step 256 / color_depth
#define color_half_step int(color_step / 2)
#define color_third_step int(color_step / 3)
#define color_two_third_step int(color_third_step*2)


uint16_t PxMatrix::color565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void PxMatrix::init(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B) 
{
   _LATCH_PIN = LATCH;
   _OE_PIN = OE;
   _display_color = 0;

   _A_PIN = A;
   _B_PIN = B;

   _width = width;
   _height = height;

   _selected_buffer = false;
   _active_buffer = false;

   _color_R_offset = 0;
   _color_G_offset = 0;
   _color_B_offset = 0;

   _test_last_call = 0;
   _test_pixel_counter = 0;
   _test_line_counter = 0;
   _rotate = 0;
   _fast_update = 0;

   _row_pattern = BINARY;
   _mux_pattern = BINARY;
   _scan_pattern = LINE;

   memset(&_transactions[0], 0, sizeof(spi_transaction_t));
   memset(&_transactions[1], 0, sizeof(spi_transaction_t));

}

void PxMatrix::setMuxPattern(mux_patterns mux_pattern)
{
   _mux_pattern = mux_pattern;

   if (STRAIGHT == _mux_pattern)
   {
      // Set pin modes for C and D to low outputs
      gpio_pad_select_gpio(_C_PIN);
      gpio_set_direction((gpio_num_t)_C_PIN, GPIO_MODE_OUTPUT);
      gpio_set_level((gpio_num_t)_C_PIN, 0);

      gpio_pad_select_gpio(_D_PIN);
      gpio_set_direction((gpio_num_t)_D_PIN, GPIO_MODE_OUTPUT);
      gpio_set_level((gpio_num_t)_D_PIN, 0);
   }
}

void PxMatrix::setScanPattern(scan_patterns scan_pattern)
{
   _scan_pattern = scan_pattern;
}

void PxMatrix::flushDisplay()
{
   spi_transaction_t *rtrans;
   esp_err_t ret;

   memset(flushBuffer, 0, _send_buffer_size);
   _transactions[0].length = _send_buffer_size;
   _transactions[0].flags = SPI_TRANS_USE_RXDATA;
   _transactions[0].rxlength = 0;
   _transactions[0].tx_buffer = flushBuffer;

   ret = spi_device_queue_trans(spi, &_transactions[0], portMAX_DELAY);
   ESP_ERROR_CHECK(ret);
   ret = spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
   ESP_ERROR_CHECK(ret);
}

void PxMatrix::setRotate(bool rotate)
{
   _rotate = rotate;
}

void PxMatrix::setFastUpdate(bool fast_update)
{
   _fast_update = fast_update;
}

PxMatrix::PxMatrix(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B)
{
   init(width, height, LATCH, OE, A, B);
}

PxMatrix::PxMatrix(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B, uint8_t C)
{
   _C_PIN = C;
   init(width, height, LATCH, OE, A, B);
}

PxMatrix::PxMatrix(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B, uint8_t C, uint8_t D)
{
   _C_PIN = C;
   _D_PIN = D;
   init(width, height, LATCH, OE, A, B);
}

PxMatrix::PxMatrix(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B, uint8_t C, uint8_t D, uint8_t E)
{
   _C_PIN = C;
   _D_PIN = D;
   _E_PIN = E;
   init(width, height, LATCH, OE, A, B);
}

void PxMatrix::clearDisplay(void)
{
}

void PxMatrix::selectBuffer(bool selected_buffer)
{
   _selected_buffer = selected_buffer;
}

void PxMatrix::swapBuffer()
{
   xEventGroupWaitBits(xDisplayEventGroup, BIT_BUFFER_SWAP_OK, pdFALSE, pdTRUE, 1000 / portTICK_PERIOD_MS);
   _selected_buffer = !_selected_buffer;
}

void PxMatrix::setColorOffset(uint8_t r, uint8_t g, uint8_t b)
{
}

void PxMatrix::fillMatrixBuffer(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b, bool selected_buffer)
{
   if (_rotate) {
      uint16_t temp_x = x;
      x = y;
      y = _height-1-temp_x;
   }

   if ((x < 0) || (x >= _width) || (y < 0) || (y >= _height))
      return;

   x = _width - 1 - x;

   uint8_t buffer_idx = selected_buffer ? 1 : 0;
   uint32_t base_offset;
   uint32_t total_offset_r = 0;
   uint32_t total_offset_g = 0;
   uint32_t total_offset_b = 0;

   if (4 == _row_pattern)
   {

   } 
   else 
   {
      // Handle The Offset
      base_offset = _row_offset[y] - (x / 8);

      // relies on integer truncation, do not simplify
      uint8_t vert_sector = y / _row_pattern;
      total_offset_r = base_offset - vert_sector * _width / 8;
      total_offset_g = total_offset_r - _pattern_color_bytes;
      total_offset_b = total_offset_g - _pattern_color_bytes;
   }

   uint8_t bit_select = x % 8;
   if ((ZAGGIZ == _scan_pattern) && ((y%8) < 4))
      bit_select = 7 - bit_select;

   // Colour Interlacing
   for (int this_color=0; this_color < color_depth; this_color++)
   {
      uint8_t color_thresh = this_color * color_step + color_half_step;

      uint32_t off_r = (this_color * _buffer_size) + total_offset_r;
      if (r > color_thresh + _color_R_offset)
         buffer[buffer_idx][off_r] |= _BV(bit_select);
      else
         buffer[buffer_idx][off_r] &= ~_BV(bit_select);

      uint32_t off_g = (((this_color + color_third_step) % color_depth) * _buffer_size ) + total_offset_g;

      if (g > color_thresh + _color_G_offset)
         buffer[buffer_idx][off_g] |= _BV(bit_select);
      else
         buffer[buffer_idx][off_g] &= ~_BV(bit_select);

      uint32_t off_b = (((this_color + color_two_third_step) % color_depth) * _buffer_size) + total_offset_b;
      if (b > color_thresh + _color_B_offset)
         buffer[buffer_idx][off_b] |= _BV(bit_select);
      else
         buffer[buffer_idx][off_b] &= ~_BV(bit_select);
   }
}

void PxMatrix::drawPixelRGB565(int16_t x, int16_t y, uint16_t color, bool selected_buffer) {
  uint8_t r = ((((color >> 11) & 0x1F) * 527) + 23) >> 6;
  uint8_t g = ((((color >> 5) & 0x3F) * 259) + 33) >> 6;
  uint8_t b = (((color & 0x1F) * 527) + 23) >> 6;
  fillMatrixBuffer( x,  y, r, g, b, selected_buffer);
}

void PxMatrix::drawPixelRGB565(int16_t x, int16_t y, uint16_t color) {
  uint8_t r = ((((color >> 11) & 0x1F) * 527) + 23) >> 6;
  uint8_t g = ((((color >> 5) & 0x3F) * 259) + 33) >> 6;
  uint8_t b = (((color & 0x1F) * 527) + 23) >> 6;
  fillMatrixBuffer( x,  y, r, g, b, _selected_buffer);
}

void PxMatrix::drawPixelRGB888(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b, bool selected_buffer) {
  fillMatrixBuffer(x, y, r, g, b, selected_buffer);
}

void PxMatrix::drawPixelRGB888(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) {
  fillMatrixBuffer(x, y, r, g, b, _selected_buffer);
}

void PxMatrix::begin()
{
  begin(8);
}

void PxMatrix::begin(uint8_t row_pattern)
{
   esp_err_t ret;

   _row_pattern = row_pattern;
   if (4 == _row_pattern)
      _scan_pattern = ZIGZAG;

   _buffer_size = ((_width * _height * 3) / 8);
   _pattern_color_bytes = (_height / _row_pattern) * (_width / 8);
   _send_buffer_size = _pattern_color_bytes * 3;

   spi_bus_config_t cfg;
   memset(&cfg, 0, sizeof(spi_bus_config_t));
   cfg.miso_io_num = SPI_BUS_MISO;
   cfg.mosi_io_num = SPI_BUS_MOSI;
   cfg.sclk_io_num = SPI_BUS_CLK;
   cfg.quadwp_io_num = -1;
   cfg.quadhd_io_num = -1;
   cfg.max_transfer_sz = _send_buffer_size;

//#define buffer_size max_matrix_width * max_matrix_height * 3 / 8

   spi_device_interface_config_t dev; 
   memset(&dev, 0, sizeof(spi_device_interface_config_t));
   dev.mode = 0;
   dev.clock_speed_hz = 20000000;
   dev.spics_io_num = SPI_BUS_SS;
   dev.flags = SPI_TRANS_USE_RXDATA;
   dev.queue_size=2;
   
   // Set Up The SPI on ESP32
   ret = spi_bus_initialize(SPI_HOST_TYPE, &cfg, 2);
   ESP_ERROR_CHECK(ret);
   ret = spi_bus_add_device(SPI_HOST_TYPE, &dev, &spi);
   ESP_ERROR_CHECK(ret);
   
   // set_data_mode = SPI_MODE0
   // set_bit_order = MSBFIRST
   // set_frequency = 20000000

   gpio_pad_select_gpio(_OE_PIN);
   gpio_pad_select_gpio(_LATCH_PIN);
   gpio_pad_select_gpio(_A_PIN);
   gpio_pad_select_gpio(_B_PIN);

   gpio_set_direction((gpio_num_t)_OE_PIN, GPIO_MODE_OUTPUT);
   gpio_set_direction((gpio_num_t)_LATCH_PIN, GPIO_MODE_OUTPUT);
   gpio_set_direction((gpio_num_t)_A_PIN, GPIO_MODE_OUTPUT);
   gpio_set_direction((gpio_num_t)_B_PIN, GPIO_MODE_OUTPUT);

   gpio_set_level((gpio_num_t)_A_PIN, 0);
   gpio_set_level((gpio_num_t)_B_PIN, 0);
   gpio_set_level((gpio_num_t)_OE_PIN, 1);

   if (_row_pattern >= 8)
   {
      gpio_pad_select_gpio(_C_PIN);
      gpio_set_direction((gpio_num_t)_C_PIN, GPIO_MODE_OUTPUT);
      gpio_set_level((gpio_num_t)_C_PIN, 0);
   }

   if (_row_pattern >= 16)
   {
      gpio_pad_select_gpio(_D_PIN);
      gpio_set_direction((gpio_num_t)_D_PIN, GPIO_MODE_OUTPUT);
      gpio_set_level((gpio_num_t)_D_PIN, 0);
   }

   if (_row_pattern >= 32)
   {
      gpio_pad_select_gpio(_E_PIN);
      gpio_set_direction((gpio_num_t)_E_PIN, GPIO_MODE_OUTPUT);
      gpio_set_level((gpio_num_t)_E_PIN, 0);
   }

   // Precompute row offset values
   for (uint8_t yy=0; yy<_height;yy++) {
      _row_offset[yy]=((yy)%_row_pattern)*_send_buffer_size+_send_buffer_size-1;
   }

   // Allocate The Stuff
   buffer[0] = (uint8_t *)heap_caps_calloc(_buffer_size, color_depth, MALLOC_CAP_DMA);
   buffer[1] = (uint8_t *)heap_caps_calloc(_buffer_size, color_depth, MALLOC_CAP_DMA);
   flushBuffer = (uint8_t *)heap_caps_calloc(_send_buffer_size, 1, MALLOC_CAP_DMA);

   // Create The Event Group
   xDisplayEventGroup = xEventGroupCreate();
}

void PxMatrix::set_mux(uint8_t value)
{
   if (BINARY == _mux_pattern)
   {
      if (value & 0x01)
         gpio_set_level((gpio_num_t)_A_PIN, 1);
      else
         gpio_set_level((gpio_num_t)_A_PIN, 0);

      if (value & 0x02)
         gpio_set_level((gpio_num_t)_B_PIN, 1);
      else
         gpio_set_level((gpio_num_t)_B_PIN, 0);

      if (_row_pattern >= 8)
      {
         if (value & 0x04)
            gpio_set_level((gpio_num_t)_C_PIN, 1);
         else
            gpio_set_level((gpio_num_t)_C_PIN, 0);
      }

      if (_row_pattern >= 16)
      {
         if (value & 0x08)
            gpio_set_level((gpio_num_t)_D_PIN, 1);
         else
            gpio_set_level((gpio_num_t)_D_PIN, 0);
      }

      if (_row_pattern >= 32)
      {
         if (value & 0x10)
            gpio_set_level((gpio_num_t)_E_PIN, 1);
         else
            gpio_set_level((gpio_num_t)_E_PIN, 0);
      }
   }

   if (STRAIGHT == _mux_pattern)
   {
      if (value == 0)
         gpio_set_level((gpio_num_t)_A_PIN, 0);
      else
         gpio_set_level((gpio_num_t)_A_PIN, 1);

      if (value == 1)
         gpio_set_level((gpio_num_t)_B_PIN, 0);
      else
         gpio_set_level((gpio_num_t)_B_PIN, 1);

      if (value == 2)
         gpio_set_level((gpio_num_t)_C_PIN, 0);
      else
         gpio_set_level((gpio_num_t)_C_PIN, 1);

      if (value == 3)
         gpio_set_level((gpio_num_t)_D_PIN, 0);
      else
         gpio_set_level((gpio_num_t)_D_PIN, 1);
   }
}

void PxMatrix::latch(uint16_t show_time)
{
   gpio_set_level((gpio_num_t)_LATCH_PIN, 1);
   gpio_set_level((gpio_num_t)_LATCH_PIN, 0);
   gpio_set_level((gpio_num_t)_OE_PIN, 0);
   // delay microseconds
   ets_delay_us(show_time);
   gpio_set_level((gpio_num_t)_OE_PIN, 1);
}

void PxMatrix::display(uint16_t show_time)
{
   spi_transaction_t *rtrans;
   esp_err_t ret;
   unsigned long start_time = 0;
   xEventGroupClearBits(xDisplayEventGroup, BIT_BUFFER_SWAP_OK);
   for (uint8_t i = 0; i < _row_pattern; i++)
   {
      //if (2 < i)
      //  continue;

      if (_fast_update) {
         printf("fast update\n");
         fflush(stdout);
      } 
      else 
      {
	 uint8_t buffer_idx = _active_buffer ? 1 : 0;
         uint32_t offset = (_display_color * _buffer_size) + (i * _send_buffer_size);
         set_mux(i);
         _transactions[0].length = _send_buffer_size << 3;
         _transactions[0].rxlength = 0;
         _transactions[0].flags = SPI_TRANS_USE_RXDATA;
         _transactions[0].tx_buffer = &(buffer[buffer_idx][offset]);

	 //ets_delay_us(100);
         ret = spi_device_queue_trans(spi, &_transactions[0], portMAX_DELAY);
         ESP_ERROR_CHECK(ret);
         ret = spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
         ESP_ERROR_CHECK(ret);
         latch(show_time);
      }
   }
   _display_color++;
   if (_display_color >= color_depth)
   {
      _display_color = 0;
// Flip the Buffer?
      _active_buffer = _selected_buffer;
      xEventGroupSetBits(xDisplayEventGroup, BIT_BUFFER_SWAP_OK);
   }

}

void PxMatrix::displayTestPattern(uint16_t show_time) 
{
   int64_t time = esp_timer_get_time();
   spi_transaction_t *rtrans;
   esp_err_t ret;

   if ((time - _test_last_call) > 500000)
   {
      buffer[0][0] = 0xff;
      _transactions[0].length = 8;
      _transactions[0].rxlength = 0;
      _transactions[0].flags = SPI_TRANS_USE_RXDATA;
      _transactions[0].tx_buffer = buffer[0];
      ret = spi_device_queue_trans(spi, &_transactions[0], portMAX_DELAY);
      ESP_ERROR_CHECK(ret);
      ret = spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
      ESP_ERROR_CHECK(ret);
      _test_last_call = time;
      _test_pixel_counter++;
      printf("_test_pixel_counter = %u\n", _test_pixel_counter);
   }

   if (_test_pixel_counter > _send_buffer_size)
   //if (_test_pixel_counter > _width)
   {
      _test_pixel_counter = 0;
      _test_line_counter++;
      printf("  _test_line_counter = %u\n", _test_line_counter);
      fflush(stdout);
      flushDisplay();
   }

   if (_test_line_counter > (_height / 2))
      _test_line_counter = 0;

   gpio_set_level((gpio_num_t)_A_PIN, 1);
   gpio_set_level((gpio_num_t)_B_PIN, 1);
   gpio_set_level((gpio_num_t)_C_PIN, 1);
   //gpio_set_level((gpio_num_t)_D_PIN, 1);
   //gpio_set_level((gpio_num_t)_E_PIN, 1); 

   gpio_set_level((gpio_num_t)_A_PIN, 0);
   gpio_set_level((gpio_num_t)_B_PIN, 0);
   gpio_set_level((gpio_num_t)_C_PIN, 0);
   //gpio_set_level((gpio_num_t)_D_PIN, 0);
   //gpio_set_level((gpio_num_t)_E_PIN, 0); 

   set_mux(_test_line_counter);

   latch(show_time);
}

void PxMatrix::displayTestPixel(uint16_t show_time)
{
   int64_t time = esp_timer_get_time();
   spi_transaction_t *rtrans;
   esp_err_t ret;

   printf("test_pixel_counter: %u\n", _test_pixel_counter);
   fflush(stdout);

   if ((time-_test_last_call) > 500000)
   {
      flushDisplay();
      uint16_t blanks = _test_pixel_counter / 8;
      buffer[0][0] = (1 << _test_pixel_counter % 8);

      if (blanks > 0)
         memset(&buffer[0][1], 0x00, blanks);

      _transactions[0].length = (blanks + 1) << 3;
      _transactions[0].rxlength = 0;
      _transactions[0].flags = SPI_TRANS_USE_RXDATA;
      _transactions[0].tx_buffer = buffer[0];
      ret = spi_device_queue_trans(spi, &_transactions[0], portMAX_DELAY);
      ESP_ERROR_CHECK(ret);
      ret = spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
      ESP_ERROR_CHECK(ret);

      _test_last_call = time;
      _test_pixel_counter++;
   }

   if (_test_pixel_counter > _send_buffer_size/3*8)
   {
      _test_pixel_counter = 0;
      _test_line_counter++;
   }

   if (_test_line_counter > (_height / 2))
      _test_line_counter = 0;

   gpio_set_level((gpio_num_t)_A_PIN, 1);
   gpio_set_level((gpio_num_t)_B_PIN, 1);
   gpio_set_level((gpio_num_t)_C_PIN, 1);
   //gpio_set_level((gpio_num_t)_D_PIN, 1);
   //gpio_set_level((gpio_num_t)_E_PIN, 1); 

   gpio_set_level((gpio_num_t)_A_PIN, 0);
   gpio_set_level((gpio_num_t)_B_PIN, 0);
   gpio_set_level((gpio_num_t)_C_PIN, 0);
   //gpio_set_level((gpio_num_t)_D_PIN, 0);
   //gpio_set_level((gpio_num_t)_E_PIN, 0); 

   set_mux(_test_line_counter);

   latch(show_time);
}

inline PxMatrix* real(pxmatrix *m) { return static_cast<PxMatrix*>(m); }

extern pxmatrix* Create_PxMatrix(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B) {
   return new PxMatrix(width, height, LATCH, OE, A, B);
}

extern pxmatrix* Create_PxMatrix3(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B, uint8_t C)
{
   return new PxMatrix(width, height, LATCH, OE, A, B, C);
}

extern pxmatrix* Create_PxMatrix4(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B, uint8_t C, uint8_t D)
{
   return new PxMatrix(width, height, LATCH, OE, A, B, C, D);
}

extern pxmatrix* Create_PxMatrix5(uint8_t width, uint8_t height, uint8_t LATCH, uint8_t OE, uint8_t A, uint8_t B, uint8_t C, uint8_t D, uint8_t E)
{
   return new PxMatrix(width, height, LATCH, OE, A, B, C, D, E);
}

extern void pxmatrix_begin(pxmatrix *matrix, uint8_t steps)
{
   real(matrix)->begin(steps);
}

extern void pxmatrix_clearDisplay(pxmatrix *matrix)
{
   real(matrix)->clearDisplay();
}

extern void pxmatrix_display(pxmatrix *matrix, uint16_t show_time)
{
   real(matrix)->display(show_time);
}

extern void pxmatrix_drawPixelRGB565(pxmatrix *matrix, int16_t x, int16_t y, uint16_t color)
{
   real(matrix)->drawPixelRGB565(x, y, color);
}

extern void pxmatrix_drawPixel(pxmatrix *matrix, int16_t x, int16_t y, uint16_t color)
{
   real(matrix)->drawPixel(x, y, color);
}

extern void pxmatrix_drawPixelRGB888(pxmatrix *matrix, int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b)
{
   real(matrix)->drawPixelRGB888(x, y, r, g, b);
}

extern uint16_t pxmatrix_color565(pxmatrix *matrix, uint8_t r, uint8_t g, uint8_t b)
{
   return real(matrix)->color565(r, g, b);
}

extern void pxmatrix_displayTestPattern(pxmatrix *matrix, uint16_t show_time)
{
   real(matrix)->displayTestPattern(show_time);
}

extern void pxmatrix_displayTestPixel(pxmatrix *matrix, uint16_t show_time)
{
   real(matrix)->displayTestPixel(show_time);
}

extern void pxmatrix_setFastUpdate(pxmatrix *matrix, bool fast_update)
{
   real(matrix)->setFastUpdate(fast_update);
}

extern void pxmatrix_selectBuffer(pxmatrix *matrix, bool selected_buffer)
{
   real(matrix)->selectBuffer(selected_buffer);
}

extern void pxmatrix_swapBuffer(pxmatrix *matrix)
{
   real(matrix)->swapBuffer();
}
