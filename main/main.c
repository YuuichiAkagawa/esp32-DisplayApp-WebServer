// Copyright (c) 2017 Yuuichi Akagawa
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
/////////////////////////////////////////////////////////////////////////////
//Http Server code from https://github.com/igrr/esp32-cam-demo
//
// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/portmacro.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "tcpip_adapter.h"
#include "soc/uart_struct.h"
#include "lwip/err.h"
#include "string.h"

static const char* TAG = "app";

/*
 * WiFi AP settings
 */
#define WIFI_SSID     "GR-LYCHEE"
#define WIFI_PASSWORD "gadgetrenesas"
#define ADD_MACADDR_TO_SSID (1)

/*
 * HTTP server variables
 */
static EventGroupHandle_t wifi_event_group;
const static char http_hdr[] = "HTTP/1.1 200 OK\r\n";

const static char http_stream_hdr[] = 
        "Content-type: multipart/x-mixed-replace; boundary=123456789000000000000987654321\r\n\r\n";
const static char http_jpg_hdr[] =
        "Content-type: image/jpg\r\n\r\n";
const static char http_pgm_hdr[] =
        "Content-type: image/x-portable-graymap\r\n\r\n";
const static char http_stream_boundary[] = "--123456789000000000000987654321\r\n";

/*
 * UART variables
 */
#if 1
//UART0
#define EX_UART_NUM UART_NUM_0
#define EX_UART_TX_PIN	UART_PIN_NO_CHANGE
#define EX_UART_RX_PIN	UART_PIN_NO_CHANGE
#define EX_UART_RTS_PIN	UART_PIN_NO_CHANGE
#define EX_UART_CTS_PIN	UART_PIN_NO_CHANGE
#else
//UART1 
#define EX_UART_NUM UART_NUM_1
#define EX_UART_TX_PIN	17
#define EX_UART_RX_PIN	16
#define EX_UART_RTS_PIN	14
#define EX_UART_CTS_PIN	15
#endif

#define BUF_SIZE (1024)
#define UART_BUF_SIZE (10240)
#define JPEG_BUF_SIZE (15360)

static QueueHandle_t uart_queue;

static uint8_t data[UART_BUF_SIZE];
//triple buffer
static uint8_t jpeg_data0[JPEG_BUF_SIZE];
static uint8_t jpeg_data1[JPEG_BUF_SIZE];
static uint8_t jpeg_data2[JPEG_BUF_SIZE];
static uint8_t* jpeg_data_r;
static uint8_t* jpeg_data_w;
static uint8_t  jpeg_data_w_side = 0; //write side 0:data1, 1:data1
static int      jpeg_data_r_size;
static int      jpeg_data_done = 1;

