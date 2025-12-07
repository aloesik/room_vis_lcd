#include <esp_err.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_log.h>
#include <lvgl.h>

#include "lcd.h"

/* notify that the flushing has finished */
static bool lcd_internal_on_color_trans_done(esp_lcd_panel_handle_t panel_handle, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_ctx)
{
    lv_display_t *display = (lv_display_t *)user_ctx;
    lv_display_flush_ready(display);
    return false;
}

/* Exported callback pointer used by main.c when registering callbacks. */
esp_lcd_rgb_panel_draw_buf_complete_cb_t end_flush_cb = lcd_internal_on_color_trans_done;

esp_lcd_panel_handle_t panel;

/* flush - send pixel data from lvgl buffer to the display */
void lvgl_flush_cb(void *display_ptr, const void *area_ptr, uint8_t *px_map)
{
    // Cast opaque pointers to LVGL types here so the header doesn't need LVGL types
    lv_display_t *display = (lv_display_t *)display_ptr;
    const lv_area_t *area = (const lv_area_t *)area_ptr;

    // retrieve the lcd panel handle stored in the lvgl display user data
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(display);

    // get the coordinates of the area that needs to be updated
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    // send the pixel data to the display driver
    // "+1" because LVGL uses inclusive coordinates, the driver expects exclusive end ones
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

/* Initialize the RGB LCD panel */
esp_err_t lcd_init(void)
{
    esp_lcd_rgb_panel_config_t panel_config = {
        .data_width = 16,
        .num_fbs = 2,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .pclk_gpio_num = CONFIG_LCD_PCLK_GPIO,
        .vsync_gpio_num = CONFIG_LCD_VSYNC_GPIO,
        .hsync_gpio_num = CONFIG_LCD_HSYNC_GPIO,
        .de_gpio_num = CONFIG_LCD_DE_GPIO,
        .data_gpio_nums = {
            CONFIG_LCD_D0_B3_GPIO,
            CONFIG_LCD_D1_B4_GPIO,
            CONFIG_LCD_D2_B5_GPIO,
            CONFIG_LCD_D3_B6_GPIO,
            CONFIG_LCD_D4_B7_GPIO,
            CONFIG_LCD_D5_G2_GPIO,
            CONFIG_LCD_D6_G3_GPIO,
            CONFIG_LCD_D7_G4_GPIO,
            CONFIG_LCD_D8_G5_GPIO,
            CONFIG_LCD_D9_G6_GPIO,
            CONFIG_LCD_D10_G7_GPIO,
            CONFIG_LCD_D11_R3_GPIO,
            CONFIG_LCD_D12_R4_GPIO,
            CONFIG_LCD_D13_R5_GPIO,
            CONFIG_LCD_D14_R6_GPIO,
            CONFIG_LCD_D15_R7_GPIO,
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

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, false, true));

    return ESP_OK;
}