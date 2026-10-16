/*
 * Copyright (c) 2026 The ZMK Smart Mouse Layer Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_smart_mouse_layer

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <dt-bindings/zmk/smart_mouse_layer.h>
#include <zmk_smart_mouse_layer/smart_mouse_layer.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_smart_mouse_layer_config {
    const struct device *processor;
    uint8_t layer;
};

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

static const struct behavior_parameter_value_metadata param_values[] = {
    {.display_name = "Toggle lock", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = SML_TOG},
    {.display_name = "Hold lock", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = SML_HOLD},
    {.display_name = "Exit", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = SML_EXIT}};

static const struct behavior_parameter_metadata_set param_metadata_set[] = {{
    .param1_values = param_values,
    .param1_values_len = ARRAY_SIZE(param_values),
}};

static const struct behavior_parameter_metadata metadata = {
    .sets_len = ARRAY_SIZE(param_metadata_set),
    .sets = param_metadata_set,
};

#endif

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_smart_mouse_layer_config *cfg = dev->config;

    switch (binding->param1) {
    case SML_TOG:
        return smart_mouse_layer_toggle_lock(cfg->processor, cfg->layer);
    case SML_HOLD:
        return smart_mouse_layer_hold(cfg->processor, cfg->layer, true);
    case SML_EXIT:
        return smart_mouse_layer_exit(cfg->processor);
    default:
        LOG_ERR("Unknown smart mouse layer command %d", binding->param1);
        return -ENOTSUP;
    }
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_smart_mouse_layer_config *cfg = dev->config;

    if (binding->param1 == SML_HOLD) {
        return smart_mouse_layer_hold(cfg->processor, cfg->layer, false);
    }

    return 0;
}

static const struct behavior_driver_api behavior_smart_mouse_layer_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif
};

#define SML_BEHAVIOR_INST(n)                                                                       \
    static const struct behavior_smart_mouse_layer_config behavior_smart_mouse_layer_config_##n = {\
        .processor = DEVICE_DT_GET(DT_INST_PHANDLE(n, input_processor)),                           \
        .layer = DT_INST_PROP(n, layer),                                                           \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_smart_mouse_layer_config_##n,           \
                            POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                      \
                            &behavior_smart_mouse_layer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SML_BEHAVIOR_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
