idf_component_register(
    SRCS
        "main.c"
        "display.c"
        "render.c"
        "maze.c"
        "game.c"
        "imu.c"
        "button.c"
        "slot_switch.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES "app_update" "esp_timer" "esp_system"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -ffast-math)
