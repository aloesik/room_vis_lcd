#include <lvgl.h>
#include <esp_log.h>
#include <time.h>
#include <sys/time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <cJSON.h>
#include <math.h>
#include <stdbool.h>

#include "lvgl_gui.h"
#include "api.h"

#define TABLE_START_HOUR 7
#define TABLE_END_HOUR   22            // virtual end time, 15 hours total
#define ROW_H_PX         60
#define ROW_COUNT        15

#define TABLE_TOTAL_MIN  ((TABLE_END_HOUR - TABLE_START_HOUR) * 60) // 900 min
#define TABLE_HEIGHT_PX  (ROW_H_PX * ROW_COUNT)                     // 900 px

void lvgl_lock_acquire(void);
void lvgl_lock_release(void);

LV_IMAGE_DECLARE(pwr_logo);

static SemaphoreHandle_t lvgl_mutex = NULL;

static const char *TAG = "lvgl_gui";

static const char *days[] = {"PONIEDZIAŁEK", "WTOREK", "ŚRODA", "CZWARTEK", "PIĄTEK"};
static int chosen_day = 0;

static lv_obj_t *logo = NULL;
static lv_obj_t *scroll_panel = NULL;
static lv_obj_t *table = NULL;
static lv_timer_t *clock_timer = NULL;
static lv_obj_t *time_label = NULL;
static lv_obj_t *date_label = NULL;
static lv_obj_t *room_label = NULL;

extern const lv_font_t lv_font_aptos_20;
extern const lv_font_t lv_font_aptos_22;
extern const lv_font_t lv_font_aptos_light_17;
extern const lv_font_t lv_font_aptos_light_25;
extern const lv_font_t lv_font_aptos_semibold_22;

