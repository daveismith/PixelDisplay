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

void register_display()
{
	// Register Some Commands
   
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



}