//initial image(waiting...)
const static char dummy_jpeg_data[] = {
0xff, 0xd8, 0xff, 0xe1, 0x00, 0x18, 0x45, 0x78, 0x69, 0x66, 0x00, 0x00, 0x49, 0x49, 0x2a, 0x00,
0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xec, 0x00, 0x11,
0x44, 0x75, 0x63, 0x6b, 0x79, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
0xee, 0x00, 0x0e, 0x41, 0x64, 0x6f, 0x62, 0x65, 0x00, 0x64, 0xc0, 0x00, 0x00, 0x00, 0x01, 0xff,
0xdb, 0x00, 0x84, 0x00, 0x1b, 0x1a, 0x1a, 0x29, 0x1d, 0x29, 0x41, 0x26, 0x26, 0x41, 0x42, 0x2f,
0x2f, 0x2f, 0x42, 0x47, 0x3f, 0x3e, 0x3e, 0x3f, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47,
0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47,
0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47,
0x47, 0x47, 0x47, 0x47, 0x01, 0x1d, 0x29, 0x29, 0x34, 0x26, 0x34, 0x3f, 0x28, 0x28, 0x3f, 0x47,
0x3f, 0x35, 0x3f, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47,
0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47,
0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47,
0x47, 0x47, 0x47, 0x47, 0x47, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0xf0, 0x01, 0x40, 0x03, 0x01,
0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01, 0xff, 0xc4, 0x00, 0x65, 0x00, 0x01, 0x00, 0x02,
0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x05,
0x01, 0x02, 0x03, 0x06, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x01, 0x00, 0x02, 0x02, 0x01, 0x02, 0x05, 0x04, 0x02,
0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x11, 0x12, 0x05, 0x21,
0x31, 0x41, 0x32, 0x13, 0x71, 0x81, 0x22, 0x42, 0x51, 0x33, 0x23, 0x43, 0x14, 0x11, 0x01, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff,
0xda, 0x00, 0x0c, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00, 0xf3, 0x20, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0,
0xd5, 0xd6, 0xa6, 0x5a, 0x4d, 0xad, 0x3c, 0x4c, 0x02, 0xbc, 0x4f, 0xd7, 0xd2, 0xf9, 0xfa, 0xa6,
0x27, 0xda, 0x87, 0xd1, 0xf9, 0x74, 0xc8, 0x34, 0x13, 0xb6, 0xf5, 0xa9, 0x86, 0xb1, 0x35, 0x9e,
0x79, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x65, 0xdb, 0x70, 0xd3, 0x35, 0xa6, 0xb7, 0x56,
0xa6, 0x68, 0x64, 0xf8, 0xf2, 0xc4, 0x83, 0x68, 0xd6, 0xe7, 0x3f, 0xc7, 0xe9, 0xca, 0x5f, 0x73,
0xd2, 0xae, 0x08, 0x89, 0xa7, 0x92, 0xc6, 0xd8, 0x62, 0x99, 0x6d, 0x9b, 0xd3, 0x8e, 0x5c, 0x6b,
0x6f, 0xfb, 0x31, 0x4c, 0x7a, 0xc4, 0x82, 0x1f, 0xfc, 0xb4, 0xa6, 0xb7, 0xc9, 0x6f, 0x74, 0xb7,
0xc3, 0x93, 0x56, 0x95, 0x8e, 0xa8, 0xe6, 0xce, 0x9d, 0xc3, 0xc2, 0xb4, 0xc3, 0x0d, 0xed, 0x87,
0x06, 0x95, 0x22, 0x72, 0x47, 0x55, 0xa4, 0x18, 0xbe, 0xb6, 0x1d, 0x9c, 0x73, 0x7c, 0x51, 0xc4,
0xc2, 0x36, 0x8e, 0xb5, 0x6f, 0x4b, 0xf5, 0x7e, 0xab, 0x4d, 0x4d, 0x8a, 0x66, 0xa5, 0xfa, 0x2b,
0xd3, 0xe0, 0x89, 0xdb, 0xfd, 0x99, 0x01, 0x23, 0xb5, 0xce, 0x38, 0xa5, 0xa3, 0x8f, 0x18, 0xf3,
0x57, 0x6c, 0x5f, 0x5e, 0xd3, 0xc5, 0x23, 0xf2, 0xe5, 0x23, 0xb6, 0x79, 0x64, 0x53, 0x7f, 0xb7,
0xee, 0x0b, 0x2d, 0xed, 0x6a, 0xe3, 0x8a, 0x71, 0xfb, 0x26, 0x64, 0xd3, 0xd7, 0xc3, 0x5a, 0xde,
0xff, 0x00, 0xc3, 0x9f, 0x72, 0xf2, 0xc6, 0xd3, 0xbb, 0x7b, 0x29, 0xf4, 0x07, 0x5b, 0x6b, 0xe0,
0xda, 0xc7, 0x36, 0xc5, 0xe1, 0x35, 0x57, 0x68, 0xe9, 0x7c, 0xd7, 0x9e, 0xaf, 0x6d, 0x7c, 0xd2,
0xfb, 0x47, 0xb6, 0xff, 0x00, 0x44, 0x8d, 0x4f, 0xe9, 0xc9, 0xc7, 0x9f, 0x88, 0x39, 0x5a, 0xfa,
0x95, 0x9f, 0x8f, 0x8f, 0xbb, 0x87, 0x73, 0xd6, 0xc7, 0x86, 0xb5, 0xb6, 0x3f, 0x55, 0x5c, 0x73,
0xd7, 0xf7, 0x5c, 0xf7, 0x5f, 0xea, 0xa7, 0xd0, 0x14, 0x23, 0x3c, 0x49, 0xc7, 0x20, 0xc0, 0x33,
0xc7, 0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x03, 0x6a, 0x5b, 0xa6, 0x62, 0x5a, 0x80, 0xf4, 0x1b, 0x5d, 0xc2, 0x97, 0xd7, 0x8a, 0xd7,
0xdd, 0xea, 0x87, 0xdb, 0x36, 0xa3, 0x05, 0xff, 0x00, 0x3f, 0x29, 0x55, 0x80, 0xb2, 0xdd, 0xda,
0x8c, 0x99, 0xba, 0xeb, 0xe5, 0x0b, 0x2b, 0x65, 0xd7, 0xdc, 0xa4, 0x7c, 0x93, 0xd3, 0x30, 0xf3,
0x6c, 0xf2, 0x0f, 0x49, 0x8b, 0x67, 0x5f, 0x05, 0x2d, 0x4a, 0x7f, 0x08, 0x7a, 0x7b, 0x54, 0xc7,
0x5b, 0xc5, 0xa7, 0xdd, 0xe4, 0xa6, 0x01, 0x6f, 0xdb, 0xf6, 0xeb, 0x8a, 0xf6, 0x8b, 0xfb, 0x6c,
0xdf, 0x63, 0x16, 0xbd, 0x3f, 0x3a, 0x5b, 0x99, 0xe5, 0x4a, 0xcf, 0x20, 0xb8, 0xdd, 0xda, 0xa6,
0x58, 0xa7, 0x4c, 0xfb, 0x5a, 0x77, 0x1d, 0x9a, 0x66, 0xad, 0x62, 0xbe, 0x90, 0xa9, 0x01, 0x6d,
0xdb, 0xb6, 0x69, 0x86, 0x2d, 0x16, 0xf5, 0x86, 0x34, 0xf7, 0xa3, 0x0d, 0xe6, 0x27, 0xc6, 0xb6,
0x55, 0x26, 0x69, 0xdf, 0x1d, 0x2d, 0xfe, 0x58, 0xe6, 0x01, 0x6d, 0xd7, 0xa9, 0xd5, 0xd5, 0x11,
0xcc, 0xcf, 0xa3, 0x1d, 0xe2, 0xd1, 0x35, 0xa7, 0x0c, 0x7c, 0xba, 0x74, 0x9e, 0xa8, 0x8f, 0x15,
0x66, 0xee, 0xdc, 0xec, 0xdb, 0x9f, 0x48, 0xf2, 0x04, 0xcc, 0xd9, 0x70, 0x4e, 0xbc, 0x56, 0xbe,
0xf7, 0x3e, 0xdd, 0x93, 0x0d, 0x39, 0xf9, 0x55, 0x40, 0x25, 0xc5, 0xa9, 0xf3, 0x73, 0xfa, 0xf2,
0x93, 0xdc, 0x72, 0x61, 0xbf, 0x1f, 0x12, 0xac, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xd9
};

