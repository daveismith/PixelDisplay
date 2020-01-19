#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"

#include "PxMatrix.h"
#include "display.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#define P_LAT CONFIG_DISPLAY_GPIO_STB_LAT
#define P_A CONFIG_DISPLAY_GPIO_A
#define P_B CONFIG_DISPLAY_GPIO_B
#define P_C CONFIG_DISPLAY_GPIO_C
#ifdef CONFIG_DISPLAY_GPIO_D
#define P_D CONFIG_DISPLAY_GPIO_D
#endif
#ifdef CONFIG_DISPLAY_GPIO_E
#define P_E CONFIG_DISPLAY_GPIO_E
#endif
#define P_OE CONFIG_DISPLAY_GPIO_P_OE
#define P_POWER 22

#define MATRIX_WIDTH 32
#define MATRIX_HEIGHT 16

const static char *TAG = "PixelDisplay";

static pxmatrix* display = NULL;
static QueueHandle_t xCommandQueue = NULL;


/****
 * Command Formats
 * DISPLAY_BRIGHTNESS:
 *   requires two 16 bit integers (signed). store inside one 32-bit unsigned integer in format
 *   [brightness|31-16][dimRate|15-0]
 * DISPLAY_POWER:
 *   requires a boolean, on = true, off = false (.b)
 * DISPLAY_MODE:
 *   requires a display_mode_e (.u)
 * DISPLAY_RATE:
 *   configure the rate at which the display updates.
 * DISPLAY_ANIMATION:
 *   sets the current animation
 * DISPLAY_COLOUR:
 *   indicates the colour to display, sends RGB in 32-bit integer as [R|23-16][G|15-8][B|7-0]
 * DISPLAY_FILE:
 *   indicates the file to display, sends a pointer to a string. if an existing pointer exists,
 *   it will need to be freed
 * DISPLAY_UPDATE:
 *   in manual mode, indicates that the display needs to be updated
 * DISPLAY_FILL
 *   fills the selected area with the given colour
 * DISPLAY_SET_PIXEL
 *   sets a specific pixel to a certain colour
 */

typedef enum {
   DISPLAY_BRIGHTNESS,
   DISPLAY_POWER,
   DISPLAY_MODE,
   DISPLAY_RATE,
   DISPLAY_ANIMATION,
   DISPLAY_COLOUR,
   DISPLAY_FILE,
   DISPLAY_UPDATE,
   DISPLAY_FILL_RECT,
   DISPLAY_DRAW_LINE,
   DISPLAY_DRAW_CIRCLE,
   DISPLAY_FILL_CIRCLE,
   DISPLAY_SET_PIXEL,
   DISPLAY_SET_FONT,
   DISPLAY_PRINT
} display_cmd_e;

typedef enum {
   FILE_TYPE_RGB
} file_type_e;

typedef struct {
   display_cmd_e command;
   union {
      uint32_t u;
      int32_t d;
      bool b;
      char *s;
      void *p;
   };
} display_cmd_t;

typedef struct {
   size_t x;
   size_t y;
   size_t w;
   size_t h;
   uint8_t r;
   uint8_t g;
   uint8_t b;
} display_fill_t;

typedef struct {
   size_t x;
   size_t y;
   uint8_t r;
   uint8_t g;
   uint8_t b;
} display_pixel_t;

typedef struct {
   size_t x0;
   size_t y0;
   size_t x1;
   size_t y1;
   uint8_t r;
   uint8_t g;
   uint8_t b;
} display_line_t;

typedef struct {
   size_t x;
   size_t y;
   size_t radius;
   uint8_t r;
   uint8_t g;
   uint8_t b;
} display_circle_t;

typedef struct {
   size_t x;
   size_t y;
   char *text;
   uint8_t r;
   uint8_t g;
   uint8_t b;
   GFXfont *font;
} display_text_t;

#define ANIM0
#define ANIM1
//#define ANIM2

const uint8_t animation_lengths[]={
#ifdef ANIM0
14,
#endif //ANIM0
#ifdef ANIM1
17,
#endif //ANIM1
#ifdef ANIM2
49,
#endif //ANIM2
};

const size_t animation_count = sizeof(animation_lengths) / sizeof(uint8_t);

