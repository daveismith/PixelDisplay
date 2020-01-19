/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "cmd_decl.h"

#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "sdkconfig.h"
#include "esp_spi_flash.h"

#include "display.h"
#define PROGMEM
#include "Fonts/FreeSans9pt7b.h"
#include "Fonts/Picopixel.h"

#include "esp_blufi_api.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "blufi_example.h"

#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#include <driver/adc.h>

#include <esp_http_server.h>

#include "libesphttpd/httpd.h"
#include "libesphttpd/httpdespfs.h"
#include "libesphttpd/cgiwifi.h"
#include "libesphttpd/cgiflash.h"
#include "libesphttpd/auth.h"
#include "libesphttpd/espfs.h"
#include "libesphttpd/captdns.h"
#include "libesphttpd/webpages-espfs.h"
#include "libesphttpd/cgiwebsocket.h"
#include "libesphttpd/httpd-freertos.h"
#include "libesphttpd/route.h"

#include <cJSON.h>

#include <mdns.h>

#include "lwip/apps/sntp.h"

typedef struct {
   httpd_handle_t httpd;
} app_context_t;

static EventGroupHandle_t ble_event_group;
EventGroupHandle_t wifi_event_group;
static esp_timer_handle_t wifi_timer_handle;
static size_t wifi_retry;

#define WIFI_RETRY_PERIOD 15
#define WIFI_MAX_RETRY_SECONDS 300

static xQueueHandle sd_gpio_evt_queue = NULL;

#define LISTEN_PORT     80u
#define MAX_CONNECTIONS 4u

static char connectionMemory[sizeof(RtosConnType) * MAX_CONNECTIONS];
static HttpdFreertosInstance httpdInstance;

const int IP4_CONNECTED_BIT = BIT0; // Both BLE and WiFi
const int IP6_CONNECTED_BIT = BIT1;
const int STARTED_BIT = BIT2;
const int BLE_ADV_BIT = BIT3;

#define ESP_INTR_FLAG_DEFAULT 0

static const char *HTTP_TAG = "HTTP";
static const char *WIFI_TAG = "WiFi";
static const char *SD_TAG = "SD";


/* store the station info for send back to phone */
static bool gl_sta_connected = false;
static uint8_t gl_sta_bssid[6];
static uint8_t gl_sta_ssid[32];
static int gl_sta_ssid_len;

TaskHandle_t xTask1;
TaskHandle_t xTask2;
TaskHandle_t xTaskBorderRouter ;

static void IRAM_ATTR sd_gpio_isr_handler(void* arg)
{
   uint32_t gpio_num = (uint32_t) arg;
   BaseType_t xHigherPriorityTaskWoken;
     
   /* We have not woken a task at the start of the ISR. */
   xHigherPriorityTaskWoken = pdFALSE;

   xQueueSendFromISR(sd_gpio_evt_queue, &gpio_num, &xHigherPriorityTaskWoken);

   if( xHigherPriorityTaskWoken )
   {
      /* Actual macro used here is port specific. */
     portYIELD_FROM_ISR();
   }
}

