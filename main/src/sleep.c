#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_check.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <port/esp_io_expander.h>
#include <port/esp_io_expander_ch422g.h>
#include <lvgl.h>
#include <driver/rtc_io.h>
#include <time.h>

#include "lcd.h"
#include "lvgl_gui.h"
#include "touch.h"

#define EXT1_WAKEUP_PIN 4
#define CH422G_EXIO2 (1 << 2)

/* Wake once per day at 06:00 (local time, uses TZ set in main) */
#define RTC_WAKEUP_HOUR 23
#define RTC_WAKEUP_MIN  30

static const char *TAG_WAKE = "wake";
static const char *TAG_SLEEP = "sleep";
static esp_io_expander_handle_t ch422g;

static esp_err_t ch422g_init(void)
{
    // Avoid re-creating CH422G handle when wakeup_init is called again
    if (ch422g)
        return ESP_OK;

    ESP_RETURN_ON_ERROR(
        esp_io_expander_new_i2c_ch422g(I2C_NUM_0, ESP_IO_EXPANDER_I2C_CH422G_ADDRESS, &ch422g),
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

static uint64_t seconds_until_next_time(int hour, int min)
{
    time_t now = time(NULL);

    struct tm tm_now;
    localtime_r(&now, &tm_now);

    struct tm tm_target = tm_now;
    tm_target.tm_hour = hour;
    tm_target.tm_min = min;
    tm_target.tm_sec = 0;

    time_t target = mktime(&tm_target);
    if (target <= now)
    {
        target += 24 * 60 * 60;
    }

    return (uint64_t)(target - now);
}

esp_err_t wakeup_init(void)
{
    const uint64_t ext_wakeup_pin_mask = 1ULL << EXT1_WAKEUP_PIN;

    ESP_LOGI(TAG_WAKE, "Enabling EXT1 wakeup on pin GPIO%d", EXT1_WAKEUP_PIN);
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(ext_wakeup_pin_mask,
                                                    ESP_EXT1_WAKEUP_ANY_LOW));

    uint64_t sec = seconds_until_next_time(RTC_WAKEUP_HOUR, RTC_WAKEUP_MIN);
    ESP_LOGI(TAG_SLEEP, "Enabling timer wakeup in %llu s (next %02d:%02d)",
             (unsigned long long)sec, RTC_WAKEUP_HOUR, RTC_WAKEUP_MIN);
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(sec * 1000000ULL));

    ESP_ERROR_CHECK(ch422g_init());
    return ESP_OK;
}

static void touch_wait_int_release(void)
{
    /* Make sure RTC pull config is sane for EXT1 */
    rtc_gpio_init(EXT1_WAKEUP_PIN);
    rtc_gpio_set_direction(EXT1_WAKEUP_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(EXT1_WAKEUP_PIN);
    rtc_gpio_pullup_en(EXT1_WAKEUP_PIN);

    /* Try to clear pending GT911 INT and wait for line to go HIGH */
    for (int i = 0; i < 50; i++) // 50 * 10ms = 500ms max
    {
        if (rtc_gpio_get_level(EXT1_WAKEUP_PIN) == 1)
            return;

        touch_clear_pending_int();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* If still low here, EXT1 will wake immediately. */
    ESP_LOGW("sleep", "EXT1 pin GPIO%d still LOW before sleep", EXT1_WAKEUP_PIN);
}

void enter_deep_sleep(void)
{
    ESP_ERROR_CHECK(wakeup_init());

    touch_wait_int_release();

    ch422g_set_disp(false);
    ESP_LOGI("sleep", "Entering deep sleep...");
    esp_deep_sleep_start();
}