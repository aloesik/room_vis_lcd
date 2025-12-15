#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <netdb.h>
#include <nvs_flash.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

#define PROXY 1

static const char *TAG_WIFI = "wifi";
static const char *TAG_HTTP = "http";
static const char *TAG_JSON = "json";

static const char *USOS_CONSUMER_KEY = "ApHWU9fCxcfjs5teb3Uj";
static const char *USOS_CONSUMER_SECRET = "YtddWP3Ap8FrWSdqet2gpKvbADpsrhZW6j2vKRey";

static const char *ROOM_ID = "1402"; // change to any room id (currently 002 in M-11)

bool schedule_ready = false;
cJSON *schedule_root = NULL;

const char *day_name[] = {
    "NIEDZIELA", "PONIEDZIAŁEK", "WTOREK",
    "ŚRODA", "CZWARTEK", "PIĄTEK", "SOBOTA"};

int current_day = 0;

static void fetch_time_task(void *pv);
static void fetch_schedule_task(void *pv);

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    // if wifi has started, begin connecting
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    } // if the station got disconnectred, try reconnecting
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG_WIFI, "Reconnecting...");
        esp_wifi_connect();
    } // if an IP address has been obtained from the router
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG_WIFI, "Connected and got IP");
        // task to fetch current date and time
        xTaskCreatePinnedToCore(fetch_time_task, "fetch_time", 4096, NULL, 3, NULL, 0);
    }
}

/* Initialize wifi station (client) */
void wifi_init_sta(void)
{
    // initialize nvs (keeps wi-fi credentials)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // erase and re-init nvs in case of corruption or outdating
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // initialize tcp/ip stack and system event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // set device as wi-fi client
    esp_netif_create_default_wifi_sta();

    // load defaut wi-fi configuration and initialize wi-fi
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));

    // register event handlers for wi-fi and ip events
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    // initialize wi-fi station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_WIFI, "Connecting to %s", CONFIG_ESP_WIFI_SSID);
}