const uint8_t animations[] = {
#ifdef ANIM0
   #include "anim0.h"
#endif //ANIM0
#ifdef ANIM1
   #include "anim1.h"
#endif //ANIM1
#ifdef ANIM2
   #include "anim2.h"
#endif //ANIM2
};

display_mode_e currentMode = DISPLAY_MODE_ANIMATION;
uint32_t currentRate;
size_t currentAnimation = 0;
size_t currentFrame = 0;
size_t totalFrames = 0;
size_t frameSize = 1024;
uint32_t currentColour = 0;
GFXfont *currentFont = NULL;

char *currentFile = NULL;
int currentFd = -1;
file_type_e currentFileType = FILE_TYPE_RGB;

static uint8_t *nextFrame;

static int16_t currentBrightness = 0;
static int16_t targetBrightness = 0;
static int16_t dimRate = 0;

#ifndef _swap_size_t
#define _swap_size_t(a, b) { size_t t = a; a = b; b = t; }
#endif

static void _display_timer_cb(void *arg)
{
   static uint8_t cnt = 0;
   pxmatrix *display = (pxmatrix *)arg;
   //pxmatrix_display(display, 15);
   //pxmatrix_display(display, 35);
   //pxmatrix_display(display, 70);
   //
   //

   pxmatrix_display(display, currentBrightness / 30);
   cnt++;
}

void draw_anim(pxmatrix *display, size_t animation)
{
   if (animation >= animation_count) {
      animation = animation % animation_count;
   }
   totalFrames = animation_lengths[animation];
   int frame_offset = 0;
   for (int idx = 0; idx < animation; idx++)
      frame_offset += animation_lengths[idx];

   const uint8_t *ptr = animations + (frame_offset + currentFrame) * frameSize;
   uint16_t val;
   for (size_t yy = 0; yy < MATRIX_HEIGHT; yy++)
   {
      for (size_t xx = 0; xx < MATRIX_WIDTH; xx++)
      {
         val = ptr[0] | (ptr[1] << 8);
         pxmatrix_drawPixelRGB565(display, xx, yy, val);
         ptr += 2;
      }
   }
   currentFrame++;
   if (currentFrame >= totalFrames)
      currentFrame = 0;
}

void draw_colour(pxmatrix *display, uint32_t colour)
{
   uint8_t r,g,b;
   r = (uint8_t)(colour >> 16);
   g = (uint8_t)(colour >> 8);
   b = (uint8_t)colour;
   for (size_t yy = 0; yy < MATRIX_HEIGHT; yy++)
   {
      for (size_t xx = 0; xx < MATRIX_WIDTH; xx++)
      {
         pxmatrix_drawPixelRGB888(display, xx, yy, r, g, b);
      }
   }
}

void draw_file(pxmatrix *display, const char *file)
{
   char data[3];
   // Draw The Next Frame Of The File
   if (-1 == currentFd) {
      struct stat sd;
      char *path = realpath(file, NULL);
      currentFd = open(path, O_RDONLY);
      free(path);
      currentFrame = 0;
      //totalFrames ==> read the file size
      if (-1 == currentFd)
         return;  // There Was An Error Loading The File

      // Check The File Type / Extension
      const char *dot = strrchr(file, '.');
      if(dot && dot != file)
      {
         dot += 1;   // Flip Over 1
         printf("dot: '%s'\n", dot);

         if (0 == strncasecmp(dot, "rgb", 4)) 
         {
            currentFileType = FILE_TYPE_RGB;
         }
      }

      fstat(currentFd, &sd);
      totalFrames = sd.st_size / (MATRIX_WIDTH * MATRIX_HEIGHT * 3);
      printf("total frames: %u\n", totalFrames);
   }

   off_t pos = (MATRIX_WIDTH * MATRIX_HEIGHT * 3) * currentFrame;
   lseek(currentFd, pos, SEEK_SET);

   // Now We Need To Load Pixels
   for (size_t yy = 0; yy < MATRIX_HEIGHT; yy++)
   {
      for (size_t xx = 0; xx < MATRIX_WIDTH; xx++)
      {
         switch(currentFileType) {
         case FILE_TYPE_RGB:
         {
            if (-1 == read(currentFd, data, sizeof(data))) {
               printf("error: %d\n", errno);
            }
         
            pxmatrix_drawPixelRGB888(display, xx, yy, data[0], data[1], data[2]);
            break;
         }
         default:
            break;
         }
      }
   }

   currentFrame++;
   if (currentFrame >= totalFrames)
      currentFrame = 0;
}

