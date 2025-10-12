#include "touch.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include <stdio.h>

static const char *TAG = "touch";

esp_lcd_touch_handle_t tp;  // touch panel

/* LVGL touch read callback */
void lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t tp_x[1] = {0};
    uint16_t tp_y[1] = {0};
    uint8_t tp_cnt = 0;

    esp_lcd_touch_read_data(tp);
    bool pressed = esp_lcd_touch_get_coordinates(tp, tp_x, tp_y, NULL, &tp_cnt, 1);

    if (pressed && tp_cnt > 0) 
    {
        data->point.x = tp_x[0];
        data->point.y = tp_y[0];
        data->state = LV_INDEV_STATE_PRESSED;

        static lv_obj_t *label = NULL;
        if (label == NULL) 
        {
            label = lv_label_create(lv_screen_active());
            lv_obj_align(label, LV_ALIGN_TOP_RIGHT, -10, 10);
        }
        char buf[32];
        snprintf(buf, sizeof(buf), "X:%d  Y:%d", tp_x[0], tp_y[0]);
        lv_label_set_text(label, buf);
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

    ESP_LOGI(TAG, "Initialize I2C bus for GT911");
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_I2C_MASTER_SDA,
        .scl_io_num = CONFIG_I2C_MASTER_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0, &io_config, &io_handle));

    esp_lcd_touch_io_gt911_config_t tp_gt911_config = {
        .dev_addr = io_config.dev_addr,
    };

    esp_lcd_touch_config_t tp_config = {
        .x_max = 800,
        .y_max = 480,
        .rst_gpio_num = -1,
        .int_gpio_num = -1,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 1,
            .mirror_y = 0,
        },
        .driver_data = &tp_gt911_config,
    };
        
    ESP_LOGI(TAG, "Initialize touch panel");
    esp_lcd_touch_new_i2c_gt911(io_handle, &tp_config, &tp);

    return ESP_OK;
}