void test_sd()
{
    vTaskDelay(500 / portTICK_PERIOD_MS);
    ESP_LOGI(SD_TAG, "Initializing SD card");

    ESP_LOGI(SD_TAG, "Using SDMMC peripheral");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_1;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    //TODO: Need External Pullups?
    gpio_set_pull_mode(15, GPIO_PULLUP_ONLY);   // CMD, needed in 4- and 1- line modes
    gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);    // D0, needed in 4- and 1-line modes
    gpio_set_pull_mode(4, GPIO_PULLUP_ONLY);    // D1, needed in 4-line mode only
    gpio_set_pull_mode(12, GPIO_PULLUP_ONLY);   // D2, needed in 4-line mode only
    gpio_set_pull_mode(13, GPIO_PULLUP_ONLY);   // D3, needed in 4- and 1-line modes

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc_mount is an all-in-one convenience function.
    // Please check its source code and implement error recovery when developing
    // production applications.
    sdmmc_card_t* card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/mnt/sd", &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(SD_TAG, "Failed to mount filesystem. "
                "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(SD_TAG, "Failed to initialize the card (%s). "
                "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);

    // Use POSIX and C standard library functions to work with files.
    // First create a file.
    ESP_LOGI(SD_TAG, "Opening file");
    FILE* f = fopen("/mnt/sd/hello.txt", "w");
    if (f == NULL) {
        ESP_LOGE(SD_TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello %s!\n", card->cid.name);
    fclose(f);
    ESP_LOGI(SD_TAG, "File written");

    // Check if destination file exists before renaming
    struct stat st;
    if (stat("/mnt/sd/foo.txt", &st) == 0) {
        // Delete it if it exists
        unlink("/mnt/sd/foo.txt");
        ESP_LOGI(SD_TAG, "Deleting File");
    }

    // Rename original file
    ESP_LOGI(SD_TAG, "Renaming file");
    if (rename("/mnt/sd/hello.txt", "/mnt/sd/foo.txt") != 0) {
        ESP_LOGE(SD_TAG, "Rename failed");
        return;
    }

    // Open renamed file for reading
    ESP_LOGI(SD_TAG, "Reading file");
    f = fopen("/mnt/sd/foo.txt", "r");
    if (f == NULL) {
        ESP_LOGE(SD_TAG, "Failed to open file for reading");
        return;
    }
    char line[64];
    fgets(line, sizeof(line), f);
    fclose(f);
    // strip newline
    char* pos = strchr(line, '\n');
    if (pos) {
        *pos = '\0';
    }
    ESP_LOGI(SD_TAG, "Read from file: '%s'", line);

    // All done, unmount partition and disable SDMMC or SPI peripheral
    //esp_vfs_fat_sdmmc_unmount();
    //ESP_LOGI(SD_TAG, "Card unmounted");
}

void sd_task(void *pvParameter)
{
    uint32_t io_num;
    int last_level = 0xff;
    // Create The Event Queue For SD Events
    sd_gpio_evt_queue = xQueueCreate(4, sizeof(uint32_t));

    gpio_set_direction(GPIO_NUM_16, GPIO_MODE_INPUT);
    gpio_set_intr_type(GPIO_NUM_16, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add(GPIO_NUM_16, sd_gpio_isr_handler, (void*) GPIO_NUM_16);

    io_num = GPIO_NUM_16;
    xQueueSend(sd_gpio_evt_queue, &io_num, 500 / portTICK_PERIOD_MS);

    for(;;) {
        if(xQueueReceive(sd_gpio_evt_queue, &io_num, portMAX_DELAY) && last_level != gpio_get_level(io_num)) {
	    last_level = gpio_get_level(io_num);
            printf("GPIO[%d] intr, val: %d\n", io_num, last_level);
	    if (GPIO_NUM_16 == io_num && last_level) {
	        test_sd();
	    }
        }
    }

    vTaskDelete(NULL);
}

void adc_task(void *pvParameter)
{
    // GPIO 34 = ADC1_CHANNEL_6
    // GPIO 35 = ADC1_CHANNEL_7

    gpio_set_direction(GPIO_NUM_34, GPIO_MODE_INPUT);
    gpio_set_direction(GPIO_NUM_35, GPIO_MODE_INPUT);

    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6,ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_7,ADC_ATTEN_DB_11);
    

    //int status = 0;
    for(;;) {
	vTaskDelay(1000/portTICK_PERIOD_MS);
    	int val1 = adc1_get_raw(ADC1_CHANNEL_6);
	int val2 = adc1_get_raw(ADC1_CHANNEL_7);

	int gpio1 = gpio_get_level(GPIO_NUM_34);
	int gpio2 = gpio_get_level(GPIO_NUM_35);

	printf("adc1: %u, gpio1: %u, adc2: %u, gpio2: %u\n", val1, gpio1, val2, gpio2);
	//gpio_set_level(GPIO_NUM_22, status);
	//status = (status != 0) ? 0 : 1;
    }

    vTaskDelete(NULL);
}

static void initialize_console()
{
   /* Disable buffering on stdin and stdout */
   setvbuf(stdin, NULL, _IONBF, 0);
   setvbuf(stdout, NULL, _IONBF, 0);

   /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
   esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
   /* Move the caret to the beginning of the next line on '\n' */
   esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);

   /* Install UART Driver for interrupt-driven reads and writes */
   ESP_ERROR_CHECK( uart_driver_install(CONFIG_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0) );

   /* Tell VFS to use UART driver */
   esp_vfs_dev_uart_use_driver(CONFIG_CONSOLE_UART_NUM);

   /* Initialize the console */
   esp_console_config_t console_config = {
      .max_cmdline_args = 9,
      .max_cmdline_length = 256,
#if CONFIG_LOG_COLORS
      .hint_color = atoi(LOG_COLOR_CYAN)
#endif
   };
   ESP_ERROR_CHECK( esp_console_init(&console_config) );

   /* Configure linenoise line completion library */
   /* Enable multiline editing. IF not set, long commands will scroll within
    * single line.
    */
   linenoiseSetMultiLine(1);

   /* Tell linenoise where to get command completion and hints */
   linenoiseSetCompletionCallback(&esp_console_get_completion);
   linenoiseSetHintsCallback((linenoiseHintsCallback*) &esp_console_get_hint);

   /* Set command history size */
   linenoiseHistorySetMaxLen(100);

#if CONFIG_STORE_HISTORY
   linenoiseHistoryLoad(HISTORY_PATH);
#endif
}

void console_task(void *pvParameter)
{
   initialize_console();
   
   esp_console_register_help_command();
   register_display();
   register_fs();
   register_system();
   register_wifi();
   
   const char *prompt = LOG_COLOR_I "esp32> " LOG_RESET_COLOR;

   for (;;) {
      char *line = linenoise(prompt);
      if (line == NULL) { /* Ignore empty line */
         continue;
      }

      /* Add the command to history */
      linenoiseHistoryAdd(line);
#if CONFIG_STORE_HISTORY
      linenoiseHistorySave(HISOTRY_PATH);
#endif

      /* Try to run the command */
      int ret;
      esp_err_t err = esp_console_run(line, &ret);
      if (err == ESP_ERR_NOT_FOUND) {
         printf("Unrecognized command\n");
      } else if (err == ESP_ERR_INVALID_ARG) {
         // command was empty
      } else if (err == ESP_OK && ret != ESP_OK) {
         printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(err));
      } else if (err != ESP_OK) {
          printf("Internal error: %s\n", esp_err_to_name(err));
      }

      /* linenoise allocates line buffer on the heap, so need to free it */
      linenoiseFree(line);
   }
}