void _drawPixel(ssize_t x, ssize_t y, uint8_t r, uint8_t g, uint8_t b) {
   if (x < 0 || x >= MATRIX_WIDTH)
      return;
   if (y < 0 || y >= MATRIX_HEIGHT)
      return;

   size_t offset = y * MATRIX_WIDTH + x;
   nextFrame[offset] = r;
   nextFrame[MATRIX_HEIGHT*MATRIX_WIDTH + offset] = g;
   nextFrame[2*MATRIX_HEIGHT*MATRIX_WIDTH + offset] = b;
}

void _fillRect(display_fill_t *fill)
 {
   for (size_t yy = fill->y; yy < (fill->y + fill->h); yy++)
   {
      for (size_t xx = fill->x; xx < (fill->x + fill->w); xx++)
      {
         _drawPixel(xx, yy, fill->r, fill->g, fill->b);
         //size_t offset = yy * MATRIX_WIDTH + xx;
         //nextFrame[offset] = fill->r;
         //nextFrame[MATRIX_HEIGHT*MATRIX_WIDTH + offset] = fill->g;
         //nextFrame[2*MATRIX_HEIGHT*MATRIX_WIDTH + offset] = fill->b;
      }
   }
}

void _drawLine(display_line_t *line) {
   // Bresenham's algorithm
   if (line->x0 == line->x1) {
      // Vertical Line
      if (line->y0 > line->y1) { _swap_size_t(line->y0, line->y1); }
      for (size_t yy = line->y0; yy <= line->y1; yy++)
      {
         _drawPixel(line->x0, yy, line->r, line->g, line->b);
         //size_t offset = yy * MATRIX_WIDTH + line->x0;
         //nextFrame[offset] = line->r;
         //nextFrame[MATRIX_HEIGHT*MATRIX_WIDTH + offset] = line->g;
         //nextFrame[2*MATRIX_HEIGHT*MATRIX_WIDTH + offset] = line->b;
      }
   } else if (line->y0 == line->y1) {
      // Horizontal Line
      if (line->x0 > line->x1) { _swap_size_t(line->x0, line->x1); }
      for (size_t xx = line->x0; xx <= line->x1; xx++)
      {
         _drawPixel(xx, line->y0, line->r, line->g, line->b);
         //size_t offset = line->y0 * MATRIX_WIDTH + xx;
         //nextFrame[offset] = line->r;
         //nextFrame[MATRIX_HEIGHT*MATRIX_WIDTH + offset] = line->g;
         //nextFrame[2*MATRIX_HEIGHT*MATRIX_WIDTH + offset] = line->b;
      }
   } else {
      // Line With Some Slope
      if (line->x1 < line->x0) {
         _swap_size_t(line->x0, line->x1);
         _swap_size_t(line->y0, line->y1);
      }

      int dx = line->x1 - line->x0;
      int dy = line->y1 - line->y0;
      int yi = 1;

      if (dy < 0) {
         yi = -1;
         dy = -dy;
      }
      int D = 2*dy - dx;
      size_t y = line->y0;

      for (size_t xx = line->x0; xx < line->x1; xx++)
      {
         _drawPixel(xx, y, line->r, line->g, line->b);
         //size_t offset = y * MATRIX_WIDTH + xx;
         //nextFrame[offset] = line->r;
         //nextFrame[MATRIX_HEIGHT*MATRIX_WIDTH + offset] = line->g;
         //nextFrame[2*MATRIX_HEIGHT*MATRIX_WIDTH + offset] = line->b;

         // Next Pixel
         if (D > 0) {
            y += yi;
            D = D - 2*dx;
         }
         D = D + 2*dy;
      }
   }
}

