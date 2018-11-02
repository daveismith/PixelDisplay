/* Console Display Commands
 */

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "cmd_decl.h"
#include "freertos/FreeRTOS.h"
#include "display.h"

static struct {
   struct arg_int *rate;
   struct arg_end *end;
} rate_args;

static int set_rate(int argc, char **argv)
{
   int nerrors = arg_parse(argc, argv, (void **) &rate_args);
   if (nerrors != 0) {
      arg_print_errors(stderr, rate_args.end, argv[0]);
      return 1;
   }

   uint32_t rate = rate_args.rate->ival[0];
   display_setRate(rate);
   return 0;
}

static struct {
   struct arg_int *brightness;
   struct arg_int *rate;
   struct arg_end *end;
} brightness_args;

static int set_brightness(int argc, char **argv)
{
   int nerrors = arg_parse(argc, argv, (void **) &brightness_args);
   if (nerrors != 0) {
      arg_print_errors(stderr, brightness_args.end, argv[0]);
      return 1;
   }

   int16_t target = brightness_args.brightness->ival[0];
   int16_t rate = brightness_args.rate->ival[0];
   display_setBrightness(target, rate);
   return 0;
}

static int set_display(int argc, char **argv)
{
   if (argc != 2) {
      return 1;
   }

   //
   if (strncmp(argv[1], "on", 3) == 0) {
      display_setPower(true);
   } else if (strncmp(argv[1], "off", 4) == 0) {
      display_setPower(false);
   } else if (strncmp(argv[1], "status", 7) == 0) {
      bool status = display_getPower();
      printf("display status: %s\n", (status) ? "on" : "off");
   }
   return 0;
}

static struct {
   struct arg_int *red;
   struct arg_int *green;
   struct arg_int *blue;
   struct arg_end *end;
} colour_args;

static int set_colour(int argc, char **argv)
{
   int nerrors = arg_parse(argc, argv, (void **) &colour_args);
   if (nerrors != 0) {
      arg_print_errors(stderr, colour_args.end, argv[0]);
      return 1;
   }

   uint8_t r = (uint8_t)colour_args.red->ival[0];
   uint8_t g = (uint8_t)colour_args.green->ival[0];
   uint8_t b = (uint8_t)colour_args.blue->ival[0];

   display_setColour(r, g, b);
   return 0;
}

static int set_mode(int argc, char **argv)
{
   if (argc != 2) {
      return 1;
   }

   if (strncmp(argv[1], "anim", 5) == 0) {
      display_setMode(DISPLAY_MODE_ANIMATION);
   } else if (strncmp(argv[1], "colour", 7) == 0) {
      display_setMode(DISPLAY_MODE_COLOUR);
   } else if (strncmp(argv[1], "file", 5) == 0) {
      display_setMode(DISPLAY_MODE_FILE);
   } else if (strncmp(argv[1], "manual", 7) == 0) {
      display_setMode(DISPLAY_MODE_MANUAL);
   }
   return 0;
}

static struct {
   struct arg_int *animation;
   struct arg_end *end;
} animation_args;

static int set_animation(int argc, char **argv)
{
   int nerrors = arg_parse(argc, argv, (void **) &animation_args);
   if (nerrors != 0) {
      arg_print_errors(stderr, animation_args.end, argv[0]);
      return 1;
   }

   display_setAnimation(animation_args.animation->ival[0]);
   return 0;
}

static struct {
   struct arg_str *file;
   struct arg_end *end;
} file_args;

static int set_file(int argc, char **argv)
{
   int nerrors = arg_parse(argc, argv, (void **) &file_args);
   if (nerrors != 0) {
      arg_print_errors(stderr, file_args.end, argv[0]);
      return 1;
   }

   // Set File
   display_setFile(file_args.file->sval[0]);
   return 0;
}

static struct {
   struct arg_int *red;
   struct arg_int *green;
   struct arg_int *blue;
   struct arg_end *end;
} fill_args;

