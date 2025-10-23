#include <stdio.h>
#include <unistd.h>
#include <sys/lock.h>
#include <sys/param.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "touch.h"
#include "lcd.h"
#include "lvgl_gui.h"
#include "api.h"

/* Configuration according to application */
#define LVGL_DRAW_BUF_LINES    120 // number of display lines in each draw buffer
#define LVGL_TICK_PERIOD_MS    1
#define LVGL_TASK_STACK_SIZE   (10 * 1024)
#define LVGL_TASK_PRIORITY     4
#define LVGL_TASK_MAX_DELAY_MS 10   // 500
#define LVGL_TASK_MIN_DELAY_MS 1    // 1000 / CONFIG_FREERTOS_HZ

#define DRAW_BUF_SIZE (LCD_H_RES * LCD_V_RES / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

static const char *TAG = "main";
static _lock_t lvgl_api_lock;
int x, y, z; // touchscreen coordinates (x, y) and pressure (z)

/* tell lvgl how many milliseconds has elapsed */
void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// Runs continuously and processes lvgl events, timers, and screen updates
void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1) {
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

void wifi_task(void *pv)
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    wifi_init_sta();
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* Initialize LCD */
    ESP_ERROR_CHECK(lcd_init());

    /* Initialize touch screen */
    ESP_LOGI(TAG, "Install touch panel driver");
    touch_init_i2c_and_driver();

    /* Initialize LVGL */
    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    
    lv_display_t * display = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_user_data(display, panel);
    lv_display_set_color_format(display, LV_COLOR_FORMAT);

    /* Partial buffer - TO BE EXPLAINED */
    ESP_LOGI(TAG, "Allocate LVGL draw buffer for PARTIAL mode");
    size_t draw_buffer_sz = LCD_H_RES * LVGL_DRAW_BUF_LINES * PIXEL_SIZE;
    void *buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(buf1);
    lv_display_set_buffers(display, buf1, NULL, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_set_flush_cb(display, (lv_display_flush_cb_t)lvgl_flush_cb);

    ESP_LOGI(TAG, "Register touch input device");
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_cb);

    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);

    ESP_LOGI(TAG, "Register event callbacks");
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_color_trans_done = room_vis_lcd_color_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel, &cbs, display));

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };
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

    xTaskCreatePinnedToCore(wifi_task, "wifi", 4096, NULL, 2, NULL, 0);
}