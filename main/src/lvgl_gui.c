#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>

#include <cJSON.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lvgl.h>
#include <math.h>

#include "api.h"
#include "lvgl_gui.h"

#define DAYS_COUNT 5

#define PWR_COLOR 0xb32e23
#define LEC_COLOR 0xf0acb6
#define LAB_COLOR 0xe3ffb4
#define SEM_COLOR 0xa6eff0
#define EXE_COLOR 0xc8adff

#define SCROLL_PANEL_W 480
#define SCROLL_PANEL_H 66
#define SCROLL_PANEL_GAP_X 20

#define DAY_BTN_H 40
#define DAY_BTN_RADIUS 20
#define DAY_BTN_TEXT_PAD_X 30

#define TABLE_START_HOUR 7
#define TABLE_END_HOUR 22 // virtual end time, 15 hours total

#define ROW_H_PX 70
#define ROW_COUNT 15

#define COL1_W 102
#define COL2_W 382

#define LESSON_X 110
#define LESSON_W 360

#define TABLE_TOTAL_MIN ((TABLE_END_HOUR - TABLE_START_HOUR) * 60) // 900 min
#define TABLE_H_PX (ROW_H_PX * ROW_COUNT)

#define WEEK_BAR_H 35
#define PWR_LOGO_H 62
#define TABLE_VISIBLE_H (800 - PWR_LOGO_H - WEEK_BAR_H - SCROLL_PANEL_H + 2)

LV_IMAGE_DECLARE(logo_pwr_pl);
LV_IMAGE_DECLARE(logo_pwr_eng);
LV_IMAGE_DECLARE(flag_pl);
LV_IMAGE_DECLARE(flag_eng);

extern const lv_font_t lv_font_aptos_20;
extern const lv_font_t lv_font_aptos_22;
extern const lv_font_t lv_font_aptos_light_17;
extern const lv_font_t lv_font_aptos_light_25;
extern const lv_font_t lv_font_aptos_semibold_22;

static SemaphoreHandle_t lvgl_mutex = NULL;
static const char *TAG = "lvgl_gui";

typedef enum
{
    GUI_LANG_PL = 0,
    GUI_LANG_EN = 1,
} gui_language_t;

static gui_language_t current_lang = GUI_LANG_PL;

static const char *days_pl[DAYS_COUNT] = {
    "PONIEDZIAŁEK",
    "WTOREK",
    "ŚRODA",
    "CZWARTEK",
    "PIĄTEK",
};

static const char *days_en[DAYS_COUNT] = {
    "MONDAY",
    "TUESDAY",
    "WEDNESDAY",
    "THURSDAY",
    "FRIDAY",
};

static const char *days_short_pl[7] = {
    "Nd",  // 0 = Sunday
    "Pn",  // 1 = Monday
    "Wt",  // 2
    "Śr",  // 3
    "Czw", // 4
    "Pt",  // 5
    "Sb",  // 6
};

static const char *days_short_en[7] = {
    "Sun", // 0 = Sunday
    "Mon", // 1 = Monday
    "Tue", // 2
    "Wed", // 3
    "Thu", // 4
    "Fri", // 5
    "Sat", // 6
};

static const char *day_name(int day_idx)
{
    return (current_lang == GUI_LANG_EN) ? days_en[day_idx] : days_pl[day_idx];
}

static const char *day_short(int wday)
{
    return (current_lang == GUI_LANG_EN) ? days_short_en[wday] : days_short_pl[wday];
}

static int chosen_day = 0; // which day button is pressed
static int start_day = 0;  // which day is first in the scroll panel

static lv_obj_t *logo;
static lv_obj_t *pl_lang;
static lv_obj_t *eng_lang;
static lv_obj_t *btn_pl;
static lv_obj_t *btn_eng;
static lv_obj_t *week_bar;
static lv_obj_t *lbl_week;
static lv_obj_t *scroll_panel;
static lv_obj_t *table;
static lv_obj_t *lessons_container;
static lv_obj_t *line_indicator;
static lv_obj_t *circle_indicator;
static lv_obj_t *lbl_time;
static lv_obj_t *lbl_date;
static lv_obj_t *lbl_room;
static lv_obj_t *date_underline;

