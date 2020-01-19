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


#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

char *cwd;
static const char *default_path = "/mnt/sd";

/*
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
*/


char *monthNames[] = {
   "Jan",
   "Feb",
   "Mar",
   "Apr",
   "May",
   "Jun",
   "Jul",
   "Aug",
   "Sep",
   "Oct",
   "Nov",
   "Dec"
};

/**
 *
 * -rwxrw-r--    10    root   root 2048    Jan 13 07:11 afile.exe
 * ?UUUGGGOOOS   00  UUUUUU GGGGGG ####    ^-- date stamp and file name are obvious ;-)
 * ^ ^  ^  ^ ^    ^      ^      ^    ^
 * | |  |  | |    |      |      |    \--- File Size
 * | |  |  | |    |      |      \-------- Group Name (for example, Users, Administrators, etc)
 * | |  |  | |    |      \--------------- Owner Acct
 * | |  |  | |    \---------------------- Link count (what constitutes a "link" here varies)
 * | |  |  | \--------------------------- Alternative Access (blank means none defined, anything else varies)
 * | \--\--\----------------------------- Read, Write and Special access modes for [U]ser, [G]roup, and [O]thers (everyone else)
 * \------------------------------------- File type flag
 */
static int ls_func(int argc, char **argv)
{
   DIR *dir;
   struct dirent *dent;

   const char *path = cwd;
   size_t pathLen = strlen(path);

   dir = opendir(path);
   if (NULL != dir) {
      char status[12];
      status[sizeof(status) - 1] = 0;  // Null Terminate
      char size[6];
      char time[13];

      struct tm *timeInfo;

      size_t cnt = 0;
      size_t maxLen = 0;
      while((dent=readdir(dir))!=NULL) {
         size_t fnLen = strlen(dent->d_name);
         if (fnLen > maxLen) { maxLen = fnLen; }
         cnt++;
      }
      rewinddir(dir);

      size_t totalLen = pathLen + maxLen;
      if ('/' != path[pathLen-1]) { totalLen++; }  // Add Room For The Slash

      char *fn = calloc(totalLen + 1, 1);
      if (NULL == fn) {
         closedir(dir);
         return -1;
      }
      memcpy(fn, path, pathLen);
      if ('/' != path[pathLen-1]) { 
         fn[pathLen] = '/'; 
         pathLen++;
      }

      printf("total %zu\n", cnt);
      while((dent=readdir(dir))!=NULL) {
            struct stat fileStat;
            size_t fnLen = strlen(dent->d_name);

            memset(status, '-', sizeof(status) - 2);
            status[sizeof(status) - 2] = ' '; 
            if (DT_DIR == dent->d_type) { status[0] = 'd'; }

            // Handle
            memcpy(&fn[pathLen], dent->d_name, fnLen);
            fn[pathLen+fnLen] = 0;   // Null Terminate

            stat(fn,&fileStat);

            // Format The Size
            float fileSize = fileStat.st_size;
            char indicator = 'B';
            if (fileStat.st_size >= 1073741824)  // GB
            {
               fileSize /= 1073741824;
               indicator = 'G';
            } else if (fileStat.st_size >= 1048576)   //MB
            {
               fileSize /= 1048576;
               indicator = 'M';
            } else if (fileStat.st_size >= 1024) //KB
            {
               fileSize /= 1024;
               indicator = 'K';
            }

            if (indicator == 'B') {
               snprintf(size, sizeof(size), "%4lu%c", fileStat.st_size, indicator);
            } else {
               snprintf(size, sizeof(size), "%2.1f%c", fileSize, indicator);
            }
            
            // Format The Time
            timeInfo = localtime(&fileStat.st_mtime);
            snprintf(time, sizeof(time), "%2u %s %02u:%02u", timeInfo->tm_mday, monthNames[timeInfo->tm_mon], timeInfo->tm_hour, timeInfo->tm_min);


            // Process User Permissions
            if (S_IRUSR & fileStat.st_mode) { status[1] = 'r'; }
            if (S_IWUSR & fileStat.st_mode) { status[2] = 'w'; }
            if (S_IXUSR & fileStat.st_mode) { status[3] = 'x'; }

            // Process Group Permissions
            if (S_IRGRP & fileStat.st_mode) { status[4] = 'r'; }
            if (S_IWGRP & fileStat.st_mode) { status[5] = 'w'; }
            if (S_IXGRP & fileStat.st_mode) { status[6] = 'x'; }

            // Process Others Permissions
            if (S_IROTH & fileStat.st_mode) { status[7] = 'r'; }
            if (S_IWOTH & fileStat.st_mode) { status[8] = 'w'; }
            if (S_IXOTH & fileStat.st_mode) { status[9] = 'x'; }

            printf("%s %2u %2u %2u %6s %s %s\n", status, fileStat.st_nlink, fileStat.st_uid, fileStat.st_gid, size, time, dent->d_name);
      }
      free(fn);
      closedir(dir);
   }

   return 0;
}