/************************* CALLBACKS *************************/
static void day_button_cb(lv_event_t * e)
{
    lv_obj_t *button = lv_event_get_target(e);
    int id = (int)lv_event_get_user_data(e);
    chosen_day = id;

    uint32_t i;
    uint32_t child_count = lv_obj_get_child_count(scroll_panel);
    for (i = 0; i < child_count; i++) {
        lv_obj_t *child_button = lv_obj_get_child(scroll_panel, i);
        lv_obj_t *lbl = lv_obj_get_child(child_button, 0);
        if (i == chosen_day) {
            lv_obj_set_style_bg_color(child_button, lv_color_hex(0xb32e23), 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        } else {
            lv_obj_set_style_bg_color(child_button, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
        }
    }

    lv_coord_t x = lv_obj_get_x(button);
    lv_obj_scroll_to_x(scroll_panel, x, LV_ANIM_ON);

    ESP_LOGI(TAG, "Button pressed");
}

static void update_clock_cb(lv_timer_t *timer)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    char time_str[16];
    char date_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
    strftime(date_str, sizeof(date_str), "%d.%m.%Y", &timeinfo);

    lvgl_lock_acquire();
    lv_label_set_text(time_label, time_str);
    lv_label_set_text(date_label, date_str);
    lvgl_lock_release();
}

/************************* DRAW WIDGETS *************************/
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

static void *draw_scroll_panel(void)
{
    scroll_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(scroll_panel, 480, 70);
    lv_obj_set_scroll_dir(scroll_panel, LV_DIR_HOR);
    //lv_obj_set_scroll_snap_x(scroll_panel, LV_SCROLL_SNAP_END | LV_SCROLL_SNAP_START);
    lv_obj_set_flex_flow(scroll_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scroll_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(scroll_panel, 0, 0);
    lv_obj_set_style_pad_column(scroll_panel, 20, 0);
    lv_obj_align_to(scroll_panel, logo, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
    lv_obj_set_scrollbar_mode(scroll_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(scroll_panel, 1, 0);
    lv_obj_set_style_border_color(scroll_panel, lv_color_hex(0xB32E23), 0);
    lv_obj_set_style_radius(scroll_panel, 0, 0);

    // prevent scrolling outside the content
    lv_obj_clear_flag(scroll_panel, LV_OBJ_FLAG_SCROLL_ELASTIC);

    for (int i = 0; i < 5; i++) {
        lv_obj_t *label = draw_text_label(lv_scr_act(), days[i], LV_ALIGN_CENTER, 0, 0, &lv_font_aptos_22, lv_color_hex(0xFFFFFF));

        lv_point_t size;
        lv_txt_get_size(&size, days[i], &lv_font_aptos_22, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int button_width = size.x + 30;

        lv_obj_t *button = draw_button(scroll_panel, day_button_cb, label, LV_ALIGN_CENTER, 0, 0, button_width, 40);
        lv_obj_add_event_cb(button, day_button_cb, LV_EVENT_CLICKED, (void*)i);

        if (i == chosen_day) {
            lv_obj_set_style_bg_color(button, lv_color_hex(0xB32E23), 0);
            lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        } else {
            lv_obj_set_style_bg_color(button, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
        }
        lv_obj_set_style_radius(button, 20, 0);
    }

    return scroll_panel;
}

static void *draw_table(void)
{
    table = lv_obj_create(lv_scr_act());
    lv_obj_set_size(table, 480, 660);
    lv_obj_align_to(table, scroll_panel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
    lv_obj_set_scroll_dir(table, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(table, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(table, 0, 0);
    lv_obj_set_style_pad_column(table, 0, 0);
    lv_obj_set_style_pad_all(table, 0, 0);
    //lv_obj_set_scroll_snap_y(table, LV_SCROLL_SNAP_END);

    lv_obj_clear_flag(table, LV_OBJ_FLAG_SCROLL_ELASTIC);

    const char *hours[] = {"7:00", "8:00", "9:00", "10:00", "11:00", "12:00",
                        "13:00", "14:00", "15:00", "16:00", "17:00",
                        "18:00", "19:00", "20:00", "21:00"};
    uint8_t rows = 15;
    uint8_t row_h = 60;
    uint8_t col1_w = 100;
    uint16_t col2_w = 380;

    for (int i = 0; i < rows; i++) 
    {
        // first column (hours)
        lv_obj_t *cell1 = lv_obj_create(table);
        lv_obj_set_size(cell1, col1_w, row_h);
        lv_obj_set_style_bg_color(cell1, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(cell1, 0, 0);
        lv_obj_set_style_radius(cell1, 0, 0);
        lv_obj_align(cell1, LV_ALIGN_TOP_LEFT, 0, i * row_h);
        lv_obj_set_style_pad_all(cell1, 0, 0);

        // dashed bottom line
        static lv_point_precise_t line_points[] = {{0, 0}, {100, 0}};
        lv_obj_t *line = lv_line_create(cell1);
        lv_line_set_points(line, line_points, 2);
        lv_obj_align(line, LV_ALIGN_BOTTOM_LEFT, 0, -1);
        lv_obj_set_style_line_color(line, lv_color_hex(0xE2AD9B), 0);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_obj_set_style_line_dash_width(line, 10, 0);
        lv_obj_set_style_line_dash_gap(line, 10, 0);

        lv_obj_t *label1 = lv_label_create(cell1);
        lv_label_set_text(label1, hours[i]);
        lv_obj_set_style_text_font(label1, &lv_font_aptos_light_25, 0);
        lv_obj_center(label1);
        
        // second column (lessons)
        lv_obj_t *cell2 = lv_obj_create(table);
        lv_obj_set_size(cell2, col2_w, row_h);
        lv_obj_set_style_bg_color(cell2, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(cell2, 1, 0);
        lv_obj_set_style_border_color(cell2, lv_color_hex(0xE2AD9B), 0);
        lv_obj_set_style_radius(cell2, 0, 0);
        lv_obj_align(cell2, LV_ALIGN_TOP_LEFT, col1_w, i * (row_h));
    }
    return table;
}

static void draw_schedule(lv_obj_t *parent)
{
    cJSON *root = api_get_schedule_root();
    if (!root) return;

    int arr_size = cJSON_GetArraySize(root);        // get number of elements (lessons) in json file
    ESP_LOGI(TAG, "Parsed %d lessons", arr_size);

    for (int i = 0; i < arr_size; i++)
    {
        cJSON *item = cJSON_GetArrayItem(root, i);  // get element's index
        if (!item) continue;

        // fetch needed data in string format
        const char *start_str = cJSON_GetObjectItem(item, "start_time")->valuestring;
        const char *end_str   = cJSON_GetObjectItem(item, "end_time")->valuestring;
        const char *title_pl  = cJSON_GetObjectItem(cJSON_GetObjectItem(item, "course_name"), "pl")->valuestring;

        // convert time strings to minutes since 7:00
        int sh, sm, eh, em; // start hour, start minute, end...
        sscanf(start_str + 11, "%d:%d", &sh, &sm);
        sscanf(end_str   + 11, "%d:%d", &eh, &em);
        int start_min = (sh * 60 + sm) - (TABLE_START_HOUR * 60);
        int end_min   = (eh * 60 + em) - (TABLE_START_HOUR * 60);
        if (start_min < 0) start_min = 0;
        if (end_min > TABLE_TOTAL_MIN) end_min = TABLE_TOTAL_MIN;
        int duration_min = end_min - start_min;

        // compute pixel position and height
        float px_per_min = (float)TABLE_HEIGHT_PX / (float)TABLE_TOTAL_MIN;
        int y = (int)round(start_min * px_per_min);
        int h = (int)round(duration_min * px_per_min);

        // craw rectangle for the lesson
        lv_obj_t *rect = lv_obj_create(parent);
        lv_obj_set_size(rect, 360, h);
        lv_obj_set_style_bg_color(rect, lv_color_hex(0xE2AD9B), 0);
        lv_obj_set_style_radius(rect, 10, 0);
        lv_obj_set_style_border_width(rect, 0, 0);
        lv_obj_set_style_pad_all(rect, 4, 0);
        lv_obj_set_scrollbar_mode(rect, LV_SCROLLBAR_MODE_OFF);

        // position inside scrollable content coordinates
        lv_obj_set_pos(rect, 110, y);

        // add labels
        char time_label[16];
        snprintf(time_label, sizeof(time_label), "%02d:%02d", sh, sm);

        lv_obj_t *lbl_time = lv_label_create(rect);
        lv_label_set_text(lbl_time, time_label);
        lv_obj_set_style_text_font(lbl_time, &lv_font_aptos_22, 0);
        lv_obj_align(lbl_time, LV_ALIGN_BOTTOM_RIGHT, -5, -5);

        lv_obj_t *lbl_title = lv_label_create(rect);
        lv_label_set_text(lbl_title, title_pl);
        lv_obj_set_style_text_font(lbl_title, &lv_font_aptos_semibold_22, 0);
        lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl_title, 360);
        lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 5, 5);
    }
    ESP_LOGI(TAG, "Drawing schedule rectangles...");
}

static void draw_room_label(void)
{
    cJSON *root = api_get_schedule_root();
    if (!root || cJSON_GetArraySize(root) == 0) return;

    cJSON *first = cJSON_GetArrayItem(root, 0);
    if (!first) return;

    cJSON *room = cJSON_GetObjectItem(first, "room_number");
    cJSON *building = cJSON_GetObjectItem(first, "building_id");
    if (!cJSON_IsString(room) || !cJSON_IsString(building)) return;

    char text[32];
    snprintf(text, sizeof(text), "Sala: %s, %s", room->valuestring, building->valuestring);
    lv_label_set_text(room_label, text);
}

static void check_schedule_cb(lv_timer_t *t)
{
    if (!api_is_schedule_ready()) return;  // not ready yet
    lv_timer_del(t);

    cJSON *root = api_get_schedule_root();
    if (!root) return;

    draw_schedule(table);
    draw_room_label();
}

void lvgl_create_gui(lv_display_t *disp)
{
    // set background color
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0xffffff), 0); // white
    
    // time
    time_label = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(time_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(time_label, 150);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(time_label, "--:--:--");
    lv_obj_align(time_label, LV_ALIGN_TOP_RIGHT, -5, 5);
    lv_obj_set_style_text_font(time_label, &lv_font_aptos_20, 0);
    lv_obj_set_style_text_color(time_label, lv_color_hex(0xB32E23), 0);

    // date
    date_label = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(date_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(date_label, 150);
    lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(date_label, "--.--.----");
    lv_obj_align_to(date_label, time_label, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_text_font(date_label, &lv_font_aptos_20, 0);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0xB32E23), 0);

    // room
    room_label = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(room_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(room_label, 150);
    lv_obj_set_style_text_align(room_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(room_label, "Sala: ---");
    lv_obj_align_to(room_label, date_label, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_text_font(room_label, &lv_font_aptos_light_17, 0);

    // update every 1 s
    clock_timer = lv_timer_create(update_clock_cb, 1000, NULL);

    // add image
    logo = lv_image_create(lv_scr_act());
    lv_image_set_src(logo, &pwr_logo);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 0, 0);

    draw_scroll_panel();
    draw_table();
    lv_timer_create(check_schedule_cb, 1000, NULL); // check every second
}

void lvgl_lock_init(void)
{
    lvgl_mutex = xSemaphoreCreateMutex();
}

void lvgl_lock_acquire(void)
{
    if (lvgl_mutex)
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
}

void lvgl_lock_release(void)
{
    if (lvgl_mutex)
        xSemaphoreGive(lvgl_mutex);
}