void _drawCircle(display_circle_t *circle) {
   // From AdafruitGFX
   size_t radius = circle->radius;

   ssize_t f = 1 - radius;
   ssize_t ddF_x = 1;
   ssize_t ddF_y = -2 * radius;
   size_t x = 0;
   size_t y = radius;

   _drawPixel(circle->x, circle->y+radius, circle->r, circle->g, circle->b);
   _drawPixel(circle->x, circle->y-radius, circle->r, circle->g, circle->b);
   _drawPixel(circle->x+radius, circle->y, circle->r, circle->g, circle->b);
   _drawPixel(circle->x-radius, circle->y, circle->r, circle->g, circle->b);

   while (x < y) {
      if (f >= 0) {
         y--;
         ddF_y += 2;
         f += ddF_y;
      }
      x++;
      ddF_x += 2;
      f += ddF_x;

      _drawPixel(circle->x + x, circle->y + y, circle->r, circle->g, circle->b);
      _drawPixel(circle->x - x, circle->y + y, circle->r, circle->g, circle->b);
      _drawPixel(circle->x + x, circle->y - y, circle->r, circle->g, circle->b);
      _drawPixel(circle->x - x, circle->y - y, circle->r, circle->g, circle->b);

      _drawPixel(circle->x + y, circle->y + x, circle->r, circle->g, circle->b);
      _drawPixel(circle->x - y, circle->y + x, circle->r, circle->g, circle->b);
      _drawPixel(circle->x + y, circle->y - x, circle->r, circle->g, circle->b);
      _drawPixel(circle->x - y, circle->y - x, circle->r, circle->g, circle->b);
   }
}

void _fillCircleHelper(size_t x0, size_t y0, size_t radius, uint8_t corner, size_t delta, uint8_t r, uint8_t g, uint8_t b)
{
   ssize_t f = 1 - radius;
   ssize_t ddF_x = 1;
   ssize_t ddF_y = -2 * radius;
   size_t x = 0;
   size_t y = radius;

   display_line_t line;
   line.r = r;
   line.g = g;
   line.b = b;


   while (x < y) {
      if (f >= 0) {
         y--;
         ddF_y += 2;
         f     += ddF_y;
      }
      x++;
      ddF_x += 2;
      f     += ddF_x;

      if (corner & 0x01) {
         line.x0 = x0+x;
         line.y0 = y0-y;
         line.x1 = x0+x;
         line.y1 = y0+y+delta;
         _drawLine(&line);

         line.x0 = x0+y;
         line.y0 = y0-x;
         line.x1 = x0+y;
         line.y1 = y0+x+delta;
         _drawLine(&line);
      }

      if (corner & 0x2) {
         line.x0 = x0-x;
         line.y0 = y0-y;
         line.x1 = x0-x;
         line.y1 = y0+y+delta;
         _drawLine(&line);

         line.x0 = x0-y;
         line.y0 = y0-x;
         line.x1 = x0-y;
         line.y1 = y0+x+delta;
         _drawLine(&line);
      }
   }
}

void _fillCircle(display_circle_t *circle) {
   display_line_t line;
   line.x0 = circle->x;
   line.y0 = circle->y - circle->radius;
   line.x1 = circle->x;
   line.y1 = circle->y + circle->radius;
   line.r = circle->r;
   line.g = circle->g;
   line.b = circle->b;
   _drawLine(&line);
   _fillCircleHelper(circle->x, circle->y, circle->radius, 3, 0, circle->r, circle->g, circle->b);
}

void _drawText(display_text_t *text) {
   // Start To Draw
   //
   //
   //
   //
   size_t x = text->x;
   size_t y = text->y;
   size_t xIdx, yIdx, idx;
   char *ptr = text->text;

   while ('\0' != *ptr) {
      uint8_t c = *ptr;
      if ('\n' == c) {
         x = text->x;
	 y += text->font->yAdvance;
      }

      if (c >= text->font->first && c <= text->font->last) {
         GFXglyph *glyph = NULL;
	 size_t offset = 0;
	 uint8_t bits;
	 printf("char: %c\n", c);
         c = c - text->font->first;
         glyph = &text->font->glyph[c];
	 offset = glyph->bitmapOffset;
         bits = text->font->bitmap[offset];

	 // Figure Out How To Draw
	 idx = 0;

         for (yIdx = 0; yIdx < glyph->height; yIdx++) {
	    // Run Through And Print
            size_t yCoord = y + glyph->yOffset + yIdx;
	    for (xIdx = 0; xIdx < glyph->width; xIdx++) {
	       size_t xCoord = x + glyph->xOffset + xIdx;

               if (bits & 0x80) {
	          _drawPixel(xCoord, yCoord, text->r, text->g, text->b);
	       }
	       bits <<= 1;

	       if (++idx >= 8) {
		  offset++;
	          bits = text->font->bitmap[offset];
		  idx = 0;
	       }
	    }
	 }
	 
	 x += glyph->xAdvance;
      }

      ptr++;   // Advance The String Pointer 
   }
}

