#include <driver/gpio.h>
#include <driver/i2c.h>
#include <esp_lcd_touch.h>
#include <esp_lcd_touch_gt911.h>
#include <esp_log.h>
#include <stdio.h>

#include "touch.h"

uint32_t touch_timer = 0; // timestampt of the last touch

esp_lcd_touch_handle_t tp; // touch panel

/* LVGL touch read callback */
void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    // X and Y coordinates buffers
    uint16_t tp_x[1] = {0};
    uint16_t tp_y[1] = {0};
    uint8_t tp_cnt = 0; // number of points reported

    esp_lcd_touch_read_data(tp);

    // true if coords of first touch point are present
    bool pressed = esp_lcd_touch_get_coordinates(tp, tp_x, tp_y, NULL, &tp_cnt, 1);

    if (pressed && tp_cnt > 0)
    {
        // pass coords to LVGL input data
        data->point.x = tp_x[0];
        data->point.y = tp_y[0];
        data->state = LV_INDEV_STATE_PRESSED;

        // Remember time that passed from last touch (ms)
        touch_timer = xTaskGetTickCount();
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* Initialize I2C and touch driver for GT911 */
esp_err_t touch_init_i2c_and_driver(void)
{
    esp_lcd_panel_io_i2c_config_t io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();

    // i2c configuration
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_I2C_MASTER_SDA,
        .scl_io_num = CONFIG_I2C_MASTER_SCL,
        .sda_pullup_en = GPIO_PULLUP_DISABLE, // activate if there are no external pull-ups
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = 400000,
    };

    // apply configuration and install i2c driver on bus 0
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_config));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

    // i2c panel handle used by touch driver
    esp_lcd_panel_io_handle_t io_handle;

    // panel i/o interface instance operating on I2C bus 0
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0, &io_config, &io_handle));

    // set the gt911 device address on the i2c bus
    esp_lcd_touch_io_gt911_config_t tp_gt911_config = {
        .dev_addr = io_config.dev_addr,
    };

    // touch configuration
    esp_lcd_touch_config_t tp_config = {
        .x_max = 800,
        .y_max = 480,
        .rst_gpio_num = -1,
        .int_gpio_num = 4,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 1,
        },
        .driver_data = &tp_gt911_config,
    };

    // create gt911 touch driver instance using i2c panel
    esp_lcd_touch_new_i2c_gt911(io_handle, &tp_config, &tp);

    return ESP_OK;
}

void touch_clear_pending_int(void)
{
    if (!tp)
        return;

    uint16_t x[1] = {0};
    uint16_t y[1] = {0};
    uint8_t cnt = 0;

    esp_lcd_touch_read_data(tp);
    esp_lcd_touch_get_coordinates(tp, x, y, NULL, &cnt, 1);
}
