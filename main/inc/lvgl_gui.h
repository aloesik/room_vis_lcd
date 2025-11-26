#pragma once

#include <lvgl.h>

void lvgl_create_gui(lv_display_t *disp);

void lvgl_lock_init(void);
void lvgl_lock_acquire(void);
void lvgl_lock_release(void);