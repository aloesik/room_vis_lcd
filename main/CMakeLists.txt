idf_component_register(
    SRCS
        "main.c"
        "lvgl_gui.c"
        "touch.c"
        "lcd.c"
        "pwr_logo.c"
       
    INCLUDE_DIRS
        "."
    REQUIRES lvgl
)