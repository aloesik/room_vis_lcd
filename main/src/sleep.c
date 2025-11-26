#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_check.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <port/esp_io_expander.h>
#include <port/esp_io_expander_ch422g.h>
#include <lvgl.h>
#include <driver/rtc_io.h>

#include "lcd.h"
#include "lvgl_gui.h"

#define EXT1_WAKEUP_PIN 4
#define CH422G_EXIO2 (1 << 2)

static const char *TAG_WAKE = "wake";
static const char *TAG_SLEEP = "sleep";
static esp_io_expander_handle_t ch422g;

static esp_err_t ch422g_init(void)
{
    ESP_RETURN_ON_ERROR(
        esp_io_expander_new_i2c_ch422g(I2C_NUM_0,
                                       ESP_IO_EXPANDER_I2C_CH422G_ADDRESS,
                                       &ch422g),
        TAG_WAKE, "Create CH422G failed");

    ESP_RETURN_ON_ERROR(
        esp_io_expander_ch422g_set_oc_push_pull(ch422g),
        TAG_WAKE, "Set OC push-pull failed");

    ESP_RETURN_ON_ERROR(
        esp_io_expander_set_dir(ch422g, CH422G_EXIO2, IO_EXPANDER_OUTPUT),
        TAG_WAKE, "Set OC2 as output failed");

    return ESP_OK;
}

esp_err_t ch422g_set_disp(bool on)
{
    return esp_io_expander_set_level(ch422g, CH422G_EXIO2, on ? 1 : 0);
}

esp_err_t wakeup_init(void)
{
    const uint64_t ext_wakeup_pin_mask = 1ULL << EXT1_WAKEUP_PIN;

    ESP_LOGI(TAG_WAKE, "Enabling EXT1 wakeup on pin GPIO%d", EXT1_WAKEUP_PIN);

    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(ext_wakeup_pin_mask, ESP_EXT1_WAKEUP_ANY_LOW));

    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_ERROR_CHECK(ch422g_init());

    return ESP_OK;
}

void enter_deep_sleep(void)
{
    ch422g_set_disp(false);
    ESP_LOGI(TAG_SLEEP, "Backlight turned off");

    rtc_gpio_init(EXT1_WAKEUP_PIN);
    rtc_gpio_set_direction(EXT1_WAKEUP_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(EXT1_WAKEUP_PIN);
    rtc_gpio_pullup_en(EXT1_WAKEUP_PIN);

    ESP_LOGI(TAG_SLEEP, "Entering deep sleep...");
    esp_deep_sleep_start();
}