static int fill_screen(int argc, char **argv)
{
   int nerrors = arg_parse(argc, argv, (void **) &fill_args);
   if (nerrors != 0) {
      arg_print_errors(stderr, fill_args.end, argv[0]);
      return 1;
   }

   uint8_t r = (uint8_t)fill_args.red->ival[0];
   uint8_t g = (uint8_t)fill_args.green->ival[0];
   uint8_t b = (uint8_t)fill_args.blue->ival[0];

   display_fillScreen(r, g, b);
   return 0;
}

static int update(int argc, char **argv)
{
   display_update();
   return 0;
}

static struct {
   struct arg_int *x;
   struct arg_int *y;
   struct arg_int *w;
   struct arg_int *h;
   struct arg_int *red;
   struct arg_int *green;
   struct arg_int *blue;
   struct arg_end *end;
} rect_args;

static int fill_rect(int argc, char **argv)
{
   int nerrors = arg_parse(argc, argv, (void **) &rect_args);
   if (nerrors != 0) {
      arg_print_errors(stderr, rect_args.end, argv[0]);
      return 1;
   }

   size_t x = (size_t)rect_args.x->ival[0];
   size_t y = (size_t)rect_args.y->ival[0];
   size_t w = (size_t)rect_args.w->ival[0];
   size_t h = (size_t)rect_args.h->ival[0];
   uint8_t r = (uint8_t)rect_args.red->ival[0];
   uint8_t g = (uint8_t)rect_args.green->ival[0];
   uint8_t b = (uint8_t)rect_args.blue->ival[0];

   display_fillRect(x, y, w, h, r, g, b);
   return 0;
}

static int draw_rect(int argc, char **argv)
{
   int nerrors = arg_parse(argc, argv, (void **) &rect_args);
   if (nerrors != 0) {
      arg_print_errors(stderr, rect_args.end, argv[0]);
      return 1;
   }

   size_t x = (size_t)rect_args.x->ival[0];
   size_t y = (size_t)rect_args.y->ival[0];
   size_t w = (size_t)rect_args.w->ival[0];
   size_t h = (size_t)rect_args.h->ival[0];
   uint8_t r = (uint8_t)rect_args.red->ival[0];
   uint8_t g = (uint8_t)rect_args.green->ival[0];
   uint8_t b = (uint8_t)rect_args.blue->ival[0];

   display_drawRect(x, y, w, h, r, g, b);
   return 0;
}

static struct {
   struct arg_int *x0;
   struct arg_int *y0;
   struct arg_int *x1;
   struct arg_int *y1;
   struct arg_int *red;
   struct arg_int *green;
   struct arg_int *blue;
   struct arg_end *end;
} line_args;

static int draw_line(int argc, char **argv)
{
   int nerrors = arg_parse(argc, argv, (void **) &line_args);
   if (nerrors != 0) {
      arg_print_errors(stderr, line_args.end, argv[0]);
      return 1;
   }

   size_t x0 = (size_t)line_args.x0->ival[0];
   size_t y0 = (size_t)line_args.y0->ival[0];
   size_t x1 = (size_t)line_args.x1->ival[0];
   size_t y1 = (size_t)line_args.y1->ival[0];
   uint8_t r = (uint8_t)line_args.red->ival[0];
   uint8_t g = (uint8_t)line_args.green->ival[0];
   uint8_t b = (uint8_t)line_args.blue->ival[0];

   display_drawLine(x0, y0, x1, y1, r, g, b);
   return 0;
}

static struct {
   struct arg_int *x;
   struct arg_int *y;
   struct arg_int *radius;
   struct arg_int *red;
   struct arg_int *green;
   struct arg_int *blue;
   struct arg_end *end;
} circle_args;