//#define DISPLAY_TIMER_PERIOD_US        500
#define DISPLAY_TIMER_PERIOD_US        1000
//#define DISPLAY_TIMER_PERIOD_US        2000

void display_task(void *pvParameter)
{
   esp_timer_handle_t timer_handle;
   display = Create_PxMatrix3(MATRIX_WIDTH, MATRIX_HEIGHT, P_LAT, P_OE, P_A, P_B, P_C);
   nextFrame = calloc(MATRIX_WIDTH * MATRIX_HEIGHT, 3);  //Every Pixel Has 24 bits of data
   pxmatrix_begin(display, CONFIG_DISPLAY_SCAN);
   pxmatrix_clearDisplay(display);
   pxmatrix_setFastUpdate(display, false);
   currentRate = DEFAULT_RATE;
   dimRate = DEFAULT_DIM_RATE;
   currentBrightness = BRIGHTNESS_MIN;
   targetBrightness = BRIGHTNESS_MAX;

   currentMode = DISPLAY_MODE_ANIMATION;
   currentAnimation = esp_random() % animation_count;

   // Set Up The Command Queue
   xCommandQueue = xQueueCreate( 10, sizeof( display_cmd_t ) );

   // GPIO22
   gpio_pad_select_gpio(P_POWER);
   gpio_set_direction(P_POWER, GPIO_MODE_INPUT_OUTPUT);
   gpio_set_level(P_POWER, 1);

   //xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);

   // Init the Timer
   esp_timer_create_args_t timer_conf = {
        .callback = _display_timer_cb,
        .arg = display,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "display_timer"
    };
    esp_err_t err = esp_timer_create(&timer_conf, &timer_handle);
    if (err) {
        ESP_LOGE(TAG, "error starting esp_timer: %d\n", err);
        return;
    }
    esp_timer_start_periodic(timer_handle, DISPLAY_TIMER_PERIOD_US);

   int taskCore = xPortGetCoreID();
   printf("display task running on %d\n", taskCore);
  
   display_cmd_t cmd; 
   display_mode_e previousMode = currentMode;
   while (true) {
      vTaskDelay(currentRate / portTICK_PERIOD_MS);
      
      // Check For Incoming Commands
      while ( xQueueReceive( xCommandQueue, (void *) &cmd, (TickType_t) 0) ) {
         // Process The Command Type
         switch(cmd.command) {
            case DISPLAY_BRIGHTNESS:
            {
               int16_t target = (int16_t)(cmd.u >> 16);
               int16_t rate = (int16_t)(cmd.u & 0xffff);

               if  (target < currentBrightness && rate > 0) {
                  rate = -rate;
               } else if (target > currentBrightness && rate < 0) {
                  rate = -rate;
               }
               targetBrightness = target;
               dimRate = rate;
               break;
            }
            case DISPLAY_POWER:
               // GPIO22
               if (display_getPower() != cmd.b) {
                  gpio_set_level(P_POWER, cmd.b);
                  if (cmd.b) {
                     esp_timer_start_periodic(timer_handle, DISPLAY_TIMER_PERIOD_US);
                  } else {
                     esp_timer_stop(timer_handle);
                  }
               }
               break;
            case DISPLAY_MODE:
               currentMode = (display_mode_e) cmd.u;
               break;
            case DISPLAY_RATE:
               currentRate = cmd.u;
               break;
            case DISPLAY_ANIMATION:
               currentAnimation = (cmd.u % animation_count);
                if (DISPLAY_MODE_ANIMATION == currentMode)
                  currentFrame = 0; 
               break;
            case DISPLAY_COLOUR:
               currentColour = cmd.u;
               break;
            case DISPLAY_FILE:
               if (NULL != currentFile) {
                  free(currentFile);
                  currentFile = NULL;
               }
               currentFile = cmd.s;
               if (-1 != currentFd) {
                  close(currentFd);
                  currentFd = -1;
               }
               if (DISPLAY_MODE_FILE == currentMode)
                  currentFrame = 0; 

               printf("file: %s\n", currentFile);
               break;
            case DISPLAY_UPDATE:
               if (DISPLAY_MODE_MANUAL == currentMode) {
                  uint8_t r,g,b;
                  for (size_t yy = 0; yy < MATRIX_HEIGHT; yy++)
                  {
                     for (size_t xx = 0; xx < MATRIX_WIDTH; xx++)
                     {
                        size_t offset = yy * MATRIX_WIDTH + xx;
                        r = nextFrame[offset];
                        g = nextFrame[MATRIX_HEIGHT*MATRIX_WIDTH + offset];
                        b = nextFrame[2*MATRIX_HEIGHT*MATRIX_WIDTH + offset];
                        pxmatrix_drawPixelRGB888(display, xx, yy, r, g, b);
                     }
                  }
               }
               break;
            case DISPLAY_FILL_RECT:
            {
               display_fill_t *fill = (display_fill_t *)cmd.p;
               _fillRect(fill);
               free(fill);
            }
               break;
            case DISPLAY_DRAW_LINE:
            {
               display_line_t *line = (display_line_t *)cmd.p;
                _drawLine(line);
               free(line);
            }
               break;
            case DISPLAY_DRAW_CIRCLE:
            {
               display_circle_t *circle = (display_circle_t *)cmd.p;
               _drawCircle(circle);
               free(circle);
            }
               break;
            case DISPLAY_FILL_CIRCLE:
            {
               display_circle_t *circle = (display_circle_t *)cmd.p;
               _fillCircle(circle);
               free(circle);
            }
               break;
            case DISPLAY_SET_PIXEL:
	    {
	       display_pixel_t *pixel = (display_pixel_t *)cmd.p;
	       _drawPixel(pixel->x, pixel->y, pixel->r, pixel->g, pixel->b);
	       free(pixel);
	    }
               break;
	    case DISPLAY_SET_FONT:
	    {
               currentFont = (GFXfont *)cmd.p;
	    }
	       break;
	    case DISPLAY_PRINT:
	    {
               // Print
	       display_text_t *text = (display_text_t *)cmd.p;
	       _drawText(text);
	       if (NULL != text->text)
                  free(text->text);
	       free(text);
	    }
            default:
               break;
         }
      }

      if (currentMode != previousMode) {
         currentFrame = 0;
         if (previousMode == DISPLAY_MODE_FILE) {
            if (currentFd != -1) {
               close(currentFd);
               currentFd = -1;
            }
         }
      }

      switch (currentMode) {
         case DISPLAY_MODE_ANIMATION:
            pxmatrix_swapBuffer(display);
            draw_anim(display, currentAnimation);
            break;
         case DISPLAY_MODE_COLOUR:
            pxmatrix_swapBuffer(display);
            draw_colour(display, currentColour);
            break;
         case DISPLAY_MODE_FILE:
            // Somehow Draw The Frames
            pxmatrix_swapBuffer(display);
            draw_file(display, currentFile);
            break;
         case DISPLAY_MODE_MANUAL:
            // Flip Is Done By A Command
            break;
         default:
            break;
      }

      // Process Dimming if Needed
      if (currentBrightness != targetBrightness) {
      	currentBrightness += dimRate;

      	if (currentBrightness <= targetBrightness && dimRate < 0) {
      	   currentBrightness = targetBrightness;
         } else if (currentBrightness >= targetBrightness && dimRate > 0) {
            currentBrightness = targetBrightness;
         }
      }

      previousMode = currentMode;
   }
}