/* Fetch current date and time from api */
static void fetch_time_task(void *pv)
{
    esp_http_client_config_t config = {
        .url = "https://apps.usos.pwr.edu.pl/services/apisrv/now",
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 3000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // open https connection and get headers
    ESP_ERROR_CHECK(esp_http_client_open(client, 0));
    int content_length = esp_http_client_fetch_headers(client);

    // read response
    char buf[32] = {0};                                                // temporary buffer for server
    int read_len = esp_http_client_read(client, buf, sizeof(buf) - 1); // read data into buffer, leave 1 byte for /0
    esp_http_client_close(client);                                     // close connection after reading

    // check if valid data received
    if (read_len > 0)
    {
        buf[read_len] = '\0';                    // ensure null termination
        ESP_LOGI(TAG_HTTP, "Raw data: %s", buf); // e.g. "2025-10-27 18:02:22.278700"

        // remove surrounding quotes
        char *pointer = buf;
        if (*pointer == '"')
        {
            pointer++; // skip first quote
        }

        char *end = strchr(pointer, '"'); // find closing quote
        if (end)
        {
            *end = '\0'; // overwrite quote
        }

        // cut off microseconds (.xxxxxx)
        char *dot = strchr(pointer, '.'); // find ms dot
        if (dot)
        {
            *dot = '\0'; // keep only "YYYY-MM-DD HH:MM:SS"
        }

        ESP_LOGI(TAG_HTTP, "Parsed datetime: %s", pointer);

        // convert to time struct and set rtc
        struct tm tmv = {0};
        if (strptime(pointer, "%Y-%m-%d %H:%M:%S", &tmv)) // parse string to datetime object
        {
            // set and reload timezone environmental variables
            setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
            tzset();

            // convert time to unix timestamp and create struct
            time_t t = mktime(&tmv);
            struct timeval now = {
                .tv_sec = t,
                .tv_usec = 0};

            // update system clock
            settimeofday(&now, NULL);
            ESP_LOGI(TAG_HTTP, "RTC updated");

            // set current day's name
            current_day = tmv.tm_wday;
            ESP_LOGI(TAG_HTTP, "Today is: %s", day_name[tmv.tm_wday]);

            // fetch room schedule after successful time update
            xTaskCreatePinnedToCore(fetch_schedule_task, "fetch_schedule", 8192, NULL, 3, NULL, 0);
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

    // cleanup resources and end task
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

/* Create url to the room's schedule */
static void create_room_url(char *url_buf, size_t url_buf_size, const char *room_id)
{
    time_t now = time(NULL); // returns current unix timestamp
    struct tm tm_target;
    localtime_r(&now, &tm_target); // convert timestamp into date/time in local time and write into tm_target

    // temporary before api sync - it returns the same day but 1 year ago
    tm_target.tm_year -= 1; // move one year back
    tm_target.tm_mday += 1; // move one day forward

    mktime(&tm_target); // normalize date - convert to unix again

    char date_buf[16];
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_target); // convert time into formatted date

    if (PROXY == 1)
    {
        // create url for particular room and date
        snprintf(url_buf, url_buf_size,
                 "http://192.168.0.217:5000/services/tt/room"
                 "?room_id=%s&start=%s&days=7"
                 "&fields=start_time|end_time|course_name|classtype_name|group_number|unit_id|room_number|building_id",
                 room_id, date_buf);
    }
    else
    {
        snprintf(url_buf, url_buf_size,
                 "https://apps.usos-szkol.pwr.edu.pl/services/tt/room"
                 "?room_id=%s&start=%s&days=7"
                 "&fields=start_time|end_time|course_name|classtype_name|group_number|unit_id|room_number|building_id"
                 "&consumer_key=%s",
                 room_id, date_buf, USOS_CONSUMER_KEY);
    }
}

/* Parse json schedule into schedule_root */
void load_schedule_from_file(void)
{
    // open json file in read mode
    FILE *file = fopen("/spiffs/schedule.json", "r");
    if (!file)
    {
        ESP_LOGE(TAG_JSON, "Can't open schedule.json");
        return;
    }

    fseek(file, 0, SEEK_END);    //  move pointer to the end
    long file_len = ftell(file); //  return files length
    fseek(file, 0, SEEK_SET);    //  move pointer to the beginning

    // save file's data into json buffer
    char *json_buf = malloc(file_len + 1);
    fread(json_buf, 1, file_len, file);
    json_buf[file_len] = '\0';
    fclose(file);

    // parse data into json tree for further operations on objects
    schedule_root = cJSON_Parse(json_buf);
    free(json_buf);

    if (!schedule_root)
    {
        ESP_LOGE(TAG_JSON, "Invalid JSON");
    }
    else
    {
        schedule_ready = true;
        ESP_LOGI(TAG_JSON, "Schedule ready");
    }
}

/* Refresh the schedule - fetch and save to JSON file in SPIFFS */
static void fetch_schedule_task(void *pv)
{
    char url[256];
    create_room_url(url, sizeof(url), ROOM_ID);
    ESP_LOGI(TAG_HTTP, "Fetching schedule: %s", url);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // open https connection
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_HTTP, "HTTP open failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
    }

    // get headers
    int total_len = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG_HTTP, "Response length: %d", total_len);

    // open json file in write mode
    FILE *f = fopen("/spiffs/schedule.json", "w");
    if (!f)
    {
        ESP_LOGE(TAG_HTTP, "Failed to open JSON file for writing");
    }

    char buf[512];
    int read_len;
    while ((read_len = esp_http_client_read(client, buf, sizeof(buf))) > 0)
    {
        fwrite(buf, 1, read_len, f);
    }

    fclose(f);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG_HTTP, "Schedule JSON saved");

    load_schedule_from_file();

    vTaskDelete(NULL);
}