static int fill_circle(int argc, char **argv)
{
   int nerrors = arg_parse(argc, argv, (void **) &circle_args);
   if (nerrors != 0) {
      arg_print_errors(stderr, circle_args.end, argv[0]);
      return 1;
   }

   size_t x = (size_t)circle_args.x->ival[0];
   size_t y = (size_t)circle_args.y->ival[0];
   size_t radius = (size_t)circle_args.radius->ival[0];
   uint8_t r = (uint8_t)circle_args.red->ival[0];
   uint8_t g = (uint8_t)circle_args.green->ival[0];
   uint8_t b = (uint8_t)circle_args.blue->ival[0];

   display_fillCircle(x, y, radius, r, g, b);
   return 0;
}

static int draw_circle(int argc, char **argv)
{
   int nerrors = arg_parse(argc, argv, (void **) &circle_args);
   if (nerrors != 0) {
      arg_print_errors(stderr, circle_args.end, argv[0]);
      return 1;
   }

   size_t x = (size_t)circle_args.x->ival[0];
   size_t y = (size_t)circle_args.y->ival[0];
   size_t radius = (size_t)circle_args.radius->ival[0];
   uint8_t r = (uint8_t)circle_args.red->ival[0];
   uint8_t g = (uint8_t)circle_args.green->ival[0];
   uint8_t b = (uint8_t)circle_args.blue->ival[0];

   display_drawCircle(x, y, radius, r, g, b);
   return 0;
}