size_t display_width() {
   return MATRIX_WIDTH;
}

size_t display_height() {
   return MATRIX_HEIGHT;
}

void display_setBrightness(int16_t target, int16_t rate) {
   if (NULL != xCommandQueue) {
      if (target < BRIGHTNESS_MIN) { target = BRIGHTNESS_MIN; }
      else if (target > BRIGHTNESS_MAX) { target = BRIGHTNESS_MAX; }

      uint32_t val = (uint32_t)(((uint16_t) target) << 16);
      val |= (uint16_t)rate;

      display_cmd_t cmd = {
         .command = DISPLAY_BRIGHTNESS,
         .u = val
      };
      xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
   }
}

uint16_t display_getBrightness() {
   return currentBrightness;
}

void display_setPower(bool power) {
   if (NULL != xCommandQueue) {
      display_cmd_t cmd = {
         .command = DISPLAY_POWER,
         .b = power
      };
      xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
   }
}

bool display_getPower() {
   return 1 == gpio_get_level(P_POWER);
}

void display_setMode(display_mode_e mode) {
   if (NULL != xCommandQueue) {
      display_cmd_t cmd = {
         .command = DISPLAY_MODE,
         .u = mode
      };
      xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
   }
}

display_mode_e display_getMode() {
   return currentMode;
}