#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_AP_START:
        printf("softAP started.\n");
        fflush(stdout);
        break;
    case SYSTEM_EVENT_AP_STOP:
        printf("softAP stopped.\n");
        fflush(stdout);
    case SYSTEM_EVENT_AP_STACONNECTED:
        printf("station connected.\n");
        fflush(stdout);
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        printf("station disconnected.\n");
        fflush(stdout);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void wifi_init(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
    
    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .ssid_len = 0,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .ssid_hidden = 0,
            .max_connection = 2,
            .beacon_interval = 100
        }
    };

#if ADD_MACADDR_TO_SSID
    //Get MAC address
    uint8_t mac[6];
    ESP_ERROR_CHECK( esp_wifi_get_mac(WIFI_IF_AP, mac) );
    char strMac[8];
    sprintf(strMac, "-%02X%02X%02X", mac[3], mac[4], mac[5]);
    //Add MAC address to SSID
    strcat((char*)ap_config.ap.ssid, (const char*)strMac);
#endif

    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP, &ap_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static void http_server_netconn_serve(struct netconn *conn)
{
    struct netbuf *inbuf;
    char *buf;
    u16_t buflen;
    err_t err;

    /* Read the data from the port, blocking if nothing yet there.
     We assume the request (the part we care about) is in one netbuf */
    err = netconn_recv(conn, &inbuf);

    if (err == ERR_OK) {
        netbuf_data(inbuf, (void**) &buf, &buflen);

        /* Is this an HTTP GET command? (only check the first 5 chars, since
         there are other formats for GET, and we're keeping it very simple )*/
        if (buflen >= 5 && buf[0] == 'G' && buf[1] == 'E' && buf[2] == 'T'
                && buf[3] == ' ' && buf[4] == '/') {
          /* Send the HTTP header
             * subtract 1 from the size, since we dont send the \0 in the string
             * NETCONN_NOCOPY: our data is const static, so no need to copy it
             */
            netconn_write(conn, http_hdr, sizeof(http_hdr) - 1, NETCONN_NOCOPY);
            //Send mjpeg stream header
            err = netconn_write(conn, http_stream_hdr, sizeof(http_stream_hdr) - 1,
                NETCONN_NOCOPY);
            ESP_LOGD(TAG, "Stream started.");
            
            //Run while everyhting is ok and connection open.
            err = ERR_OK;
            while(err == ERR_OK) {
                if (jpeg_data_done == 1) {
                    ESP_LOGD(TAG, "JPEG image received.");
                    jpeg_data_done = 1;
                    //Send JPEG header
                    err = netconn_write(conn, http_jpg_hdr, sizeof(http_jpg_hdr) - 1,
                        NETCONN_NOCOPY);
                    //Send JPEG image
                    err = netconn_write(conn, jpeg_data_r, jpeg_data_r_size, NETCONN_NOCOPY);
                    if(err == ERR_OK)
                    {
                        //Send boundary to next jpeg
                        err = netconn_write(conn, http_stream_boundary,
                                sizeof(http_stream_boundary) -1, NETCONN_NOCOPY);
                    }
                }else{
                    vTaskDelay(1 / portTICK_RATE_MS);;
                }
            }
            ESP_LOGD(TAG, "Stream ended.");
        }
    }
    /* Close the connection (server closes in HTTP) */
    netconn_close(conn);

    /* Delete the buffer (netconn_recv gives us ownership,
     so we have to make sure to deallocate the buffer) */
    netbuf_delete(inbuf);
}

