#include <cJSON.h>

void wifi_init_sta(void);

void start_fetch_task(void);

cJSON *api_get_schedule_root(void);

bool api_is_schedule_ready(void); 