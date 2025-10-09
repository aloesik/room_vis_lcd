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

// user libraries

/* Configuration according to used LCD: ESP32-S3-Touch-LCD-7 Waveshare 27078*/
#define LCD_H_RES       800
#define LCD_V_RES       480
#define LCD_HSYNC       4
#define LCD_HBP         8                   // Horizontal back porch
#define LCD_HFP         8                   // Horizontal front porch
#define LCD_VSYNC       4
#define LCD_VBP         16
#define LCD_VFP         16
#define LCD_PCLK_HZ     (16 * 1000 * 1000)  // Refresh Rate = 25000000/(4+8+8+800)(4+8+8+480) = 60Hz

#define LCD_BK_LIGHT_ON_LEVEL   1
#define LCD_BK_LIGHT_OFF_LEVEL  !LCD_BK_LIGHT_ON_LEVEL

#define LCD_NUM_FB  1

#define DATA_BUS_WIDTH      16
#define PIXEL_SIZE          2
#define LV_COLOR_FORMAT     LV_COLOR_FORMAT_RGB565

/* Configuration according to application */
#define LVGL_DRAW_BUF_LINES    50 // number of display lines in each draw buffer
#define LVGL_TICK_PERIOD_MS    2
#define LVGL_TASK_STACK_SIZE   (5 * 1024)
#define LVGL_TASK_PRIORITY     2
#define LVGL_TASK_MAX_DELAY_MS 500
#define LVGL_TASK_MIN_DELAY_MS 1000 / CONFIG_FREERTOS_HZ

#define DRAW_BUF_SIZE (LCD_H_RES * LCD_V_RES / 10 * (LV_COLOR_DEPTH / 8))
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

static const char *TAG = "main";
static _lock_t lvgl_api_lock;
static esp_lcd_panel_handle_t panel;

extern void lvgl_create_gui(lv_display_t *disp);

// Flush ready notification
bool notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_ctx)
{
    lv_display_t *display = (lv_display_t *)user_ctx;
    lv_display_flush_ready(display);
    return false;
}

// Flush callback
void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(display);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

/* Tell LVGL how many milliseconds has elapsed */
void increase_lvgl_tick(void *arg)
{
    lv_tick_inc(2);
}

void lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        // in case of task watch dog timeout
        time_till_next_ms = MAX(time_till_next_ms, LVGL_TASK_MIN_DELAY_MS);
        // in case of lvgl display not ready yet
        time_till_next_ms = MIN(time_till_next_ms, LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Install RGB LCD panel driver");
    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16,
        .num_fbs = 1,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .pclk_gpio_num = CONFIG_LCD_PCLK_GPIO,
        .vsync_gpio_num = CONFIG_LCD_VSYNC_GPIO,
        .hsync_gpio_num = CONFIG_LCD_HSYNC_GPIO,
        .de_gpio_num = CONFIG_LCD_DE_GPIO,
        .data_gpio_nums = {
            CONFIG_LCD_D0_B3_GPIO, CONFIG_LCD_D1_B4_GPIO,
            CONFIG_LCD_D2_B5_GPIO, CONFIG_LCD_D3_B6_GPIO,
            CONFIG_LCD_D4_B7_GPIO, CONFIG_LCD_D5_G2_GPIO,
            CONFIG_LCD_D6_G3_GPIO, CONFIG_LCD_D7_G4_GPIO,
            CONFIG_LCD_D8_G5_GPIO, CONFIG_LCD_D9_G6_GPIO,
            CONFIG_LCD_D10_G7_GPIO, CONFIG_LCD_D11_R3_GPIO,
            CONFIG_LCD_D12_R4_GPIO, CONFIG_LCD_D13_R5_GPIO,
            CONFIG_LCD_D14_R6_GPIO, CONFIG_LCD_D15_R7_GPIO,
        },
        .timings = {
            .pclk_hz = LCD_PCLK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_back_porch = LCD_HBP,
            .hsync_front_porch = LCD_HFP,
            .hsync_pulse_width = LCD_HSYNC,
            .vsync_back_porch = LCD_VBP,
            .vsync_front_porch = LCD_VFP,
            .vsync_pulse_width = LCD_VSYNC,
            .flags = {.pclk_active_neg = true},
            
        },
        .flags.fb_in_psram = true,
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel));

    ESP_LOGI(TAG, "Initialize RGB LCD panel");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, false, true));

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();
    
    lv_display_t * display = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_user_data(display, panel);
    lv_display_set_color_format(display, LV_COLOR_FORMAT);

    // Partial buffer - TO BE EXPLAINED
    ESP_LOGI(TAG, "Allocate LVGL draw buffer for PARTIAL mode");
    size_t draw_buffer_sz = LCD_H_RES * LVGL_DRAW_BUF_LINES * PIXEL_SIZE;
    void *buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    assert(buf1);
    lv_display_set_buffers(display, buf1, NULL, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_set_flush_cb(display, lvgl_flush_cb);

    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_270);

    ESP_LOGI(TAG, "Register event callbacks");
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
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
    xTaskCreate(lvgl_port_task, "LVGL", LVGL_TASK_STACK_SIZE, NULL, LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "Display LVGL UI");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    _lock_acquire(&lvgl_api_lock);
    lvgl_create_gui(display);
    _lock_release(&lvgl_api_lock);
}