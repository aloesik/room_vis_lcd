#include "lvgl.h"

/**
 * @brief Create a text label on the LVGL screen.
 * 
 * @param parent       Parent object (e.g., lv_screen_active()).
 * @param text         Text to display.
 * @param align        Alignment (e.g., LV_ALIGN_CENTER, LV_ALIGN_TOP_LEFT, etc.).
 * @param x_ofs        X offset relative to the alignment point.
 * @param y_ofs        Y offset relative to the alignment point.
 * @param font         Pointer to font (e.g., &lv_font_montserrat_20).
 * @param text_color   Text color in LVGL format (e.g., lv_color_hex(0xFFFFFF)).
 * 
 * @return lv_obj_t*   Pointer to the created label object.
 */
static lv_obj_t* draw_text_label(lv_obj_t *parent, const char *text,
                                 lv_align_t align, int x_ofs, int y_ofs,
                                 const lv_font_t *font, lv_color_t text_color)
{
    // Create new label object
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_align(label, align, x_ofs, y_ofs);

    // Create and configure style for text
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_font(&style, font);
    lv_style_set_text_color(&style, text_color);

    // Apply style to the label
    lv_obj_add_style(label, &style, 0);

    return label;
}

void lvgl_create_gui(lv_display_t *disp)
{
    // Set background color
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x4fc3f7), 0);

    // Example 1: centered label
    draw_text_label(lv_screen_active(), "Hello! o/",
                    LV_ALIGN_CENTER, 0, 0,
                    &lv_font_montserrat_20,
                    lv_color_hex(0xFFFFFF));

    // Example 2: bottom label
    draw_text_label(lv_screen_active(), "Status: OK",
                    LV_ALIGN_BOTTOM_MID, 0, -30,
                    &lv_font_montserrat_14,
                    lv_color_hex(0x000000));
}