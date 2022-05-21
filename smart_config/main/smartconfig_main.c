/* Esptouch example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "esp_smartconfig.h"
#include "smartconfig_ack.h"

#include "driver/gpio.h"

/* The examples use smartconfig type that you can set via project configuration menu.

   If you'd rather not, just change the below entries to enum with
   the config you want - ie #define EXAMPLE_ESP_SMARTCOFNIG_TYPE SC_TYPE_ESPTOUCH
*/
#define EXAMPLE_ESP_SMARTCOFNIG_TYPE SC_TYPE_ESPTOUCH_AIRKISS

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *TAG = "smart_config";

static void smartconfig_task(void *parm);

wifi_config_t wifi_config = {0};
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        bzero(&wifi_config, sizeof(wifi_config_t));
        ESP_ERROR_CHECK(esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config));
        if (strlen((const char *)wifi_config.sta.ssid))
        {
            ESP_LOGI(TAG, "Found ssid [%s], psk[%s], connect to it!",
                     (const char *)wifi_config.sta.ssid, (const char *)wifi_config.sta.password);
            ESP_ERROR_CHECK(esp_wifi_connect());
        }
        xTaskCreate(smartconfig_task, "smartconfig_task", 4096, NULL, 3, NULL);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE)
    {
        ESP_LOGI(TAG, "Scan done");
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL)
    {
        ESP_LOGI(TAG, "Found channel");
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD)
    {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        uint8_t ssid[33] = {0};
        uint8_t password[65] = {0};
        uint8_t rvd_data[33] = {0};

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;

        if (wifi_config.sta.bssid_set == true)
        {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);
        if (evt->type == SC_TYPE_ESPTOUCH_V2)
        {
            ESP_ERROR_CHECK(esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)));
            ESP_LOGI(TAG, "RVD_DATA:%s", rvd_data);
        }

        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE)
    {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

#define GPIO_OUTPUT_IO_LED 16
#define GPIO_OUTPUT_IO_SEL (BIT(GPIO_OUTPUT_IO_LED))

static void wifi_connected_task(void *parm);

static void init_gpio_led(void)
{
    gpio_config_t io_conf;
    // disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    // set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    // bit mask of the pins that you want to set,e.g.GPIO15/16
    io_conf.pin_bit_mask = GPIO_OUTPUT_IO_SEL;
    // disable pull-down mode
    io_conf.pull_down_en = 0;
    // disable pull-up mode
    io_conf.pull_up_en = 0;
    // configure GPIO with the given settings
    gpio_config(&io_conf);

    xTaskCreate(wifi_connected_task, "wifi_connected_task", 4096, NULL, 3, NULL);
}

uint32_t g_led_cnt = 0;
bool g_wifi_connected = false;
static void wifi_connected_task(void *parm)
{
#if 0
    wifi_mode_t mode = 0;
    uint8_t mac[6] = {0};
    wifi_country_t country = {0};
    wifi_bandwidth_t bw = 0;
    uint8_t ch_primary = 0;
    uint8_t ch_second = 0;

    ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));

    switch (mode)
    {
    case WIFI_MODE_STA:
        ESP_LOGI(TAG, "wifi mode STA");
        break;
    case WIFI_MODE_AP:
        ESP_LOGI(TAG, "wifi mode AP");
        break;
    case WIFI_MODE_APSTA:
        ESP_LOGI(TAG, "wifi mode APSTA");
        break;
    default:
        ESP_LOGI(TAG, "wifi mode UNKNOWN");
        break;
    }

    ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_STA, mac));
    ESP_LOGI(TAG, "wifi sta mac: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_AP, mac));
    ESP_LOGI(TAG, "wifi ap mac: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_ERROR_CHECK(esp_wifi_get_country(&country));
    ESP_LOGI(TAG, "wifi country:[%s], schan[%d], nchan[%d], tx_power[%d], policy[%s]",
             country.cc, country.schan, country.nchan, country.max_tx_power, ((country.policy == WIFI_COUNTRY_POLICY_AUTO) ? "auto" : "manual"));

    ESP_ERROR_CHECK(esp_wifi_get_channel(&ch_primary, &ch_second));
    ESP_LOGI(TAG, "wifi channel: primary[%d], secondary[%d]", ch_primary, ch_second);

    // ESP_ERROR_CHECK(esp_wifi_get_bandwidth(ESP_IF_WIFI_AP, &bw));
    // ESP_LOGI(TAG, "wifi ap bandwidth: %s", ((bw == WIFI_BW_HT20) ? "HT20" : "HT40"));
    ESP_ERROR_CHECK(esp_wifi_get_bandwidth(ESP_IF_WIFI_STA, &bw));
    ESP_LOGI(TAG, "wifi sta bandwidth: %s", ((bw == WIFI_BW_HT20) ? "HT20" : "HT40"));
#endif

    ESP_LOGI(TAG, "wifi_connected_task starting!");
    while (1)
    {
        gpio_set_level(GPIO_OUTPUT_IO_LED, g_led_cnt % 2);
        ESP_LOGI(TAG, "cnt: %d\n", g_led_cnt++);
        if (g_wifi_connected)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        else
        {
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
        // g_led_cnt++;
    }
}

static void smartconfig_task(void *parm)
{
    EventBits_t uxBits = 0;
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();

    ESP_LOGI(TAG, "Waiting for sta connect!");
    uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT, true, false, (5000 / portTICK_PERIOD_MS));
    if (uxBits & CONNECTED_BIT)
    {
        g_wifi_connected = true;
        ESP_LOGI(TAG, "wifi connected, skip smartconfig!");
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "Waiting timeout, start smartconfig!");

    ESP_ERROR_CHECK(esp_smartconfig_set_type(EXAMPLE_ESP_SMARTCOFNIG_TYPE));
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));

    while (1)
    {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT, true, false, portMAX_DELAY);
        if (uxBits & CONNECTED_BIT)
        {
            g_wifi_connected = true;
            esp_smartconfig_stop();
            ESP_LOGI(TAG, "WiFi Connected to ap, do smartconfig over!");
            vTaskDelete(NULL);
        }
    }
}

void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());
    init_gpio_led();
    initialise_wifi();
}