/********************************************************************************
 * HTTP Server Callbacks and Methods
 ********************************************************************************/

/* This handler gets the present value of the display mode */
esp_err_t mode_get_handler(httpd_req_t *req)
{
    char outbuf[50];

    /* Respond with the accumulated value */
    snprintf(outbuf, sizeof(outbuf),"%u", display_getMode());
    httpd_resp_send(req, outbuf, strlen(outbuf));
    return ESP_OK;
}

/* This handler sets the value of the mode */
esp_err_t mode_put_handler(httpd_req_t *req)
{
    char buf[10];
    char outbuf[50];
    int  ret;

    /* Read data received in the request */
    ret = httpd_req_recv(req, buf, sizeof(buf));
    if (ret <= 0) {
        return ESP_FAIL;
    }

    buf[ret] = '\0';
    int val = atoi(buf);
    ESP_LOGI(HTTP_TAG, "/mode PUT handler read %d", val);


    display_setMode((display_mode_e) val);

    /* Respond with the reset value */
    snprintf(outbuf, sizeof(outbuf),"%d", val);
    httpd_resp_send(req, outbuf, strlen(outbuf));
    return ESP_OK;
}

httpd_uri_t mode_get = {
    .uri      = "/mode",
    .method   = HTTP_GET,
    .handler  = mode_get_handler
};

httpd_uri_t mode_put = {
    .uri      = "/mode",
    .method   = HTTP_PUT,
    .handler  = mode_put_handler
};

httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    // Start the httpd server
    ESP_LOGI(HTTP_TAG, "Starting server on port: '%d'", config.server_port);
    httpd_handle_t server;

    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(HTTP_TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &mode_get);
        httpd_register_uri_handler(server, &mode_put);
        //httpd_register_uri_handler(server, &adder_post);
        return server;
    }

    ESP_LOGI(HTTP_TAG, "Error starting server!");
    return NULL;
}

void stop_webserver(httpd_handle_t server)
{
    // Stop the httpd server
    httpd_stop(server);
}

/********************************************************************************
 * WiFi Callbacks and Methods
 ********************************************************************************/

esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
   wifi_mode_t mode;
   app_context_t *context = (app_context_t *)ctx;

   switch (event->event_id) {
   case SYSTEM_EVENT_STA_START:
      ESP_LOGI(WIFI_TAG, "STA_START");
      xEventGroupSetBits(wifi_event_group, STARTED_BIT);
      wifi_retry = 0;   // Reset The Retry Counter
      esp_err_t err = esp_wifi_connect();
      if (ESP_ERR_WIFI_SSID == err) {
         ESP_LOGI(WIFI_TAG, "No Station Configuration");
      }
      break;
   case SYSTEM_EVENT_STA_GOT_IP:
   {
      esp_blufi_extra_info_t info;

      xEventGroupSetBits(wifi_event_group, IP4_CONNECTED_BIT);
      wifi_retry = 0;
      esp_wifi_get_mode(&mode);

      /*
      tcpip_adapter_ip_info_t sta_ip;
      tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &sta_ip);
      ip4_addr_t ip;
      ip4_addr_t netmask;
      ip4_addr_t gw;
      */

      if ((xEventGroupGetBits(ble_event_group) & IP4_CONNECTED_BIT) == IP4_CONNECTED_BIT) {
         memset(&info, 0, sizeof(esp_blufi_extra_info_t));
         memcpy(info.sta_bssid, gl_sta_bssid, 6);
         info.sta_bssid_set = true;
         info.sta_ssid = gl_sta_ssid;
         info.sta_ssid_len = gl_sta_ssid_len;
         esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
      }

      //if (context->httpd == NULL) {
      //   context->httpd = start_webserver();
      //}
   }
      break;
   case SYSTEM_EVENT_AP_STA_GOT_IP6:
      xEventGroupSetBits(wifi_event_group, IP6_CONNECTED_BIT);
      //ESP_LOGI(WIFI_TAG, "SYSTEM_EVENT_STA_GOT_IP6");
      //char *ip6 = ip6addr_ntoa(&event->event_info.got_ip6.ip6_info.ip);
      //ESP_LOGI(WIFI_TAG, "IPv6: %s", ip6);
      break;
   case SYSTEM_EVENT_STA_CONNECTED:
      gl_sta_connected = true;
      memcpy(gl_sta_bssid, event->event_info.connected.bssid, 6);
      memcpy(gl_sta_ssid, event->event_info.connected.ssid, event->event_info.connected.ssid_len);
      gl_sta_ssid_len = event->event_info.connected.ssid_len;
      esp_timer_stop( wifi_timer_handle );

      ESP_LOGI(WIFI_TAG, "try to get ipv6");
      tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA);
      break;
   case SYSTEM_EVENT_STA_DISCONNECTED:
      /* This is a workaround as ESP32 WiFi libs don't currently
         auto-reassociate. */
      gl_sta_connected = false;
      memset(gl_sta_ssid, 0, 32);
      memset(gl_sta_bssid, 0, 6);
      gl_sta_ssid_len = 0;

      //if (context->httpd) {
      //   stop_webserver(context->httpd);
      //   context->httpd = NULL;
      //}

      ESP_LOGI(WIFI_TAG, "STA_DISCONNECTED");
      esp_wifi_disconnect();
      xEventGroupClearBits(wifi_event_group, IP4_CONNECTED_BIT | IP6_CONNECTED_BIT);

      size_t idx = (1 << wifi_retry);
      uint64_t timeout = idx * WIFI_RETRY_PERIOD;
      ESP_LOGI(WIFI_TAG, "wifi retry multiplier: %zu, timeout: %"PRIu64"s", idx, timeout);
      if (timeout > WIFI_MAX_RETRY_SECONDS)
         timeout = WIFI_MAX_RETRY_SECONDS;

      esp_timer_start_once( wifi_timer_handle, 1000 * 1000 * timeout);
      break;
   case SYSTEM_EVENT_AP_START:
      esp_wifi_get_mode(&mode);

      /* TODO: get config or information of softap, then set to report extra_info */
      if ((xEventGroupGetBits(ble_event_group) & IP4_CONNECTED_BIT) == IP4_CONNECTED_BIT) {
         if (gl_sta_connected) {  
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, NULL);
         } else {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
         }
      }
      break;
   case SYSTEM_EVENT_SCAN_DONE: {
      uint16_t apCount = 0;
      esp_wifi_scan_get_ap_num(&apCount);
      if (apCount == 0) {
         BLUFI_INFO("Nothing AP found");
         break;
      }
      wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount);
      if (!ap_list) {
         BLUFI_ERROR("malloc error, ap_list is NULL");
         break;
      }
      ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, ap_list));
      esp_blufi_ap_record_t * blufi_ap_list = (esp_blufi_ap_record_t *)malloc(apCount * sizeof(esp_blufi_ap_record_t));
      if (!blufi_ap_list) {
         if (ap_list) {
            free(ap_list);
         }
         BLUFI_ERROR("malloc error, blufi_ap_list is NULL");
         break;
      }
      for (int i = 0; i < apCount; ++i)
      {
         blufi_ap_list[i].rssi = ap_list[i].rssi;
         memcpy(blufi_ap_list[i].ssid, ap_list[i].ssid, sizeof(ap_list[i].ssid));
      }
      esp_blufi_send_wifi_list(apCount, blufi_ap_list);
      esp_wifi_scan_stop();
      free(ap_list);
      free(blufi_ap_list);
      break;
   }
   default:
      break;
   }
   mdns_handle_system_event(context, event);   
   return ESP_OK;
}

static void _wifi_timer_cb(void *arg)
{
   esp_wifi_connect();
   wifi_retry++;
}

void wifi_conn_init(app_context_t *context)
{
   tcpip_adapter_init();
   wifi_event_group = xEventGroupCreate();
   ESP_ERROR_CHECK( esp_event_loop_init(wifi_event_handler, context) );
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
   ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_FLASH) );
   ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
   /*wifi_config_t sta_config = {
      .sta = {
         .ssid = "joshua",
         .password = "woprwopr",
      }
   };
   ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );*/

   // Configure The Timer
   esp_timer_create_args_t timer_conf = {
      .callback = _wifi_timer_cb,
	   .arg = NULL,
	   .dispatch_method = ESP_TIMER_TASK,
	   .name = "wifi_timer"
   };
   ESP_ERROR_CHECK( esp_timer_create(&timer_conf, &wifi_timer_handle) );
   ESP_ERROR_CHECK( esp_wifi_start() );

   ESP_LOGI( WIFI_TAG, "Initializing SNTP");
   sntp_setoperatingmode( SNTP_OPMODE_POLL );
   sntp_setservername(0, "time.google.com");
   sntp_init();

   setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0", 1);
   tzset();
}



/********************************************************************************
 * BLE Callbacks and Methods
 ********************************************************************************/
static void example_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);

#define BLUFI_DEVICE_NAME            "MMB_DEVICE"
static uint8_t example_service_uuid128[32] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    //first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
};

#define TEST_MANUFACTURER_DATA_LEN 4
static uint8_t test_manufacturer[TEST_MANUFACTURER_DATA_LEN] =  {0x12, 0x23, 0x45, 0x56};
static esp_ble_adv_data_t example_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x100,
    .max_interval = 0x100,
    .appearance = 0x00,
    .manufacturer_len = TEST_MANUFACTURER_DATA_LEN,
    .p_manufacturer_data =  test_manufacturer,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = 16,
    .p_service_uuid = example_service_uuid128,
    .flag = 0x6,
};