void display_setRate(uint32_t rate) {
   if (NULL == xCommandQueue)
      return;

   if (RATE_MIN > rate)
      rate = RATE_MIN;

   display_cmd_t cmd = {
      .command = DISPLAY_RATE,
      .u = rate
   };
   xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
}

void display_setColour(uint8_t r, uint8_t g, uint8_t b) {
   if (NULL != xCommandQueue) {
      display_cmd_t cmd = {
         .command = DISPLAY_COLOUR,
         .u = (r << 16) | (g << 8) | b
      };
      xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
   }
}

void display_setAnimation(size_t animation) {
   if (NULL != xCommandQueue) {
      display_cmd_t cmd = {
         .command = DISPLAY_ANIMATION,
         .u = (animation % animation_count)
      };
      xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
   }
}

size_t display_getAnimation() {
   return currentAnimation;
}

void display_setFile(const char *file) {
   if (NULL != xCommandQueue) {
      size_t len = strlen(file) + 1;
      char *f = calloc(1, len);
      if (NULL == f)
         return;

      memcpy(f, file, len);
      display_cmd_t cmd = {
         .command = DISPLAY_FILE,
         .s = f
      };
      xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
   }

}

void display_update() {
   if (NULL != xCommandQueue) {
      display_cmd_t cmd = {
         .command = DISPLAY_UPDATE,
      };
      xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
   }
}

static void _rect(size_t x, size_t y, size_t w, size_t h, uint8_t r, uint8_t g, uint8_t b, bool fill) {
   if (NULL == xCommandQueue)
      return;

   if (fill) {
      display_fill_t *fill = (display_fill_t *)calloc(sizeof(display_fill_t), 1);
      fill->x = x;
      fill->y = y;
      fill->w = w;
      fill->h = h;
      fill->r = r;
      fill->g = g;
      fill->b = b;

      display_cmd_t cmd = {
         .command = DISPLAY_FILL_RECT,
         .p = fill
      };
      xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
   } else {
      // Draw Lines
      size_t coords[4][4] = {
         {x, y, x+w-1, y},
         {x, y, x, y+h-1},
         {x+w-1, y, x+w-1, y+h-1},
         {x, y+h-1, x+w-1, y+h-1}
      };
      
      for (size_t idx = 0; idx < 4; idx++) {
         display_drawLine(coords[idx][0], coords[idx][1], coords[idx][2], coords[idx][3], r, g, b);
      }
   }
}


void display_fillScreen(uint8_t r, uint8_t g, uint8_t b) {
   _rect(0, 0, MATRIX_WIDTH, MATRIX_HEIGHT, r, g, b, true);
}


void display_drawRect(size_t x, size_t y, size_t w, size_t h, uint8_t r, uint8_t g, uint8_t b) {
   _rect(x, y, w, h, r, g, b, false);
}

void display_fillRect(size_t x, size_t y, size_t w, size_t h, uint8_t r, uint8_t g, uint8_t b) {
   _rect(x, y, w, h, r, g, b, true);
}

void display_drawLine(size_t x0, size_t y0, size_t x1, size_t y1, uint8_t r, uint8_t g, uint8_t b) {
   if (NULL == xCommandQueue)
      return;

   display_line_t *line = (display_line_t *)calloc(sizeof(display_line_t), 1);
   line->x0 = x0;
   line->y0 = y0;
   line->x1 = x1;
   line->y1 = y1;
   line->r = r;
   line->g = g;
   line->b = b;

   display_cmd_t cmd = {
      .command = DISPLAY_DRAW_LINE,
      .p = line
   };
   xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
}

