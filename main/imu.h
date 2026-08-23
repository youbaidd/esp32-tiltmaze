#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

// Turns QMI8658 accelerometer samples into a 2D tilt vector.
//
// An accelerometer at rest reads the reaction to gravity, so once the signal
// is low-passed to remove hand shake, what is left points "down" in whatever
// direction the board is tilted. That is exactly the force that should push
// a marble across the screen.

esp_err_t imu_init(i2c_master_bus_handle_t bus);

// Samples the IMU and fills in the low-passed gravity components lying in the
// screen plane, in m/s^2. Returns false if no fresh sample was available, in
// which case the previous tilt is still valid and unchanged.
bool imu_read_tilt(float dt, float *tilt_x, float *tilt_y);
