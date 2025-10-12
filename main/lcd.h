// LCD module public header
#pragma once

#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_err.h"

extern esp_lcd_panel_handle_t panel;

void lvgl_flush_cb(void *display, const void *area, uint8_t *px_map);

/* Notify LVGL when flush is done */
extern esp_lcd_rgb_panel_draw_buf_complete_cb_t room_vis_lcd_color_cb;

/* Initialize the RGB LCD panel and return ESP_OK on success */
esp_err_t lcd_init(void);

/* LCD configuration macros */
#define LCD_H_RES       800
#define LCD_V_RES       480
#define LCD_HSYNC       4
#define LCD_HBP         8                  // Horizontal back porch
#define LCD_HFP         8                  // Horizontal front porch
#define LCD_VSYNC       4
#define LCD_VBP         4
#define LCD_VFP         4
#define LCD_PCLK_HZ     (18 * 1000 * 1000)

#define LCD_BK_LIGHT_ON_LEVEL   1
#define LCD_BK_LIGHT_OFF_LEVEL  !LCD_BK_LIGHT_ON_LEVEL

#define DATA_BUS_WIDTH      16
#define PIXEL_SIZE          2
#define LV_COLOR_FORMAT     LV_COLOR_FORMAT_RGB565