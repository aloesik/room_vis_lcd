#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netdb.h>


static const char *TAG_WIFI = "wifi";
static const char *TAG_HTTP = "http";

static void fetch_time_task(void *pv);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
        esp_wifi_connect();

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) 
    {
        ESP_LOGW(TAG_WIFI, "Reconnecting...");
        esp_wifi_connect();
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG_WIFI, "Connected and got IP");
        xTaskCreate(fetch_time_task, "fetch_time", 4096, NULL, 3, NULL);
    }
}

void wifi_init_sta(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WIFI, "Connecting to %s", CONFIG_ESP_WIFI_SSID);
}

static void fetch_time_task(void *pv)
{
    esp_http_client_config_t config = {
        .url = "https://apps.usos.pwr.edu.pl/services/apisrv/now",
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .skip_cert_common_name_check = true   // for testing
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    ESP_ERROR_CHECK(esp_http_client_open(client, 0));
    int content_length = esp_http_client_fetch_headers(client);

    char buf[128] = {0};
    int read_len = esp_http_client_read(client, buf, sizeof(buf) - 1);
    esp_http_client_close(client);

    if (read_len > 0)
    {
        buf[read_len] = '\0';
        ESP_LOGI(TAG_HTTP, "Raw body: %s", buf);

        // trim whitespace
        char *p = buf;
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;

        // remove quotes if present
        if (*p == '"') p++;
        char *q = p;
        while (*q && *q != '"' && *q != '\r' && *q != '\n') q++;
        *q = '\0';

        // keep first 19 chars -> YYYY-MM-DD HH:MM:SS
        char datetime[20] = {0};
        strncpy(datetime, p, 19);
        datetime[19] = '\0';
        ESP_LOGI(TAG_HTTP, "Parsed datetime: %s", datetime);

        struct tm tmv;
        memset(&tmv, 0, sizeof(tmv));
        if (strptime(datetime, "%Y-%m-%d %H:%M:%S", &tmv))
        {
            time_t t = mktime(&tmv);
            struct timeval now = { .tv_sec = t, .tv_usec = 0 };
            settimeofday(&now, NULL);
            ESP_LOGI(TAG_HTTP, "RTC updated");
        }
        else
        {
            ESP_LOGW(TAG_HTTP, "strptime failed");
        }
    }
    else
    {
        ESP_LOGW(TAG_HTTP, "No data read (len=%d, content_length=%d)", read_len, content_length);
    }

    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}