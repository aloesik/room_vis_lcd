#include <cJSON.h>

extern bool schedule_ready;
extern cJSON *schedule_root;
extern int current_day;

void wifi_init_sta(void);

void start_fetch_task(void);

void load_schedule_from_file(void);