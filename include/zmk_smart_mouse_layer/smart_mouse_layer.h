/*
 * Copyright (c) 2026 The ZMK Smart Mouse Layer Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

// Entry points used by the smart mouse layer behavior. `dev` is the input processor device.

// Toggle lock: activates the layer and locks it, or deactivates it when already locked.
int smart_mouse_layer_toggle_lock(const struct device *dev, uint8_t layer);

// Hold lock: layer stays active while pressed; on release it is deactivated unless toggle-locked.
int smart_mouse_layer_hold(const struct device *dev, uint8_t layer, bool pressed);

// Deactivates the layer and clears all lock state.
int smart_mouse_layer_exit(const struct device *dev);
