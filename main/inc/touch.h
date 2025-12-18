#pragma once

#include <esp_lcd_touch.h>
#include <lvgl.h>

extern esp_lcd_touch_handle_t tp;

/* LVGL touch read callback */
void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data);

/* Initialize touch hardware */
esp_err_t touch_init_i2c_and_driver(void);

void touch_clear_pending_int(void);
