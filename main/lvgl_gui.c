#include "lvgl.h"
#include "lvgl_gui.h"
#include "esp_log.h"

LV_IMAGE_DECLARE(pwr_logo);

static const char *TAG = "lvgl_gui";

static const char *days[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday"};
static int current_day = 0;

/**
 * @brief Create a text label on the LVGL screen with custom font and color
 *
 * @param parent       parent LVGL object (e.g. lv_screen_active())
 * @param text         text to display
 * @param align        alignment (e.g. LV_ALIGN_CENTER)
 * @param x_ofs        horizontal offset relative to alignment point
 * @param y_ofs        vertical offset relative to alignment point
 * @param font         pointer to LVGL font structure (e.g. &lv_font_montserrat_20)
 * @param text_color   text color (use lv_color_hex())
 *
 * @return lv_obj_t*   pointer to created LVGL label object.
 */
static lv_obj_t *draw_text_label(lv_obj_t *parent, const char *text, lv_align_t align, int x_ofs,
                                 int y_ofs, const lv_font_t *font, lv_color_t text_color)
{
    // create new label object
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_align(label, align, x_ofs, y_ofs);

    // create style for text
    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_text_font(&style, font);
    lv_style_set_text_color(&style, text_color);

    // apply style to the label
    lv_obj_add_style(label, &style, 0);

    return label;
}

static lv_obj_t *draw_button(lv_obj_t *parent, lv_event_cb_t event_cb,  lv_obj_t *label,
                             lv_align_t align, int x_ofs, int y_ofs, int width, int height)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_align(button, align, x_ofs, y_ofs);

    if (event_cb)
    {
        lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, NULL);
    }
    
    if (label)
    {
        lv_obj_set_parent(label, button);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    }

    return button;
}

static void button_cb(lv_event_t *e)
{
    ESP_LOGI(TAG, "Button pressed");
}

static void update_day(lv_obj_t *table)
{
    lv_table_set_cell_value(table, 0, 0, days[current_day]);
}

static void scroll_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *table = (lv_obj_t *) lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Gesture event detected");

    if (code == LV_EVENT_GESTURE)
    {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_RIGHT)
        {
            current_day = (current_day + 1) % 5;  // next day
            update_day(table);
        } 
        else if (dir == LV_DIR_LEFT)
        {
            current_day = (current_day - 1 + 5) % 5;  // previous day
            update_day(table);
        }
    }
}

void lvgl_create_gui(lv_display_t *disp)
{
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0xffffff), 0); // white
    lv_obj_set_scroll_dir(lv_screen_active(), LV_DIR_HOR);

    // Create table
    lv_obj_t *table = lv_table_create(lv_screen_active());
    lv_obj_set_size(table, 470, 725);
    lv_obj_align(table, LV_ALIGN_TOP_MID, 0, 65);

    lv_table_set_col_cnt(table, 2);
    lv_table_set_row_cnt(table, 17);

    lv_table_set_column_width(table, 0, 80);
    lv_table_set_column_width(table, 1, 370);

    lv_table_set_cell_ctrl(table, 0, 0, LV_TABLE_CELL_CTRL_MERGE_RIGHT);
    lv_obj_set_style_text_align(table, LV_TEXT_ALIGN_CENTER, LV_PART_ITEMS);

    lv_table_set_cell_value(table, 0, 0, "Monday");

    const char *hours[] = {"7:00", "8:00", "9:00", "10:00", "11:00", "12:00",
                           "13:00", "14:00", "15:00", "16:00", "17:00",
                           "18:00", "19:00", "20:00", "21:00", "22:00"};

    for (int i = 1; i <= 16; i++)
    {
        lv_table_set_cell_value(table, i, 0, hours[i - 1]);
        lv_table_set_cell_value(table, i, 1, "");
    }
    
    lv_obj_set_style_bg_opa(table, LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(table, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_PRESSED);

    lv_obj_set_scroll_dir(table, LV_DIR_VER);
    lv_obj_add_flag(table, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_add_flag(table, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_add_flag(table, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(lv_screen_active(), scroll_event_cb, LV_EVENT_GESTURE, table);

    lv_obj_t *logo = lv_image_create(lv_screen_active());
    lv_image_set_src(logo, &pwr_logo);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 0, 0);

        // Top title
    /*draw_text_label(lv_screen_active(), "Politechnika Wrocławska",
                    LV_ALIGN_TOP_LEFT, 10, 10,
                    &lv_font_montserrat_28, lv_color_hex(0x000000));*/

    /*lv_obj_t *btn_label = draw_text_label(lv_screen_active(), "Click me", LV_ALIGN_TOP_MID,
                                          0, 0, &lv_font_montserrat_18, lv_color_hex(0xffffff));
    // Add a button below the table
    draw_button(lv_screen_active(), button_cb, btn_label, LV_ALIGN_BOTTOM_MID, 0, 0, 150, 60);*/
}