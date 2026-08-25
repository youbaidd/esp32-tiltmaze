// TiltMaze: tilt the board to roll a marble through a randomly generated
// labyrinth to the gold goal. Solving one immediately generates the next;
// the PWR button regenerates on demand if you get stuck.
//
// Same two-task split as FluidBox: physics runs on core 1, rendering on
// core 0, and they only meet at a small spinlock around the game's state.

#include <stdio.h>

#include "button.h"
#include "config.h"
#include "display.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "game.h"
#include "imu.h"
#include "render.h"
#include "slot_switch.h"

#define I2C_PORT I2C_NUM_0
#define I2C_SDA GPIO_NUM_15
#define I2C_SCL GPIO_NUM_14

#define PHYSICS_YIELD_TICKS 1
#define BUTTON_PERIOD_MS 25
#define STATS_PERIOD_MS 5000

static const char *TAG = "tiltmaze";

static i2c_master_bus_handle_t s_i2c_bus;

static volatile uint32_t s_frames;
static volatile uint32_t s_steps;

static TaskHandle_t s_render_task_handle;
static TaskHandle_t s_physics_task_handle;

static esp_err_t i2c_init(void)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &s_i2c_bus);
}

static void render_task(void *arg)
{
    (void)arg;

    for (;;) {
        render_frame();
        s_frames++;

        // The panel transfer paces this loop; yielding one tick keeps the
        // idle task fed so the watchdog stays happy.
        vTaskDelay(1);
    }
}

static void physics_task(void *arg)
{
    (void)arg;

    const TickType_t period = pdMS_TO_TICKS(1000 / PHYSICS_HZ);
    TickType_t last_wake = xTaskGetTickCount();

    int64_t last_us = esp_timer_get_time();
    int64_t last_button_us = last_us;

    float tilt_x = 0.0f, tilt_y = 0.0f;

    for (;;) {
        const int64_t now = esp_timer_get_time();
        float dt = (float)(now - last_us) * 1e-6f;
        last_us = now;

        // A long stall (logging, flash access) must not turn into a huge
        // step that throws the ball across the maze.
        if (dt > 0.05f) {
            dt = 0.05f;
        } else if (dt < 1e-4f) {
            dt = 1e-4f;
        }

        imu_read_tilt(dt, &tilt_x, &tilt_y);
        game_step(dt, tilt_x, tilt_y);
        s_steps++;

        if (now - last_button_us >= BUTTON_PERIOD_MS * 1000) {
            last_button_us = now;
            button_poll();
            if (button_take_short_press()) {
                ESP_LOGI(TAG, "PWR pressed, new maze");
                game_force_reset();
            }
            if (button_take_long_press()) {
                ESP_LOGI(TAG, "PWR long-press, returning to launcher");
                slot_switch_boot_into(-1);
            }
        }

        vTaskDelayUntil(&last_wake, period);
    }
}

static void stats_loop(void)
{
    uint32_t last_frames = 0, last_steps = 0;
    int64_t last_us = esp_timer_get_time();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(STATS_PERIOD_MS));

        const int64_t now = esp_timer_get_time();
        const float elapsed = (float)(now - last_us) * 1e-6f;
        last_us = now;

        const uint32_t frames = s_frames;
        const uint32_t steps = s_steps;

        ESP_LOGI(TAG, "%.1f fps | %.1f steps/s | render stack free %u | physics stack free %u",
                 (double)((float)(frames - last_frames) / elapsed),
                 (double)((float)(steps - last_steps) / elapsed),
                 (unsigned)uxTaskGetStackHighWaterMark(s_render_task_handle),
                 (unsigned)uxTaskGetStackHighWaterMark(s_physics_task_handle));

        last_frames = frames;
        last_steps = steps;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "TiltMaze starting");

    ESP_ERROR_CHECK(i2c_init());
    ESP_ERROR_CHECK(display_init(s_i2c_bus));
    ESP_ERROR_CHECK(display_set_brightness(DISPLAY_BRIGHTNESS));

    // The maze still works without the IMU, it just never feels any tilt, so
    // a sensor failure is logged rather than fatal.
    if (imu_init(s_i2c_bus) != ESP_OK) {
        ESP_LOGW(TAG, "continuing without motion input");
    }
    if (button_init(s_i2c_bus) != ESP_OK) {
        ESP_LOGW(TAG, "continuing without the reset button");
    }

    game_init();
    render_init();

    xTaskCreatePinnedToCore(physics_task, "physics", 4096, NULL, 5, &s_physics_task_handle, 1);
    xTaskCreatePinnedToCore(render_task, "render", 4096, NULL, 5, &s_render_task_handle, 0);

    stats_loop();
}