void register_display()
{
	// Register Some Commands
   rate_args.rate = arg_int0(NULL, NULL, "<r>", "frame rate (ms/frame)");
   rate_args.rate->ival[0] = DEFAULT_RATE;
   rate_args.end = arg_end(2);

   const esp_console_cmd_t rate_cmd = {
      .command = "rate",
      .help = "Set Current Frame Rate",
      .hint = NULL,
      .func = &set_rate,
      .argtable = &rate_args
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&rate_cmd) );

   
   brightness_args.brightness = arg_int0(NULL, NULL, "<brightness>", "Brightness Level");
   brightness_args.brightness->ival[0] = 2100;
   brightness_args.rate = arg_int0(NULL, "rate", "<r>", "Dimming Rate");
   brightness_args.rate->ival[0] = 10;
   brightness_args.end = arg_end(2);

   const esp_console_cmd_t brightness_cmd = {
      .command = "brightness",
      .help = "Set Current Display Brightness",
      .hint = NULL,
      .func = &set_brightness,
      .argtable = &brightness_args
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&brightness_cmd) );

   const esp_console_cmd_t display_cmd = {
      .command = "display",
      .help = "Set the display on or off, or query the status",
      .hint = "on | off | status",
      .func = &set_display,
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&display_cmd) );

   colour_args.red = arg_int0(NULL, NULL, "<red>", "red level (0-255)");
   colour_args.green = arg_int0(NULL, NULL, "<green>", "green level (0-255)");
   colour_args.blue = arg_int0(NULL, NULL, "<blue>", "blue level (0-255)");
   colour_args.end = arg_end(3);

   const esp_console_cmd_t colour_cmd = {
      .command = "colour",
      .help = "Set the display colour when in colour mode",
      .hint = NULL,
      .func = &set_colour,
      .argtable = &colour_args
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&colour_cmd) );

   const esp_console_cmd_t mode_cmd = {
      .command = "mode",
      .help = "Set the display mode",
      .hint = "anim | colour | file",
      .func = &set_mode
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&mode_cmd) );

   animation_args.animation = arg_int0(NULL, NULL, "<animation_id>", "animation identifier (0 - ...)");
   animation_args.end = arg_end(1);

   const esp_console_cmd_t animation_cmd = {
      .command = "animation",
      .help = "Set the animiation when in animation mode",
      .hint = NULL,
      .func = &set_animation,
      .argtable = &animation_args
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&animation_cmd) );

   file_args.file = arg_str1(NULL, NULL, "<file>", "filename of animation");
   file_args.end = arg_end(1);
   const esp_console_cmd_t file_cmd = {
      .command = "file",
      .help = "Set the file to play when in file mode",
      .hint = NULL,
      .func = &set_file,
      .argtable = &file_args
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&file_cmd) );


   fill_args.red = arg_int0(NULL, NULL, "<red>", "red level (0-255)");
   fill_args.green = arg_int0(NULL, NULL, "<green>", "green level (0-255)");
   fill_args.blue = arg_int0(NULL, NULL, "<blue>", "blue level (0-255)");
   fill_args.end = arg_end(3);

   const esp_console_cmd_t fill_screen_cmd = {
      .command = "fillScreen",
      .help = "fill the display with the provided colour",
      .hint = NULL,
      .func = &fill_screen,
      .argtable = &fill_args
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&fill_screen_cmd) );

   const esp_console_cmd_t update_cmd = {
      .command = "update",
      .help = "update the display when in manual mode",
      .hint = NULL,
      .func = &update,
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&update_cmd) );


   rect_args.x =  arg_int0(NULL, NULL, "<x>", "x coordinate");
   rect_args.y =  arg_int0(NULL, NULL, "<y>", "y coordinate");
   rect_args.w =  arg_int0(NULL, NULL, "<w>", "width");
   rect_args.h =  arg_int0(NULL, NULL, "<h>", "height");
   rect_args.red = arg_int0(NULL, NULL, "<red>", "red level (0-255)");
   rect_args.green = arg_int0(NULL, NULL, "<green>", "green level (0-255)");
   rect_args.blue = arg_int0(NULL, NULL, "<blue>", "blue level (0-255)");
   rect_args.end = arg_end(7);

   const esp_console_cmd_t fill_rect_cmd = {
      .command = "fillRect",
      .help = "draw a filled rectangle to the display with the provided colour",
      .hint = NULL,
      .func = &fill_rect,
      .argtable = &rect_args
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&fill_rect_cmd) );

   const esp_console_cmd_t draw_rect_cmd = {
      .command = "drawRect",
      .help = "draw a rectangle to the display with the provided colour",
      .hint = NULL,
      .func = &draw_rect,
      .argtable = &rect_args
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&draw_rect_cmd) );


   line_args.x0 =  arg_int0(NULL, NULL, "<x0>", "x0 coordinate");
   line_args.y0 =  arg_int0(NULL, NULL, "<y0>", "y0 coordinate");
   line_args.x1 =  arg_int0(NULL, NULL, "<x1>", "width");
   line_args.y1 =  arg_int0(NULL, NULL, "<y1>", "height");
   line_args.red = arg_int0(NULL, NULL, "<red>", "red level (0-255)");
   line_args.green = arg_int0(NULL, NULL, "<green>", "green level (0-255)");
   line_args.blue = arg_int0(NULL, NULL, "<blue>", "blue level (0-255)");
   line_args.end = arg_end(7);

   const esp_console_cmd_t draw_line_cmd = {
      .command = "drawLine",
      .help = "draw a line to the display with the provided colour",
      .hint = NULL,
      .func = &draw_line,
      .argtable = &line_args
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&draw_line_cmd) );


   circle_args.x =  arg_int0(NULL, NULL, "<x>", "x coordinate");
   circle_args.y =  arg_int0(NULL, NULL, "<y>", "y coordinate");
   circle_args.radius =  arg_int0(NULL, NULL, "<radius>", "radius");
   circle_args.red = arg_int0(NULL, NULL, "<red>", "red level (0-255)");
   circle_args.green = arg_int0(NULL, NULL, "<green>", "green level (0-255)");
   circle_args.blue = arg_int0(NULL, NULL, "<blue>", "blue level (0-255)");
   circle_args.end = arg_end(7);

   const esp_console_cmd_t fill_circle_cmd = {
      .command = "fillCircle",
      .help = "draw a filled circle to the display with the provided colour",
      .hint = NULL,
      .func = &fill_circle,
      .argtable = &circle_args
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&fill_circle_cmd) );

   const esp_console_cmd_t draw_circle_cmd = {
      .command = "drawCircle",
      .help = "draw a circle to the display with the provided colour",
      .hint = NULL,
      .func = &draw_circle,
      .argtable = &circle_args
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&draw_circle_cmd) );
}