char *getcwd(char *buf, size_t size) {
   // Actually Pull cwd
   size_t cwdLen = strlen(cwd);
   if (size < cwdLen) {
           printf("size: %zu, cwdLen: %zu\n", size, cwdLen);
      errno = ERANGE;
      return NULL;
   }

   strcpy(buf, cwd);

   return buf;
}

static int pwd_func(int argc, char **argv)
{
   printf("%s\n", cwd);
   return 0;
}

static struct {
   //struct arg_str *dir;
   struct arg_file *dir;
   struct arg_end *end;
} cd_args;

static int cd_func(int argc, char **argv)
{
   // Options
   //   blank - move to the default path
   //   . - current path
   //   .. - parent directory
   //   <rel_dir> - a relative directory
   //   <abs_dir> - an absoute directory
   
   if (argc < 2) {
      size_t len = strlen(default_path);
      char *tmp = calloc(len+1, 1);
      if (NULL == tmp)
         return -1;
      tmp = strncpy(tmp, default_path, len+1);
      tmp[len] = '\0';
      if (NULL != cwd) { free(cwd); }
      cwd = tmp;
      return 0;
   } else if (strncmp(argv[1], ".", 2) == 0) {
      // Do Nothing as this is a move to the same dir
      return 0;
   }

   // Now Figure Out The New directory
   char *tmp = NULL, *tmp2 = NULL;
   size_t cwdLen = strlen(cwd);
   size_t argLen = strlen(argv[1]);
   size_t newLen = cwdLen + argLen;

   if ('/' != cwd[cwdLen - 1]) {
      newLen++;
   }
   tmp = calloc(newLen+1, 1);
   memcpy(tmp, cwd, cwdLen);
   if ('/' != cwd[cwdLen - 1]) {
      tmp[cwdLen] = '/';
      cwdLen++;
   }
   memcpy(&tmp[cwdLen], argv[1], argLen);
   tmp2 = realpath(tmp, NULL);
   free(tmp);
   tmp = 0;

   if (NULL != tmp2) {
      struct stat fileStat;

      if (0 != stat(tmp2, &fileStat))
      {
         free(tmp2);
         printf("cd: %s: No such file or directory\n", argv[1]);
      } else if (!S_ISDIR(fileStat.st_mode)) 
      {
         free(tmp2);
         printf("cd: %s: Not a directory\n", argv[1]);
      } else {
         if (NULL != cwd) { free(cwd); }
         cwd = tmp2;
      }
   }

   return 0;
}

static struct {
   //struct arg_str *dir;
   struct arg_file *files;
   struct arg_lit *number;
   struct arg_end *end;
} cat_args;

static int cat_func(int argc, char **argv)
{
   int nerrors = arg_parse(argc, argv, (void **) &cat_args);
   if (nerrors != 0) {
      arg_print_errors(stderr, cat_args.end, argv[0]);
      return 1;
   }

   // Run cat (output files)
   // if -n then (6 - line number)(2 spaces)
   bool lineNumbers = (1 == cat_args.number->count);
   printf("argc: %u, lineNumbers: %u, file count: %u\n", argc, lineNumbers, cat_args.files->count);

   // Need To Loop Through

   return 0;
}

void register_fs()
{
   size_t len = strlen(default_path);
   cwd = calloc(len+1, 1);
   if (NULL == cwd)
      return;

   cwd = strncpy(cwd, default_path, len+1);
   cwd[len] = '\0';

   //mount
   //umount
   //cat

   // Register Some Commands
   const esp_console_cmd_t ls_cmd = {
      .command = "ls",
      .help = "list contents of the current directory",
      .hint = NULL,
      .func = &ls_func
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&ls_cmd) );

   const esp_console_cmd_t pwd_cmd = {
      .command = "pwd",
      .help = "print current working directory",
      .hint = NULL,
      .func = &pwd_func
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&pwd_cmd) );

   cd_args.dir = arg_filen(NULL, NULL, "<dir>", 0, 1, "directory name to move to");
   //cd_args.dir = arg_str1(NULL, NULL, "<dir>", "directory name to move to");
   //cd_args.dir->sval[0] = default_path;
   cd_args.end = arg_end(1);

   const esp_console_cmd_t cd_cmd = {
      .command = "cd",
      .help = "Change the shell working directory.",
      .hint = NULL,
      .func = &cd_func,
      .argtable = &cd_args
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&cd_cmd) );

   cat_args.files = arg_filen(NULL, NULL, "<files>", 1, 10, "input files");
   cat_args.number = arg_litn("n", "number", 0, 1, "number all output lines"),
   cat_args.end = arg_end(1);

   const esp_console_cmd_t cat_cmd = {
      .command = "cat",
      .help = "Concatenate FILE(s) to standard output.",
      .hint = NULL,
      .func = &cat_func, 
      .argtable = &cat_args
   };
   ESP_ERROR_CHECK( esp_console_cmd_register(&cat_cmd) );
}