static esp_ble_adv_params_t example_adv_params = {
    .adv_int_min        = 0x100,
    .adv_int_max        = 0x100,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    //.peer_addr            =
    //.peer_addr_type       =
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

#define WIFI_LIST_NUM   10

static wifi_config_t sta_config;
static wifi_config_t ap_config;

static esp_blufi_callbacks_t example_callbacks = {
    .event_cb = example_event_callback,
    .negotiate_data_handler = blufi_dh_negotiate_data_handler,
    .encrypt_func = blufi_aes_encrypt,
    .decrypt_func = blufi_aes_decrypt,
    .checksum_func = blufi_crc_checksum,
};

/* connect infor*/
static uint8_t server_if;
static uint16_t conn_id;

static void example_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    /* actually, should post to blufi_task handle the procedure,
     * now, as a example, we do it more simply */
    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        BLUFI_INFO("BLUFI init finish\n");

        esp_ble_gap_set_device_name(BLUFI_DEVICE_NAME);
        esp_ble_gap_config_adv_data(&example_adv_data);
        break;
    case ESP_BLUFI_EVENT_DEINIT_FINISH:
        BLUFI_INFO("BLUFI deinit finish\n");
        break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        BLUFI_INFO("BLUFI ble connect\n");
        server_if = param->connect.server_if;
        conn_id = param->connect.conn_id;
        esp_ble_gap_stop_advertising();
        xEventGroupSetBits(ble_event_group, IP4_CONNECTED_BIT);
        xEventGroupClearBits(wifi_event_group, BLE_ADV_BIT);
        blufi_security_init();
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        BLUFI_INFO("BLUFI ble disconnect\n");
        blufi_security_deinit();
        xEventGroupClearBits(ble_event_group, IP4_CONNECTED_BIT);
        if ((xEventGroupGetBits(wifi_event_group) & IP4_CONNECTED_BIT) != IP4_CONNECTED_BIT) {
          esp_ble_gap_start_advertising(&example_adv_params);
          xEventGroupSetBits(wifi_event_group, BLE_ADV_BIT);
        }
        break;
    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        BLUFI_INFO("BLUFI Set WIFI opmode %d\n", param->wifi_mode.op_mode);
        ESP_ERROR_CHECK( esp_wifi_set_mode(param->wifi_mode.op_mode) );
        break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        BLUFI_INFO("BLUFI requset wifi connect to AP\n");
        /* there is no wifi callback when the device has already connected to this wifi
        so }disconnect wifi before connection.
        */
        esp_wifi_disconnect();
        esp_wifi_connect();
        break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        BLUFI_INFO("BLUFI requset wifi disconnect from AP\n");
        esp_wifi_disconnect();
        break;
    case ESP_BLUFI_EVENT_REPORT_ERROR:
        BLUFI_ERROR("BLUFI report error, error code %d\n", param->report_error.state);
        esp_blufi_send_error_info(param->report_error.state);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
        wifi_mode_t mode;
        esp_blufi_extra_info_t info;

        esp_wifi_get_mode(&mode);

        if (gl_sta_connected ) {  
            memset(&info, 0, sizeof(esp_blufi_extra_info_t));
            memcpy(info.sta_bssid, gl_sta_bssid, 6);
            info.sta_bssid_set = true;
            info.sta_ssid = gl_sta_ssid;
            info.sta_ssid_len = gl_sta_ssid_len;
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
        } else {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
        }
        BLUFI_INFO("BLUFI get wifi status from AP\n");

        break;
    }
    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        BLUFI_INFO("blufi close a gatt connection");
        esp_blufi_close(server_if, conn_id);
        break;
    case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
        /* TODO */
        break;
	case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        memcpy(sta_config.sta.bssid, param->sta_bssid.bssid, 6);
        sta_config.sta.bssid_set = 1;
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        BLUFI_INFO("Recv STA BSSID %s\n", sta_config.sta.ssid);
        break;
	case ESP_BLUFI_EVENT_RECV_STA_SSID:
        strncpy((char *)sta_config.sta.ssid, (char *)param->sta_ssid.ssid, param->sta_ssid.ssid_len);
        sta_config.sta.ssid[param->sta_ssid.ssid_len] = '\0';
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        BLUFI_INFO("Recv STA SSID %s\n", sta_config.sta.ssid);
        break;
	case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        strncpy((char *)sta_config.sta.password, (char *)param->sta_passwd.passwd, param->sta_passwd.passwd_len);
        sta_config.sta.password[param->sta_passwd.passwd_len] = '\0';
        esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        BLUFI_INFO("Recv STA PASSWORD %s\n", sta_config.sta.password);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_SSID:
        strncpy((char *)ap_config.ap.ssid, (char *)param->softap_ssid.ssid, param->softap_ssid.ssid_len);
        ap_config.ap.ssid[param->softap_ssid.ssid_len] = '\0';
        ap_config.ap.ssid_len = param->softap_ssid.ssid_len;
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        BLUFI_INFO("Recv SOFTAP SSID %s, ssid len %d\n", ap_config.ap.ssid, ap_config.ap.ssid_len);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_PASSWD:
        strncpy((char *)ap_config.ap.password, (char *)param->softap_passwd.passwd, param->softap_passwd.passwd_len);
        ap_config.ap.password[param->softap_passwd.passwd_len] = '\0';
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        BLUFI_INFO("Recv SOFTAP PASSWORD %s len = %d\n", ap_config.ap.password, param->softap_passwd.passwd_len);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_MAX_CONN_NUM:
        if (param->softap_max_conn_num.max_conn_num > 4) {
            return;
        }
        ap_config.ap.max_connection = param->softap_max_conn_num.max_conn_num;
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        BLUFI_INFO("Recv SOFTAP MAX CONN NUM %d\n", ap_config.ap.max_connection);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_AUTH_MODE:
        if (param->softap_auth_mode.auth_mode >= WIFI_AUTH_MAX) {
            return;
        }
        ap_config.ap.authmode = param->softap_auth_mode.auth_mode;
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        BLUFI_INFO("Recv SOFTAP AUTH MODE %d\n", ap_config.ap.authmode);
        break;
	case ESP_BLUFI_EVENT_RECV_SOFTAP_CHANNEL:
        if (param->softap_channel.channel > 13) {
            return;
        }
        ap_config.ap.channel = param->softap_channel.channel;
        esp_wifi_set_config(WIFI_IF_AP, &ap_config);
        BLUFI_INFO("Recv SOFTAP CHANNEL %d\n", ap_config.ap.channel);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_LIST:{
        wifi_scan_config_t scanConf = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = false
        };
        ESP_ERROR_CHECK(esp_wifi_disconnect() );
        ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, true));
        break;
    }
    case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
        BLUFI_INFO("Recv Custom Data %d\n", param->custom_data.data_len);
        esp_log_buffer_hex("Custom Data", param->custom_data.data, param->custom_data.data_len);
        break;
	case ESP_BLUFI_EVENT_RECV_USERNAME:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CA_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CLIENT_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_SERVER_CERT:
        /* Not handle currently */
        break;
	case ESP_BLUFI_EVENT_RECV_CLIENT_PRIV_KEY:
        /* Not handle currently */
        break;;
	case ESP_BLUFI_EVENT_RECV_SERVER_PRIV_KEY:
        /* Not handle currently */
        break;
    default:
        break;
    }
}