static void draw_scroll_panel(void);
static void draw_table(void);
static void draw_schedule(lv_obj_t *parent);
static void draw_room_label(void);

static void language_button_cb(lv_event_t *e);
static void apply_language(gui_language_t lang);

void lvgl_lock_acquire(void);
void lvgl_lock_release(void);

/* =========================
 * Callbacks / timers
 * ========================= */

static void day_button_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    chosen_day = idx;

    uint32_t child_count = lv_obj_get_child_count(scroll_panel);
    for (uint32_t i = 0; i < child_count; i++)
    {
        lv_obj_t *btn_child = lv_obj_get_child(scroll_panel, i);
        lv_obj_t *lbl = lv_obj_get_child(btn_child, 0);

        if ((int)i == chosen_day)
        {
            lv_obj_set_style_bg_color(btn_child, lv_color_hex(PWR_COLOR), 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        }
        else
        {
            lv_obj_set_style_bg_color(btn_child, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
        }
    }

    ESP_LOGI(TAG, "Chosen day: %s", day_name((start_day + chosen_day) % DAYS_COUNT));

    lv_obj_clean(lessons_container);
    draw_schedule(lessons_container);
}

static void language_button_cb(lv_event_t *e)
{
    gui_language_t lang = (gui_language_t)(intptr_t)lv_event_get_user_data(e);
    apply_language(lang);
}

static void apply_language(gui_language_t lang)
{
    if (current_lang == lang)
        return;

    current_lang = lang;

    if (logo)
    {
        if (current_lang == GUI_LANG_EN)
            lv_image_set_src(logo, &logo_pwr_eng);
        else
            lv_image_set_src(logo, &logo_pwr_pl);
    }

    if (scroll_panel)
    {
        uint32_t child_count = lv_obj_get_child_count(scroll_panel);
        for (uint32_t i = 0; i < child_count && i < DAYS_COUNT; i++)
        {
            int day_idx = (start_day + (int)i) % DAYS_COUNT;

            lv_obj_t *btn = lv_obj_get_child(scroll_panel, i);
            lv_obj_t *lbl = lv_obj_get_child(btn, 0);

            lv_label_set_text(lbl, day_name(day_idx));

            lv_point_t lbl_width;
            lv_txt_get_size(&lbl_width, day_name(day_idx), &lv_font_aptos_22, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            lv_obj_set_width(btn, lbl_width.x + DAY_BTN_TEXT_PAD_X);

            if ((int)i == chosen_day)
            {
                lv_obj_set_style_bg_color(btn, lv_color_hex(PWR_COLOR), 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            }
            else
            {
                lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
            }
        }
    }

    if (lessons_container)
    {
        lv_obj_clean(lessons_container);
        draw_schedule(lessons_container);
    }

    // Refresh clock label immediately (day short name)
    extern void update_clock_cb(lv_timer_t *timer);
    update_clock_cb(NULL);
}

static void update_week_range_label(void)
{
    if (!lbl_week)
        return;

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    time_t end_t = now + (time_t)6 * 24 * 60 * 60;
    struct tm tm_end;
    localtime_r(&end_t, &tm_end);

    char start_date[16];
    char end_date[16];
    char range[40];

    strftime(start_date, sizeof(start_date), "%d.%m.%Y", &tm_now);
    strftime(end_date, sizeof(end_date), "%d.%m.%Y", &tm_end);
    snprintf(range, sizeof(range), "%s - %s", start_date, end_date);

    lvgl_lock_acquire();
    lv_label_set_text(lbl_week, range);
    lvgl_lock_release();
}

void update_clock_cb(lv_timer_t *timer)
{
    (void)timer;

    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    static int last_yday = -1;
    if (timeinfo.tm_yday != last_yday)
    {
        last_yday = timeinfo.tm_yday;
        update_week_range_label();
    }

    char time_str[16];
    char time_with_dow[24];
    char date_str[16];

    strftime(time_str, sizeof(time_str), "%H:%M", &timeinfo);
    strftime(date_str, sizeof(date_str), "%d.%m.%Y", &timeinfo);

    snprintf(time_with_dow, sizeof(time_with_dow), "%s, %s",
             day_short(timeinfo.tm_wday), time_str);

    lvgl_lock_acquire();
    lv_label_set_text(lbl_time, time_with_dow);
    lv_label_set_text(lbl_date, date_str);

    if (line_indicator)
    {
        int now_min = timeinfo.tm_hour * 60 + timeinfo.tm_min;
        int y = now_min - (TABLE_START_HOUR * 60);

        if (y < 0 || y > TABLE_TOTAL_MIN)
        {
            lv_obj_add_flag(line_indicator, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(line_indicator, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(line_indicator, COL1_W, (lv_coord_t)y - 1);
            lv_obj_move_foreground(line_indicator);

            lv_obj_clear_flag(circle_indicator, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(circle_indicator, COL1_W - 5, (lv_coord_t)y - 5);
            lv_obj_move_foreground(circle_indicator);
        }
    }
    lvgl_lock_release();
}

static void check_schedule_cb(lv_timer_t *t)
{
    if (!schedule_ready)
        return;

    lv_timer_del(t);

    if (!schedule_root)
        return;

    lvgl_lock_acquire();
    draw_schedule(lessons_container);
    draw_room_label();
    lvgl_lock_release();
}

/* =========================
 * UI builders
 * ========================= */

static void draw_scroll_panel(void)
{
    scroll_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(scroll_panel, SCROLL_PANEL_W, SCROLL_PANEL_H);
    lv_obj_set_scroll_dir(scroll_panel, LV_DIR_HOR);

    lv_obj_clear_flag(scroll_panel, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(scroll_panel, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(scroll_panel, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_clear_flag(scroll_panel, LV_OBJ_FLAG_SCROLL_CHAIN_VER);

    lv_obj_set_scroll_snap_x(scroll_panel, LV_SCROLL_SNAP_CENTER);
    lv_obj_add_flag(scroll_panel, LV_OBJ_FLAG_SCROLL_ONE);

    lv_obj_set_flex_flow(scroll_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scroll_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_set_style_pad_row(scroll_panel, 0, 0);
    lv_obj_set_style_pad_column(scroll_panel, SCROLL_PANEL_GAP_X, 0);
    lv_obj_align_to(scroll_panel, week_bar, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);

    lv_obj_set_scrollbar_mode(scroll_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(scroll_panel, 1, 0);
    lv_obj_set_style_border_color(scroll_panel, lv_color_hex(PWR_COLOR), 0);
    lv_obj_set_style_radius(scroll_panel, 0, 0);

    for (int i = 0; i < DAYS_COUNT; i++)
    {
        int day_idx = (start_day + i) % DAYS_COUNT;

        lv_point_t lbl_width;
        lv_txt_get_size(&lbl_width, day_name(day_idx), &lv_font_aptos_22, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        int btn_width = lbl_width.x + DAY_BTN_TEXT_PAD_X;

        lv_obj_t *btn = lv_button_create(scroll_panel);
        lv_obj_set_size(btn, btn_width, DAY_BTN_H);
        lv_obj_set_style_radius(btn, DAY_BTN_RADIUS, 0);
        lv_obj_add_event_cb(btn, day_button_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i); // i = position

        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, day_name(day_idx));
        lv_obj_set_style_text_font(lbl, &lv_font_aptos_22, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

        if (i == chosen_day)
        {
            lv_obj_set_style_bg_color(btn, lv_color_hex(PWR_COLOR), 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
        }
        else
        {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
        }
    }
}

static void draw_table(void)
{
    table = lv_obj_create(lv_scr_act());
    lv_obj_set_size(table, SCROLL_PANEL_W + 4, TABLE_VISIBLE_H);
    lv_obj_align_to(table, scroll_panel, LV_ALIGN_OUT_BOTTOM_LEFT, -1, -4);

    lv_obj_set_style_pad_row(table, 0, 0);
    lv_obj_set_style_pad_column(table, 0, 0);
    lv_obj_set_style_pad_all(table, 0, 0);

    lv_obj_set_scroll_dir(table, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(table, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(table, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(table, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(table, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_clear_flag(table, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);

    static lv_point_precise_t line_points[] = {{0, 0}, {COL1_W, 0}};

    for (int i = 0; i < ROW_COUNT; i++)
    {
        lv_obj_t *cell1 = lv_obj_create(table);
        lv_obj_set_size(cell1, COL1_W, ROW_H_PX);
        lv_obj_set_style_bg_color(cell1, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(cell1, 0, 0);
        lv_obj_set_style_radius(cell1, 0, 0);
        lv_obj_align(cell1, LV_ALIGN_TOP_LEFT, 0, i * ROW_H_PX);
        lv_obj_set_style_pad_all(cell1, 0, 0);

        lv_obj_t *line = lv_line_create(cell1);
        lv_line_set_points(line, line_points, 2);
        lv_obj_align(line, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(0xE2AD9B), 0);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_obj_set_style_line_dash_width(line, 10, 0);
        lv_obj_set_style_line_dash_gap(line, 10, 0);

        char hour_txt[8];
        snprintf(hour_txt, sizeof(hour_txt), "%d:00", TABLE_START_HOUR + i);

        lv_obj_t *lbl1 = lv_label_create(cell1);
        lv_label_set_text(lbl1, hour_txt);
        lv_obj_set_style_text_font(lbl1, &lv_font_aptos_light_25, 0);
        lv_obj_center(lbl1);

        lv_obj_t *cell2 = lv_obj_create(table);
        lv_obj_set_size(cell2, COL2_W, ROW_H_PX);
        lv_obj_set_style_bg_color(cell2, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_width(cell2, 1, 0);
        lv_obj_set_style_border_color(cell2, lv_color_hex(0xE2AD9B), 0);
        lv_obj_set_style_border_side(cell2, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_radius(cell2, 0, 0);
        lv_obj_align(cell2, LV_ALIGN_TOP_LEFT, COL1_W, i * ROW_H_PX);
    }

    lessons_container = lv_obj_create(table);
    lv_obj_set_size(lessons_container, LV_PCT(100), TABLE_H_PX);
    lv_obj_set_style_pad_all(lessons_container, 0, 0);
    lv_obj_set_style_bg_opa(lessons_container, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(lessons_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(lessons_container, 0, 0);
    lv_obj_set_style_radius(lessons_container, 0, 0);

    line_indicator = lv_obj_create(table);
    lv_obj_remove_style_all(line_indicator);
    lv_obj_set_size(line_indicator, COL2_W, 2);
    lv_obj_set_style_bg_color(line_indicator, lv_color_hex(PWR_COLOR), 0);
    lv_obj_set_style_bg_opa(line_indicator, LV_OPA_COVER, 0);
    lv_obj_add_flag(line_indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(line_indicator, COL1_W - 5, 0);

    circle_indicator = lv_obj_create(table);
    lv_obj_remove_style_all(circle_indicator);
    lv_obj_set_size(circle_indicator, 11, 11);
    lv_obj_set_style_bg_color(circle_indicator, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(circle_indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(circle_indicator, 2, 0);
    lv_obj_set_style_border_color(circle_indicator, lv_color_hex(PWR_COLOR), 0);
    lv_obj_set_style_border_opa(circle_indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(circle_indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_pos(circle_indicator, COL1_W, 0);
    lv_obj_add_flag(circle_indicator, LV_OBJ_FLAG_HIDDEN);
}

static void draw_schedule(lv_obj_t *parent)
{
    if (!schedule_root)
        return;

    int display_day = (start_day + chosen_day) % DAYS_COUNT;
    const int arr_size = cJSON_GetArraySize(schedule_root);
    const float px_per_min = (float)TABLE_H_PX / (float)TABLE_TOTAL_MIN;

    for (int i = 0; i < arr_size; i++)
    {
        cJSON *item = cJSON_GetArrayItem(schedule_root, i);
        if (!item)
            continue;

        cJSON *start = cJSON_GetObjectItem(item, "start_time");
        cJSON *end = cJSON_GetObjectItem(item, "end_time");
        if (!cJSON_IsString(start) || !cJSON_IsString(end))
            continue;

        cJSON *course_name = cJSON_GetObjectItem(item, "course_name");
        cJSON *title_pl_item = course_name ? cJSON_GetObjectItem(course_name, "pl") : NULL;
        cJSON *title_en_item = course_name ? cJSON_GetObjectItem(course_name, "en") : NULL;

        const char *title_str = NULL;
        if (current_lang == GUI_LANG_EN && cJSON_IsString(title_en_item))
            title_str = title_en_item->valuestring;
        else if (cJSON_IsString(title_pl_item))
            title_str = title_pl_item->valuestring;

        if (!title_str)
            continue;

        cJSON *classtype_name = cJSON_GetObjectItem(item, "classtype_name");
        cJSON *type_pl_item = classtype_name ? cJSON_GetObjectItem(classtype_name, "pl") : NULL;
        cJSON *type_en_item = classtype_name ? cJSON_GetObjectItem(classtype_name, "en") : NULL;

        cJSON *group_obj = cJSON_GetObjectItem(item, "group_number");
        int group_num = cJSON_IsNumber(group_obj) ? group_obj->valueint : -1;

        const char *start_str = start->valuestring;
        const char *end_str = end->valuestring;
        const char *type_pl_str = cJSON_IsString(type_pl_item) ? type_pl_item->valuestring :
                                  (cJSON_IsString(type_en_item) ? type_en_item->valuestring : NULL); // fallback

        // Parse full timestamp to get weekday (tm_wday: Sun=0 .. Sat=6)
        struct tm tm_class = {0};
        if (!strptime(start_str, "%Y-%m-%d %H:%M:%S", &tm_class))
            continue;

        // Map weekday to Mon=0 .. Fri=4 (Mon=1 in tm_wday)
        int class_day = tm_class.tm_wday - 1;
        if (class_day != display_day)
            continue;

        // Parse HH:MM from "YYYY-MM-DD HH:MM:SS" (time starts at offset 11)
        int sh, sm, eh, em;
        if (sscanf(start_str + 11, "%d:%d", &sh, &sm) != 2)
            continue;
        if (sscanf(end_str + 11, "%d:%d", &eh, &em) != 2)
            continue;

        // Convert lesson times to minutes relative to the table start hour
        int start_min = (sh * 60 + sm) - (TABLE_START_HOUR * 60);
        int end_min = (eh * 60 + em) - (TABLE_START_HOUR * 60);

        // Convert minutes to pixel coordinates inside the table
        int y = (int)roundf((float)start_min * px_per_min);
        int h = (int)roundf((float)(end_min - start_min) * px_per_min);

        // Map type name to a single-letter code used in UI
        char type_short = '?';
        if (type_pl_str)
        {
            if (strstr(type_pl_str, "Wyk"))
                type_short = 'W';
            else if (strstr(type_pl_str, "Lab"))
                type_short = 'L';
            else if (strstr(type_pl_str, "Cwi") || strstr(type_pl_str, "Ćwi"))
                type_short = 'C';
            else if (strstr(type_pl_str, "Sem"))
                type_short = 'S';
        }

        // Pick rectangle color based on class type
        uint32_t lesson_color = LEC_COLOR;
        if (type_short == 'L')
            lesson_color = LAB_COLOR;
        else if (type_short == 'C')
            lesson_color = EXE_COLOR;
        else if (type_short == 'S')
            lesson_color = SEM_COLOR;

        // Build meta label text displayed under the course title
        char meta[64];
        const char *group_label = (current_lang == GUI_LANG_EN) ? "group" : "gr.";

        if (current_lang == GUI_LANG_EN)
        {
            const char *type_abbr = "?";
            if (type_short == 'W')
                type_abbr = "Lec";
            else if (type_short == 'L')
                type_abbr = "Lab";
            else if (type_short == 'C')
                type_abbr = "Exe";
            else if (type_short == 'S')
                type_abbr = "Sem";

            if (group_num >= 0)
                snprintf(meta, sizeof(meta), "%s, %s %d", type_abbr, group_label, group_num);
            else
                snprintf(meta, sizeof(meta), "%s", type_abbr);
        }
        else
        {
            if (group_num >= 0)
                snprintf(meta, sizeof(meta), "%c, %s %d", type_short, group_label, group_num);
            else
                snprintf(meta, sizeof(meta), "%c", type_short);
        }

        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", sh, sm);

        lv_obj_t *rect = lv_obj_create(parent);
        lv_obj_set_size(rect, LESSON_W, h);
        lv_obj_set_pos(rect, LESSON_X, y);

        lv_obj_set_style_bg_color(rect, lv_color_hex(lesson_color), 0);
        lv_obj_set_style_radius(rect, 10, 0);
        lv_obj_set_style_border_width(rect, 0, 0);
        lv_obj_set_style_pad_all(rect, 4, 0);
        lv_obj_set_scrollbar_mode(rect, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(rect, LV_DIR_VER);

        lv_obj_t *lbl_time = lv_label_create(rect);
        lv_label_set_text(lbl_time, tbuf);
        lv_obj_set_style_text_font(lbl_time, &lv_font_aptos_20, 0);
        lv_obj_align(lbl_time, LV_ALIGN_BOTTOM_RIGHT, -3, -3);

        lv_obj_t *lbl_title = lv_label_create(rect);
        lv_label_set_text(lbl_title, title_str);
        lv_obj_set_style_text_font(lbl_title, &lv_font_aptos_semibold_22, 0);
        lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl_title, LESSON_W - 6);
        lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 3, 3);

        lv_obj_t *lbl_meta = lv_label_create(rect);
        lv_label_set_text(lbl_meta, meta);
        lv_obj_set_style_text_font(lbl_meta, &lv_font_aptos_20, 0);
        lv_label_set_long_mode(lbl_meta, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl_meta, LESSON_W - 6);
        lv_obj_align_to(lbl_meta, lbl_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    }

    ESP_LOGI(TAG, "Drawing schedule rectangles...");
}

static void draw_room_label(void)
{
    cJSON *root = schedule_root;
    if (!root || cJSON_GetArraySize(root) == 0)
        return;

    cJSON *first = cJSON_GetArrayItem(root, 0);
    if (!first)
        return;

    cJSON *room = cJSON_GetObjectItem(first, "room_number");
    cJSON *building = cJSON_GetObjectItem(first, "building_id");
    if (!cJSON_IsString(room) || !cJSON_IsString(building))
        return;

    char text[32];
    snprintf(text, sizeof(text), "%s, s: %s", building->valuestring, room->valuestring);
    lv_label_set_text(lbl_room, text);
}

static void draw_header(lv_obj_t *parent)
{
    lbl_time = lv_label_create(parent);
    lv_label_set_long_mode(lbl_time, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl_time, 150);
    lv_obj_set_style_text_align(lbl_time, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_time, "--, --:--");
    lv_obj_align(lbl_time, LV_ALIGN_TOP_RIGHT, -5, 3);
    lv_obj_set_style_text_font(lbl_time, &lv_font_aptos_20, 0);
    lv_obj_set_style_text_color(lbl_time, lv_color_hex(PWR_COLOR), 0);

    lbl_date = lv_label_create(parent);
    lv_label_set_long_mode(lbl_date, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl_date, 150);
    lv_obj_set_style_text_align(lbl_date, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_date, "--.--.----");
    lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_text_font(lbl_date, &lv_font_aptos_20, 0);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(PWR_COLOR), 0);

    date_underline = lv_line_create(parent);
    static lv_point_precise_t underline_points[] = {{0, 0}, {120, 0}};
    lv_line_set_points(date_underline, underline_points, 2);
    lv_obj_align_to(date_underline, lbl_date, LV_ALIGN_OUT_BOTTOM_RIGHT, 5, 0);
    lv_obj_set_style_line_color(date_underline, lv_color_hex(PWR_COLOR), 0);
    lv_obj_set_style_line_width(date_underline, 1, 0);

    lbl_room = lv_label_create(parent);
    lv_label_set_long_mode(lbl_room, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl_room, 150);
    lv_obj_set_style_text_align(lbl_room, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_room, "---, s. ---");
    lv_obj_align_to(lbl_room, lbl_date, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_text_font(lbl_room, &lv_font_aptos_light_17, 0);

    // pwr logo
    logo = lv_image_create(parent);
    lv_image_set_src(logo, &logo_pwr_pl);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 0, 0);

    // flags buttons
    btn_pl = lv_button_create(parent);
    lv_obj_set_size(btn_pl, 26, 26);
    lv_obj_align_to(btn_pl, logo, LV_ALIGN_OUT_RIGHT_MID, 25, -14);
    lv_obj_set_style_radius(btn_pl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(btn_pl, LV_OPA_TRANSP, 0 | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_pl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(btn_pl, language_button_cb, LV_EVENT_CLICKED, (void *)(intptr_t)GUI_LANG_PL);

    pl_lang = lv_image_create(btn_pl);
    lv_image_set_src(pl_lang, &flag_pl);
    lv_obj_center(pl_lang);

    btn_eng = lv_button_create(parent);
    lv_obj_set_size(btn_eng, 26, 26);
    lv_obj_align_to(btn_eng, logo, LV_ALIGN_OUT_RIGHT_MID, 25, 18);
    lv_obj_set_style_radius(btn_eng, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(btn_eng, LV_OPA_TRANSP, 0 | LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn_eng, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(btn_eng, language_button_cb, LV_EVENT_CLICKED, (void *)(intptr_t)GUI_LANG_EN);

    eng_lang = lv_image_create(btn_eng);
    lv_image_set_src(eng_lang, &flag_eng);
    lv_obj_center(eng_lang);
}

static void draw_week_bar(lv_obj_t *parent)
{
    week_bar = lv_obj_create(parent);
    lv_obj_set_size(week_bar, SCROLL_PANEL_W, WEEK_BAR_H);
    lv_obj_set_style_bg_color(week_bar, lv_color_hex(PWR_COLOR), 0);
    lv_obj_set_style_radius(week_bar, 0, 0);
    lv_obj_set_style_border_width(week_bar, 0, 0);
    lv_obj_set_style_pad_all(week_bar, 0, 0);
    lv_obj_clear_flag(week_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align_to(week_bar, logo, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 5);

    lbl_week = lv_label_create(week_bar);
    lv_obj_set_style_text_font(lbl_week, &lv_font_aptos_22, 0);
    lv_obj_set_style_text_color(lbl_week, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(lbl_week, "--.--.---- - --.--.----");
    lv_obj_center(lbl_week);
}

/* =========================
 * Public API
 * ========================= */

void lvgl_create_gui(void)
{
    // background
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xffffff), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(scr, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_timer_create(update_clock_cb, 1000, NULL);

    draw_header(scr);

    draw_week_bar(scr);
    update_week_range_label();

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    // Mon..Fri -> 0..4, weekend -> fallback to Monday
    start_day = (tm_now.tm_wday >= 1 && tm_now.tm_wday <= 5) ? (tm_now.tm_wday - 1) : 0;
    chosen_day = 0; // first button = today

    draw_scroll_panel();

    draw_table();

    lv_obj_move_foreground(scroll_panel);

    lv_timer_create(check_schedule_cb, 1000, NULL);
}

/* Create the lvgl mutex semaphore */
void lvgl_lock_init(void)
{
    if (!lvgl_mutex)
    {
        lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    }
}

void lvgl_lock_acquire(void)
{
    if (lvgl_mutex)
    {
        xSemaphoreTakeRecursive(lvgl_mutex, portMAX_DELAY);
    }
}

void lvgl_lock_release(void)
{
    if (lvgl_mutex)
    {
        xSemaphoreGiveRecursive(lvgl_mutex);
    }
}