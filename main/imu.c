#include "imu.h"

#include <math.h>

#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// qmi8658.h defines its own single-precision M_PI, which collides with the
// one math.h already provided. Drop ours first; this file doesn't need it
// anyway (see TWO_PI below).
#undef M_PI
#include "qmi8658.h"

#define IMU_PROBE_TIMEOUT_MS 100
#define QMI8658_RESET_REGISTER 0x60
#define QMI8658_RESET_COMMAND 0xB0
#define QMI8658_CTRL1_VALUE 0x60
#define QMI8658_RESET_DELAY_MS 20

#define TWO_PI 6.28318530718f

static const char *TAG = "imu";

static qmi8658_dev_t s_dev;
static bool s_ready;

static float s_lp[2];  // low-passed gravity, screen x/y
static bool s_lp_primed;

static esp_err_t detect_address(i2c_master_bus_handle_t bus, uint8_t *address)
{
    const uint8_t candidates[] = {QMI8658_ADDRESS_HIGH, QMI8658_ADDRESS_LOW};

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (i2c_master_probe(bus, candidates[i], IMU_PROBE_TIMEOUT_MS) == ESP_OK) {
            *address = candidates[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t configure(void)
{
    esp_err_t ret = qmi8658_write_register(&s_dev, QMI8658_RESET_REGISTER, QMI8658_RESET_COMMAND);
    if (ret != ESP_OK) {
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(QMI8658_RESET_DELAY_MS));

    ret = qmi8658_write_register(&s_dev, QMI8658_CTRL1, QMI8658_CTRL1_VALUE);
    if (ret != ESP_OK) {
        return ret;
    }

    // 8g covers a hard knock to the case without clipping; 250 Hz is
    // comfortably above the physics rate so there is always a fresh sample.
    if ((ret = qmi8658_set_accel_range(&s_dev, QMI8658_ACCEL_RANGE_8G)) != ESP_OK) return ret;
    if ((ret = qmi8658_set_accel_odr(&s_dev, QMI8658_ACCEL_ODR_250HZ)) != ESP_OK) return ret;

    qmi8658_set_accel_unit_mps2(&s_dev, true);

    // The gyro is never read here: a tilt maze only needs "which way is
    // down", not rotation rate, so it stays off to save power and I2C time.
    return qmi8658_enable_sensors(&s_dev, QMI8658_ENABLE_ACCEL);
}

esp_err_t imu_init(i2c_master_bus_handle_t bus)
{
    uint8_t address = 0;
    esp_err_t ret = detect_address(bus, &address);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "QMI8658 not found on I2C");
        return ret;
    }

    ret = qmi8658_init(&s_dev, bus, address);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = configure();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "configure failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "QMI8658 ready at 0x%02x", address);
    s_ready = true;
    return ESP_OK;
}

bool imu_read_tilt(float dt, float *tilt_x, float *tilt_y)
{
    if (!s_ready) {
        return false;
    }

    bool data_ready = false;
    if (qmi8658_is_data_ready(&s_dev, &data_ready) != ESP_OK || !data_ready) {
        return false;
    }

    qmi8658_data_t data;
    if (qmi8658_read_sensor_data(&s_dev, &data) != ESP_OK) {
        return false;
    }

    const float ax = IMU_MAP_X(data.accelX, data.accelY, data.accelZ);
    const float ay = IMU_MAP_Y(data.accelX, data.accelY, data.accelZ);

    if (!s_lp_primed) {
        s_lp[0] = ax;
        s_lp[1] = ay;
        s_lp_primed = true;
    } else {
        // One-pole low pass; the coefficient is derived from the cutoff so
        // the feel does not change if the loop rate does.
        const float k = 1.0f - expf(-TWO_PI * GRAVITY_LP_HZ * dt);
        s_lp[0] += k * (ax - s_lp[0]);
        s_lp[1] += k * (ay - s_lp[1]);
    }

    // The accelerometer reads the reaction to gravity, so "down" -- the
    // direction that should pull the ball -- is the opposite of what the
    // low pass settled on.
    *tilt_x = -s_lp[0];
    *tilt_y = -s_lp[1];
    return true;
}