static void http_server_task(void *pvParameters)
{
    struct netconn *conn, *newconn;
    err_t err;
    conn = netconn_new(NETCONN_TCP);
    netconn_bind(conn, NULL, 80);
    netconn_listen(conn);
    do {
        err = netconn_accept(conn, &newconn);
        if (err == ERR_OK) {
            http_server_netconn_serve(newconn);
            netconn_delete(newconn);
        }
    } while (err == ERR_OK);
    netconn_close(conn);
    netconn_delete(conn);
}

static void uart_init()
{
    uart_config_t uart_config = {
       .baud_rate = 1000000,
       .data_bits = UART_DATA_8_BITS,
       .parity = UART_PARITY_DISABLE,
       .stop_bits = UART_STOP_BITS_1,
       .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
       .rx_flow_ctrl_thresh = 122,
    };
    //Set UART parameters
    ESP_ERROR_CHECK( uart_param_config(EX_UART_NUM, &uart_config) );
    //Set UART log level
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    //Install UART driver, and get the queue.
    ESP_ERROR_CHECK( uart_driver_install(EX_UART_NUM, UART_BUF_SIZE, 128, 10, &uart_queue, 0) );
    //Set UART pins (using UART0 default pins ie no changes.)
    ESP_ERROR_CHECK( uart_set_pin(EX_UART_NUM, EX_UART_TX_PIN, EX_UART_RX_PIN, EX_UART_RTS_PIN, EX_UART_CTS_PIN) );
    //Set uart pattern detect function.
    //ESP_ERROR_CHECK( uart_enable_pattern_det_intr(EX_UART_NUM, '+', 3, 10000, 10, 10) );

}