static void example_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&example_adv_params);
        xEventGroupSetBits(wifi_event_group, BLE_ADV_BIT);
        break;
    default:
        break;
    }
}

void ble_init(void)
{
   esp_err_t ret;

   ble_event_group = xEventGroupCreate();
   ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

   esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
   ret = esp_bt_controller_init(&bt_cfg);
   if (ret) {
      BLUFI_ERROR("%s initialize bt controller failed: %s\n", __func__, esp_err_to_name(ret));
   }

   ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
   if (ret) {
      BLUFI_ERROR("%s enable bt controller failed: %s\n", __func__, esp_err_to_name(ret));
      return;
   }

   ret = esp_bluedroid_init();
   if (ret) {
      BLUFI_ERROR("%s init bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
      return;
   }

   ret = esp_bluedroid_enable();
   if (ret) {
      BLUFI_ERROR("%s init bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
      return;
   }

   BLUFI_INFO("BD ADDR: "ESP_BD_ADDR_STR"\n", ESP_BD_ADDR_HEX(esp_bt_dev_get_address()));

   BLUFI_INFO("BLUFI VERSION %04x\n", esp_blufi_get_version());

   ret = esp_ble_gap_register_callback(example_gap_event_handler);
   if(ret){
      BLUFI_ERROR("%s gap register failed, error code = %x\n", __func__, ret);
      return;
   }

   ret = esp_blufi_register_callbacks(&example_callbacks);
   if(ret){
      BLUFI_ERROR("%s blufi register failed, error code = %x\n", __func__, ret);
      return;
   }

   esp_blufi_profile_init();
}

/********************************************************************************
 * Main Function
 ********************************************************************************/
//On reception of a message, echo it back verbatim
void myEchoWebsocketRecv(Websock *ws, char *data, int len, int flags) {
	printf("EchoWs: echo, len=%d\n", len);
	cgiWebsocketSend(&httpdInstance.httpdInstance,
	                 ws, data, len, flags);
}

//Echo websocket connected. Install reception handler.
void myEchoWebsocketConnect(Websock *ws) {
	printf("EchoWs: connect\n");
	ws->recvCb=myEchoWebsocketRecv;
}

//Broadcast the uptime in seconds every second over connected websockets
static void websocketBcast(void *arg) {
	static int ctr=0;
	char buff[128];
	while(1) {
		ctr++;
		sprintf(buff, "Up for %d minutes %d seconds!\n", ctr/60, ctr%60);
		cgiWebsockBroadcast(&httpdInstance.httpdInstance,
		                    "/websocket/ws.cgi", buff, strlen(buff),
		                    WEBSOCK_FLAG_NONE);

		vTaskDelay(1000/portTICK_RATE_MS);
	}
}

//On reception of a message, send "You sent: " plus whatever the other side sent
static void myApiRecv(Websock *ws, char *data, int len, int flags) {
   int status = 0;
   int sequence = -1;
   //cgiWebsocketSend(&httpdInstance.httpdInstance,
   //                 ws, buff, strlen(buff), WEBSOCK_FLAG_NONE);


   // Let's Parse This
   cJSON *cmd = cJSON_Parse(data);
   if (NULL == cmd) {
      status = -1;
      goto finish;
   }

   const cJSON *item = cJSON_GetObjectItemCaseSensitive(cmd, "command");
   if (!cJSON_IsString(item) || (item->valuestring == NULL))
   {
      status = -2;
      goto finish;
   }

   const cJSON *sequenceJson = cJSON_GetObjectItemCaseSensitive(cmd, "sequence");
   if (!cJSON_IsNumber(sequenceJson) || sequenceJson->valueint < 0)
   {
      status = -15;
      goto finish;
   } else {
      sequence = sequenceJson->valueint;
   }

   // Process The Command
   if (strncmp(item->valuestring, "display", 8) == 0) {
      // Turn The Display On / Off
      // Set Up The Brightness & Rate

      const cJSON *pwrJson = cJSON_GetObjectItemCaseSensitive(cmd, "power");
      if (!cJSON_IsBool(pwrJson))
      {
         status = -3;
         goto finish;
      }
      bool power = cJSON_IsTrue(pwrJson);

      const cJSON *brightnessJson = cJSON_GetObjectItemCaseSensitive(cmd, "brightness");
      if (!cJSON_IsNumber(brightnessJson) || 
          BRIGHTNESS_MIN > brightnessJson->valueint || 
          BRIGHTNESS_MAX < brightnessJson->valueint) 
      {
         status = -4;
         goto finish;
      }
      int brightness = brightnessJson->valueint;

      const cJSON *rateJson = cJSON_GetObjectItemCaseSensitive(cmd, "rate");
      if (!cJSON_IsNumber(rateJson) ||
          DIM_RATE_MIN > rateJson->valueint ||
          DIM_RATE_MAX < rateJson->valueint) {
         status = -5;
         goto finish;
      }
      int rate = rateJson->valueint;

      // Actually Issue The Command
      display_setPower(power);
      display_setBrightness(brightness, rate);
   } else if (strncmp(item->valuestring, "animation", 10) == 0) {
      // Set Animation Mode + Number
      const cJSON *animationJson = cJSON_GetObjectItemCaseSensitive(cmd, "animation");
      if (!cJSON_IsNumber(animationJson) || 
          0 > animationJson->valueint) 
      {
         status = -6;
         goto finish;
      }
      display_setAnimation(animationJson->valueint);
      display_setMode(DISPLAY_MODE_ANIMATION);
   } else if (strncmp(item->valuestring, "colour", 7) == 0) {
      // Set Colour Mode + Colour
      const cJSON *colourJson = cJSON_GetObjectItemCaseSensitive(cmd, "colour");
      if (!cJSON_IsString(colourJson) || 
          6 != strlen(colourJson->valuestring))
      {
         status = -7;
         goto finish;
      }

      // check that all of the characters are allowed
      const char *end;
      long int colour = strtol(colourJson->valuestring, (char **)&end, 16); 
      if (end != colourJson->valuestring + 6) {
         status = -8;
         goto finish;
      }

      uint8_t r = (uint8_t)(colour >> 16);
      uint8_t g = (uint8_t)(colour >> 8);
      uint8_t b = (uint8_t)(colour);

      display_setColour(r, g, b);
      display_setMode(DISPLAY_MODE_COLOUR);
   } else if (strncmp(item->valuestring, "file", 5) == 0) {
      // Set File Mode + File Name
      const cJSON *fileJson = cJSON_GetObjectItemCaseSensitive(cmd, "file");
      if (!cJSON_IsString(fileJson) || 
          0 >= strlen(fileJson->valuestring)) 
      {
         status = -9;
         goto finish;
      }
      display_setFile(fileJson->valuestring);
      display_setMode(DISPLAY_MODE_FILE);
   } else if (strncmp(item->valuestring, "mode", 5) == 0) {
      const cJSON *modeJson = cJSON_GetObjectItemCaseSensitive(cmd, "mode");
      if (!cJSON_IsNumber(modeJson) || 
          0 > modeJson->valueint ||
	  DISPLAY_MODE_END <= modeJson->valueint)

      {
         status = -10;
         goto finish;
      }
      display_setMode((display_mode_e) modeJson->valueint);
   } else if (strncmp(item->valuestring, "setPixel",9) == 0) {
      // Set Colour Mode + Colour
      const cJSON *colourJson = cJSON_GetObjectItemCaseSensitive(cmd, "colour");
      if (!cJSON_IsString(colourJson) || 
          6 != strlen(colourJson->valuestring))
      {
         status = -11;
         goto finish;
      }

      const cJSON *xJson = cJSON_GetObjectItemCaseSensitive(cmd, "x");
      if (!cJSON_IsNumber(xJson) || 
          0 > xJson->valueint ||
	  32 <= xJson->valueint)

      {
         status = -12;
         goto finish;
      }

      const cJSON *yJson = cJSON_GetObjectItemCaseSensitive(cmd, "y");
      if (!cJSON_IsNumber(yJson) || 
          0 > yJson->valueint ||
	  16 <= yJson->valueint)

      {
         status = -13;
         goto finish;
      }

      // check that all of the characters are allowed
      const char *end;
      long int colour = strtol(colourJson->valuestring, (char **)&end, 16); 
      if (end != colourJson->valuestring + 6) {
         status = -14;
         goto finish;
      }

      uint8_t r = (uint8_t)(colour >> 16);
      uint8_t g = (uint8_t)(colour >> 8);
      uint8_t b = (uint8_t)(colour);

      //display_setColour(r, g, b);
      //display_setMode(DISPLAY_MODE_COLOUR);
      display_setPixel(xJson->valueint, yJson->valueint, r, g, b);
      display_update();
   } else if (strncmp(item->valuestring, "getFrame", 9) == 0) {
      const cJSON *fileJson = cJSON_GetObjectItemCaseSensitive(cmd, "file");
      if (!cJSON_IsString(fileJson) || 
          0 >= strlen(fileJson->valuestring)) 
      {
         status = -15;
         goto finish;
      }

      const cJSON *frameJson = cJSON_GetObjectItemCaseSensitive(cmd, "animation");
      if (!cJSON_IsNumber(frameJson) || 
          0 > frameJson->valueint) 
      {
         status = -16;
         goto finish;
      }

      // Actually Pull The Frame Pixels
   } else if (strncmp(item->valuestring, "setFont", 8) == 0) {
      size_t x1, y1, w, h;
      char *text = "hello";
      display_setFont(&Picopixel);
      //display_setFont(&FreeSans9pt7b);  


      display_getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
      printf("x1: %zu, y1: %zu, w: %zu, h: %zu\r\n", x1, y1, w, h);
   } else if (strncmp(item->valuestring, "print", 6) == 0) {
      const cJSON *textJson = cJSON_GetObjectItemCaseSensitive(cmd, "text");
      if (!cJSON_IsString(textJson) || 
          0 >= strlen(textJson->valuestring)) 
      {
         status = -16;
         goto finish;
      }

      display_print(textJson->valuestring);
      display_update();
   }

finish:
   if (NULL != cmd) {
      cJSON_Delete(cmd);
   }

   cJSON *result = cJSON_CreateObject();
   cJSON_AddNumberToObject(result, "status", status);
   cJSON_AddNumberToObject(result, "sequence", sequence);
   char *sResult = cJSON_PrintUnformatted(result);
   cJSON_Delete(result);
   cgiWebsocketSend(&httpdInstance.httpdInstance,
	                 ws, sResult, strlen(sResult), WEBSOCK_FLAG_NONE);
   free(sResult);
}

//Websocket connected. Install reception handler and send welcome message.
static void myApiConnect(Websock *ws) {
	ws->recvCb=myApiRecv;
	cgiWebsocketSend(&httpdInstance.httpdInstance,
	                 ws, "{\"status\": 0}", 14, WEBSOCK_FLAG_NONE);
}

HttpdBuiltInUrl builtInUrls[]={
   ROUTE_REDIRECT("/", "/index.html"),
	ROUTE_CGI("/flash/reboot", cgiRebootFirmware),
   //	{"/wifi/*", authBasic, myPassFn},
   //ROUTE_WS("/websocket/ws.cgi", myWebsocketConnect),
   ROUTE_WS("/api", myApiConnect),
	ROUTE_FILESYSTEM(),
	ROUTE_END()
};

void start_mdns_service() 
{
   esp_err_t err = mdns_init();
   if (err) {
        printf("MDNS Init failed: %d\n", err);
        return;
   }

   ESP_ERROR_CHECK( mdns_hostname_set("my-esp32") );
   ESP_ERROR_CHECK( mdns_instance_name_set("David's ESP32 Thing") );
   ESP_ERROR_CHECK( mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0) );
}

void app_main()
{
   esp_err_t ret;
   static app_context_t context;

   context.httpd = NULL;

   ret = nvs_flash_init();
   if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
      ESP_ERROR_CHECK( nvs_flash_erase() );
      ret = nvs_flash_init();
   }
   ESP_ERROR_CHECK( ret );

   // Configure The GPIO Stuff
   gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

   // Print chip information
   esp_chip_info_t chip_info;
   esp_chip_info(&chip_info);
   printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
            chip_info.cores,
            (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
            (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

   printf("silicon revision %d, ", chip_info.revision);

   printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
            (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

   //wifi_conn_init();

   xTaskCreatePinnedToCore(
      display_task, 
      "display_task", 
      4096, 
      NULL, 
      configMAX_PRIORITIES - 4, 
      &xTask1,
      1);

   xTaskCreate(
      &sd_task,
      "sd_task",
      4096,
      NULL,
      configMAX_PRIORITIES - 5,
      &xTask2);

/*   xTaskCreate(
      &adc_task,
      "adc_task",
      2048,
      NULL,
      configMAX_PRIORITIES - 6,
      &xTask2);
*/
   
   xTaskCreate(
      &console_task,
      "console_task",
      4096,
      NULL,
      configMAX_PRIORITIES - 6,
      &xTaskBorderRouter);


   wifi_conn_init(&context);
   
   espFsInit((void*)(webpages_espfs_start));
   httpdFreertosInit(&httpdInstance,
                   builtInUrls,
                   LISTEN_PORT,
                   connectionMemory,
                   MAX_CONNECTIONS,
                   HTTPD_FLAG_NONE);
   httpdFreertosStart(&httpdInstance);

   ble_init();
   start_mdns_service();

   display_setFont(&Picopixel);
   //xTaskCreate(websocketBcast, "wsbcast", 3000, NULL, 3, NULL);
   
}
