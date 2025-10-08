#ifndef LVGL_DISPLAY_H
#define LVGL_DISPLAY_H

void log_print(lv_log_level_t, const char * buf);

bool notify_lvgl_flush_ready(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_ctx);

void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

void increase_lvgl_tick(void *arg);

void lvgl_create_gui(void);

#endif