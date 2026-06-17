/*
 * Copyright (c) 2026 The ZMK Smart Mouse Layer Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_smart_mouse_layer

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>
#include <zmk/behavior.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/matrix.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct smart_mouse_layer_config {
    int16_t movement_threshold;
    int16_t require_prior_idle_ms;
    const char *const *keep_behaviors;
    size_t keep_behaviors_len;
    const uint32_t *keep_keycodes;
    size_t keep_keycodes_len;
    const char *const *transparent_behaviors;
    size_t transparent_behaviors_len;
};

struct smart_mouse_layer_data {
    struct k_mutex lock;
    struct k_work_delayable disable_work;
    uint8_t target_layer;
    uint32_t timeout_ms;
    bool is_active;
    int64_t last_tapped_timestamp;
    uint32_t accumulated_movement;
    bool keep_position_pressed[ZMK_KEYMAP_LEN];
    uint8_t keep_position_pressed_count;
};

static bool behavior_name_in_list(const char *behavior_dev, const char *const *behaviors,
                                  size_t behaviors_len) {
    if (!behavior_dev) {
        return false;
    }

    for (size_t i = 0; i < behaviors_len; i++) {
        if (strcmp(behavior_dev, behaviors[i]) == 0) {
            return true;
        }
    }

    return false;
}

static bool binding_is_transparent(const struct smart_mouse_layer_config *config,
                                   const struct zmk_behavior_binding *binding) {
    return binding && behavior_name_in_list(binding->behavior_dev, config->transparent_behaviors,
                                            config->transparent_behaviors_len);
}

static bool binding_keeps_layer(const struct smart_mouse_layer_config *config,
                                const struct zmk_behavior_binding *binding) {
    if (!binding) {
        return false;
    }

    if (behavior_name_in_list(binding->behavior_dev, config->keep_behaviors,
                              config->keep_behaviors_len)) {
        return true;
    }

    for (size_t i = 0; i < config->keep_keycodes_len; i++) {
        uint32_t keycode = config->keep_keycodes[i];

        if (binding->param1 == keycode || binding->param2 == keycode) {
            return true;
        }
    }

    return false;
}

static bool effective_binding_keeps_layer(const struct smart_mouse_layer_config *config,
                                          uint32_t position) {
    for (int layer_idx = ZMK_KEYMAP_LAYERS_LEN - 1; layer_idx >= 0; layer_idx--) {
        zmk_keymap_layer_id_t layer_id = zmk_keymap_layer_index_to_id(layer_idx);

        if (layer_id == ZMK_KEYMAP_LAYER_ID_INVAL || !zmk_keymap_layer_active(layer_id)) {
            continue;
        }

        const struct zmk_behavior_binding *binding =
            zmk_keymap_get_layer_binding_at_idx(layer_id, position);

        if (!binding) {
            continue;
        }

        if (binding_is_transparent(config, binding)) {
            continue;
        }

        return binding_keeps_layer(config, binding);
    }

    return false;
}

static bool should_block_activation(const struct smart_mouse_layer_config *config,
                                    int64_t last_tapped, int64_t current_time) {
    return config->require_prior_idle_ms > 0 &&
           (last_tapped + config->require_prior_idle_ms) > current_time;
}

static void smart_mouse_layer_clear_keep_positions(struct smart_mouse_layer_data *data) {
    memset(data->keep_position_pressed, 0, sizeof(data->keep_position_pressed));
    data->keep_position_pressed_count = 0;
}

static void smart_mouse_layer_set_active(struct smart_mouse_layer_data *data, bool active) {
    if (data->is_active == active) {
        return;
    }

    data->is_active = active;
    data->accumulated_movement = 0;

    if (active) {
        zmk_keymap_layer_activate(data->target_layer);
        LOG_DBG("Smart mouse layer %d activated", data->target_layer);
    } else {
        smart_mouse_layer_clear_keep_positions(data);
        zmk_keymap_layer_deactivate(data->target_layer);
        LOG_DBG("Smart mouse layer %d deactivated", data->target_layer);
    }
}

static void disable_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct smart_mouse_layer_data *data =
        CONTAINER_OF(dwork, struct smart_mouse_layer_data, disable_work);

    int ret = k_mutex_lock(&data->lock, K_FOREVER);
    if (ret < 0) {
        return;
    }

    smart_mouse_layer_set_active(data, false);
    k_mutex_unlock(&data->lock);
}

static void smart_mouse_layer_schedule_timeout(struct smart_mouse_layer_data *data,
                                               uint32_t timeout_ms) {
    if (data->keep_position_pressed_count > 0) {
        k_work_cancel_delayable(&data->disable_work);
        return;
    }

    if (timeout_ms > 0) {
        k_work_reschedule(&data->disable_work, K_MSEC(timeout_ms));
    }
}

static uint16_t rel_xy_movement(const struct input_event *event) {
    if (event->type != INPUT_EV_REL) {
        return 0;
    }

    switch (event->code) {
    case INPUT_REL_X:
    case INPUT_REL_Y:
        return abs(event->value);
    default:
        return 0;
    }
}

static bool is_mouse_activity(const struct input_event *event) {
    if (event->type == INPUT_EV_REL) {
        switch (event->code) {
        case INPUT_REL_X:
        case INPUT_REL_Y:
        case INPUT_REL_WHEEL:
        case INPUT_REL_HWHEEL:
            return event->value != 0;
        default:
            return false;
        }
    }

    if (event->type == INPUT_EV_KEY) {
        return event->code >= INPUT_BTN_0 && event->code <= INPUT_BTN_4;
    }

    return false;
}

static int smart_mouse_layer_handle_event(const struct device *dev, struct input_event *event,
                                          uint32_t param1, uint32_t param2,
                                          struct zmk_input_processor_state *state) {
    ARG_UNUSED(state);

    if (param1 >= ZMK_KEYMAP_LAYERS_LEN) {
        LOG_ERR("Invalid layer index: %d", param1);
        return -EINVAL;
    }

    struct smart_mouse_layer_data *data = dev->data;
    const struct smart_mouse_layer_config *config = dev->config;

    int ret = k_mutex_lock(&data->lock, K_FOREVER);
    if (ret < 0) {
        return ret;
    }

    data->target_layer = param1;
    data->timeout_ms = param2;

    if (data->is_active) {
        if (is_mouse_activity(event)) {
            smart_mouse_layer_schedule_timeout(data, param2);
        }
        k_mutex_unlock(&data->lock);
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (should_block_activation(config, data->last_tapped_timestamp, k_uptime_get())) {
        data->accumulated_movement = 0;
        k_mutex_unlock(&data->lock);
        return ZMK_INPUT_PROC_CONTINUE;
    }

    uint16_t movement = rel_xy_movement(event);
    if (movement > 0) {
        data->accumulated_movement += movement;
    }

    if (movement > 0 && data->accumulated_movement >= config->movement_threshold) {
        smart_mouse_layer_set_active(data, true);
        smart_mouse_layer_schedule_timeout(data, param2);
    }

    k_mutex_unlock(&data->lock);

    return ZMK_INPUT_PROC_CONTINUE;
}

static int handle_layer_state_changed(const struct device *dev, const zmk_event_t *eh) {
    struct smart_mouse_layer_data *data = dev->data;

    int ret = k_mutex_lock(&data->lock, K_FOREVER);
    if (ret < 0) {
        return ret;
    }

    if (data->is_active && !zmk_keymap_layer_active(data->target_layer)) {
        data->is_active = false;
        data->accumulated_movement = 0;
        smart_mouse_layer_clear_keep_positions(data);
        k_work_cancel_delayable(&data->disable_work);
    }

    k_mutex_unlock(&data->lock);
    return ZMK_EV_EVENT_BUBBLE;
}

static int handle_keycode_state_changed(const struct device *dev, const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (!ev || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    struct smart_mouse_layer_data *data = dev->data;

    int ret = k_mutex_lock(&data->lock, K_FOREVER);
    if (ret < 0) {
        return ret;
    }

    data->last_tapped_timestamp = ev->timestamp;
    k_mutex_unlock(&data->lock);

    return ZMK_EV_EVENT_BUBBLE;
}

static int handle_position_state_changed(const struct device *dev, const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    struct smart_mouse_layer_data *data = dev->data;
    const struct smart_mouse_layer_config *config = dev->config;

    int ret = k_mutex_lock(&data->lock, K_FOREVER);
    if (ret < 0) {
        return ret;
    }

    if (!data->is_active) {
        k_mutex_unlock(&data->lock);
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->state && effective_binding_keeps_layer(config, ev->position)) {
        if (ev->position < ZMK_KEYMAP_LEN && !data->keep_position_pressed[ev->position]) {
            data->keep_position_pressed[ev->position] = true;
            data->keep_position_pressed_count++;
        }
        smart_mouse_layer_schedule_timeout(data, data->timeout_ms);
    } else if (ev->state) {
        smart_mouse_layer_set_active(data, false);
        k_work_cancel_delayable(&data->disable_work);
    } else if (ev->position < ZMK_KEYMAP_LEN && data->keep_position_pressed[ev->position]) {
        data->keep_position_pressed[ev->position] = false;
        if (data->keep_position_pressed_count > 0) {
            data->keep_position_pressed_count--;
        }
        smart_mouse_layer_schedule_timeout(data, data->timeout_ms);
    }

    k_mutex_unlock(&data->lock);

    return ZMK_EV_EVENT_BUBBLE;
}

static int handle_state_changed_dispatcher(const struct device *dev, const zmk_event_t *eh) {
    if (as_zmk_layer_state_changed(eh) != NULL) {
        return handle_layer_state_changed(dev, eh);
    }

    if (as_zmk_position_state_changed(eh) != NULL) {
        return handle_position_state_changed(dev, eh);
    }

    if (as_zmk_keycode_state_changed(eh) != NULL) {
        return handle_keycode_state_changed(dev, eh);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

#define DISPATCH_EVENT(inst)                                                                       \
    {                                                                                              \
        int err = handle_state_changed_dispatcher(DEVICE_DT_INST_GET(inst), eh);                   \
        if (err < 0) {                                                                             \
            return err;                                                                            \
        }                                                                                          \
    }

static int handle_event_dispatcher(const zmk_event_t *eh) {
    DT_INST_FOREACH_STATUS_OKAY(DISPATCH_EVENT)
    return 0;
}

static int smart_mouse_layer_init(const struct device *dev) {
    struct smart_mouse_layer_data *data = dev->data;

    k_mutex_init(&data->lock);
    k_work_init_delayable(&data->disable_work, disable_work_cb);

    return 0;
}

static const struct zmk_input_processor_driver_api smart_mouse_layer_driver_api = {
    .handle_event = smart_mouse_layer_handle_event,
};

ZMK_LISTENER(smart_mouse_layer, handle_event_dispatcher);
ZMK_SUBSCRIPTION(smart_mouse_layer, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(smart_mouse_layer, zmk_position_state_changed);
ZMK_SUBSCRIPTION(smart_mouse_layer, zmk_keycode_state_changed);

#define BEHAVIOR_NAME_AT_IDX(idx, prop, inst) DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(inst, prop, idx))
#define KEYCODE_AT_IDX(idx, prop, inst) DT_INST_PROP_BY_IDX(inst, prop, idx)

#define SMART_MOUSE_LAYER_INST(n)                                                                  \
    static struct smart_mouse_layer_data smart_mouse_layer_data_##n = {};                          \
    static const char *const keep_behaviors_##n[] = {                                              \
        LISTIFY(DT_INST_PROP_LEN(n, keep_behaviors), BEHAVIOR_NAME_AT_IDX, (, ), keep_behaviors,   \
                n)};                                                                               \
    static const uint32_t keep_keycodes_##n[] = {                                                   \
        LISTIFY(DT_INST_PROP_LEN(n, keep_keycodes), KEYCODE_AT_IDX, (, ), keep_keycodes, n)};       \
    static const char *const transparent_behaviors_##n[] = {                                       \
        LISTIFY(DT_INST_PROP_LEN(n, transparent_behaviors), BEHAVIOR_NAME_AT_IDX, (, ),            \
                transparent_behaviors, n)};                                                        \
    static const struct smart_mouse_layer_config smart_mouse_layer_config_##n = {                  \
        .movement_threshold = DT_INST_PROP_OR(n, movement_threshold, 0),                           \
        .require_prior_idle_ms = DT_INST_PROP_OR(n, require_prior_idle_ms, 0),                     \
        .keep_behaviors = keep_behaviors_##n,                                                      \
        .keep_behaviors_len = ARRAY_SIZE(keep_behaviors_##n),                                      \
        .keep_keycodes = keep_keycodes_##n,                                                        \
        .keep_keycodes_len = ARRAY_SIZE(keep_keycodes_##n),                                        \
        .transparent_behaviors = transparent_behaviors_##n,                                        \
        .transparent_behaviors_len = ARRAY_SIZE(transparent_behaviors_##n),                        \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, smart_mouse_layer_init, NULL, &smart_mouse_layer_data_##n,            \
                          &smart_mouse_layer_config_##n, POST_KERNEL,                              \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &smart_mouse_layer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SMART_MOUSE_LAYER_INST)
