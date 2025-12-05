#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_log.h>
#include <esp_spiffs.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <unistd.h>
#include <esp_sleep.h>

#include "api.h"
#include "lcd.h"
#include "lvgl_gui.h"
#include "touch.h"
#include "sleep.h"

/* Configuration according to application */
#define LVGL_DRAW_BUF_LINES 30 // number of display lines in each draw buffer
#define LVGL_TICK_PERIOD_MS 1
#define LVGL_TASK_STACK_SIZE (16 * 1024)
#define LVGL_TASK_PRIORITY 2
#define LVGL_TASK_MAX_DELAY_MS 10 // 500
#define LVGL_TASK_MIN_DELAY_MS 1  // 1000 / CONFIG_FREERTOS_HZ

#define DRAW_BUF_SIZE (LCD_H_RES * LCD_V_RES / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

static const char *TAG = "main";
static _lock_t lvgl_api_lock;
int x, y, z; // touchscreen coordinates (x, y) and pressure (z)

extern uint32_t touch_timer;

/* Tell lvgl how many milliseconds has elapsed */
void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/* Runs continuously and processes lvgl events, timers, and screen updates */
void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1)
    {
        // lvgl is not thread-safe, so it's needed to use a lock to protect access
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);

        // in case of task watch dog timeout
        time_till_next_ms = MAX(time_till_next_ms, LVGL_TASK_MIN_DELAY_MS);
        // in case of lvgl display not ready yet
        time_till_next_ms = MIN(time_till_next_ms, LVGL_TASK_MAX_DELAY_MS);

        vTaskDelay(pdMS_TO_TICKS(time_till_next_ms));
    }
}

void api_task(void *pv)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    wifi_init_sta();
    vTaskDelete(NULL);
}

static void spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE("SPIFFS", "SPIFFS init failed: %s", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret == ESP_OK)
    {
        ESP_LOGI("SPIFFS", "Mounted. Total=%d, Used=%d bytes", total, used);
    }
    else
    {
        ESP_LOGW("SPIFFS", "Failed to get info: %s", esp_err_to_name(ret));
    }
}

static void sleep_task(void *pv)
{
    touch_timer = xTaskGetTickCount();

    while (1)
    {
        uint32_t now = xTaskGetTickCount();
        uint32_t diff_ms = (now - touch_timer) * portTICK_PERIOD_MS;

        if (diff_ms >= 10000) // 10 s
        {
            enter_deep_sleep();

            touch_timer = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void app_main(void)
{
    /* Initialize LCD */
    ESP_ERROR_CHECK(lcd_init());

    /* Initialize touch screen */
    ESP_LOGI(TAG, "Install touch panel driver");
    touch_init_i2c_and_driver();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool power_up_boot = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);

    ESP_LOGI("main", "Wakeup cause: %d (%s)", cause,
             power_up_boot ? "POWER UP" : "WAKE FROM SLEEP");

    wakeup_init();
    spiffs_init();

    if (power_up_boot)
    {
        xTaskCreatePinnedToCore(api_task, "wifi", 4096, NULL, 3, NULL, 0);
    }
    else
    {
        load_schedule_from_file();
    }

    /* Initialize LVGL */
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_user_data(display, panel);
    lv_display_set_color_format(display, LV_COLOR_FORMAT);

    /* Partial buffer */
    ESP_LOGI(TAG, "Allocate LVGL draw buffer for PARTIAL mode");
    size_t draw_buffer_sz = LCD_H_RES * LVGL_DRAW_BUF_LINES * PIXEL_SIZE;
    void *buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(buf1);
    lv_display_set_buffers(display, buf1, NULL, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

    /* Manual double buffer to prevent screen shift while scrolling */
    // ESP_LOGI(TAG, "Allocate LVGL draw buffer for PARTIAL mode");
    // size_t buf_sz = LCD_H_RES * LVGL_DRAW_BUF_LINES * PIXEL_SIZE;
    // void *buf1 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // void *buf2 = heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // assert(buf1 && buf2);

    lv_display_set_flush_cb(display, (lv_display_flush_cb_t)lvgl_flush_cb);

    ESP_LOGI(TAG, "Register touch input device");
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_cb);

    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);

    lv_display_set_render_mode(display, LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "Register event callbacks");
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_color_trans_done = room_vis_lcd_color_cb};
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, display));

    ESP_LOGI(TAG, "Install LVGL tick timer");

    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Create LVGL task");
    xTaskCreatePinnedToCore(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL, 1);

    ESP_LOGI(TAG, "Display LVGL UI");

    // Lock the mutex due to the LVGL APIs are not thread-safe
    _lock_acquire(&lvgl_api_lock);
    lvgl_create_gui(display);
    _lock_release(&lvgl_api_lock);

    xTaskCreatePinnedToCore(sleep_task, "sleep_task", 4096, NULL, 1, NULL, 0);

    esp_log_level_set("wifi", ESP_LOG_DEBUG);
}