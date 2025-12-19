#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <esp_spiffs.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <stdio.h>
#include <sys/param.h>
#include <sys/time.h>
#include <unistd.h>

#include "api.h"
#include "lcd.h"
#include "lvgl_gui.h"
#include "touch.h"
#include "sleep.h"

/* Configuration according to application */
#define LVGL_DRAW_BUF_LINES 30 // number of display lines in each draw buffer
#define LVGL_TICK_PERIOD_MS 1  // tick period passed to LVGL
#define LVGL_TASK_STACK_SIZE (16 * 1024)
#define LVGL_TASK_PRIORITY 2
#define LVGL_TASK_MAX_DELAY_MS 5 // upper limit for LVGL task sleep
#define LVGL_TASK_MIN_DELAY_MS 1 // lower limit for LVGL task sleep

extern uint32_t touch_timer;
extern bool schedule_ready;

/* Tell LVGL how many ms has elapsed - required for timers and animations */
void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/* Runs continuously and processes LVGL events, timers, and screen updates */
void lvgl_port_task(void *arg)
{
    while (1)
    {
        // LVGL is not thread-safe, so its API must be protected
        lvgl_lock_acquire();
        uint32_t delay = lv_timer_handler();
        lvgl_lock_release();

        // In case of task watch dog timeout
        delay = MAX(delay, LVGL_TASK_MIN_DELAY_MS);
        // In case of lvgl display not ready yet
        delay = MIN(delay, LVGL_TASK_MAX_DELAY_MS);

        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

/* Initialize Wi-Fi connection after boot */
void api_task(void *pv)
{
    vTaskDelay(pdMS_TO_TICKS(200));

    wifi_init_sta();
    vTaskDelete(NULL);
}

/* Monitor user inactivity and enter deep sleep when no touch bas been detected for some time */
static void sleep_task(void *pv)
{
    touch_timer = xTaskGetTickCount();

    while (1)
    {
        uint32_t now = xTaskGetTickCount();
        uint32_t diff_ms = (now - touch_timer) * portTICK_PERIOD_MS;

        if (diff_ms >= 15000) // 10 s of inactivity
        {
            enter_deep_sleep();
            touch_timer = xTaskGetTickCount(); // Reset time after wake
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* Initialize SPIFFS filesystem to store cached schedule data */
static void spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 1,
        .format_if_mount_failed = true};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE("SPIFFS", "SPIFFS init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Check how many space is taken
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

static void update_task(void *pv)
{
    const uint32_t timeout_ms = 20000;
    uint32_t start = xTaskGetTickCount();

    while (!schedule_ready)
    {
        uint32_t elapsed_ms = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
        if (elapsed_ms >= timeout_ms)
            break;

        vTaskDelay(pdMS_TO_TICKS(200));
    }

    enter_deep_sleep();
    vTaskDelete(NULL);
}

void app_main(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool power_up_boot = (cause == ESP_SLEEP_WAKEUP_UNDEFINED);
    bool rtc_wakeup = (cause == ESP_SLEEP_WAKEUP_TIMER);

    ESP_LOGI("main", "Wakeup cause: %d (%s)", cause,
             power_up_boot ? "POWER UP" : rtc_wakeup ? "RTC TIMER"
                                                     : "TOUCH / OTHER");

    ESP_ERROR_CHECK(touch_init_i2c_and_driver());
    ESP_ERROR_CHECK(wakeup_init());
    ch422g_set_disp(false);
    spiffs_init();

    if (power_up_boot || rtc_wakeup)
    {
        xTaskCreatePinnedToCore(api_task, "wifi", 4096, NULL, 3, NULL, 0);
        xTaskCreatePinnedToCore(update_task, "update_sleep", 4096, NULL, 4, NULL, 0);
        return;
    }

    ESP_ERROR_CHECK(lcd_init());
    load_schedule_from_file();

    lvgl_lock_init();

    // Initialize LVGL
    lv_init();

    // Create LVGL driver instance
    lv_display_t *display = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_user_data(display, panel);
    lv_display_set_color_format(display, LV_COLOR_FORMAT);
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);

    // Allocate LVGL partial draw buffer
    size_t draw_buffer_sz = LCD_H_RES * LVGL_DRAW_BUF_LINES * PIXEL_SIZE;
    void *buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(buf1);

    lv_display_set_buffers(display, buf1, NULL, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, (lv_display_flush_cb_t)lvgl_flush_cb);

    // Register touch input device
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_cb);

    // Register LCD event callbacks (if DMA transfer complete, then inform LVGL)
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_color_trans_done = end_flush_cb};
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, display));

    // LVGL tick source using ESP timer
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    // Rendering and event scheduling
    xTaskCreatePinnedToCore(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL, 1);

    // Lock the mutex due to the LVGL APIs are not thread-safe
    lvgl_lock_acquire();
    lvgl_create_gui();
    lvgl_lock_release();

    ch422g_set_disp(true);

    // Background sleep monitoring task
    xTaskCreatePinnedToCore(sleep_task, "sleep_task", 4096, NULL, 1, NULL, 0);
}