static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    size_t buffered_size;
//    uint8_t* dtmp = (uint8_t*) malloc(BUF_SIZE);
    while(1) {
        //Waiting for UART event.
        if(xQueueReceive(uart_queue, (void * )&event, (portTickType)portMAX_DELAY)) {
            ESP_LOGD(TAG, "uart[%d] event:", EX_UART_NUM);
            switch(event.type) {
                case UART_DATA:
                    uart_get_buffered_data_len(EX_UART_NUM, &buffered_size);
                    ESP_LOGD(TAG, "data, len: %d; buffered len: %d", event.size, buffered_size);
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGD(TAG, "hw fifo overflow\n");
                    //If fifo overflow happened, you should consider adding flow control for your application.
                    //We can read data out out the buffer, or directly flush the rx buffer.
                    uart_flush(EX_UART_NUM);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGD(TAG, "ring buffer full\n");
                    //If buffer full happened, you should consider encreasing your buffer size
                    //We can read data out out the buffer, or directly flush the rx buffer.
                    uart_flush(EX_UART_NUM);
                    break;
                //Others
                default:
                    ESP_LOGD(TAG, "uart event type: %d\n", event.type);
                    break;
            }
        }
    }
//    free(dtmp);
//    dtmp = NULL;
    vTaskDelete(NULL);
}

enum UartState
{
    INITIAL,
    HEADER1,
    HEADER2,
    HEADER3,
    HEADER4,
    HEADER5,
    HEADER6,
    HEADER7,
    HEADER8,
    HEADER9,
    HEADER10,
    HEADER11,
    HEADER12,
    HEADER13,
    HEADER14,
    HEADER15,
    DATA,
    READDONE
};

