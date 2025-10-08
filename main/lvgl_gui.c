#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "lvgl.h"
#include "sdkconfig.h"

// Debugging
void log_print(lv_log_level_t level, const char * buf)
{
    LV_UNUSED(level);
    ESP_LOGI("LVGL", "%s", buf);
}

// Flush ready notification
bool notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

// Flush callback
void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

void increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(2);
}

void lvgl_create_gui(void)
{
    // Create text label
    lv_obj_t * text_label = lv_label_create(lv_screen_active());
    lv_label_set_text(text_label, "Hello, world!");
    lv_obj_align(text_label, LV_ALIGN_CENTER, 0, 0);
    
    // Create label style
    static lv_style_t style_text_label;
    lv_style_init(&style_text_label); 
    lv_style_set_text_font(&style_text_label, LV_FONT_MONTSERRAT_18);

    // Apply style to the label
    lv_obj_add_style(text_label, &style_text_label, 0);  
}