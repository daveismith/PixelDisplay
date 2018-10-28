#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

#include "PxMatrix.h"
#include "display.h"
#include "esp_log.h"

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
 * DISPLAY_ANIMATION:
 *   sets the current animation
 * DISPLAY_COLOUR:
 *   indicates the colour to display, sends RGB in 32-bit integer as [R|23-16][G|15-8][B|7-0]
 * DISPLAY_FILE:
 *   indicates the file to display, sends a pointer to a string. if an existing pointer exists,
 *   it will need to be freed
 */

typedef enum {
   DISPLAY_BRIGHTNESS,
   DISPLAY_POWER,
   DISPLAY_MODE,
   DISPLAY_ANIMATION,
   DISPLAY_COLOUR,
   DISPLAY_FILE
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

#define ANIM0
#define ANIM1
#define ANIM2

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
size_t currentAnimation = 0;
size_t currentFrame = 0;
size_t totalFrames = 0;
size_t frameSize = 1024;
uint32_t currentColour = 0;

char *currentFile = NULL;
int currentFd = -1;
file_type_e currentFileType = FILE_TYPE_RGB;

static int16_t currentBrightness = 0;
static int16_t targetBrightness = 0;
static int16_t dimRate = 0;

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
   uint16_t val;
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
         
            //val = (data[0] & 0xf8) << 8;
            //val |= (data[1] & 0xfc) << 3;
            //val |= data[2] >> 3;

            //val = ptr[0] | (ptr[1] << 8);
            //pxmatrix_drawPixelRGB565(display, xx, yy, val);
            pxmatrix_drawPixelRGB888(display, xx, yy, data[0], data[1], data[2]);
            //ptr += 2;
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

//#define DISPLAY_TIMER_PERIOD_US        500
//#define DISPLAY_TIMER_PERIOD_US        1000
#define DISPLAY_TIMER_PERIOD_US        2000

void display_task(void *pvParameter)
{
   esp_timer_handle_t timer_handle;
   display = Create_PxMatrix3(MATRIX_WIDTH, MATRIX_HEIGHT, P_LAT, P_OE, P_A, P_B, P_C);
   pxmatrix_begin(display, CONFIG_DISPLAY_SCAN);
   pxmatrix_clearDisplay(display);
   pxmatrix_setFastUpdate(display, false);
   dimRate = DEFAULT_RATE;
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
      vTaskDelay(33 / portTICK_PERIOD_MS);
      
      // Check For Incoming Commands
      if ( xQueueReceive( xCommandQueue, (void *) &cmd, (TickType_t) 0) ) {
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

      pxmatrix_swapBuffer(display);

      switch (currentMode) {
         case DISPLAY_MODE_ANIMATION:
            draw_anim(display, currentAnimation);
            break;
         case DISPLAY_MODE_COLOUR:
            draw_colour(display, currentColour);
            break;
         case DISPLAY_MODE_FILE:
            // Somehow Draw The Frames
            draw_file(display, currentFile);
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
