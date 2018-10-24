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

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "sdkconfig.h"
#include "PxMatrix.h"
#include "esp_spi_flash.h"

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

static EventGroupHandle_t ble_event_group;
static EventGroupHandle_t wifi_event_group;

static xQueueHandle sd_gpio_evt_queue = NULL;

const static int CONNECTED_BIT = BIT0; // Both BLE and WiFi
const static int STARTED_BIT = BIT1;
const static int BLE_ADV_BIT = BIT2;

//#define P_LAT 22 // Old
#define P_LAT 26
//#define P_A 19 // Old
#define P_A 27
//#define P_B 23 // Old
#define P_B 17
//#define P_C 18 // Old
#define P_C 25
#define P_D 5
#define P_E 15
//#define P_OE 2	// Old
#define P_OE 21

#define ESP_INTR_FLAG_DEFAULT 0

const static char *TAG = "PixelDisplay";
static const char *SD_TAG = "SD";


/* store the station info for send back to phone */
static bool gl_sta_connected = false;
static uint8_t gl_sta_bssid[6];
static uint8_t gl_sta_ssid[32];
static int gl_sta_ssid_len;

static int taskCore = 0;
TaskHandle_t xTask1;
TaskHandle_t xTask2;

pxmatrix* display = NULL;

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

size_t currentAnimation = 0;
size_t currentFrame = 0;
size_t frameSize = 1024;


static void _display_timer_cb(void *arg)
{
   static uint8_t cnt = 0;
   pxmatrix *display = (pxmatrix *)arg;
   //pxmatrix_display(display, 15);
   //pxmatrix_display(display, 35);
   pxmatrix_display(display, 70);
}

void draw_anim(pxmatrix *display, size_t animation)
{
   if (animation >= animation_count) {
      animation = animation % animation_count;
   }
   int frames = animation_lengths[animation];
   int frame_offset = 0;
   for (int idx = 0; idx < animation; idx++)
      frame_offset += animation_lengths[idx];

   const uint8_t *ptr = animations + (frame_offset + currentFrame) * frameSize;
   uint16_t val;
   for (size_t yy = 0; yy < 16; yy++)
   {
      for (size_t xx = 0; xx < 32; xx++)
      {
         val = ptr[0] | (ptr[1] << 8);
	 //val &= 0x001f;
	 //val &= 0x07e0;
	 //val &= 
         pxmatrix_drawPixelRGB565(display, xx, yy, val);
         ptr += 2;
      }
   }
   currentFrame++;
   if (currentFrame >= frames)
      currentFrame = 0;
}

//#define DISPLAY_TIMER_PERIOD_US        500
#define DISPLAY_TIMER_PERIOD_US        1000
//#define DISPLAY_TIMER_PERIOD_US        2000

void display_task(void *pvParameter)
{
   esp_timer_handle_t timer_handle;
   display = Create_PxMatrix3(32, 16, P_LAT, P_OE, P_A, P_B, P_C);
   pxmatrix_begin(display, 8);
   pxmatrix_clearDisplay(display);
   pxmatrix_setFastUpdate(display, false);

   // GPIO22
   gpio_pad_select_gpio(GPIO_NUM_22);
   gpio_set_direction(GPIO_NUM_22, GPIO_MODE_OUTPUT);
   gpio_set_level(GPIO_NUM_22, 1);

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
         printf("error starting esp_timer: %d\n", err);
        return;
    }
    esp_timer_start_periodic(timer_handle, DISPLAY_TIMER_PERIOD_US);

   int taskCore = xPortGetCoreID();
   printf("display task running on %d\n", taskCore);
   //pxmatrix_selectBuffer(display, false);
   draw_anim(display, currentAnimation);
   //pxmatrix_selectBuffer(display, true);
   //draw_anim(display, 0);
  
   size_t cnt = 0; 
   while (true) {
      //vTaskDelay(1000 / portTICK_PERIOD_MS);
      //vTaskDelay(200 / portTICK_PERIOD_MS);
      //vTaskDelay(100 / portTICK_PERIOD_MS);
      //vTaskDelay(66 / portTICK_PERIOD_MS);
      vTaskDelay(33 / portTICK_PERIOD_MS);
      
      pxmatrix_swapBuffer(display);
      draw_anim(display, currentAnimation);

      if (currentFrame == 0) {
         currentAnimation++;

         if (currentAnimation > animation_count) {
            currentAnimation = 0;
	 }
      }
      
   }
}

