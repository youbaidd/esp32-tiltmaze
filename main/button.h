#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

// The PWR side button, read through pin 4 of the TCA9554 IO expander.
//
// A short press and a long press are reported separately. Holding PWR for
// six seconds is a separate physical button wired straight into the AXP2101
// power chip's hardware kill switch - this one is not it, so a long press
// here is free to mean whatever the game wants (returning to the launcher).

esp_err_t button_init(i2c_master_bus_handle_t bus);

// Call periodically (once per polling period - not once per "take" call).
// Debounces the input and updates the pending short/long press flags.
void button_poll(void);

// Each returns true exactly once per completed press of that length, and
// only after button_poll() has been called since the press ended.
bool button_take_short_press(void);
bool button_take_long_press(void);