int app_main(void)
{
	enum UartState state;
	//Initial state
	state = INITIAL;
	int jpeg_size  = 0;				//jpeg image size
	int remain     = 0;				//jpeg image size remain
	int buf_remain = JPEG_BUF_SIZE;	//receive buffer size remain
	jpeg_data_w_side = 0;
	jpeg_data_w    = jpeg_data0;
	jpeg_data_r    = dummy_jpeg_data;
	jpeg_data_r_size = sizeof(dummy_jpeg_data);
	jpeg_data_done = 1;

    nvs_flash_init();
    //system_init();
    wifi_init();
    uart_init();

    //Create a task to handler UART event from ISR
    xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);

    //Create a task to handler HTTP Server event
    xTaskCreate(&http_server_task, "http_server_task", 2048, NULL, 5, NULL);

    int l = 0;

    while(1) {
        int len = uart_read_bytes(EX_UART_NUM, data, BUF_SIZE, 100 / portTICK_RATE_MS);
        if( len == 0 ) {
            continue;
        }
        ESP_LOGD(TAG, "incomming uart data. start process \n");
        uint8_t *readp = data;
        while( len ){
            switch(state) {
                case INITIAL:
                    jpeg_size = 0;
                    buf_remain = JPEG_BUF_SIZE;
                    switch(jpeg_data_w_side){
                        case 0 :
                            jpeg_data_w = jpeg_data0;
                            break;
                        case 1 :
                            jpeg_data_w = jpeg_data1;
                            break;
                        case 2 :
                            jpeg_data_w = jpeg_data2;
                            break;
                        default:
                            jpeg_data_w = jpeg_data0;
                            break;
                    }
                    if( *readp++ == 0xff ) {
                        state = HEADER1;
                    }
                    len--;
                    break;
                case HEADER1:
                    if( *readp++ == 0xff ) {
                        state = HEADER2;
                    }else{
                        state = INITIAL;
                    }
                    len--;
                    break;
                case HEADER2:
                    if( *readp++ == 0xaa ) {
                        state = HEADER3;
                    }else{
                        state = INITIAL;
                    }
                    len--;
                    break;
                case HEADER3:
                    if( *readp++ == 0x55 ) {
                        state = HEADER4;    //found header
                    }else{
                        state = INITIAL;
                    }
                    len--;
                    break;
                case HEADER4:               //skip dummy(1)
                    ESP_LOGD(TAG, "found jpeg header");
                    readp++;
                    len--;
                    state = HEADER5;
                    break;
                case HEADER5:               //skip dummy(2)
                    readp++;
                    len--;
                    state = HEADER6;
                    break;
                case HEADER6:               //skip dummy(3)
                    readp++;
                    len--;
                    state = HEADER7;
                    break;
                case HEADER7:               //skip dummy(4)
                    readp++;
                    len--;
                    state = HEADER8;
                    break;
                case HEADER8:               //jpegsize(l)
                    jpeg_size = (int)(*readp++);
                    len--;
                    state = HEADER9;
                    break;
                case HEADER9:               //jpegsize(m)
                    jpeg_size += (int)(*readp++) * 0x100;
                    len--;
                    state = HEADER10;
                    break;
                case HEADER10:              //jpegsize(h)
                    jpeg_size += (int)(*readp++) * 0x10000;
                    len--;
                    ESP_LOGD(TAG, "jpeg size: %d", jpeg_size);
                    if( jpeg_size > JPEG_BUF_SIZE ) {   //JPEG image size too long
                        state = INITIAL;
                    }else{
                        state = HEADER11;
                        buf_remain = JPEG_BUF_SIZE;
                        remain = jpeg_size;
                    }
                    break;
                case HEADER11:              //skip
                    readp++;
                    len--;
                    state = HEADER12;
                    break;
                case HEADER12:              //jpeg header1(0xff)
                    *(jpeg_data_w++) = *readp++;
                    len--;
                    state = HEADER13;
                    break;
                case HEADER13:              //jpeg header2(0xfd)
                    *(jpeg_data_w++) = *readp++;
                    len--;
                    state = HEADER14;
                    break;
                case HEADER14:              //jpeg header3(0xff)
                    *(jpeg_data_w++) = *readp++;
                    len--;
                    state = HEADER15;
                    break;
                case HEADER15:              //jpeg header4(0xfb)
                    *(jpeg_data_w++) = *readp++;
                    len--;
                    state = DATA;
                    buf_remain -= 4;
                    remain = jpeg_size - 4;
                    break;
                case DATA:                  //jpeg data
                    if( len > buf_remain ){ //buffer overflow
                        state = INITIAL;
                    }else{
                        if( remain > len ){
                            l = len;
                        }else{
                            l = remain;
                            //done!
                            state = READDONE;
                        }
                        memcpy(jpeg_data_w, readp, l);
                        buf_remain -= l;
                        remain -= l;
                        len -= l;
                        readp += l;
                        jpeg_data_w += l;
                    }
                    break;
                case READDONE:
                    ESP_LOGD(TAG, "jpeg read done. w=%d (%d)", jpeg_data_w_side, jpeg_size);
                    //rotate buffer
                    switch(jpeg_data_w_side){
                        case 0 :
                            jpeg_data_r = jpeg_data0;
                            jpeg_data_w = jpeg_data1;
                            jpeg_data_w_side = 1;
                            break;
                        case 1:
                            jpeg_data_r = jpeg_data1;
                            jpeg_data_w = jpeg_data2;
                            jpeg_data_w_side = 2;
                            break;
                        case 2:
                            jpeg_data_r = jpeg_data2;
                            jpeg_data_w = jpeg_data0;
                            jpeg_data_w_side = 0;
                            break;
                        default:
                            break;
                    }
                    jpeg_data_done = 1;
                    jpeg_data_r_size = jpeg_size;
                    state = INITIAL;
                    break;
                default:
                    break;
            }
        }
    }
    return 0;
}