static void IRAM_ATTR sd_gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(sd_gpio_evt_queue, &gpio_num, NULL);
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
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

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
    FILE* f = fopen("/sdcard/hello.txt", "w");
    if (f == NULL) {
        ESP_LOGE(SD_TAG, "Failed to open file for writing");
        return;
    }
    fprintf(f, "Hello %s!\n", card->cid.name);
    fclose(f);
    ESP_LOGI(TAG, "File written");

    // Check if destination file exists before renaming
    struct stat st;
    if (stat("/sdcard/foo.txt", &st) == 0) {
        // Delete it if it exists
        unlink("/sdcard/foo.txt");
    }

    // Rename original file
    ESP_LOGI(SD_TAG, "Renaming file");
    if (rename("/sdcard/hello.txt", "/sdcard/foo.txt") != 0) {
        ESP_LOGE(SD_TAG, "Rename failed");
        return;
    }

    // Open renamed file for reading
    ESP_LOGI(SD_TAG, "Reading file");
    f = fopen("/sdcard/foo.txt", "r");
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
    esp_vfs_fat_sdmmc_unmount();
    ESP_LOGI(SD_TAG, "Card unmounted");
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

/********************************************************************************
 * WiFi Callbacks and Methods
 ********************************************************************************/

esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
   wifi_mode_t mode;
   
   switch (event->event_id) {
   case SYSTEM_EVENT_STA_START:
      ESP_LOGI(TAG, "STA_START");
      xEventGroupSetBits(wifi_event_group, STARTED_BIT);
      ESP_ERROR_CHECK( esp_wifi_connect() );
      break;
   case SYSTEM_EVENT_STA_GOT_IP:
   {
      esp_blufi_extra_info_t info;

      xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
      esp_wifi_get_mode(&mode);

      /*
      tcpip_adapter_ip_info_t sta_ip;
      tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &sta_ip);
      ip4_addr_t ip;
      ip4_addr_t netmask;
      ip4_addr_t gw;
      */

      if ((xEventGroupGetBits(ble_event_group) & CONNECTED_BIT) == CONNECTED_BIT) {
         memset(&info, 0, sizeof(esp_blufi_extra_info_t));
         memcpy(info.sta_bssid, gl_sta_bssid, 6);
         info.sta_bssid_set = true;
         info.sta_ssid = gl_sta_ssid;
         info.sta_ssid_len = gl_sta_ssid_len;
         esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
      }
   }
      break;
   case SYSTEM_EVENT_STA_CONNECTED:
      gl_sta_connected = true;
      memcpy(gl_sta_bssid, event->event_info.connected.bssid, 6);
      memcpy(gl_sta_ssid, event->event_info.connected.ssid, event->event_info.connected.ssid_len);
      gl_sta_ssid_len = event->event_info.connected.ssid_len;
      break;
   case SYSTEM_EVENT_STA_DISCONNECTED:
      /* This is a workaround as ESP32 WiFi libs don't currently
         auto-reassociate. */
      gl_sta_connected = false;
      memset(gl_sta_ssid, 0, 32);
      memset(gl_sta_bssid, 0, 6);
      gl_sta_ssid_len = 0;

      esp_wifi_connect();
      xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
      break;
   case SYSTEM_EVENT_AP_START:
      esp_wifi_get_mode(&mode);

      /* TODO: get config or information of softap, then set to report extra_info */
      if ((xEventGroupGetBits(ble_event_group) & CONNECTED_BIT) == CONNECTED_BIT) {
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
   return ESP_OK;
}

void wifi_conn_init(void)
{
   tcpip_adapter_init();
   wifi_event_group = xEventGroupCreate();
   ESP_ERROR_CHECK( esp_event_loop_init(wifi_event_handler, NULL) );
   wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
   ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
   ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
   ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
   wifi_config_t sta_config = {
      .sta = {
         .ssid = "joshua",
         .password = "woprwopr",
      }
   };
   ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
   ESP_ERROR_CHECK( esp_wifi_start() );
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
        xEventGroupSetBits(ble_event_group, CONNECTED_BIT);
        xEventGroupClearBits(wifi_event_group, BLE_ADV_BIT);
        blufi_security_init();
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        BLUFI_INFO("BLUFI ble disconnect\n");
        blufi_security_deinit();
        xEventGroupClearBits(ble_event_group, CONNECTED_BIT);
        if ((xEventGroupGetBits(wifi_event_group) & CONNECTED_BIT) != CONNECTED_BIT) {
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
        so disconnect wifi before connection.
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

void app_main()
{
   esp_err_t ret;

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

   /*xTaskCreatePinnedToCore(
      display_task, 
      "display_task", 
      4096, 
      NULL, 
      1, 
      &xTask1,
      1);*/


   xTaskCreate(
      &display_task, 
      "display_task", 
      4096, 
      NULL, 
      configMAX_PRIORITIES, 
      &xTask1);

   xTaskCreate(
      &sd_task,
      "sd_task",
      4096,
      NULL,
      configMAX_PRIORITIES - 1,
      &xTask2);

/*   xTaskCreate(
      &adc_task,
      "adc_task",
      2048,
      NULL,
      configMAX_PRIORITIES - 2,
      &xTask2);
*/
   

   wifi_conn_init();
   ble_init();

/*
    for (int i = 10; i >= 0; i--) {
        printf("Restarting in %d seconds...\n", i);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    printf("Restarting now.\n");
    fflush(stdout);
    esp_restart();
*/
}
