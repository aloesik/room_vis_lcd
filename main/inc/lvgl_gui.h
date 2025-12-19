#pragma once

#include <lvgl.h>

void lvgl_create_gui(void);

void lvgl_lock_init(void);
void lvgl_lock_acquire(void);
void lvgl_lock_release(void);