void _circle(size_t x0, size_t y0, size_t radius, uint8_t r, uint8_t g, uint8_t b, bool fill) {
   if (NULL == xCommandQueue)
      return;

   display_circle_t *circle = (display_circle_t *)calloc(sizeof(display_circle_t), 1);
   circle->x = x0;
   circle->y = y0;
   circle->radius = radius;
   circle->r = r;
   circle->g = g;
   circle->b = b;

   display_cmd_t cmd = {
      .command = (fill ? DISPLAY_FILL_CIRCLE : DISPLAY_DRAW_CIRCLE),
      .p = circle
   };
   xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
}

void display_drawCircle(size_t x0, size_t y0, size_t radius, uint8_t r, uint8_t g, uint8_t b) {
   _circle(x0, y0, radius, r, g, b, false);
}

void display_fillCircle(size_t x0, size_t y0, size_t radius, uint8_t r, uint8_t g, uint8_t b) {
   _circle(x0, y0, radius, r, g, b, true);
}

void display_setPixel(size_t x, size_t y, uint8_t r, uint8_t g, uint8_t b) {
//display_drawLine(x, y, x, y, r, g, b);
   if (NULL == xCommandQueue)
      return;

   display_pixel_t *pixel = (display_pixel_t *)calloc(sizeof(display_pixel_t), 1);
   pixel->x = x;
   pixel->y = y;
   pixel->r = r;
   pixel->g = g;
   pixel->b = b;

   display_cmd_t cmd = {
      .command = DISPLAY_SET_PIXEL,
      .p = pixel
   };
   xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
}

void display_setFont(GFXfont *font) {
   if (NULL == xCommandQueue)
      return;

   display_cmd_t cmd = {
      .command = DISPLAY_SET_FONT,
      .p = font
   };
   xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
}

void display_print(char *text) {
   if (NULL == xCommandQueue)
      return;

   size_t len = strlen(text);

   display_text_t *print = calloc(1, sizeof(display_text_t));
   
   print->y = 8;
   print->r = 128;
   print->g = 128;
   print->b = 128;
   print->font = currentFont;

   print->text = calloc(1, len+1);
   memcpy(print->text, text, len);

   display_cmd_t cmd = {
      .command = DISPLAY_PRINT,
      .p = print
   };
   xQueueSend( xCommandQueue, &cmd, (TickType_t) 0 );
}

void display_getTextBounds(char *text, size_t x, size_t y, size_t *x1, size_t *y1, size_t *w, size_t *h) {
   if (NULL == x1 || NULL == y1 ||
       NULL == w || NULL == h ||
       NULL == currentFont) {
      return;
   }

   size_t tmpW = 0;
   char *ptr = text;

   *w = 0;
   *h = currentFont->yAdvance;

   *x1 = x;
   *y1 = y;

   printf("\ncurrentFont->yAdvance: %u\n", currentFont->yAdvance);

   while ('\0' != *ptr) {
      uint8_t c = *ptr;
      if ('\n' == c) {
         if (tmpW > *w)
            *w = tmpW;
         *w = 0;
         *h += currentFont->yAdvance;
      }

      if (c >= currentFont->first && c <= currentFont->last) {
         GFXglyph *glyph = NULL;

	 printf("measure %c", c);

         c = c - currentFont->first;
         glyph = &currentFont->glyph[c];

         printf(" offset: %u, width: %u, height: %u, xAdvance: %u, xOFfset: %d, yOffset: %d\n", glyph->bitmapOffset, glyph->width, glyph->height, glyph->xAdvance, glyph->xOffset, glyph->yOffset);

         tmpW += glyph->xAdvance;

         if (*y1 > (y + *h) + glyph->yOffset) {
            *y1 = (y + *h) + glyph->yOffset;
         }

         if (*x1 > (x + *w) + glyph->xOffset) {
            *x1 = (x + *w) + glyph->xOffset;
         }
      }

      ptr++;   // Advance The String Pointer 
   }

   if (tmpW > *w) { *w = tmpW; }
}
