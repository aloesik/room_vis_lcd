#include <stdio.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "lvgl.h"
#include "lvgl_gui.h"

/* Configuration according to used LCD: ESP32-S3-Touch-LCD-7 Waveshare 27078*/
#define LCD_H_RES       800
#define LCD_V_RES       480
#define LCD_HSYNC       4
#define LCD_HBP         8                   // Horizontal back porch
#define LCD_HFP         8                   // Horizontal front porch
#define LCD_VSYNC       4
#define LCD_VBP         16
#define LCD_VFP         16
#define LCD_PCLK_HZ     (25 * 1000 * 1000)  // Refresh Rate = 25000000/(4+8+8+800)(4+8+8+480) = 60Hz

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

static const char *TAG = "room_vis";


static void lcd_init_panel(esp_lcd_panel_handle_t *panel)
{
    esp_lcd_rgb_panel_config_t cfg = {
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

    ESP_LOGI(TAG, "Creating RGB panel...");
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&cfg, panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*panel));
    ESP_LOGI(TAG, "LCD panel initialized.");
}

/* LVGL periodic tick using esp_timer */
static void lvgl_tick_timer_cb(void *arg)
{
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/* LVGL main task */
static void lvgl_task(void *arg)
{
    while (1)
    {
        lv_task_handler();
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_DELAY_MS));
    }
}

void app_main(void)
{
    esp_lcd_panel_handle_t panel;
    lcd_init_panel(&panel);

    lv_init();
    lv_log_register_print_cb(log_print);

    lv_display_t * disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT);

    lv_display_set_driver_data(disp, panel);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

    lvgl_create_gui();
}